#pragma once

#include "renderer/effects/shadow/ShadowCascade.h"

#include <array>

namespace ark {
    class RenderView;
    struct SceneLighting;
    struct ShadowSettings;

    struct CascadeSplitDistances {
        u32 cascadeCount = 0;
        // 级联边界距离数组。N 个 cascade 会产生 N+1 个边界：
        // cascade i 覆盖 [distances[i], distances[i + 1]]。
        std::array<float, MaxShadowCascadeCount + 1> distances{};

        bool isValid() const {
            return cascadeCount > 0 && cascadeCount <= MaxShadowCascadeCount;
        }
    };

    // 使用 practical split scheme 计算相机深度切片边界，供 CSM builder 和后续 shader contract 复用。
    CascadeSplitDistances computeCascadeSplitDistances(const ShadowSettings& settings);
    CascadeSplitDistances computeCascadeSplitDistances(float nearDistance,
                                                       float farDistance,
                                                       u32 cascadeCount,
                                                       float splitLambda);

    // 基于当前相机矩阵、主方向光和 CSM 设置生成每级 cascade 的 light-space projection。
    CascadeShadowFrameData buildCascadeShadowFrameData(const RenderView& view,
                                                       const SceneLighting& lighting,
                                                       const ShadowSettings& settings);
} // namespace ark
