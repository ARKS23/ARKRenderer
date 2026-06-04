#include "rhi/vulkan/VulkanSwapChain.h"

#include "core/Log.h"
#include "core/Memory.h"
#include "rhi/vulkan/VulkanDevice.h"

#include <VkBootstrap.h>

#include <fmt/core.h>

#include <algorithm>
#include <stdexcept>

namespace ark::rhi::vulkan {
    namespace {
        std::string makeVkbErrorMessage(const char* operation, const auto& result) {
            std::string message = fmt::format("{} failed: {}", operation, result.error().message());

            const VkResult vkResult = result.vk_result();
            if (vkResult != VK_SUCCESS) {
                message += fmt::format(" (VkResult: {})", static_cast<int>(vkResult));
            }

            return message;
        }

        VkSurfaceFormatKHR chooseDesiredFormat(Format format) {
            VkFormat vkFormat = toVkFormat(format);
            if (vkFormat == VK_FORMAT_UNDEFINED) {
                vkFormat = VK_FORMAT_B8G8R8A8_UNORM;
            }

            return VkSurfaceFormatKHR{
                .format = vkFormat,
                .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
            };
        }
    } // namespace

    VulkanSwapChain::VulkanSwapChain(const SwapChainCreateInfo& createInfo) : m_Desc(createInfo.desc) {
        m_Device = dynamic_cast<VulkanDevice*>(createInfo.device);
        if (!m_Device) {
            throw std::runtime_error("VulkanSwapChain requires a VulkanDevice");
        }

        if (!isValidExtent(m_Desc.extent)) {
            ARK_WARN("Skip Vulkan swapchain creation because extent is zero");
            return;
        }

        try {
            create();
        } catch (...) {
            destroy();
            throw;
        }
    }

    VulkanSwapChain::~VulkanSwapChain() {
        destroy();
    }

    const SwapChainDesc& VulkanSwapChain::getDesc() const {
        return m_Desc;
    }

    u32 VulkanSwapChain::getBackBufferCount() const {
        return m_BackBufferCount;
    }

    TextureView* VulkanSwapChain::getCurrentBackBufferView() {
        if (m_BackBufferViews.empty()) {
            return nullptr;
        }

        return m_BackBufferViews[m_CurrentBackBufferIndex].get();
    }

    TextureView* VulkanSwapChain::getDepthBufferView() {
        return m_DepthBufferView.get();
    }

    SwapChainStatus VulkanSwapChain::resize(Extent2D extent) {
        m_Desc.extent = extent;

        if (!isValidExtent(extent)) {
            m_Device->waitIdle();
            destroy();
            return SwapChainStatus::OutOfDate;
        }

        m_Device->waitIdle();
        destroy();
        create();
        return SwapChainStatus::Ready;
    }

    void VulkanSwapChain::create() {
        // SwapChain 借用 VulkanDevice 持有的 surface；它只拥有 swapchain 和 image view。
        vkb::SwapchainBuilder builder(m_Device->getPhysicalDevice(), m_Device->getDevice(), m_Device->getSurface(),
                                      m_Device->getGraphicsQueueFamily(), m_Device->getPresentQueueFamily());

        builder.set_desired_extent(m_Desc.extent.width, m_Desc.extent.height)
            .set_desired_min_image_count(std::max<u32>(2, m_Desc.imageCount))
            .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

        if (m_Desc.colorFormat != Format::Unknown) {
            builder.set_desired_format(chooseDesiredFormat(m_Desc.colorFormat))
                .add_fallback_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                .add_fallback_format({VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
        } else {
            builder.use_default_format_selection();
        }

        if (m_Desc.enableVSync) {
            builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
        } else {
            builder.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
                .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR);
        }

        auto swapChainResult = builder.build();
        if (!swapChainResult) {
            throw std::runtime_error(makeVkbErrorMessage("vkb::SwapchainBuilder::build", swapChainResult));
        }

        vkb::Swapchain swapChain = swapChainResult.value();
        m_SwapChain = swapChain.swapchain;
        m_ColorFormat = swapChain.image_format;
        m_PresentMode = swapChain.present_mode;
        m_Desc.colorFormat = fromVkFormat(swapChain.image_format);
        m_Desc.extent = Extent2D{swapChain.extent.width, swapChain.extent.height};
        m_BackBufferCount = swapChain.image_count;

        auto imagesResult = swapChain.get_images();
        if (!imagesResult) {
            throw std::runtime_error(makeVkbErrorMessage("vkb::Swapchain::get_images", imagesResult));
        }

        auto imageViewsResult = swapChain.get_image_views();
        if (!imageViewsResult) {
            throw std::runtime_error(makeVkbErrorMessage("vkb::Swapchain::get_image_views", imageViewsResult));
        }

        m_BackBufferImages = imagesResult.value();
        std::vector<VkImageView> imageViews = imageViewsResult.value();
        m_BackBufferViews.reserve(imageViews.size());

        TextureViewDesc viewDesc{};
        viewDesc.format = m_Desc.colorFormat;
        for (VkImageView imageView : imageViews) {
            // swapchain image 由 VkSwapchainKHR 拥有，只有 image view 需要我们销毁。
            m_BackBufferViews.push_back(makeScope<VulkanTextureView>(m_Device->getDevice(), imageView, viewDesc));
        }

        m_CurrentBackBufferIndex = 0;

        ARK_INFO("Vulkan swapchain created: extent={}x{}, images={}, format={}, presentMode={}", m_Desc.extent.width,
                 m_Desc.extent.height, m_BackBufferCount, vkFormatName(m_ColorFormat), presentModeName(m_PresentMode));
        ARK_INFO("Default depth buffer is owned by SwapChain and will be allocated before the first clear pass");
    }

    void VulkanSwapChain::destroy() {
        // image view 必须早于 VkSwapchainKHR 销毁。
        m_DepthBufferView.reset();
        m_BackBufferViews.clear();
        m_BackBufferImages.clear();
        m_BackBufferCount = 0;
        m_CurrentBackBufferIndex = 0;

        if (m_SwapChain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(m_Device->getDevice(), m_SwapChain, nullptr);
            m_SwapChain = VK_NULL_HANDLE;
        }

        m_ColorFormat = VK_FORMAT_UNDEFINED;
        m_PresentMode = VK_PRESENT_MODE_FIFO_KHR;
    }
} // namespace ark::rhi::vulkan
