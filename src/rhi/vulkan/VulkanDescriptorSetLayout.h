#pragma once

#include "rhi/DescriptorSetLayout.h"
#include "rhi/vulkan/VulkanCommon.h"

namespace ark::rhi::vulkan {
    // 拥有 VkDescriptorSetLayout；具体 buffer/texture 资源由 DescriptorSet 绑定。
    class VulkanDescriptorSetLayout final : public DescriptorSetLayout {
    public:
        VulkanDescriptorSetLayout(VkDevice device, const DescriptorSetLayoutDesc& desc);
        ~VulkanDescriptorSetLayout() override;

        VulkanDescriptorSetLayout(const VulkanDescriptorSetLayout&) = delete;
        VulkanDescriptorSetLayout& operator=(const VulkanDescriptorSetLayout&) = delete;

        VulkanDescriptorSetLayout(VulkanDescriptorSetLayout&& other) noexcept;
        VulkanDescriptorSetLayout& operator=(VulkanDescriptorSetLayout&& other) noexcept;

        const DescriptorSetLayoutDesc& getDesc() const override;

        VkDescriptorSetLayout getHandle() const;

    private:
        void reset();

        VkDevice m_Device = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
        DescriptorSetLayoutDesc m_Desc;
    };
} // namespace ark::rhi::vulkan
