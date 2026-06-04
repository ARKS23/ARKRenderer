#pragma once

#include "rhi/RHICommon.h"

namespace ark::rhi {
    enum class TextureUsage : u32 {
        None = 0,
        RenderTarget = 1 << 0,
        DepthStencil = 1 << 1,
        ShaderResource = 1 << 2,
        UnorderedAccess = 1 << 3,
        TransferSrc = 1 << 4,
        TransferDst = 1 << 5,
    };

    struct TextureDesc {
        Extent2D extent;
        Format format = Format::Unknown;
        u32 mipLevels = 1;
        u32 arrayLayers = 1;
        TextureUsage usage = TextureUsage::None;
    };

    class Texture {
    public:
        virtual ~Texture() = default;
    };
} // namespace ark::rhi
