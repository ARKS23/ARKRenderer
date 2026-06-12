#pragma once

#include "rhi/RHICommon.h"
#include "rhi/ResourceBarrier.h"

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

    constexpr TextureUsage operator|(TextureUsage lhs, TextureUsage rhs) {
        return static_cast<TextureUsage>(static_cast<u32>(lhs) | static_cast<u32>(rhs));
    }

    constexpr TextureUsage operator&(TextureUsage lhs, TextureUsage rhs) {
        return static_cast<TextureUsage>(static_cast<u32>(lhs) & static_cast<u32>(rhs));
    }

    constexpr bool hasTextureUsage(TextureUsage value, TextureUsage flag) {
        return static_cast<u32>(value & flag) != 0;
    }

    enum class TextureType {
        Texture2D,
        Cube,
    };

    constexpr u32 calculateMipLevelCount(Extent2D extent) {
        if (!isValidExtent(extent)) {
            return 0;
        }

        u32 levels = 1;
        while (extent.width > 1 || extent.height > 1) {
            extent.width = extent.width > 1 ? extent.width / 2 : 1;
            extent.height = extent.height > 1 ? extent.height / 2 : 1;
            ++levels;
        }

        return levels;
    }

    struct TextureDesc {
        Extent2D extent;
        Format format = Format::Unknown;
        u32 mipLevels = 1;
        u32 arrayLayers = 1;
        TextureUsage usage = TextureUsage::None;
        TextureType type = TextureType::Texture2D;
    };

    class Texture {
    public:
        virtual ~Texture() = default;

        virtual const TextureDesc& getDesc() const = 0;
        virtual ResourceState getState() const = 0;
    };
} // namespace ark::rhi
