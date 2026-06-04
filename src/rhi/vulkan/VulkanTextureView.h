#pragma once

#include "rhi/TextureView.h"
#include "rhi/vulkan/VulkanCommon.h"

namespace ark::rhi::vulkan {
    class VulkanTextureView : public TextureView {
    public:
        VulkanTextureView() = default;
        VulkanTextureView(VkDevice device, VkImageView imageView, const TextureViewDesc& desc);
        ~VulkanTextureView() override;

        VulkanTextureView(const VulkanTextureView&) = delete;
        VulkanTextureView& operator=(const VulkanTextureView&) = delete;

        VulkanTextureView(VulkanTextureView&& other) noexcept;
        VulkanTextureView& operator=(VulkanTextureView&& other) noexcept;

        VkImageView getHandle() const;
        const TextureViewDesc& getDesc() const;

    private:
        void reset();

        VkDevice m_Device = VK_NULL_HANDLE;
        VkImageView m_ImageView = VK_NULL_HANDLE;
        TextureViewDesc m_Desc;
    };
} // namespace ark::rhi::vulkan
