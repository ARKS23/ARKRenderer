#pragma once

#include "core/Types.h"
#include "rhi/vulkan/VulkanCommon.h"

#include <vector>

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
        VkDescriptorPool createPool() const;
        VkDescriptorSet allocateFromPool(VkDescriptorPool pool, VkDescriptorSetLayout layout, VkResult& result) const;
        void reset();

        VkDevice m_Device = VK_NULL_HANDLE;
        std::vector<VkDescriptorPool> m_Pools;
    };
} // namespace ark::rhi::vulkan
