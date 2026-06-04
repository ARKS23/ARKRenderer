#include "rhi/vulkan/VulkanCommon.h"

#include <fmt/core.h>

namespace ark::rhi::vulkan {
    const char* formatName(Format format) {
        switch (format) {
        case Format::Unknown:
            return "Unknown";
        case Format::BGRA8Unorm:
            return "BGRA8Unorm";
        case Format::RGBA8Unorm:
            return "RGBA8Unorm";
        case Format::RGBA16Float:
            return "RGBA16Float";
        case Format::D24UnormS8UInt:
            return "D24UnormS8UInt";
        case Format::D32Float:
            return "D32Float";
        }

        return "Unknown";
    }

    const char* vkFormatName(VkFormat format) {
        switch (format) {
        case VK_FORMAT_UNDEFINED:
            return "VK_FORMAT_UNDEFINED";
        case VK_FORMAT_B8G8R8A8_UNORM:
            return "VK_FORMAT_B8G8R8A8_UNORM";
        case VK_FORMAT_R8G8B8A8_UNORM:
            return "VK_FORMAT_R8G8B8A8_UNORM";
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return "VK_FORMAT_R16G16B16A16_SFLOAT";
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return "VK_FORMAT_D24_UNORM_S8_UINT";
        case VK_FORMAT_D32_SFLOAT:
            return "VK_FORMAT_D32_SFLOAT";
        default:
            return "VK_FORMAT_OTHER";
        }
    }

    const char* presentModeName(VkPresentModeKHR presentMode) {
        switch (presentMode) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:
            return "Immediate";
        case VK_PRESENT_MODE_MAILBOX_KHR:
            return "Mailbox";
        case VK_PRESENT_MODE_FIFO_KHR:
            return "Fifo";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
            return "FifoRelaxed";
        default:
            return "Unknown";
        }
    }

    std::string vulkanVersionToString(u32 version) {
        return fmt::format("{}.{}.{}", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version),
                           VK_API_VERSION_PATCH(version));
    }

    VkFormat toVkFormat(Format format) {
        switch (format) {
        case Format::BGRA8Unorm:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::RGBA8Unorm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA16Float:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::D24UnormS8UInt:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case Format::D32Float:
            return VK_FORMAT_D32_SFLOAT;
        case Format::Unknown:
            return VK_FORMAT_UNDEFINED;
        }

        return VK_FORMAT_UNDEFINED;
    }

    Format fromVkFormat(VkFormat format) {
        switch (format) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            return Format::BGRA8Unorm;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return Format::RGBA8Unorm;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return Format::RGBA16Float;
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return Format::D24UnormS8UInt;
        case VK_FORMAT_D32_SFLOAT:
            return Format::D32Float;
        default:
            return Format::Unknown;
        }
    }
} // namespace ark::rhi::vulkan
