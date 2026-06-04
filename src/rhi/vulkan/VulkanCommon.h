#pragma once

#include "rhi/RHICommon.h"

#include <volk.h>

#include <string>

namespace ark::rhi::vulkan {
    // 后端内部工具函数统一放在这里，避免 VkFormat/VkPresentModeKHR 映射散落到各个模块。
    const char* formatName(Format format);
    const char* vkFormatName(VkFormat format);
    const char* presentModeName(VkPresentModeKHR presentMode);
    std::string vulkanVersionToString(u32 version);

    VkFormat toVkFormat(Format format);
    Format fromVkFormat(VkFormat format);
} // namespace ark::rhi::vulkan
