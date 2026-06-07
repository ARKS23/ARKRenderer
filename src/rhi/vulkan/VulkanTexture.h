#pragma once

#include "rhi/Texture.h"
#include "rhi/vulkan/VulkanAllocator.h"
#include "rhi/vulkan/VulkanCommon.h"

namespace ark::rhi::vulkan {
    enum class VulkanTextureOwnership {
        Borrowed,   // 包装外部image，不负责销毁；如 swapchain backbuffer
        Owned,
    };

    class VulkanTexture final : public Texture {
    public:
        VulkanTexture(VmaAllocator allocator, const TextureDesc& desc);
        VulkanTexture(VkDevice device, VkImage image, const TextureDesc& desc, VulkanTextureOwnership ownership);
        ~VulkanTexture() override;

        VulkanTexture(const VulkanTexture&) = delete;
        VulkanTexture& operator=(const VulkanTexture&) = delete;

        VulkanTexture(VulkanTexture&& other) noexcept;
        VulkanTexture& operator=(VulkanTexture&& other) noexcept;

        const TextureDesc& getDesc() const override;
        ResourceState getState() const override;

        VkImage getHandle() const;
        VmaAllocation getAllocation() const;
        VulkanTextureOwnership getOwnership() const;
        void setState(ResourceState state);

    private:
        void reset();

        VmaAllocator m_Allocator = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkImage m_Image = VK_NULL_HANDLE;
        TextureDesc m_Desc;
        ResourceState m_State = ResourceState::Undefined;
        VulkanTextureOwnership m_Ownership = VulkanTextureOwnership::Borrowed;
    };
} // namespace ark::rhi::vulkan
