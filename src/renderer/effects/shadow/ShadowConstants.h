#pragma once

#include "core/Types.h"

namespace ark {
    // 第一版 CSM contract 固定最多 4 级 cascade，后续做 atlas / texture array 时继续沿用该上限。
    inline constexpr u32 MaxShadowCascadeCount = 4;
} // namespace ark
