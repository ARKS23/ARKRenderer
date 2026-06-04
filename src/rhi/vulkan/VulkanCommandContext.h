#pragma once

#include "rhi/DeviceContext.h"

namespace ark::rhi::vulkan {
    class VulkanCommandContext final : public DeviceContext {
    public:
        void begin() override;
        void end() override;
        void submit(const SubmitDesc& desc) override;

        void beginRendering(const RenderingDesc& desc) override;
        void endRendering() override;

        void setPipeline(PipelineState& pipeline) override;
        void bindDescriptorSet(u32 setIndex, DescriptorSet& descriptorSet) override;
        void setVertexBuffer(u32 slot, Buffer& buffer) override;
        void setIndexBuffer(Buffer& buffer) override;
        void drawIndexed(const DrawIndexedDesc& desc) override;
        void pipelineBarrier(std::span<const ResourceBarrier> barriers) override;
    };
} // namespace ark::rhi::vulkan
