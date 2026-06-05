#include "rhi/vulkan/VulkanTextureView.h"

#include "rhi/vulkan/VulkanTexture.h"

namespace ark::rhi::vulkan {
    VulkanTextureView::VulkanTextureView(VkDevice device, VkImageView imageView, VulkanTexture* texture,
                                         const TextureViewDesc& desc)
        : m_Device(device), m_ImageView(imageView), m_Texture(texture), m_Desc(desc) {
    }

    VulkanTextureView::~VulkanTextureView() {
        reset();
    }

    VulkanTextureView::VulkanTextureView(VulkanTextureView&& other) noexcept
        : m_Device(other.m_Device),
          m_ImageView(other.m_ImageView),
          m_Texture(other.m_Texture),
          m_Desc(other.m_Desc) {
        other.m_Device = VK_NULL_HANDLE;
        other.m_ImageView = VK_NULL_HANDLE;
        other.m_Texture = nullptr;
    }

    VulkanTextureView& VulkanTextureView::operator=(VulkanTextureView&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        reset();
        m_Device = other.m_Device;
        m_ImageView = other.m_ImageView;
        m_Texture = other.m_Texture;
        m_Desc = other.m_Desc;

        other.m_Device = VK_NULL_HANDLE;
        other.m_ImageView = VK_NULL_HANDLE;
        other.m_Texture = nullptr;
        return *this;
    }

    Texture* VulkanTextureView::getTexture() const {
        return m_Texture;
    }

    const TextureViewDesc& VulkanTextureView::getDesc() const {
        return m_Desc;
    }

    VkImageView VulkanTextureView::getHandle() const {
        return m_ImageView;
    }

    VulkanTexture* VulkanTextureView::getVulkanTexture() const {
        return m_Texture;
    }

    void VulkanTextureView::reset() {
        // TextureView 是 VkImageView 的 RAII owner；VkImage 本身可能由别的对象持有。
        if (m_Device != VK_NULL_HANDLE && m_ImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_Device, m_ImageView, nullptr);
        }

        m_Device = VK_NULL_HANDLE;
        m_ImageView = VK_NULL_HANDLE;
        m_Texture = nullptr;
    }
} // namespace ark::rhi::vulkan
