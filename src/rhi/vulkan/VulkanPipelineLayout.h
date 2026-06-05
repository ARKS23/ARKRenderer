#pragma once

#include "rhi/PipelineLayout.h"
#include "rhi/vulkan/VulkanCommon.h"

namespace ark::rhi::vulkan {
    class VulkanPipelineLayout final : public PipelineLayout {
    public:
        VulkanPipelineLayout(VkDevice device, const PipelineLayoutDesc& desc);
        ~VulkanPipelineLayout() override;

        VulkanPipelineLayout(const VulkanPipelineLayout&) = delete;
        VulkanPipelineLayout& operator=(const VulkanPipelineLayout&) = delete;

        VulkanPipelineLayout(VulkanPipelineLayout&& other) noexcept;
        VulkanPipelineLayout& operator=(VulkanPipelineLayout&& other) noexcept;

        const PipelineLayoutDesc& getDesc() const override;

        VkPipelineLayout getHandle() const;

    private:
        void reset();

        VkDevice m_Device = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        PipelineLayoutDesc m_Desc;
    };
} // namespace ark::rhi::vulkan
