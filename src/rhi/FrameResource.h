#pragma once

#include "core/Types.h"

namespace ark::rhi {
    // 公共 RHI 只表达一帧的抽象 token；具体 command buffer/sync 由后端 FrameResource 承担。
    struct FrameResource {
        virtual ~FrameResource() = default;

        // frameSlot 是环形帧资源槽位，frameIndex 是持续递增的逻辑帧号。
        u32 frameSlot = 0;
        u64 frameIndex = 0;
    };
} // namespace ark::rhi
