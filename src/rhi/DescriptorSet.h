#pragma once

#include "core/Types.h"

namespace ark::rhi {
    class Buffer;
    class Sampler;
    class TextureView;

    struct BufferDescriptor {
        Buffer* buffer = nullptr;
        u64 offset = 0;
        u64 range = 0;
    };

    struct SampledImageDescriptor {
        TextureView* view = nullptr;
    };

    struct SamplerDescriptor {
        Sampler* sampler = nullptr;
    };

    class DescriptorSet {
    public:
        virtual ~DescriptorSet() = default;

        virtual void updateUniformBuffer(u32 binding, const BufferDescriptor& buffer) = 0;
        virtual void updateSampledImage(u32 binding, const SampledImageDescriptor& image) = 0;
        virtual void updateSampler(u32 binding, const SamplerDescriptor& sampler) = 0;
    };
} // namespace ark::rhi
