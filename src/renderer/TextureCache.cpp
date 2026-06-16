#include "renderer/TextureCache.h"

#include "asset/TextureLoader.h"
#include "core/Log.h"
#include "rhi/DeviceContext.h"
#include "rhi/RenderDevice.h"

#include <array>
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

        const char* fallbackTextureName(FallbackTextureKind kind) {
            switch (kind) {
            case FallbackTextureKind::White:
                return "White";
            case FallbackTextureKind::MissingBaseColor:
                return "MissingBaseColor";
            case FallbackTextureKind::FlatNormal:
                return "FlatNormal";
            case FallbackTextureKind::MetallicRoughnessDefault:
                return "MetallicRoughnessDefault";
            case FallbackTextureKind::MissingMetallicRoughness:
                return "MissingMetallicRoughness";
            case FallbackTextureKind::OcclusionDefault:
                return "OcclusionDefault";
            case FallbackTextureKind::Black:
                return "Black";
            }

            return "Unknown";
        }

        TextureColorSpace fallbackColorSpace(FallbackTextureKind kind) {
            switch (kind) {
            case FallbackTextureKind::White:
            case FallbackTextureKind::MissingBaseColor:
            case FallbackTextureKind::Black:
                return TextureColorSpace::Srgb;
            case FallbackTextureKind::FlatNormal:
            case FallbackTextureKind::MetallicRoughnessDefault:
            case FallbackTextureKind::MissingMetallicRoughness:
            case FallbackTextureKind::OcclusionDefault:
                return TextureColorSpace::Linear;
            }

            return TextureColorSpace::Linear;
        }

        std::array<u8, 4> fallbackPixel(FallbackTextureKind kind) {
            switch (kind) {
            case FallbackTextureKind::White:
            case FallbackTextureKind::OcclusionDefault:
                return {255, 255, 255, 255};
            case FallbackTextureKind::MissingBaseColor:
                return {170, 160, 145, 255};
            case FallbackTextureKind::MetallicRoughnessDefault:
                return {255, 255, 255, 255};
            case FallbackTextureKind::MissingMetallicRoughness:
                return {0, 230, 0, 255};
            case FallbackTextureKind::FlatNormal:
                return {128, 128, 255, 255};
            case FallbackTextureKind::Black:
                return {0, 0, 0, 255};
            }

            return {255, 255, 255, 255};
        }

        asset::ImageData makeFallbackImage(FallbackTextureKind kind) {
            const std::array<u8, 4> pixel = fallbackPixel(kind);

            asset::ImageData image{};
            image.width = 1;
            image.height = 1;
            image.format = asset::ImageFormat::Rgba8Unorm;
            image.bytesPerPixel = 4;
            image.pixels.assign(pixel.begin(), pixel.end());
            image.debugName = std::string("FallbackTexture.") + fallbackTextureName(kind);
            return image;
        }
    } // namespace

    TextureResource* TextureCache::getOrCreate(rhi::RenderDevice& device, const TextureResourceDesc& desc) {
        if (desc.path.empty()) {
            ARK_ERROR("TextureCache requires a texture path");
            return nullptr;
        }

        TextureCacheKey key{};
        key.source = TextureCacheSource::File;
        key.canonicalPath = normalizeTexturePath(desc.path);
        key.colorSpace = desc.colorSpace;
        key.hasSamplerOverride = desc.hasSamplerOverride;
        if (desc.hasSamplerOverride) {
            key.minFilter = desc.sampler.minFilter;
            key.magFilter = desc.sampler.magFilter;
            key.mipFilter = desc.sampler.mipFilter;
            key.addressU = desc.sampler.addressU;
            key.addressV = desc.sampler.addressV;
            key.addressW = desc.sampler.addressW;
        }

        auto existing = m_Textures.find(key);
        if (existing != m_Textures.end()) {
            return existing->second.get();
        }

        asset::ImageData image = asset::loadImageAuto(desc.path);
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

    TextureResource* TextureCache::getOrCreateFallback(rhi::RenderDevice& device, FallbackTextureKind kind) {
        TextureCacheKey key{};
        key.source = TextureCacheSource::Fallback;
        key.canonicalPath = std::string("fallback:") + fallbackTextureName(kind);
        key.colorSpace = fallbackColorSpace(kind);
        key.fallbackKind = kind;

        auto existing = m_Textures.find(key);
        if (existing != m_Textures.end()) {
            return existing->second.get();
        }

        asset::ImageData image = makeFallbackImage(kind);

        TextureResourceDesc resourceDesc{};
        resourceDesc.colorSpace = key.colorSpace;
        resourceDesc.generateMips = false;
        resourceDesc.debugName = image.debugName + "." + colorSpaceName(key.colorSpace);

        Scope<TextureResource> texture = makeScope<TextureResource>();
        if (!texture->create(device, image, resourceDesc)) {
            ARK_ERROR("TextureCache failed to create fallback texture: {}", fallbackTextureName(kind));
            return nullptr;
        }

        TextureResource* result = texture.get();
        m_Textures.emplace(std::move(key), std::move(texture));
        return result;
    }

    bool TextureCache::clearDeferred(rhi::DeviceContext& context) {
        // 调用方必须先确保没有后续 draw 继续引用这些 TextureResource 指针。
        for (auto& [key, texture] : m_Textures) {
            if (texture && !texture->releaseDeferred(context)) {
                ARK_ERROR("TextureCache failed to defer texture release: {}", key.canonicalPath);
                return false;
            }
        }

        m_Textures.clear();
        return true;
    }

    void TextureCache::clear() {
        // shutdown / GPU idle 路径使用；运行期卸载应走 clearDeferred()。
        m_Textures.clear();
    }
} // namespace ark
