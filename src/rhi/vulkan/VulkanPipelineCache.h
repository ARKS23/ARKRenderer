#pragma once

namespace ark::rhi::vulkan {
    // 缓存 graphics/compute pipeline，避免重复创建昂贵的 Vulkan pipeline。
    class VulkanPipelineCache {};
} // namespace ark::rhi::vulkan
