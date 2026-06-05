#include "rhi/vulkan/VulkanCommandPool.h"

#include <stdexcept>

namespace ark::rhi::vulkan {
    VulkanCommandPool::VulkanCommandPool(VkDevice device, u32 queueFamilyIndex) : m_Device(device) {
        VkCommandPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        createInfo.queueFamilyIndex = queueFamilyIndex;

        if (!ARK_VK_CHECK(vkCreateCommandPool(m_Device, &createInfo, nullptr, &m_CommandPool))) {
            throw std::runtime_error("vkCreateCommandPool failed");
        }
    }

    VulkanCommandPool::~VulkanCommandPool() {
        if (m_Device != VK_NULL_HANDLE && m_CommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
        }
    }

    VkCommandPool VulkanCommandPool::getHandle() const {
        return m_CommandPool;
    }

    VkCommandBuffer VulkanCommandPool::allocatePrimaryCommandBuffer() const {
        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = m_CommandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!ARK_VK_CHECK(vkAllocateCommandBuffers(m_Device, &allocateInfo, &commandBuffer))) {
            throw std::runtime_error("vkAllocateCommandBuffers failed");
        }

        return commandBuffer;
    }

    bool VulkanCommandPool::reset() {
        return ARK_VK_CHECK(vkResetCommandPool(m_Device, m_CommandPool, 0));
    }
} // namespace ark::rhi::vulkan
