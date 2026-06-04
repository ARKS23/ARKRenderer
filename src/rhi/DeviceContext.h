#pragma once

#include "rhi/RHICommon.h"
#include "rhi/ResourceBarrier.h"

#include <span>

namespace ark::rhi {
    class Buffer;
    class DescriptorSet;
    class PipelineState;
    class TextureView;

    struct RenderingDesc {
        TextureView* colorAttachment = nullptr;
        TextureView* depthStencilAttachment = nullptr;
    };

    struct DrawIndexedDesc {
        u32 indexCount = 0;
        u32 instanceCount = 1;
        u32 firstIndex = 0;
        i32 vertexOffset = 0;
        u32 firstInstance = 0;
    };

    struct SubmitDesc {
        bool waitForSwapChainImage = true;
        bool signalRenderFinished = true;
    };

    class DeviceContext {
    public:
        virtual ~DeviceContext() = default;

        // DeviceContext 是命令录制与提交对象，负责使用 RenderDevice 创建出的资源。
        virtual void begin() = 0;
        virtual void end() = 0;
        virtual void submit(const SubmitDesc& desc) = 0;

        virtual void beginRendering(const RenderingDesc& desc) = 0;
        virtual void endRendering() = 0;

        virtual void setPipeline(PipelineState& pipeline) = 0;
        virtual void bindDescriptorSet(u32 setIndex, DescriptorSet& descriptorSet) = 0;
        virtual void setVertexBuffer(u32 slot, Buffer& buffer) = 0;
        virtual void setIndexBuffer(Buffer& buffer) = 0;
        virtual void drawIndexed(const DrawIndexedDesc& desc) = 0;
        virtual void pipelineBarrier(std::span<const ResourceBarrier> barriers) = 0;
    };
} // namespace ark::rhi
