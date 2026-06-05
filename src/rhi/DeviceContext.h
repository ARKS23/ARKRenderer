#pragma once

#include "rhi/RHICommon.h"
#include "rhi/ResourceBarrier.h"

#include <span>

namespace ark::rhi {
    class Buffer;
    class DescriptorSet;
    struct FrameResource;
    class PipelineState;
    class TextureView;

    // dynamic rendering 的附件描述，后续 ForwardPass / ClearPass 会通过它进入 beginRendering。
    struct RenderingDesc {
        TextureView* colorAttachment = nullptr;
        TextureView* depthStencilAttachment = nullptr;
    };

    // 索引绘制参数保持 API 无关，由后端翻译成 vkCmdDrawIndexed 等调用。
    struct DrawIndexedDesc {
        u32 indexCount = 0;
        u32 instanceCount = 1;
        u32 firstIndex = 0;
        i32 vertexOffset = 0;
        u32 firstInstance = 0;
    };

    // submit 描述当前只覆盖 swapchain 最小同步，后续可以扩展为多 semaphore / timeline semaphore。
    struct SubmitDesc {
        FrameResource* frameResource = nullptr;
        bool waitForSwapChainImage = true;
        bool signalRenderFinished = true;
    };

    struct ClearColor {
        float r = 0.05f;
        float g = 0.08f;
        float b = 0.12f;
        float a = 1.0f;
    };

    // DeviceContext 是资源使用与命令执行对象，负责 command recording、barrier、draw/dispatch 和 submit。
    // 它对应 Vulkan 的 command buffer / command encoder / queue submit 这一侧，不负责创建资源。
    class DeviceContext {
    public:
        virtual ~DeviceContext() = default;

        // 一帧的典型调用顺序：
        // beginFrame -> swapchain acquire -> begin -> record commands -> end -> submit -> present -> advanceFrame。
        virtual FrameResource& beginFrame() = 0;
        virtual bool begin(FrameResource& frameResource) = 0;
        virtual bool end() = 0;
        virtual bool submit(const SubmitDesc& desc) = 0;
        virtual void advanceFrame() = 0;

        // 渲染区域控制接口暂时保留，Phase 0.3 的 clear 路径还没有使用 dynamic rendering。
        virtual void beginRendering(const RenderingDesc& desc) = 0;
        virtual void endRendering() = 0;

        // 下面是后续真实绘制需要的状态绑定和 draw 接口，当前 Vulkan 实现仍是占位。
        virtual void setPipeline(PipelineState& pipeline) = 0;
        virtual void bindDescriptorSet(u32 setIndex, DescriptorSet& descriptorSet) = 0;
        virtual void setVertexBuffer(u32 slot, Buffer& buffer) = 0;
        virtual void setIndexBuffer(Buffer& buffer) = 0;
        virtual void drawIndexed(const DrawIndexedDesc& desc) = 0;

        // barrier 和 clear 是 Phase 0.3 清屏闭环已经使用的最小命令。
        virtual void pipelineBarrier(std::span<const ResourceBarrier> barriers) = 0;
        virtual void clearRenderTarget(TextureView& renderTargetView, const ClearColor& color) = 0;
    };
} // namespace ark::rhi
