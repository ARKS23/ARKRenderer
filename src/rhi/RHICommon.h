#pragma once

#include "core/Types.h"

namespace ark::rhi {
    // 渲染后端类型，目前只落地 Vulkan。
    enum class RenderBackendType {
        Vulkan,
    };

    // RHI 公共格式枚举保持 API 无关，后端负责映射到 VkFormat 等原生格式。
    enum class Format {
        Unknown,
        BGRA8Unorm,
        RGBA8Unorm,
        RGBA16Float,
        D24UnormS8UInt,
        D32Float,
    };

    enum class NativeWindowType {
        GLFW,
        Win32,
        SDL,
    };

    // 上层只传递原生窗口句柄，具体 surface 创建和生命周期由后端处理。
    struct NativeWindowHandle {
        NativeWindowType type = NativeWindowType::GLFW;
        void* handle = nullptr;
    };

    struct Extent2D {
        u32 width = 0;
        u32 height = 0;
    };

    // 窗口最小化时尺寸可能为 0，创建或重建 swapchain 前需要先过滤。
    constexpr bool isValidExtent(Extent2D extent) {
        return extent.width > 0 && extent.height > 0;
    }
} // namespace ark::rhi
