#pragma once

#include "rhi/SwapChain.h"
#include "rhi/vulkan/VulkanCommon.h"
#include "rhi/vulkan/VulkanTexture.h"
#include "rhi/vulkan/VulkanTextureView.h"

#include <vector>

namespace ark::rhi::vulkan {
    class VulkanDevice;

    class VulkanSwapChain : public SwapChain {
    public:
        explicit VulkanSwapChain(const SwapChainCreateInfo& createInfo);
        ~VulkanSwapChain() override;

        const SwapChainDesc& getDesc() const override;
        u32 getBackBufferCount() const override;
        u32 getCurrentBackBufferIndex() const override;
        TextureView* getCurrentBackBufferView() override;
        TextureView* getDepthBufferView() override;

        AcquireResult acquireNextImage(FrameResource& frameResource) override;
        SwapChainStatus present(FrameResource& frameResource) override;
        SwapChainStatus resize(Extent2D extent) override;

    private:
        void create();
        void destroy();

        VulkanDevice* m_Device = nullptr;
        SwapChainDesc m_Desc;
        u32 m_BackBufferCount = 0;
        u32 m_CurrentBackBufferIndex = InvalidBackBufferIndex;

        VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
        VkFormat m_ColorFormat = VK_FORMAT_UNDEFINED;
        VkPresentModeKHR m_PresentMode = VK_PRESENT_MODE_FIFO_KHR;
        std::vector<VkImage> m_BackBufferImages;
        std::vector<Scope<VulkanTexture>> m_BackBufferTextures;
        std::vector<Scope<VulkanTextureView>> m_BackBufferViews;
        Scope<VulkanTextureView> m_DepthBufferView;
    };
} // namespace ark::rhi::vulkan
