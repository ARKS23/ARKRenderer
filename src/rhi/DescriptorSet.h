#pragma once

#include "core/Types.h"

namespace ark::rhi {
    class Buffer;

    struct BufferDescriptor {
        Buffer* buffer = nullptr;
        u64 offset = 0;
        u64 range = 0;
    };

    class DescriptorSet {
    public:
        virtual ~DescriptorSet() = default;

        // Phase 0.5 第一版只需要 uniform buffer，后续再扩展为通用 WriteDescriptorSetDesc。
        virtual void updateUniformBuffer(u32 binding, const BufferDescriptor& buffer) = 0;
    };
} // namespace ark::rhi
