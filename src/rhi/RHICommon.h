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
        R32G32Float,
        R32G32B32Float,
        R32G32B32A32Float,
        BGRA8Unorm,
        RGBA8Unorm,
        RGBA16Float,
        D24UnormS8UInt,
        D32Float,
    };

    // 窗口类别
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

    struct ClearColor {
        float r = 0.05f;
        float g = 0.08f;
        float b = 0.12f;
        float a = 1.0f;
    };

    // dynamic rendering / render pass 附件加载策略，表达 RHI 语义而不是 Vulkan loadOp。
    enum class LoadOp {
        Load,
        Clear,
        DontCare,
    };

    // dynamic rendering / render pass 附件存储策略，后端负责映射到具体 API。
    enum class StoreOp {
        Store,
        DontCare,
    };

    struct Viewport {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    struct ScissorRect {
        i32 x = 0;
        i32 y = 0;
        u32 width = 0;
        u32 height = 0;
    };

    enum class IndexType {
        UInt16,
        UInt32,
    };

    // 窗口最小化时尺寸可能为 0，创建或重建 swapchain 前需要先过滤。
    constexpr bool isValidExtent(Extent2D extent) {
        return extent.width > 0 && extent.height > 0;
    }
} // namespace ark::rhi
