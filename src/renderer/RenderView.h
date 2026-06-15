#pragma once

#include "asset/MeshData.h"
#include "renderer/PostProcessingSettings.h"
#include "rhi/RHICommon.h"

#include <cmath>
#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace ark {
    struct ToneMappingSettings {
        float exposure = 1.0f;
        float outputGamma = 2.2f;
    };

    class RenderView {
    public:
        void setMatrices(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition) {
            m_View = view;
            m_Projection = projection;
            m_CameraPosition = cameraPosition;
        }

        void setMatrices(const glm::mat4& view, const glm::mat4& projection) {
            m_View = view;
            m_Projection = projection;
            m_CameraPosition = glm::vec3{glm::affineInverse(view)[3]};
        }

        void setDefaultPerspective(rhi::Extent2D extent) {
            const float aspect =
                extent.height == 0 ? 1.0f : static_cast<float>(extent.width) / static_cast<float>(extent.height);

            m_CameraPosition = glm::vec3{0.0f, 0.0f, -4.0f};
            m_View = glm::lookAt(m_CameraPosition, glm::vec3{0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
            m_Projection = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 0.1f, 100.0f);
            m_Projection[1][1] *= -1.0f;
        }

        bool setPerspectiveCamera(const asset::CameraData& camera,
                                  const asset::TransformData& worldTransform,
                                  rhi::Extent2D extent) {
            if (camera.type != asset::CameraProjectionType::Perspective) {
                return false;
            }

            const asset::PerspectiveCameraData& perspective = camera.perspective;
            const float farPlane = perspective.hasZfar ? perspective.zfar : 1000.0f;
            if (!isFinitePositive(perspective.yfov) ||
                !isFinitePositive(perspective.znear) ||
                !isFinitePositive(farPlane) ||
                farPlane <= perspective.znear) {
                return false;
            }

            const float aspect =
                perspective.aspectRatio > 0.0f ? perspective.aspectRatio : aspectFromExtent(extent);
            if (!isFinitePositive(aspect)) {
                return false;
            }

            const glm::mat4 worldMatrix = toMat4(worldTransform);
            glm::mat4 projection = glm::perspectiveRH_ZO(perspective.yfov, aspect, perspective.znear, farPlane);
            projection[1][1] *= -1.0f;
            setMatrices(glm::affineInverse(worldMatrix), projection, glm::vec3{worldMatrix[3]});
            return true;
        }

        const glm::mat4& viewMatrix() const {
            return m_View;
        }

        const glm::mat4& projectionMatrix() const {
            return m_Projection;
        }

        const glm::vec3& cameraPosition() const {
            return m_CameraPosition;
        }

        const ToneMappingSettings& toneMappingSettings() const {
            return m_ToneMappingSettings;
        }

        void setToneMappingSettings(const ToneMappingSettings& settings) {
            m_ToneMappingSettings = settings;
        }

        const PostProcessingSettings& postProcessingSettings() const {
            return m_PostProcessingSettings;
        }

        void setPostProcessingSettings(const PostProcessingSettings& settings) {
            m_PostProcessingSettings = sanitizePostProcessingSettings(settings);
        }

    private:
        static bool isFinitePositive(float value) {
            return std::isfinite(value) && value > 0.0f;
        }

        static float aspectFromExtent(rhi::Extent2D extent) {
            if (extent.width == 0 || extent.height == 0) {
                return 1.0f;
            }

            return static_cast<float>(extent.width) / static_cast<float>(extent.height);
        }

        static glm::mat4 toMat4(const asset::TransformData& transform) {
            glm::mat4 matrix{1.0f};
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    matrix[column][row] = transform.matrix[column * 4 + row];
                }
            }
            return matrix;
        }

        glm::mat4 m_View{1.0f};
        glm::mat4 m_Projection{1.0f};
        glm::vec3 m_CameraPosition{0.0f};
        ToneMappingSettings m_ToneMappingSettings;
        PostProcessingSettings m_PostProcessingSettings;
    };
} // namespace ark
