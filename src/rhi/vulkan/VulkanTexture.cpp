#include "rhi/vulkan/VulkanTexture.h"

namespace ark::rhi::vulkan {
    VulkanTexture::VulkanTexture(VkDevice device, VkImage image, const TextureDesc& desc,
                                 VulkanTextureOwnership ownership)
        : m_Device(device), m_Image(image), m_Desc(desc), m_Ownership(ownership) {
    }

    VulkanTexture::~VulkanTexture() {
        reset();
    }

    VulkanTexture::VulkanTexture(VulkanTexture&& other) noexcept
        : m_Device(other.m_Device),
          m_Image(other.m_Image),
          m_Desc(other.m_Desc),
          m_State(other.m_State),
          m_Ownership(other.m_Ownership) {
        other.m_Device = VK_NULL_HANDLE;
        other.m_Image = VK_NULL_HANDLE;
        other.m_Ownership = VulkanTextureOwnership::Borrowed;
    }

    VulkanTexture& VulkanTexture::operator=(VulkanTexture&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        reset();

        m_Device = other.m_Device;
        m_Image = other.m_Image;
        m_Desc = other.m_Desc;
        m_State = other.m_State;
        m_Ownership = other.m_Ownership;

        other.m_Device = VK_NULL_HANDLE;
        other.m_Image = VK_NULL_HANDLE;
        other.m_Ownership = VulkanTextureOwnership::Borrowed;

        return *this;
    }

    const TextureDesc& VulkanTexture::getDesc() const {
        return m_Desc;
    }

    ResourceState VulkanTexture::getState() const {
        return m_State;
    }

    VkImage VulkanTexture::getHandle() const {
        return m_Image;
    }

    VulkanTextureOwnership VulkanTexture::getOwnership() const {
        return m_Ownership;
    }

    void VulkanTexture::setState(ResourceState state) {
        m_State = state;
    }

    void VulkanTexture::reset() {
        // Swapchain image 由 VkSwapchainKHR 拥有，借用型 texture 绝不能销毁 VkImage。
        if (m_Device != VK_NULL_HANDLE && m_Image != VK_NULL_HANDLE && m_Ownership == VulkanTextureOwnership::Owned) {
            vkDestroyImage(m_Device, m_Image, nullptr);
        }

        m_Device = VK_NULL_HANDLE;
        m_Image = VK_NULL_HANDLE;
        m_Ownership = VulkanTextureOwnership::Borrowed;
        m_State = ResourceState::Undefined;
    }
} // namespace ark::rhi::vulkan
