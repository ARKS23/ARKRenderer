#include "rhi/vulkan/VulkanCommon.h"

#include "core/Log.h"

#include <fmt/core.h>

namespace ark::rhi::vulkan {
    const char* formatName(Format format) {
        switch (format) {
        case Format::Unknown:
            return "Unknown";
        case Format::R32G32Float:
            return "R32G32Float";
        case Format::R32G32B32Float:
            return "R32G32B32Float";
        case Format::R32G32B32A32Float:
            return "R32G32B32A32Float";
        case Format::BGRA8Unorm:
            return "BGRA8Unorm";
        case Format::RGBA8Unorm:
            return "RGBA8Unorm";
        case Format::RGBA8Srgb:
            return "RGBA8Srgb";
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
        case VK_FORMAT_R32G32_SFLOAT:
            return "VK_FORMAT_R32G32_SFLOAT";
        case VK_FORMAT_R32G32B32_SFLOAT:
            return "VK_FORMAT_R32G32B32_SFLOAT";
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return "VK_FORMAT_R32G32B32A32_SFLOAT";
        case VK_FORMAT_B8G8R8A8_UNORM:
            return "VK_FORMAT_B8G8R8A8_UNORM";
        case VK_FORMAT_R8G8B8A8_UNORM:
            return "VK_FORMAT_R8G8B8A8_UNORM";
        case VK_FORMAT_R8G8B8A8_SRGB:
            return "VK_FORMAT_R8G8B8A8_SRGB";
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

    const char* vkResultName(VkResult result) {
        switch (result) {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_EVENT_SET:
            return "VK_EVENT_SET";
        case VK_EVENT_RESET:
            return "VK_EVENT_RESET";
        case VK_INCOMPLETE:
            return "VK_INCOMPLETE";
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:
            return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:
            return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
            return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        default:
            return "VK_RESULT_UNKNOWN";
        }
    }

    bool checkVkResult(VkResult result, const char* expression, const char* file, int line) {
        if (result == VK_SUCCESS) {
            return true;
        }

        ARK_ERROR("{} failed: {} ({}) at {}:{}", expression, vkResultName(result), static_cast<int>(result), file, line);
        return false;
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
        case Format::R32G32Float:
            return VK_FORMAT_R32G32_SFLOAT;
        case Format::R32G32B32Float:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case Format::R32G32B32A32Float:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::BGRA8Unorm:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::RGBA8Unorm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8Srgb:
            return VK_FORMAT_R8G8B8A8_SRGB;
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
        case VK_FORMAT_R32G32_SFLOAT:
            return Format::R32G32Float;
        case VK_FORMAT_R32G32B32_SFLOAT:
            return Format::R32G32B32Float;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return Format::R32G32B32A32Float;
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            return Format::BGRA8Unorm;
        case VK_FORMAT_R8G8B8A8_UNORM:
            return Format::RGBA8Unorm;
        case VK_FORMAT_R8G8B8A8_SRGB:
            return Format::RGBA8Srgb;
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
