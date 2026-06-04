#pragma once

#include "core/Types.h"

namespace ark::rhi {
    // 公共 RHI 只保留抽象帧索引；具体 command buffer/sync 由后端 FrameResource 承担。
    struct FrameResource {
        u64 frameIndex = 0;
    };
} // namespace ark::rhi
