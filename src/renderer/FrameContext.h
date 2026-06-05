#pragma once

#include "core/Types.h"
#include "rhi/RHICommon.h"

namespace ark::rhi {
    class DeviceContext;
    struct FrameResource;
    class RenderDevice;
    class SwapChain;
    class TextureView;
} // namespace ark::rhi

namespace ark {
    class RenderQueue;
    class RenderScene;
    class RenderView;

    // FrameContext 是 renderer 层的一帧逻辑上下文，负责把 pass 执行所需的公共 RHI 对象集中传递。
    // 它不拥有资源，也不暴露 Vulkan 类型；底层同步对象仍由 rhi::FrameResource 后端实现承担。
    struct FrameContext {
        u64 frameIndex = 0;
        float deltaTime = 0.0f;

        RenderScene* scene = nullptr;
        const RenderView* view = nullptr;
        RenderQueue* queue = nullptr;

        rhi::RenderDevice* device = nullptr;
        rhi::DeviceContext* context = nullptr;
        rhi::SwapChain* swapChain = nullptr;
        rhi::FrameResource* frameResource = nullptr;
        rhi::TextureView* backBufferView = nullptr;

        rhi::Extent2D extent{};
        rhi::ClearColor clearColor{};
    };
} // namespace ark
