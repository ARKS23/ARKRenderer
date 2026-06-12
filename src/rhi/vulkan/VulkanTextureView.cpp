#include "rhi/vulkan/VulkanTextureView.h"

#include "rhi/vulkan/VulkanTexture.h"

#include <stdexcept>

namespace ark::rhi::vulkan {
    namespace {
        constexpr u32 CubemapLayerCount = 6;

        VkImageAspectFlags toAspectMask(Format format) {
            switch (format) {
            case Format::D24UnormS8UInt:
                return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            case Format::D32Float:
                return VK_IMAGE_ASPECT_DEPTH_BIT;
            default:
                return VK_IMAGE_ASPECT_COLOR_BIT;
            }
        }

        VkImageViewType toVkImageViewType(TextureViewType type) {
            switch (type) {
            case TextureViewType::Texture2D:
                return VK_IMAGE_VIEW_TYPE_2D;
            case TextureViewType::Cube:
                return VK_IMAGE_VIEW_TYPE_CUBE;
            }

            return VK_IMAGE_VIEW_TYPE_2D;
        }

        void validateTextureViewDesc(const VulkanTexture& texture, const TextureViewDesc& desc) {
            const TextureDesc& textureDesc = texture.getDesc();

            if (desc.mipLevelCount == 0) {
                throw std::runtime_error("VulkanTextureView requires at least one mip level");
            }

            if (desc.arrayLayerCount == 0) {
                throw std::runtime_error("VulkanTextureView requires at least one array layer");
            }

            if (desc.baseMipLevel >= textureDesc.mipLevels ||
                desc.mipLevelCount > textureDesc.mipLevels - desc.baseMipLevel) {
                throw std::runtime_error("VulkanTextureView mip range exceeds texture mip levels");
            }

            if (desc.baseArrayLayer >= textureDesc.arrayLayers ||
                desc.arrayLayerCount > textureDesc.arrayLayers - desc.baseArrayLayer) {
                throw std::runtime_error("VulkanTextureView array range exceeds texture array layers");
            }

            if (desc.type == TextureViewType::Texture2D && desc.arrayLayerCount != 1) {
                throw std::runtime_error("VulkanTextureView 2D views require exactly one array layer");
            }

            if (desc.type == TextureViewType::Cube) {
                if (textureDesc.type != TextureType::Cube) {
                    throw std::runtime_error("VulkanTextureView cube views require a cube texture");
                }

                if (desc.arrayLayerCount != CubemapLayerCount) {
                    throw std::runtime_error("VulkanTextureView cube views require exactly 6 array layers");
                }
            }
        }
    } // namespace

    VulkanTextureView::VulkanTextureView(VkDevice device, VkImageView imageView, VulkanTexture* texture,
                                         const TextureViewDesc& desc)
        : m_Device(device), m_ImageView(imageView), m_Texture(texture), m_Desc(desc) {
    }

    VulkanTextureView::VulkanTextureView(VkDevice device, VulkanTexture& texture, const TextureViewDesc& desc)
        : m_Device(device), m_Texture(&texture), m_Desc(desc) {
        if (m_Device == VK_NULL_HANDLE || texture.getHandle() == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanTextureView requires valid device and image");
        }

        validateTextureViewDesc(texture, m_Desc);

        const Format viewFormat = m_Desc.format == Format::Unknown ? texture.getDesc().format : m_Desc.format;
        const VkFormat vkFormat = toVkFormat(viewFormat);
        if (vkFormat == VK_FORMAT_UNDEFINED) {
            throw std::runtime_error("VulkanTextureView requires a valid format");
        }

        VkImageViewCreateInfo viewCreateInfo{};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.image = texture.getHandle();
        viewCreateInfo.viewType = toVkImageViewType(m_Desc.type);
        viewCreateInfo.format = vkFormat;
        viewCreateInfo.subresourceRange.aspectMask = toAspectMask(viewFormat);
        viewCreateInfo.subresourceRange.baseMipLevel = m_Desc.baseMipLevel;
        viewCreateInfo.subresourceRange.levelCount = m_Desc.mipLevelCount;
        viewCreateInfo.subresourceRange.baseArrayLayer = m_Desc.baseArrayLayer;
        viewCreateInfo.subresourceRange.layerCount = m_Desc.arrayLayerCount;

        if (!ARK_VK_CHECK(vkCreateImageView(m_Device, &viewCreateInfo, nullptr, &m_ImageView))) {
            throw std::runtime_error("vkCreateImageView failed");
        }

        m_Desc.format = viewFormat;
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
