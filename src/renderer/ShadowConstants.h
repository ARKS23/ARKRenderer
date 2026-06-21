#pragma once

#include "core/Types.h"

namespace ark {
    // Public shader/data contract: 第一版 CSM 固定最大 4 级 cascade。
    // ShadowPass 可以选择实际启用 1/2/4 级，但 CPU 常量布局和 shader 上限保持一致。
    inline constexpr u32 MaxShadowCascadeCount = 4;
} // namespace ark
