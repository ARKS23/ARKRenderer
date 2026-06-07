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

    struct RenderingAttachmentDesc {
        TextureView* view = nullptr;
        LoadOp loadOp = LoadOp::Clear;
        StoreOp storeOp = StoreOp::Store;
        ClearColor clearColor{};
    };

    struct DepthStencilAttachmentDesc {
        TextureView* view = nullptr;
        LoadOp loadOp = LoadOp::Clear;
        StoreOp storeOp = StoreOp::Store;
        float clearDepth = 1.0f;
        u32 clearStencil = 0;
    };

    // dynamic rendering 的附件描述，renderer 只表达 RHI 语义，后端负责映射到 VkRenderingAttachmentInfo。
    struct RenderingDesc {
        Extent2D extent{};
        RenderingAttachmentDesc colorAttachment;
        DepthStencilAttachmentDesc depthStencilAttachment;
    };

    // 索引绘制参数保持 API 无关，由后端翻译成 vkCmdDrawIndexed 等调用。
    struct DrawIndexedDesc {
        u32 indexCount = 0;
        u32 instanceCount = 1;
        u32 firstIndex = 0;
        i32 vertexOffset = 0;
        u32 firstInstance = 0;
    };

    struct DrawDesc {
        u32 vertexCount = 0;
        u32 instanceCount = 1;
        u32 firstVertex = 0;
        u32 firstInstance = 0;
    };

    // submit 描述当前只覆盖 swapchain 最小同步，后续可以扩展为多 semaphore / timeline semaphore。
    struct SubmitDesc {
        FrameResource* frameResource = nullptr;
        bool waitForSwapChainImage = true;
        bool signalRenderFinished = true;
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

        // 渲染区域控制接口对应 Vulkan dynamic rendering 或其他 API 的 render encoder。
        virtual bool beginRendering(const RenderingDesc& desc) = 0;
        virtual void endRendering() = 0;
        virtual void setViewport(const Viewport& viewport) = 0;
        virtual void setScissorRect(const ScissorRect& rect) = 0;

        // 下面是绘制需要的状态绑定和 draw 接口；资源创建仍由 RenderDevice 负责。
        virtual void setPipeline(PipelineState& pipeline) = 0;
        virtual void bindDescriptorSet(u32 setIndex, DescriptorSet& descriptorSet) = 0;
        // CPU 可见 buffer 的直接更新路径；调用方需要保证不会覆盖 GPU 仍在读取的 in-flight 数据。
        virtual bool updateBuffer(Buffer& buffer, const void* data, u64 size, u64 offset = 0) = 0;
        virtual void setVertexBuffer(u32 slot, Buffer& buffer, u64 offset = 0) = 0;
        virtual void setIndexBuffer(Buffer& buffer, IndexType indexType = IndexType::UInt32, u64 offset = 0) = 0;
        virtual void draw(const DrawDesc& desc) = 0;
        virtual void drawIndexed(const DrawIndexedDesc& desc) = 0;

        // barrier 和 clear 是 Phase 0.3 清屏闭环已经使用的最小命令。
        virtual void pipelineBarrier(std::span<const ResourceBarrier> barriers) = 0;
        virtual void clearRenderTarget(TextureView& renderTargetView, const ClearColor& color) = 0;
    };
} // namespace ark::rhi
