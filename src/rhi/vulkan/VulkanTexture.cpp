#include "rhi/vulkan/VulkanTexture.h"

#include <stdexcept>

namespace ark::rhi::vulkan {
    namespace {
        constexpr u32 CubemapLayerCount = 6;

        VkImageUsageFlags toVkImageUsage(TextureUsage usage) {
            VkImageUsageFlags flags = 0;

            if (hasTextureUsage(usage, TextureUsage::RenderTarget)) {
                flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            }
            if (hasTextureUsage(usage, TextureUsage::DepthStencil)) {
                flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            }
            if (hasTextureUsage(usage, TextureUsage::ShaderResource)) {
                flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
            }
            if (hasTextureUsage(usage, TextureUsage::UnorderedAccess)) {
                flags |= VK_IMAGE_USAGE_STORAGE_BIT;
            }
            if (hasTextureUsage(usage, TextureUsage::TransferSrc)) {
                flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            }
            if (hasTextureUsage(usage, TextureUsage::TransferDst)) {
                flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            }

            return flags;
        }

        VkImageCreateFlags toVkImageCreateFlags(TextureType type) {
            switch (type) {
            case TextureType::Texture2D:
                return 0;
            case TextureType::Cube:
                return VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            }

            return 0;
        }

        void validateTextureDesc(const TextureDesc& desc) {
            if (desc.mipLevels == 0) {
                throw std::runtime_error("VulkanTexture requires at least one mip level");
            }

            if (desc.arrayLayers == 0) {
                throw std::runtime_error("VulkanTexture requires at least one array layer");
            }

            if (desc.type == TextureType::Cube) {
                if (desc.extent.width != desc.extent.height) {
                    throw std::runtime_error("VulkanTexture cube textures require square faces");
                }

                if (desc.arrayLayers != CubemapLayerCount) {
                    throw std::runtime_error("VulkanTexture cube textures require exactly 6 array layers");
                }
            }
        }
    } // namespace

    VulkanTexture::VulkanTexture(VmaAllocator allocator, const TextureDesc& desc)
        : m_Allocator(allocator), m_Desc(desc), m_Ownership(VulkanTextureOwnership::Owned) {
        if (m_Allocator == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanTexture requires a valid VMA allocator");
        }

        if (!isValidExtent(m_Desc.extent)) {
            throw std::runtime_error("VulkanTexture requires a valid extent");
        }

        validateTextureDesc(m_Desc);

        const VkFormat format = toVkFormat(m_Desc.format);
        if (format == VK_FORMAT_UNDEFINED) {
            throw std::runtime_error("VulkanTexture requires a valid format");
        }

        const VkImageUsageFlags usage = toVkImageUsage(m_Desc.usage);
        if (usage == 0) {
            throw std::runtime_error("VulkanTexture requires at least one usage flag");
        }

        VkImageCreateInfo imageCreateInfo{};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.flags = toVkImageCreateFlags(m_Desc.type);
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = format;
        imageCreateInfo.extent = VkExtent3D{m_Desc.extent.width, m_Desc.extent.height, 1};
        imageCreateInfo.mipLevels = m_Desc.mipLevels;
        imageCreateInfo.arrayLayers = m_Desc.arrayLayers;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage = usage;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocationCreateInfo{};
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        if (!ARK_VK_CHECK(vmaCreateImage(m_Allocator, &imageCreateInfo, &allocationCreateInfo, &m_Image,
                                         &m_Allocation, nullptr))) {
            throw std::runtime_error("vmaCreateImage failed");
        }
    }

    VulkanTexture::VulkanTexture(VkDevice device, VkImage image, const TextureDesc& desc,
                                 VulkanTextureOwnership ownership)
        : m_Device(device), m_Image(image), m_Desc(desc), m_Ownership(ownership) {
        if (m_Ownership == VulkanTextureOwnership::Owned) {
            throw std::runtime_error("Owned VulkanTexture requires VMA allocator constructor");
        }

        if (m_Device == VK_NULL_HANDLE || m_Image == VK_NULL_HANDLE) {
            throw std::runtime_error("Borrowed VulkanTexture requires valid device and image");
        }
    }

    VulkanTexture::~VulkanTexture() {
        reset();
    }

    VulkanTexture::VulkanTexture(VulkanTexture&& other) noexcept
        : m_Allocator(other.m_Allocator),
          m_Allocation(other.m_Allocation),
          m_Device(other.m_Device),
          m_Image(other.m_Image),
          m_Desc(other.m_Desc),
          m_State(other.m_State),
          m_Ownership(other.m_Ownership) {
        other.m_Allocator = VK_NULL_HANDLE;
        other.m_Allocation = VK_NULL_HANDLE;
        other.m_Device = VK_NULL_HANDLE;
        other.m_Image = VK_NULL_HANDLE;
        other.m_Ownership = VulkanTextureOwnership::Borrowed;
    }

    VulkanTexture& VulkanTexture::operator=(VulkanTexture&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        reset();

        m_Allocator = other.m_Allocator;
        m_Allocation = other.m_Allocation;
        m_Device = other.m_Device;
        m_Image = other.m_Image;
        m_Desc = other.m_Desc;
        m_State = other.m_State;
        m_Ownership = other.m_Ownership;

        other.m_Allocator = VK_NULL_HANDLE;
        other.m_Allocation = VK_NULL_HANDLE;
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

    VmaAllocation VulkanTexture::getAllocation() const {
        return m_Allocation;
    }

    VulkanTextureOwnership VulkanTexture::getOwnership() const {
        return m_Ownership;
    }

    void VulkanTexture::setState(ResourceState state) {
        m_State = state;
    }

    void VulkanTexture::reset() {
        // Swapchain image 由 VkSwapchainKHR 拥有，借用型 texture 绝不能销毁 VkImage。
        if (m_Ownership == VulkanTextureOwnership::Owned && m_Allocator != VK_NULL_HANDLE &&
            m_Image != VK_NULL_HANDLE) {
            vmaDestroyImage(m_Allocator, m_Image, m_Allocation);
        }

        m_Allocator = VK_NULL_HANDLE;
        m_Allocation = VK_NULL_HANDLE;
        m_Device = VK_NULL_HANDLE;
        m_Image = VK_NULL_HANDLE;
        m_Ownership = VulkanTextureOwnership::Borrowed;
        m_State = ResourceState::Undefined;
    }
} // namespace ark::rhi::vulkan
