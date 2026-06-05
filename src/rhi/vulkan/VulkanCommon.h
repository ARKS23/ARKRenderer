#pragma once

#include "rhi/RHICommon.h"

#include <volk.h>

#include <string>

namespace ark::rhi::vulkan {
    // 后端内部工具函数统一放在这里，避免 VkFormat/VkPresentModeKHR 映射散落到各个模块。
    const char* formatName(Format format);
    const char* vkResultName(VkResult result);
    const char* vkFormatName(VkFormat format);
    const char* presentModeName(VkPresentModeKHR presentMode);
    std::string vulkanVersionToString(u32 version);

    // 只用于必须返回 VK_SUCCESS 的 Vulkan 调用；swapchain acquire/present 仍使用专门状态转换。
    bool checkVkResult(VkResult result, const char* expression, const char* file, int line);

    VkFormat toVkFormat(Format format);
    Format fromVkFormat(VkFormat format);
} // namespace ark::rhi::vulkan

#define ARK_VK_CHECK(expression) \
    (::ark::rhi::vulkan::checkVkResult((expression), #expression, __FILE__, __LINE__))
