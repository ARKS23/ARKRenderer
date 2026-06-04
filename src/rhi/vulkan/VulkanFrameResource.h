#pragma once

#include "core/Types.h"
#include "rhi/FrameResource.h"
#include "rhi/vulkan/VulkanCommandBuffer.h"
#include "rhi/vulkan/VulkanCommandPool.h"
#include "rhi/vulkan/VulkanDeletionQueue.h"
#include "rhi/vulkan/VulkanSync.h"

namespace ark::rhi::vulkan {
    struct VulkanFrameResource : FrameResource {
        VulkanCommandPool* commandPool = nullptr;
        VulkanCommandBuffer* commandBuffer = nullptr;
        VulkanSync* imageAvailableSemaphore = nullptr;
        VulkanSync* renderFinishedSemaphore = nullptr;
        VulkanSync* inFlightFence = nullptr;
        VulkanDeletionQueue* deferredDeletion = nullptr;
    };
} // namespace ark::rhi::vulkan
