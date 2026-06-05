#pragma once

#include "rhi/PipelineState.h"
#include "rhi/vulkan/VulkanCommon.h"

namespace ark::rhi::vulkan {
    class VulkanPipelineState final : public PipelineState {
    public:
        VulkanPipelineState(VkDevice device, const GraphicsPipelineDesc& desc);
        ~VulkanPipelineState() override;

        VulkanPipelineState(const VulkanPipelineState&) = delete;
        VulkanPipelineState& operator=(const VulkanPipelineState&) = delete;

        VulkanPipelineState(VulkanPipelineState&& other) noexcept;
        VulkanPipelineState& operator=(VulkanPipelineState&& other) noexcept;

        const GraphicsPipelineDesc& getDesc() const override;

        VkPipeline getHandle() const;
        VkPipelineLayout getLayoutHandle() const;

    private:
        void reset();

        VkDevice m_Device = VK_NULL_HANDLE;
        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        GraphicsPipelineDesc m_Desc;
    };
} // namespace ark::rhi::vulkan
