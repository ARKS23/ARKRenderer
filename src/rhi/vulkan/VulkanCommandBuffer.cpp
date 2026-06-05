#include "rhi/vulkan/VulkanCommandBuffer.h"

namespace ark::rhi::vulkan {
    VulkanCommandBuffer::VulkanCommandBuffer(VkDevice device, VkCommandPool commandPool, VkCommandBuffer commandBuffer)
        : m_Device(device), m_CommandPool(commandPool), m_CommandBuffer(commandBuffer) {
    }

    VulkanCommandBuffer::~VulkanCommandBuffer() {
        if (m_Device != VK_NULL_HANDLE && m_CommandPool != VK_NULL_HANDLE && m_CommandBuffer != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &m_CommandBuffer);
        }
    }

    VkCommandBuffer VulkanCommandBuffer::getHandle() const {
        return m_CommandBuffer;
    }

    bool VulkanCommandBuffer::begin() {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        return ARK_VK_CHECK(vkBeginCommandBuffer(m_CommandBuffer, &beginInfo));
    }

    bool VulkanCommandBuffer::end() {
        return ARK_VK_CHECK(vkEndCommandBuffer(m_CommandBuffer));
    }
} // namespace ark::rhi::vulkan
