#pragma once

#include "asset/MeshData.h"
#include "core/Types.h"
#include "renderer/PostProcessingSettings.h"
#include "renderer/ShadowConstants.h"
#include "rhi/RHICommon.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <string_view>

namespace ark {
    enum class ToneMappingOperator : u32 {
        Reinhard = 0,
        Linear = 1,
        ACES = 2,
    };

    enum class ShadowFilterMode : u32 {
        Hard = 0,
        Pcf3x3 = 1,
        Pcf5x5 = 2,
    };

    struct ToneMappingSettings {
        float exposure = 1.0f;
        float outputGamma = 2.2f;
        ToneMappingOperator operatorType = ToneMappingOperator::Reinhard;
    };

    struct CascadeShadowSettings {
        // CSM 运行时设置；0.67.2 只建立数据契约，实际多 cascade 渲染由后续阶段接入。
        bool enabled = false;
        // 第一版只允许 1 / 2 / 4 级，避免 UI 和 shader contract 先膨胀成任意数量。
        u32 cascadeCount = MaxShadowCascadeCount;
        // 0 表示线性切分，1 表示对数切分，中间值混合两者以兼顾近处精度和远处覆盖。
        float splitLambda = 0.65f;
        // CSM 阴影覆盖距离独立于相机 far plane，防止远平面过大导致 cascade texel density 被稀释。
        float maxDistance = 80.0f;
        // 每一级 cascade 使用正方形 shadow map；后续 texture array/atlas 均沿用该边长。
        u32 cascadeExtent = 2048;
        // 后续复用 texel snapping 稳定 cascade 投影，降低相机移动时的阴影抖动。
        bool stabilize = true;
    };

    struct ShadowSettings {
        bool enabled = false;
        float strength = 0.7f;
        float bias = 0.0015f;
        u32 mapExtent = 1024;
        float orthographicHalfExtent = 8.0f;
        float nearPlane = 0.1f;
        float farPlane = 64.0f;
        float lightDistance = 16.0f;
        bool fitSceneBounds = true;
        bool stabilizeProjection = true;
        ShadowFilterMode filterMode = ShadowFilterMode::Hard;
        float filterRadiusTexels = 1.0f;
        CascadeShadowSettings cascades;
    };

    struct VisibilitySettings {
        bool enableFrustumCulling = false;
    };

    ToneMappingOperator parseToneMappingOperator(
        std::string_view name,
        ToneMappingOperator fallback = ToneMappingOperator::Reinhard);
    ShadowFilterMode parseShadowFilterMode(
        std::string_view name,
        ShadowFilterMode fallback = ShadowFilterMode::Hard);

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

        const ShadowSettings& shadowSettings() const {
            return m_ShadowSettings;
        }

        void setShadowSettings(const ShadowSettings& settings) {
            m_ShadowSettings = sanitizeShadowSettings(settings);
        }

        const VisibilitySettings& visibilitySettings() const {
            return m_VisibilitySettings;
        }

        void setVisibilitySettings(const VisibilitySettings& settings) {
            m_VisibilitySettings = settings;
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

        static ShadowSettings sanitizeShadowSettings(const ShadowSettings& settings) {
            ShadowSettings sanitized = settings;
            sanitized.strength = std::clamp(sanitized.strength, 0.0f, 1.0f);
            sanitized.bias = std::clamp(sanitized.bias, 0.0f, 0.05f);
            sanitized.mapExtent = std::clamp(sanitized.mapExtent, 128u, 4096u);
            sanitized.orthographicHalfExtent = std::clamp(sanitized.orthographicHalfExtent, 1.0f, 128.0f);
            sanitized.nearPlane = std::clamp(sanitized.nearPlane, 0.01f, 512.0f);
            sanitized.farPlane = std::clamp(sanitized.farPlane, sanitized.nearPlane + 0.01f, 1024.0f);
            sanitized.lightDistance = std::clamp(sanitized.lightDistance, 1.0f, 512.0f);
            if (sanitized.filterMode != ShadowFilterMode::Hard &&
                sanitized.filterMode != ShadowFilterMode::Pcf3x3 &&
                sanitized.filterMode != ShadowFilterMode::Pcf5x5) {
                sanitized.filterMode = ShadowFilterMode::Hard;
            }
            if (!std::isfinite(sanitized.filterRadiusTexels)) {
                sanitized.filterRadiusTexels = 1.0f;
            }
            sanitized.filterRadiusTexels = std::clamp(sanitized.filterRadiusTexels, 0.0f, 8.0f);
            // CSM 参数来自 preset/UI，进入 renderer 前统一收敛到当前后端和 shader 能承受的范围。
            sanitized.cascades.cascadeCount = sanitizeCascadeCount(sanitized.cascades.cascadeCount);
            sanitized.cascades.splitLambda = sanitizeFiniteRange(sanitized.cascades.splitLambda, 0.0f, 1.0f, 0.65f);
            sanitized.cascades.maxDistance =
                sanitizeFiniteRange(sanitized.cascades.maxDistance, sanitized.nearPlane + 0.01f, 10000.0f, 80.0f);
            sanitized.cascades.cascadeExtent = std::clamp(sanitized.cascades.cascadeExtent, 128u, 4096u);
            if (sanitized.strength <= 0.0f) {
                sanitized.enabled = false;
            }
            return sanitized;
        }

        static u32 sanitizeCascadeCount(u32 cascadeCount) {
            if (cascadeCount <= 1u) {
                return 1u;
            }
            if (cascadeCount <= 2u) {
                return 2u;
            }
            return MaxShadowCascadeCount;
        }

        static float sanitizeFiniteRange(float value, float minValue, float maxValue, float fallback) {
            if (!std::isfinite(value)) {
                value = fallback;
            }
            return std::clamp(value, minValue, maxValue);
        }

        glm::mat4 m_View{1.0f};
        glm::mat4 m_Projection{1.0f};
        glm::vec3 m_CameraPosition{0.0f};
        ToneMappingSettings m_ToneMappingSettings;
        PostProcessingSettings m_PostProcessingSettings;
        ShadowSettings m_ShadowSettings;
        VisibilitySettings m_VisibilitySettings;
    };
} // namespace ark
