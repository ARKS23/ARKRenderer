#pragma once

#include "rhi/TextureView.h"
#include "rhi/vulkan/VulkanCommon.h"

namespace ark::rhi::vulkan {
    class VulkanTexture;

    class VulkanTextureView : public TextureView {
    public:
        VulkanTextureView() = default;
        VulkanTextureView(VkDevice device, VkImageView imageView, VulkanTexture* texture, const TextureViewDesc& desc);
        ~VulkanTextureView() override;

        VulkanTextureView(const VulkanTextureView&) = delete;
        VulkanTextureView& operator=(const VulkanTextureView&) = delete;

        VulkanTextureView(VulkanTextureView&& other) noexcept;
        VulkanTextureView& operator=(VulkanTextureView&& other) noexcept;

        Texture* getTexture() const override;
        const TextureViewDesc& getDesc() const override;

        VkImageView getHandle() const;
        VulkanTexture* getVulkanTexture() const;

    private:
        void reset();

        VkDevice m_Device = VK_NULL_HANDLE;
        VkImageView m_ImageView = VK_NULL_HANDLE;
        VulkanTexture* m_Texture = nullptr;
        TextureViewDesc m_Desc;
    };
} // namespace ark::rhi::vulkan
