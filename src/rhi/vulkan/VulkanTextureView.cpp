#include "rhi/vulkan/VulkanTextureView.h"

namespace ark::rhi::vulkan {
    VulkanTextureView::VulkanTextureView(VkDevice device, VkImageView imageView, const TextureViewDesc& desc)
        : m_Device(device), m_ImageView(imageView), m_Desc(desc) {
    }

    VulkanTextureView::~VulkanTextureView() {
        reset();
    }

    VulkanTextureView::VulkanTextureView(VulkanTextureView&& other) noexcept
        : m_Device(other.m_Device), m_ImageView(other.m_ImageView), m_Desc(other.m_Desc) {
        other.m_Device = VK_NULL_HANDLE;
        other.m_ImageView = VK_NULL_HANDLE;
    }

    VulkanTextureView& VulkanTextureView::operator=(VulkanTextureView&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        reset();
        m_Device = other.m_Device;
        m_ImageView = other.m_ImageView;
        m_Desc = other.m_Desc;

        other.m_Device = VK_NULL_HANDLE;
        other.m_ImageView = VK_NULL_HANDLE;
        return *this;
    }

    VkImageView VulkanTextureView::getHandle() const {
        return m_ImageView;
    }

    const TextureViewDesc& VulkanTextureView::getDesc() const {
        return m_Desc;
    }

    void VulkanTextureView::reset() {
        // TextureView 是 VkImageView 的 RAII owner；VkImage 本身可能由别的对象持有。
        if (m_Device != VK_NULL_HANDLE && m_ImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_Device, m_ImageView, nullptr);
        }

        m_Device = VK_NULL_HANDLE;
        m_ImageView = VK_NULL_HANDLE;
    }
} // namespace ark::rhi::vulkan
