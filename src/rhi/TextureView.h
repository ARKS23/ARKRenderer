#pragma once

#include "rhi/RHICommon.h"

namespace ark::rhi {
    class Texture;

    struct TextureViewDesc {
        Format format = Format::Unknown;
        u32 baseMipLevel = 0;
        u32 mipLevelCount = 1;
        u32 baseArrayLayer = 0;
        u32 arrayLayerCount = 1;
    };

    class TextureView {
    public:
        virtual ~TextureView() = default;

        virtual Texture* getTexture() const = 0;
        virtual const TextureViewDesc& getDesc() const = 0;
    };
} // namespace ark::rhi
