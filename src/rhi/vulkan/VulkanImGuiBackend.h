#pragma once

#include "core/Types.h"
#include "rhi/RHICommon.h"
#include "rhi/vulkan/VulkanCommon.h"

struct ImDrawData;

namespace ark::rhi::vulkan {
    class VulkanCommandContext;
    class VulkanDevice;
    struct VulkanFrameResource;

    struct VulkanImGuiBackendDesc {
        void* glfwWindow = nullptr;
        Format colorFormat = Format::Unknown;
        u32 imageCount = 2;
        bool installGlfwCallbacks = true;
    };

    // Vulkan 专用 ImGui 后端封装，避免把 VkCommandBuffer 泄漏到通用 RHI 接口。
    class VulkanImGuiBackend {
    public:
        VulkanImGuiBackend() = default;
        ~VulkanImGuiBackend();

        VulkanImGuiBackend(const VulkanImGuiBackend&) = delete;
        VulkanImGuiBackend& operator=(const VulkanImGuiBackend&) = delete;

        bool initialize(VulkanDevice& device, const VulkanImGuiBackendDesc& desc);
        void shutdown();
        void newFrame();
        bool renderDrawData(VulkanCommandContext& context,
                            VulkanFrameResource& frameResource,
                            ImDrawData* drawData);
        void setMinImageCount(u32 imageCount);

        bool isInitialized() const {
            return m_Initialized;
        }

    private:
        bool m_Initialized = false;
        VkFormat m_ColorFormat = VK_FORMAT_UNDEFINED;
    };
} // namespace ark::rhi::vulkan
