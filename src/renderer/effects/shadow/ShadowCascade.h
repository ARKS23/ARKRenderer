#pragma once

#include "renderer/Bounds.h"
#include "renderer/effects/shadow/ShadowConstants.h"

#include <array>
#include <glm/mat4x4.hpp>

namespace ark {
    // 单个 cascade 的帧内描述。它记录相机深度切片和对应的光源投影，供后续 shader 选择 cascade 后采样。
    struct ShadowCascade {
        float nearDistance = 0.0f;
        float farDistance = 0.0f;
        glm::mat4 lightViewProjection{1.0f};
        Bounds3 worldBounds;
    };

    // ShadowPass / CSM builder 写入，ForwardPass 读取。当前只是 contract，真实 texture array 绑定后续阶段接入。
    struct CascadeShadowFrameData {
        bool enabled = false;
        u32 cascadeCount = 0;
        u32 cascadeExtent = 0;
        std::array<ShadowCascade, MaxShadowCascadeCount> cascades{};

        bool isEnabled() const {
            return enabled && cascadeCount > 0;
        }
    };
} // namespace ark
