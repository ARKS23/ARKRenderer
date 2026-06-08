#include "asset/TextureLoader.h"

#include "core/Log.h"

// stb_image 的实现只放在一个 .cpp 中，避免多个翻译单元重复定义。
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <limits>

namespace ark::asset {
    namespace {
        constexpr i32 RequestedChannels = 4;
        constexpr u32 Rgba8BytesPerPixel = 4;

        // 防止无效尺寸和像素字节数在分配 vector 前溢出。
        bool isImageSizeValid(i32 width, i32 height) {
            if (width <= 0 || height <= 0) {
                return false;
            }

            const u64 pixelCount = static_cast<u64>(width) * static_cast<u64>(height);
            const u64 maxByteSize = static_cast<u64>(std::numeric_limits<usize>::max());
            return pixelCount <= maxByteSize / Rgba8BytesPerPixel;
        }
    } // namespace

    ImageData TextureLoader::loadRgba8(const Path& path) {
        return loadImageRgba8(path);
    }

    ImageData loadImageRgba8(const Path& path) {
        ImageData image{};
        image.debugName = path.string();

        // 文件读取保持在 core/asset 层，TextureLoader 不接触 renderer 或 RHI。
        const std::vector<u8> fileData = readBinaryFile(path);
        if (fileData.empty()) {
            ARK_ERROR("Failed to read image file: {}", path.string());
            return {};
        }

        if (fileData.size() > static_cast<usize>(std::numeric_limits<int>::max())) {
            ARK_ERROR("Image file is too large for stb_image: {}", path.string());
            return {};
        }

        const auto* bytes = reinterpret_cast<const stbi_uc*>(fileData.data());
        const int byteCount = static_cast<int>(fileData.size());

        // HDR 必须走独立 float 路径，不能在 LDR loader 中静默量化到 8-bit。
        if (stbi_is_hdr_from_memory(bytes, byteCount) != 0) {
            ARK_ERROR("HDR image is not supported by loadImageRgba8: {}", path.string());
            return {};
        }

        i32 width = 0;
        i32 height = 0;
        i32 sourceChannels = 0;
        // 强制输出 4 通道，后续 upload 可以按 tightly packed RGBA8 处理。
        stbi_uc* decodedPixels = stbi_load_from_memory(bytes, byteCount, &width, &height, &sourceChannels, RequestedChannels);
        if (!decodedPixels) {
            const char* reason = stbi_failure_reason();
            ARK_ERROR("Failed to decode image as RGBA8: {} ({})", path.string(), reason ? reason : "unknown error");
            return {};
        }

        if (!isImageSizeValid(width, height)) {
            ARK_ERROR("Decoded image has invalid dimensions: {}", path.string());
            stbi_image_free(decodedPixels);
            return {};
        }

        image.width = static_cast<u32>(width);
        image.height = static_cast<u32>(height);
        image.format = ImageFormat::Rgba8Unorm;
        image.bytesPerPixel = Rgba8BytesPerPixel;

        const usize byteSize = static_cast<usize>(image.width) * static_cast<usize>(image.height) * Rgba8BytesPerPixel;
        // stb 返回的内存由 stb 管理；拷贝进 ImageData 后立即释放临时内存。
        image.pixels.assign(decodedPixels, decodedPixels + byteSize);
        stbi_image_free(decodedPixels);

        return image;
    }
} // namespace ark::asset
