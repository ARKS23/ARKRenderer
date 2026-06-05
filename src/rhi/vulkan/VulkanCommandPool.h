#pragma once

#include "core/Types.h"
#include "rhi/vulkan/VulkanCommon.h"

namespace ark::rhi::vulkan {
    class VulkanCommandPool final {
    public:
        VulkanCommandPool(VkDevice device, u32 queueFamilyIndex);
        ~VulkanCommandPool();

        VulkanCommandPool(const VulkanCommandPool&) = delete;
        VulkanCommandPool& operator=(const VulkanCommandPool&) = delete;

        VkCommandPool getHandle() const;
        VkCommandBuffer allocatePrimaryCommandBuffer() const;
        bool reset();

    private:
        VkDevice m_Device = VK_NULL_HANDLE;
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    };
} // namespace ark::rhi::vulkan
