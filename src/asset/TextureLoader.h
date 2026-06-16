#pragma once

#include "core/FileSystem.h"
#include "core/Types.h"

#include <string>
#include <vector>

namespace ark::asset {
    // CPU-side decoded image format. GPU format selection stays in renderer/RHI.
    enum class ImageFormat {
        Unknown,
        Rgba8Unorm,
        Rgba32Float,
    };

    // Decoded CPU pixel payload only. It does not own or create GPU/RHI resources.
    struct ImageData {
        u32 width = 0;
        u32 height = 0;
        ImageFormat format = ImageFormat::Unknown;
        u32 bytesPerPixel = 0;
        std::vector<u8> pixels;
        std::string debugName;

        bool empty() const {
            return width == 0 || height == 0 || pixels.empty();
        }

        u64 byteSize() const {
            return static_cast<u64>(pixels.size());
        }
    };

    class TextureLoader {
    public:
        TextureLoader() = default;

        static ImageData loadRgba8(const Path& path);
        static ImageData loadHdrRgba32F(const Path& path);
        static ImageData loadKtx(const Path& path);
        static ImageData loadAuto(const Path& path);
    };

    ImageData loadImageRgba8(const Path& path);
    ImageData loadImageHdrRgba32F(const Path& path);
    ImageData loadImageKtx(const Path& path);
    ImageData loadImageAuto(const Path& path);
} // namespace ark::asset
