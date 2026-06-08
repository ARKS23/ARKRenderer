#pragma once

#include "core/FileSystem.h"
#include "core/Types.h"

#include <string>
#include <vector>

namespace ark::asset {
    // CPU 侧图片格式描述；GPU 格式映射由 renderer/RHI 后续阶段处理。
    enum class ImageFormat {
        Unknown,
        Rgba8Unorm,
        Rgba32Float,
    };

    // 只保存解码后的 CPU 像素数据，不拥有或创建任何 GPU/RHI 资源。
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

        // LDR RGBA8 便捷入口，语义等同于 loadImageRgba8()。
        static ImageData loadRgba8(const Path& path);
    };

    // Phase 0.7 只提供 LDR RGBA8 路径；HDR 输入必须显式失败，避免动态范围被静默压缩。
    ImageData loadImageRgba8(const Path& path);
} // namespace ark::asset
