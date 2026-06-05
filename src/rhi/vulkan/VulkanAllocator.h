#pragma once

#include "rhi/vulkan/VulkanCommon.h"

#include <vma/vk_mem_alloc.h>

namespace ark::rhi::vulkan {
    class VulkanAllocator final {
    public:
        VulkanAllocator() = default;
        VulkanAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, u32 apiVersion);
        ~VulkanAllocator();

        VulkanAllocator(const VulkanAllocator&) = delete;
        VulkanAllocator& operator=(const VulkanAllocator&) = delete;

        VulkanAllocator(VulkanAllocator&& other) noexcept;
        VulkanAllocator& operator=(VulkanAllocator&& other) noexcept;

        VmaAllocator getHandle() const;

    private:
        void reset();

        VmaAllocator m_Allocator = VK_NULL_HANDLE;
    };
} // namespace ark::rhi::vulkan
