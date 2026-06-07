#pragma once

#include "core/Types.h"
#include "rhi/vulkan/VulkanCommon.h"

namespace ark::rhi::vulkan {
    class VulkanDescriptorSetLayout;

    // 管理 descriptor pool、descriptor set 分配与回收策略。
    class VulkanDescriptorManager final {
    public:
        explicit VulkanDescriptorManager(VkDevice device);
        ~VulkanDescriptorManager();

        VulkanDescriptorManager(const VulkanDescriptorManager&) = delete;
        VulkanDescriptorManager& operator=(const VulkanDescriptorManager&) = delete;

        VulkanDescriptorManager(VulkanDescriptorManager&& other) noexcept;
        VulkanDescriptorManager& operator=(VulkanDescriptorManager&& other) noexcept;

        VkDescriptorSet allocateDescriptorSet(const VulkanDescriptorSetLayout& layout);

    private:
        void reset();

        VkDevice m_Device = VK_NULL_HANDLE;
        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    };
} // namespace ark::rhi::vulkan
