#pragma once

#include "core/Memory.h"
#include "core/Types.h"
#include "renderer/resources/TextureResource.h"

#include <map>
#include <string>

namespace ark::rhi {
    class DeviceContext;
    class RenderDevice;
} // namespace ark::rhi

namespace ark {
    enum class FallbackTextureKind {
        White,
        MissingBaseColor,
        FlatNormal,
        MetallicRoughnessDefault,
        MissingMetallicRoughness,
        OcclusionDefault,
        Black,
    };

    class TextureCache final {
    public:
        TextureCache() = default;

        TextureResource* getOrCreate(rhi::RenderDevice& device, const TextureResourceDesc& desc);
        TextureResource* getOrCreateFallback(rhi::RenderDevice& device, FallbackTextureKind kind);
        bool clearDeferred(rhi::DeviceContext& context);
        void clear();

        usize size() const {
            return m_Textures.size();
        }

    private:
        enum class TextureCacheSource {
            File,
            Fallback,
        };

        struct TextureCacheKey {
            TextureCacheSource source = TextureCacheSource::File;
            // fallback 使用稳定语义 key，不伪装成磁盘文件路径。
            std::string canonicalPath;
            TextureColorSpace colorSpace = TextureColorSpace::Linear;
            FallbackTextureKind fallbackKind = FallbackTextureKind::White;
            bool hasSamplerOverride = false;
            rhi::FilterMode minFilter = rhi::FilterMode::Linear;
            rhi::FilterMode magFilter = rhi::FilterMode::Linear;
            rhi::FilterMode mipFilter = rhi::FilterMode::Linear;
            rhi::AddressMode addressU = rhi::AddressMode::Repeat;
            rhi::AddressMode addressV = rhi::AddressMode::Repeat;
            rhi::AddressMode addressW = rhi::AddressMode::Repeat;

            bool operator<(const TextureCacheKey& other) const {
                if (source != other.source) {
                    return static_cast<int>(source) < static_cast<int>(other.source);
                }
                if (canonicalPath != other.canonicalPath) {
                    return canonicalPath < other.canonicalPath;
                }
                if (colorSpace != other.colorSpace) {
                    return static_cast<int>(colorSpace) < static_cast<int>(other.colorSpace);
                }
                if (fallbackKind != other.fallbackKind) {
                    return static_cast<int>(fallbackKind) < static_cast<int>(other.fallbackKind);
                }
                if (hasSamplerOverride != other.hasSamplerOverride) {
                    return hasSamplerOverride < other.hasSamplerOverride;
                }
                if (minFilter != other.minFilter) {
                    return static_cast<int>(minFilter) < static_cast<int>(other.minFilter);
                }
                if (magFilter != other.magFilter) {
                    return static_cast<int>(magFilter) < static_cast<int>(other.magFilter);
                }
                if (mipFilter != other.mipFilter) {
                    return static_cast<int>(mipFilter) < static_cast<int>(other.mipFilter);
                }
                if (addressU != other.addressU) {
                    return static_cast<int>(addressU) < static_cast<int>(other.addressU);
                }
                if (addressV != other.addressV) {
                    return static_cast<int>(addressV) < static_cast<int>(other.addressV);
                }
                return static_cast<int>(addressW) < static_cast<int>(other.addressW);
            }
        };

        std::map<TextureCacheKey, Scope<TextureResource>> m_Textures;
    };
} // namespace ark
