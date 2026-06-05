#include "rhi/vulkan/VulkanSync.h"

#include <stdexcept>
#include <utility>

namespace ark::rhi::vulkan {
    VulkanSync::VulkanSync(VkDevice device, const VulkanSyncDesc& desc) : m_Device(device), m_Type(desc.type) {
        if (m_Device == VK_NULL_HANDLE) {
            throw std::invalid_argument("VulkanSync requires a valid VkDevice");
        }

        if (m_Type == VulkanSyncType::Semaphore) {
            // Semaphore 用于 acquire/present 和 queue submit 之间的 GPU-GPU 同步。
            VkSemaphoreCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            const VkResult result = vkCreateSemaphore(m_Device, &createInfo, nullptr, &m_Semaphore);
            if (result != VK_SUCCESS) {
                throw std::runtime_error("vkCreateSemaphore failed");
            }
            return;
        }

        // Fence 用于 CPU 等待当前 frame resource 不再被 GPU 使用。
        VkFenceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        createInfo.flags = desc.signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

        const VkResult result = vkCreateFence(m_Device, &createInfo, nullptr, &m_Fence);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("vkCreateFence failed");
        }
    }

    VulkanSync::~VulkanSync() {
        reset();
    }

    VulkanSync::VulkanSync(VulkanSync&& other) noexcept
        : m_Device(other.m_Device), m_Type(other.m_Type), m_Semaphore(other.m_Semaphore), m_Fence(other.m_Fence) {
        other.m_Device = VK_NULL_HANDLE;
        other.m_Semaphore = VK_NULL_HANDLE;
        other.m_Fence = VK_NULL_HANDLE;
    }

    VulkanSync& VulkanSync::operator=(VulkanSync&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        reset();

        m_Device = other.m_Device;
        m_Type = other.m_Type;
        m_Semaphore = other.m_Semaphore;
        m_Fence = other.m_Fence;

        other.m_Device = VK_NULL_HANDLE;
        other.m_Semaphore = VK_NULL_HANDLE;
        other.m_Fence = VK_NULL_HANDLE;

        return *this;
    }

    VulkanSyncType VulkanSync::getType() const {
        return m_Type;
    }

    VkSemaphore VulkanSync::getSemaphore() const {
        return m_Semaphore;
    }

    VkFence VulkanSync::getFence() const {
        return m_Fence;
    }

    void VulkanSync::reset() {
        if (m_Device == VK_NULL_HANDLE) {
            return;
        }

        if (m_Semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_Device, m_Semaphore, nullptr);
        }

        if (m_Fence != VK_NULL_HANDLE) {
            vkDestroyFence(m_Device, m_Fence, nullptr);
        }

        m_Device = VK_NULL_HANDLE;
        m_Semaphore = VK_NULL_HANDLE;
        m_Fence = VK_NULL_HANDLE;
    }
} // namespace ark::rhi::vulkan
