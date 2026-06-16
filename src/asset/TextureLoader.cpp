#include "asset/TextureLoader.h"

#include "core/Log.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <ktx.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <memory>

namespace ark::asset {
    namespace {
        constexpr i32 RequestedChannels = 4;
        constexpr u32 Rgba8BytesPerPixel = 4;
        constexpr u32 Rgba32FloatBytesPerPixel = 16;
        constexpr ktx_uint32_t GlUnsignedByte = 0x1401;
        constexpr ktx_uint32_t GlRgba = 0x1908;
        constexpr ktx_uint32_t GlRgba8 = 0x8058;
        constexpr ktx_uint32_t GlSrgb8Alpha8 = 0x8C43;
        constexpr ktx_uint32_t VkFormatR8G8B8A8Unorm = 37;
        constexpr ktx_uint32_t VkFormatR8G8B8A8Srgb = 43;

        struct KtxTextureDestroyer {
            void operator()(ktxTexture* texture) const {
                if (texture) {
                    ktxTexture_Destroy(texture);
                }
            }
        };

        using KtxTexturePtr = std::unique_ptr<ktxTexture, KtxTextureDestroyer>;

        bool isImageSizeValid(i32 width, i32 height, u32 bytesPerPixel) {
            if (width <= 0 || height <= 0) {
                return false;
            }

            const u64 pixelCount = static_cast<u64>(width) * static_cast<u64>(height);
            const u64 maxByteSize = static_cast<u64>(std::numeric_limits<usize>::max());
            return pixelCount <= maxByteSize / bytesPerPixel;
        }

        bool isImageSizeValid(u32 width, u32 height, u32 bytesPerPixel) {
            if (width == 0 || height == 0) {
                return false;
            }

            const u64 pixelCount = static_cast<u64>(width) * static_cast<u64>(height);
            const u64 maxByteSize = static_cast<u64>(std::numeric_limits<usize>::max());
            return pixelCount <= maxByteSize / bytesPerPixel;
        }

        bool isFileSizeValidForStb(usize fileSize) {
            return fileSize <= static_cast<usize>(std::numeric_limits<int>::max());
        }

        std::string lowerExtension(const Path& path) {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return extension;
        }

        bool isSupportedKtx1Rgba8(const ktxTexture1& texture) {
            return texture.glType == GlUnsignedByte && texture.glFormat == GlRgba &&
                   texture.glBaseInternalformat == GlRgba &&
                   (texture.glInternalformat == GlRgba8 || texture.glInternalformat == GlSrgb8Alpha8);
        }

        bool isSupportedKtx2Rgba8(const ktxTexture2& texture) {
            return texture.supercompressionScheme == KTX_SS_NONE &&
                   (texture.vkFormat == VkFormatR8G8B8A8Unorm || texture.vkFormat == VkFormatR8G8B8A8Srgb);
        }

        bool validateKtxTextureShape(const ktxTexture& texture, const Path& path) {
            if (texture.numDimensions != 2 || texture.baseWidth == 0 || texture.baseHeight == 0 ||
                texture.baseDepth > 1 || texture.numFaces != 1 || texture.isArray || texture.numLayers > 1 ||
                texture.isCubemap || texture.isCompressed || texture.numLevels == 0) {
                ARK_ERROR("Unsupported KTX texture shape: {} ({}D, {}x{}x{}, layers {}, faces {}, compressed {})",
                          path.string(),
                          texture.numDimensions,
                          texture.baseWidth,
                          texture.baseHeight,
                          texture.baseDepth,
                          texture.numLayers,
                          texture.numFaces,
                          texture.isCompressed != 0);
                return false;
            }

            return true;
        }

        bool validateKtxTextureFormat(const ktxTexture& texture, const Path& path) {
            if (texture.classId == ktxTexture1_c) {
                const auto* texture1 = reinterpret_cast<const ktxTexture1*>(&texture);
                if (!isSupportedKtx1Rgba8(*texture1)) {
                    ARK_ERROR("Unsupported KTX1 texture format: {} (glType {:#x}, glFormat {:#x}, glInternalFormat {:#x})",
                              path.string(),
                              texture1->glType,
                              texture1->glFormat,
                              texture1->glInternalformat);
                    return false;
                }

                return true;
            }

            if (texture.classId == ktxTexture2_c) {
                const auto* texture2 = reinterpret_cast<const ktxTexture2*>(&texture);
                if (!isSupportedKtx2Rgba8(*texture2)) {
                    ARK_ERROR("Unsupported KTX2 texture format: {} (vkFormat {}, supercompression {})",
                              path.string(),
                              texture2->vkFormat,
                              static_cast<int>(texture2->supercompressionScheme));
                    return false;
                }

                return true;
            }

            ARK_ERROR("Unsupported KTX texture class: {}", path.string());
            return false;
        }
    } // namespace

    ImageData TextureLoader::loadRgba8(const Path& path) {
        return loadImageRgba8(path);
    }

    ImageData TextureLoader::loadHdrRgba32F(const Path& path) {
        return loadImageHdrRgba32F(path);
    }

    ImageData TextureLoader::loadKtx(const Path& path) {
        return loadImageKtx(path);
    }

    ImageData TextureLoader::loadAuto(const Path& path) {
        return loadImageAuto(path);
    }

    ImageData loadImageRgba8(const Path& path) {
        ImageData image{};
        image.debugName = path.string();

        const std::vector<u8> fileData = readBinaryFile(path);
        if (fileData.empty()) {
            ARK_ERROR("Failed to read image file: {}", path.string());
            return {};
        }

        if (!isFileSizeValidForStb(fileData.size())) {
            ARK_ERROR("Image file is too large for stb_image: {}", path.string());
            return {};
        }

        const auto* bytes = reinterpret_cast<const stbi_uc*>(fileData.data());
        const int byteCount = static_cast<int>(fileData.size());

        if (stbi_is_hdr_from_memory(bytes, byteCount) != 0) {
            ARK_ERROR("HDR image is not supported by loadImageRgba8: {}", path.string());
            return {};
        }

        i32 width = 0;
        i32 height = 0;
        i32 sourceChannels = 0;
        stbi_uc* decodedPixels =
            stbi_load_from_memory(bytes, byteCount, &width, &height, &sourceChannels, RequestedChannels);
        if (!decodedPixels) {
            const char* reason = stbi_failure_reason();
            ARK_ERROR("Failed to decode image as RGBA8: {} ({})", path.string(), reason ? reason : "unknown error");
            return {};
        }

        if (!isImageSizeValid(width, height, Rgba8BytesPerPixel)) {
            ARK_ERROR("Decoded image has invalid dimensions: {}", path.string());
            stbi_image_free(decodedPixels);
            return {};
        }

        image.width = static_cast<u32>(width);
        image.height = static_cast<u32>(height);
        image.format = ImageFormat::Rgba8Unorm;
        image.bytesPerPixel = Rgba8BytesPerPixel;

        const usize byteSize = static_cast<usize>(image.width) * static_cast<usize>(image.height) * Rgba8BytesPerPixel;
        image.pixels.assign(decodedPixels, decodedPixels + byteSize);
        stbi_image_free(decodedPixels);

        return image;
    }

    ImageData loadImageHdrRgba32F(const Path& path) {
        ImageData image{};
        image.debugName = path.string();

        const std::vector<u8> fileData = readBinaryFile(path);
        if (fileData.empty()) {
            ARK_ERROR("Failed to read HDR image file: {}", path.string());
            return {};
        }

        if (!isFileSizeValidForStb(fileData.size())) {
            ARK_ERROR("HDR image file is too large for stb_image: {}", path.string());
            return {};
        }

        const auto* bytes = reinterpret_cast<const stbi_uc*>(fileData.data());
        const int byteCount = static_cast<int>(fileData.size());
        if (stbi_is_hdr_from_memory(bytes, byteCount) == 0) {
            ARK_ERROR("Image is not an HDR image: {}", path.string());
            return {};
        }

        i32 width = 0;
        i32 height = 0;
        i32 sourceChannels = 0;
        float* decodedPixels = stbi_loadf_from_memory(bytes, byteCount, &width, &height, &sourceChannels, RequestedChannels);
        if (!decodedPixels) {
            const char* reason = stbi_failure_reason();
            ARK_ERROR("Failed to decode image as RGBA32F: {} ({})", path.string(), reason ? reason : "unknown error");
            return {};
        }

        if (!isImageSizeValid(width, height, Rgba32FloatBytesPerPixel)) {
            ARK_ERROR("Decoded HDR image has invalid dimensions: {}", path.string());
            stbi_image_free(decodedPixels);
            return {};
        }

        image.width = static_cast<u32>(width);
        image.height = static_cast<u32>(height);
        image.format = ImageFormat::Rgba32Float;
        image.bytesPerPixel = Rgba32FloatBytesPerPixel;

        const usize byteSize = static_cast<usize>(image.width) * static_cast<usize>(image.height) * Rgba32FloatBytesPerPixel;
        image.pixels.resize(byteSize);
        std::memcpy(image.pixels.data(), decodedPixels, byteSize);
        stbi_image_free(decodedPixels);

        return image;
    }

    ImageData loadImageKtx(const Path& path) {
        ImageData image{};
        image.debugName = path.string();

        const std::vector<u8> fileData = readBinaryFile(path);
        if (fileData.empty()) {
            ARK_ERROR("Failed to read KTX image file: {}", path.string());
            return {};
        }

        ktxTexture* rawTexture = nullptr;
        const KTX_error_code createStatus = ktxTexture_CreateFromMemory(fileData.data(),
                                                                        fileData.size(),
                                                                        KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
                                                                        &rawTexture);
        KtxTexturePtr texture{rawTexture};
        if (createStatus != KTX_SUCCESS || !texture) {
            ARK_ERROR("Failed to decode KTX image: {} ({})", path.string(), ktxErrorString(createStatus));
            return {};
        }

        if (!validateKtxTextureShape(*texture, path) || !validateKtxTextureFormat(*texture, path)) {
            return {};
        }

        const u32 width = texture->baseWidth;
        const u32 height = texture->baseHeight;
        if (!isImageSizeValid(width, height, Rgba8BytesPerPixel)) {
            ARK_ERROR("Decoded KTX image has invalid dimensions: {}", path.string());
            return {};
        }

        ktx_size_t baseMipOffset = 0;
        const KTX_error_code offsetStatus = ktxTexture_GetImageOffset(texture.get(), 0, 0, 0, &baseMipOffset);
        if (offsetStatus != KTX_SUCCESS) {
            ARK_ERROR("Failed to locate KTX base mip: {} ({})", path.string(), ktxErrorString(offsetStatus));
            return {};
        }

        const usize byteSize = static_cast<usize>(width) * static_cast<usize>(height) * Rgba8BytesPerPixel;
        const ktx_size_t dataSize = ktxTexture_GetDataSize(texture.get());
        const ktx_uint8_t* data = ktxTexture_GetData(texture.get());
        if (!data || baseMipOffset > dataSize || static_cast<ktx_size_t>(byteSize) > dataSize - baseMipOffset) {
            ARK_ERROR("KTX base mip data is out of range: {}", path.string());
            return {};
        }

        image.width = width;
        image.height = height;
        image.format = ImageFormat::Rgba8Unorm;
        image.bytesPerPixel = Rgba8BytesPerPixel;
        image.pixels.assign(data + baseMipOffset, data + baseMipOffset + byteSize);
        return image;
    }

    ImageData loadImageAuto(const Path& path) {
        const std::string extension = lowerExtension(path);
        if (extension == ".hdr") {
            return loadImageHdrRgba32F(path);
        }

        if (extension == ".ktx" || extension == ".ktx2") {
            return loadImageKtx(path);
        }

        return loadImageRgba8(path);
    }
} // namespace ark::asset
