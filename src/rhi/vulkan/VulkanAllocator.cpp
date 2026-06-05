#define VMA_IMPLEMENTATION
#include "rhi/vulkan/VulkanAllocator.h"

#include <stdexcept>

namespace ark::rhi::vulkan {
    VulkanAllocator::VulkanAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                                     u32 apiVersion) {
        VmaAllocatorCreateInfo createInfo{};
        createInfo.instance = instance;
        createInfo.physicalDevice = physicalDevice;
        createInfo.device = device;
        createInfo.vulkanApiVersion = apiVersion;

        // 项目使用 volk + VK_NO_PROTOTYPES，VMA 也从 volk 导入函数表，避免依赖静态 Vulkan 符号。
        VmaVulkanFunctions vulkanFunctions{};
        if (!ARK_VK_CHECK(vmaImportVulkanFunctionsFromVolk(&createInfo, &vulkanFunctions))) {
            throw std::runtime_error("vmaImportVulkanFunctionsFromVolk failed");
        }

        createInfo.pVulkanFunctions = &vulkanFunctions;
        if (!ARK_VK_CHECK(vmaCreateAllocator(&createInfo, &m_Allocator))) {
            throw std::runtime_error("vmaCreateAllocator failed");
        }
    }

    VulkanAllocator::~VulkanAllocator() {
        reset();
    }

    VulkanAllocator::VulkanAllocator(VulkanAllocator&& other) noexcept : m_Allocator(other.m_Allocator) {
        other.m_Allocator = VK_NULL_HANDLE;
    }

    VulkanAllocator& VulkanAllocator::operator=(VulkanAllocator&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        reset();
        m_Allocator = other.m_Allocator;
        other.m_Allocator = VK_NULL_HANDLE;
        return *this;
    }

    VmaAllocator VulkanAllocator::getHandle() const {
        return m_Allocator;
    }

    void VulkanAllocator::reset() {
        if (m_Allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(m_Allocator);
            m_Allocator = VK_NULL_HANDLE;
        }
    }
} // namespace ark::rhi::vulkan
