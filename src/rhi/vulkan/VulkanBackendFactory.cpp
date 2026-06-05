#include "rhi/detail/RenderBackendFactory.h"

#include "core/Memory.h"
#include "rhi/vulkan/VulkanCommandContext.h"
#include "rhi/vulkan/VulkanDevice.h"
#include "rhi/vulkan/VulkanSwapChain.h"

#include <stdexcept>

namespace ark::rhi::detail {
    Scope<RenderDevice> createRenderDevice(const RenderDeviceCreateInfo& createInfo) {
        switch (createInfo.desc.backend) {
            case RenderBackendType::Vulkan:
                return makeScope<vulkan::VulkanDevice>(createInfo);
        }

        throw std::invalid_argument("Unsupported render backend type");
    }

    Scope<DeviceContext> createDeviceContext(RenderDevice& device) {
        switch (device.getBackendType()) {
            case RenderBackendType::Vulkan: {
                auto* vulkanDevice = dynamic_cast<vulkan::VulkanDevice*>(&device);
                if (!vulkanDevice) {
                    throw std::invalid_argument("createDeviceContext requires VulkanDevice");
                }

                return makeScope<vulkan::VulkanCommandContext>(*vulkanDevice);
            }
        }

        throw std::invalid_argument("Unsupported device context backend type");
    }

    Scope<SwapChain> createSwapChain(const SwapChainCreateInfo& createInfo) {
        if (!createInfo.device) {
            throw std::invalid_argument("createSwapChain requires a valid RenderDevice");
        }

        switch (createInfo.device->getBackendType()) {
        case RenderBackendType::Vulkan:
            return makeScope<vulkan::VulkanSwapChain>(createInfo);
        }

        throw std::invalid_argument("Unsupported swapchain backend type");
    }
} // namespace ark::rhi::detail
