#include "renderer/TextureCache.h"

#include "asset/TextureLoader.h"
#include "core/Log.h"
#include "rhi/RenderDevice.h"

#include <filesystem>
#include <system_error>
#include <utility>

namespace ark {
    namespace {
        std::string normalizeTexturePath(const Path& path) {
            std::error_code error;
            Path normalized = std::filesystem::weakly_canonical(path, error);
            if (error) {
                normalized = path.lexically_normal();
            }

            return normalized.generic_string();
        }

        const char* colorSpaceName(TextureColorSpace colorSpace) {
            switch (colorSpace) {
            case TextureColorSpace::Linear:
                return "Linear";
            case TextureColorSpace::Srgb:
                return "Srgb";
            }

            return "Unknown";
        }
    } // namespace

    TextureResource* TextureCache::getOrCreate(rhi::RenderDevice& device, const TextureResourceDesc& desc) {
        if (desc.path.empty()) {
            ARK_ERROR("TextureCache requires a texture path");
            return nullptr;
        }

        TextureCacheKey key{};
        key.canonicalPath = normalizeTexturePath(desc.path);
        key.colorSpace = desc.colorSpace;

        auto existing = m_Textures.find(key);
        if (existing != m_Textures.end()) {
            return existing->second.get();
        }

        asset::ImageData image = asset::loadImageRgba8(desc.path);
        if (image.empty()) {
            ARK_ERROR("TextureCache failed to load image: {}", desc.path.string());
            return nullptr;
        }

        TextureResourceDesc resourceDesc = desc;
        resourceDesc.debugName =
            resourceDesc.debugName.empty() ? "Texture." + std::string(colorSpaceName(desc.colorSpace))
                                           : resourceDesc.debugName;

        Scope<TextureResource> texture = makeScope<TextureResource>();
        if (!texture->create(device, image, resourceDesc)) {
            ARK_ERROR("TextureCache failed to create texture resource: {}", desc.path.string());
            return nullptr;
        }

        TextureResource* result = texture.get();
        m_Textures.emplace(std::move(key), std::move(texture));
        return result;
    }

    void TextureCache::clear() {
        m_Textures.clear();
    }
} // namespace ark
