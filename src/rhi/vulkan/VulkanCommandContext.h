#pragma once

#include "core/Memory.h"
#include "rhi/DeviceContext.h"
#include "rhi/vulkan/VulkanFrameResource.h"

#include <vector>

namespace ark::rhi::vulkan {
    class VulkanDevice;

    // VulkanCommandContext 是 DeviceContext 的 Vulkan 实现，集中管理每帧 command buffer 和 queue submit。
    class VulkanCommandContext final : public DeviceContext {
    public:
        explicit VulkanCommandContext(VulkanDevice& device);
        ~VulkanCommandContext() override;

        FrameResource& beginFrame() override;
        bool begin(FrameResource& frameResource) override;
        bool end() override;
        bool submit(const SubmitDesc& desc) override;
        void advanceFrame() override;

        void beginRendering(const RenderingDesc& desc) override;
        void endRendering() override;

        void setPipeline(PipelineState& pipeline) override;
        void bindDescriptorSet(u32 setIndex, DescriptorSet& descriptorSet) override;
        void setVertexBuffer(u32 slot, Buffer& buffer) override;
        void setIndexBuffer(Buffer& buffer) override;
        void drawIndexed(const DrawIndexedDesc& desc) override;
        void pipelineBarrier(std::span<const ResourceBarrier> barriers) override;
        void clearRenderTarget(TextureView& renderTargetView, const ClearColor& color) override;

    private:
        // 公共 FrameResource 只暴露抽象 token，进入 Vulkan 后端时需要还原为 VulkanFrameResource。
        VulkanFrameResource* toVulkanFrameResource(FrameResource& frameResource);
        VulkanFrameResource* currentVulkanFrame();
        VkCommandBuffer currentCommandBuffer() const;

        // VulkanDevice 拥有 VkDevice / queue 等设备级对象，CommandContext 只借用。
        VulkanDevice& m_Device;

        // 双缓冲帧资源：每个槽位有独立 command pool / command buffer / semaphore / fence。
        std::vector<Scope<VulkanFrameResource>> m_FrameResources;
        u64 m_FrameIndex = 0;
        u32 m_FrameSlot = 0;

        // 当前正在录制的帧；用于 barrier/clear/submit 找到正确 command buffer。
        VulkanFrameResource* m_RecordingFrame = nullptr;
        bool m_IsRecording = false;
    };
} // namespace ark::rhi::vulkan
