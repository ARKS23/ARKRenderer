#pragma once

#include "rhi/vulkan/VulkanCommon.h"

namespace ark::rhi::vulkan {
    enum class VulkanSyncType {
        Semaphore,
        Fence,
    };

    struct VulkanSyncDesc {
        VulkanSyncType type = VulkanSyncType::Semaphore;
        bool signaled = false;
    };

    // VulkanSync 是 semaphore/fence 的轻量 RAII 包装，具体用途由 VulkanFrameResource 命名字段表达。
    class VulkanSync final {
    public:
        VulkanSync(VkDevice device, const VulkanSyncDesc& desc);
        ~VulkanSync();

        VulkanSync(const VulkanSync&) = delete;
        VulkanSync& operator=(const VulkanSync&) = delete;

        VulkanSync(VulkanSync&& other) noexcept;
        VulkanSync& operator=(VulkanSync&& other) noexcept;

        VulkanSyncType getType() const;
        VkSemaphore getSemaphore() const;
        VkFence getFence() const;

    private:
        void reset();

        VkDevice m_Device = VK_NULL_HANDLE;
        VulkanSyncType m_Type = VulkanSyncType::Semaphore;
        VkSemaphore m_Semaphore = VK_NULL_HANDLE;
        VkFence m_Fence = VK_NULL_HANDLE;
    };
} // namespace ark::rhi::vulkan
