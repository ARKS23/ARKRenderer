#pragma once

#include "core/Memory.h"
#include "core/Types.h"
#include "renderer/TextureResource.h"

#include <map>
#include <string>

namespace ark::rhi {
    class RenderDevice;
} // namespace ark::rhi

namespace ark {
    class TextureCache final {
    public:
        TextureCache() = default;

        TextureResource* getOrCreate(rhi::RenderDevice& device, const TextureResourceDesc& desc);
        void clear();

        usize size() const {
            return m_Textures.size();
        }

    private:
        struct TextureCacheKey {
            std::string canonicalPath;
            TextureColorSpace colorSpace = TextureColorSpace::Linear;

            bool operator<(const TextureCacheKey& other) const {
                if (canonicalPath != other.canonicalPath) {
                    return canonicalPath < other.canonicalPath;
                }
                return static_cast<int>(colorSpace) < static_cast<int>(other.colorSpace);
            }
        };

        std::map<TextureCacheKey, Scope<TextureResource>> m_Textures;
    };
} // namespace ark
