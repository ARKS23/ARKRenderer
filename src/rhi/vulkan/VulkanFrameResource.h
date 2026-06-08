#pragma once

#include "core/Memory.h"
#include "core/Types.h"
#include "rhi/FrameResource.h"
#include "rhi/vulkan/VulkanCommandBuffer.h"
#include "rhi/vulkan/VulkanCommandPool.h"
#include "rhi/vulkan/VulkanDeletionQueue.h"
#include "rhi/vulkan/VulkanSync.h"

namespace ark::rhi::vulkan {
    struct VulkanFrameResource : FrameResource {
        VulkanFrameResource() = default;
        VulkanFrameResource(VkDevice device, u32 graphicsQueueFamily, u32 slot);

        // VulkanFrameResource 拥有一帧录制和提交所需的后端对象。
        Scope<VulkanCommandPool> commandPool;
        Scope<VulkanCommandBuffer> commandBuffer;
        Scope<VulkanSync> imageAvailableSemaphore;
        Scope<VulkanSync> renderFinishedSemaphore;
        Scope<VulkanSync> inFlightFence;
        Scope<VulkanDeletionQueue> deferredDeletion;
        VkSemaphore swapChainRenderFinishedSemaphore = VK_NULL_HANDLE;

        VkSemaphore getImageAvailableSemaphore() const {
            return imageAvailableSemaphore ? imageAvailableSemaphore->getSemaphore() : VK_NULL_HANDLE;
        }

        VkSemaphore getRenderFinishedSemaphore() const {
            if (swapChainRenderFinishedSemaphore != VK_NULL_HANDLE) {
                return swapChainRenderFinishedSemaphore;
            }

            return renderFinishedSemaphore ? renderFinishedSemaphore->getSemaphore() : VK_NULL_HANDLE;
        }

        void setSwapChainRenderFinishedSemaphore(VkSemaphore semaphore) {
            swapChainRenderFinishedSemaphore = semaphore;
        }

        VkFence getInFlightFence() const {
            return inFlightFence ? inFlightFence->getFence() : VK_NULL_HANDLE;
        }

        VkCommandBuffer getCommandBuffer() const {
            return commandBuffer ? commandBuffer->getHandle() : VK_NULL_HANDLE;
        }
    };
} // namespace ark::rhi::vulkan
