#include "renderer/core/Frustum.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace {
    ark::Bounds3 makeBounds(const glm::vec3& min, const glm::vec3& max) {
        ark::Bounds3 bounds = ark::makeBoundsFromPoint(min);
        ark::expandBounds(bounds, max);
        return bounds;
    }

    bool near(float lhs, float rhs, float epsilon = 0.0001f) {
        return std::fabs(lhs - rhs) <= epsilon;
    }

    bool validateIdentityZeroToOneClipVolume() {
        const ark::Frustum frustum = ark::Frustum::fromViewProjection(glm::mat4{1.0f});

        const ark::Bounds3 inside = makeBounds(glm::vec3{-0.5f, -0.5f, 0.25f},
                                               glm::vec3{0.5f, 0.5f, 0.75f});
        if (!frustum.intersects(inside) || !frustum.contains(inside)) {
            std::cerr << "Identity frustum should contain bounds inside the zero-to-one clip volume\n";
            return false;
        }

        const ark::Bounds3 behindNear = makeBounds(glm::vec3{-0.1f, -0.1f, -0.4f},
                                                   glm::vec3{0.1f, 0.1f, -0.1f});
        if (frustum.intersects(behindNear) || frustum.contains(behindNear)) {
            std::cerr << "Identity frustum should reject bounds with z below zero\n";
            return false;
        }

        const ark::Bounds3 beyondFar = makeBounds(glm::vec3{-0.1f, -0.1f, 1.1f},
                                                  glm::vec3{0.1f, 0.1f, 1.3f});
        if (frustum.intersects(beyondFar) || frustum.contains(beyondFar)) {
            std::cerr << "Identity frustum should reject bounds with z beyond one\n";
            return false;
        }

        const ark::Bounds3 crossingLeft = makeBounds(glm::vec3{-1.2f, -0.1f, 0.4f},
                                                     glm::vec3{-0.8f, 0.1f, 0.6f});
        if (!frustum.intersects(crossingLeft) || frustum.contains(crossingLeft)) {
            std::cerr << "Identity frustum should intersect but not contain bounds crossing a side plane\n";
            return false;
        }

        return true;
    }

    bool validatePerspectiveViewProjection() {
        const glm::vec3 cameraPosition{0.0f, 0.0f, -5.0f};
        const glm::mat4 view = glm::lookAt(cameraPosition,
                                          glm::vec3{0.0f, 0.0f, 0.0f},
                                          glm::vec3{0.0f, 1.0f, 0.0f});
        glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 20.0f);
        projection[1][1] *= -1.0f;

        const ark::Frustum frustum = ark::Frustum::fromViewProjection(projection * view);

        const ark::Bounds3 centerObject = makeBounds(glm::vec3{-1.0f, -1.0f, -1.0f},
                                                     glm::vec3{1.0f, 1.0f, 1.0f});
        if (!frustum.intersects(centerObject) || !frustum.contains(centerObject)) {
            std::cerr << "Perspective frustum should contain the centered object\n";
            return false;
        }

        const ark::Bounds3 farObject = makeBounds(glm::vec3{-1.0f, -1.0f, 20.5f},
                                                  glm::vec3{1.0f, 1.0f, 22.0f});
        if (frustum.intersects(farObject)) {
            std::cerr << "Perspective frustum should reject object beyond the far plane\n";
            return false;
        }

        const ark::Bounds3 sideObject = makeBounds(glm::vec3{30.0f, -1.0f, -1.0f},
                                                   glm::vec3{32.0f, 1.0f, 1.0f});
        if (frustum.intersects(sideObject)) {
            std::cerr << "Perspective frustum should reject object outside the side plane\n";
            return false;
        }

        const ark::Bounds3 nearCrossing = makeBounds(glm::vec3{-0.05f, -0.05f, -5.05f},
                                                     glm::vec3{0.05f, 0.05f, -4.85f});
        if (!frustum.intersects(nearCrossing) || frustum.contains(nearCrossing)) {
            std::cerr << "Perspective frustum should intersect but not contain bounds crossing near plane\n";
            return false;
        }

        return true;
    }

    bool validateInvalidBoundsIsConservative() {
        const ark::Frustum frustum = ark::Frustum::fromViewProjection(glm::mat4{1.0f});
        const ark::Bounds3 invalid = ark::makeInvalidBounds();
        if (!frustum.intersects(invalid)) {
            std::cerr << "Invalid bounds should be treated as intersecting for conservative culling\n";
            return false;
        }

        if (frustum.contains(invalid)) {
            std::cerr << "Invalid bounds should not be treated as fully contained\n";
            return false;
        }

        return true;
    }

    bool validatePlaneNormalization() {
        const glm::mat4 scaledClip = glm::scale(glm::mat4{1.0f}, glm::vec3{2.0f, 3.0f, 4.0f});
        const ark::Frustum frustum = ark::Frustum::fromViewProjection(scaledClip);
        for (const ark::FrustumPlane& plane : frustum.planes()) {
            if (!plane.isValid() || !near(glm::length(plane.normal), 1.0f)) {
                std::cerr << "Frustum plane was not normalized\n";
                return false;
            }
        }

        return true;
    }
} // namespace

int main() {
    return validateIdentityZeroToOneClipVolume() &&
                   validatePerspectiveViewProjection() &&
                   validateInvalidBoundsIsConservative() &&
                   validatePlaneNormalization()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}

