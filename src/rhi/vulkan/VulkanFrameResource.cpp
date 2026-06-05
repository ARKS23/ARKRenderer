#include "rhi/vulkan/VulkanFrameResource.h"

#include "core/Memory.h"

namespace ark::rhi::vulkan {
    VulkanFrameResource::VulkanFrameResource(VkDevice device, u32 graphicsQueueFamily, u32 slot) {
        frameSlot = slot;

        commandPool = makeScope<VulkanCommandPool>(device, graphicsQueueFamily);
        commandBuffer =
            makeScope<VulkanCommandBuffer>(device, commandPool->getHandle(), commandPool->allocatePrimaryCommandBuffer());

        VulkanSyncDesc imageAvailableDesc{};
        imageAvailableDesc.type = VulkanSyncType::Semaphore;
        imageAvailableSemaphore = makeScope<VulkanSync>(device, imageAvailableDesc);

        VulkanSyncDesc renderFinishedDesc{};
        renderFinishedDesc.type = VulkanSyncType::Semaphore;
        renderFinishedSemaphore = makeScope<VulkanSync>(device, renderFinishedDesc);

        VulkanSyncDesc inFlightDesc{};
        inFlightDesc.type = VulkanSyncType::Fence;
        inFlightDesc.signaled = true;
        inFlightFence = makeScope<VulkanSync>(device, inFlightDesc);

        deferredDeletion = makeScope<VulkanDeletionQueue>();
    }
} // namespace ark::rhi::vulkan
