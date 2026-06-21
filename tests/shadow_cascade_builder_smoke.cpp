#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/ShadowCascadeBuilder.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace {
    bool near(float lhs, float rhs, float epsilon = 0.0001f) {
        return std::abs(lhs - rhs) <= epsilon;
    }

    bool isFiniteMatrix(const glm::mat4& matrix) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!std::isfinite(matrix[column][row])) {
                    return false;
                }
            }
        }
        return true;
    }

    glm::vec3 projectPoint(const glm::mat4& matrix, const glm::vec3& point) {
        const glm::vec4 clip = matrix * glm::vec4{point, 1.0f};
        return glm::vec3{clip} / clip.w;
    }

    bool projectedBoundsInsideClip(const ark::ShadowCascade& cascade) {
        if (!cascade.worldBounds.isValid()) {
            return false;
        }

        for (const glm::vec3& corner : ark::boundsCorners(cascade.worldBounds)) {
            const glm::vec3 ndc = projectPoint(cascade.lightViewProjection, corner);
            if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y) || !std::isfinite(ndc.z) ||
                ndc.x < -1.001f || ndc.x > 1.001f ||
                ndc.y < -1.001f || ndc.y > 1.001f ||
                ndc.z < -0.001f || ndc.z > 1.001f) {
                return false;
            }
        }
        return true;
    }

    bool validateSplitDistances() {
        const ark::CascadeSplitDistances linear =
            ark::computeCascadeSplitDistances(0.1f, 100.0f, 4, 0.0f);
        if (!linear.isValid() ||
            !near(linear.distances[0], 0.1f) ||
            !near(linear.distances[1], 25.075f) ||
            !near(linear.distances[2], 50.05f) ||
            !near(linear.distances[3], 75.025f) ||
            !near(linear.distances[4], 100.0f)) {
            std::cerr << "Linear CSM split distances are invalid\n";
            return false;
        }

        const ark::CascadeSplitDistances logarithmic =
            ark::computeCascadeSplitDistances(0.1f, 100.0f, 4, 1.0f);
        if (!logarithmic.isValid() ||
            !near(logarithmic.distances[1], 0.562341f, 0.0001f) ||
            !near(logarithmic.distances[2], 3.162278f, 0.0001f) ||
            !near(logarithmic.distances[3], 17.782795f, 0.0001f)) {
            std::cerr << "Logarithmic CSM split distances are invalid\n";
            return false;
        }

        const ark::CascadeSplitDistances practical =
            ark::computeCascadeSplitDistances(0.1f, 100.0f, 4, 0.5f);
        if (!practical.isValid()) {
            std::cerr << "Practical CSM split distances were not generated\n";
            return false;
        }

        for (ark::u32 index = 1; index <= practical.cascadeCount; ++index) {
            if (!(practical.distances[index] > practical.distances[index - 1])) {
                std::cerr << "Practical CSM split distances are not monotonic\n";
                return false;
            }
        }

        const ark::CascadeSplitDistances invalid =
            ark::computeCascadeSplitDistances(0.1f, 10.0f, ark::MaxShadowCascadeCount + 1, 0.5f);
        if (invalid.isValid()) {
            std::cerr << "Invalid cascade count should not produce split distances\n";
            return false;
        }

        return true;
    }

    bool validateCascadeFrameData() {
        ark::RenderView view{};
        const glm::vec3 cameraPosition{0.0f, 2.0f, -6.0f};
        const glm::mat4 viewMatrix = glm::lookAt(cameraPosition,
                                                glm::vec3{0.0f, 1.0f, 0.0f},
                                                glm::vec3{0.0f, 1.0f, 0.0f});
        glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 120.0f);
        projection[1][1] *= -1.0f;
        view.setMatrices(viewMatrix, projection, cameraPosition);
        view.setClipRange(0.1f, 120.0f);

        ark::ShadowSettings settings{};
        settings.enabled = true;
        settings.strength = 1.0f;
        settings.nearPlane = 0.05f;
        settings.stabilizeProjection = true;
        settings.cascades.enabled = true;
        settings.cascades.cascadeCount = 4;
        settings.cascades.splitLambda = 0.5f;
        settings.cascades.maxDistance = 60.0f;
        settings.cascades.cascadeExtent = 1024;
        view.setShadowSettings(settings);

        ark::SceneLighting lighting{};
        lighting.mainLight.direction = glm::vec3{-0.4f, -0.8f, -0.2f};

        const ark::CascadeShadowFrameData frameData =
            ark::buildCascadeShadowFrameData(view, lighting, view.shadowSettings());
        if (!frameData.isEnabled() ||
            frameData.cascadeCount != 4 ||
            frameData.cascadeExtent != 1024) {
            std::cerr << "CSM frame data header is invalid\n";
            return false;
        }

        for (ark::u32 index = 0; index < frameData.cascadeCount; ++index) {
            const ark::ShadowCascade& cascade = frameData.cascades[index];
            if (!(cascade.farDistance > cascade.nearDistance) ||
                !cascade.worldBounds.isValid() ||
                !isFiniteMatrix(cascade.lightViewProjection) ||
                !projectedBoundsInsideClip(cascade)) {
                std::cerr << "CSM cascade " << index << " is invalid\n";
                return false;
            }
            if (index > 0 && !near(cascade.nearDistance, frameData.cascades[index - 1].farDistance)) {
                std::cerr << "CSM cascade ranges are not contiguous\n";
                return false;
            }
        }

        if (!near(frameData.cascades[0].nearDistance, view.cameraNearPlane()) ||
            !near(frameData.cascades[frameData.cascadeCount - 1].farDistance, settings.cascades.maxDistance)) {
            std::cerr << "CSM cascade range did not use camera near and shadow max distance\n";
            return false;
        }

        settings.cascades.enabled = false;
        view.setShadowSettings(settings);
        if (ark::buildCascadeShadowFrameData(view, lighting, view.shadowSettings()).isEnabled()) {
            std::cerr << "Disabled CSM should not produce frame data\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateSplitDistances() && validateCascadeFrameData()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
