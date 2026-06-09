#pragma once

#include "core/Memory.h"
#include "core/Types.h"
#include "renderer/TextureResource.h"

#include <map>
#include <string>

namespace ark::rhi {
    class DeviceContext;
    class RenderDevice;
} // namespace ark::rhi

namespace ark {
    enum class FallbackTextureKind {
        White,
        FlatNormal,
        MetallicRoughnessDefault,
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
                return static_cast<int>(fallbackKind) < static_cast<int>(other.fallbackKind);
            }
        };

        std::map<TextureCacheKey, Scope<TextureResource>> m_Textures;
    };
} // namespace ark
