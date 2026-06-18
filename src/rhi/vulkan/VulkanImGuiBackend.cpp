#include "rhi/vulkan/VulkanImGuiBackend.h"

#include "core/Log.h"
#include "rhi/vulkan/VulkanCommandContext.h"
#include "rhi/vulkan/VulkanCommon.h"
#include "rhi/vulkan/VulkanDevice.h"
#include "rhi/vulkan/VulkanFrameResource.h"

#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>

namespace ark::rhi::vulkan {
    namespace {
        struct ImGuiVulkanLoaderData {
            VkInstance instance = VK_NULL_HANDLE;
            VkDevice device = VK_NULL_HANDLE;
        };

        void checkImGuiVkResult(VkResult result) {
            if (result != VK_SUCCESS) {
                ARK_ERROR("Dear ImGui Vulkan backend error: {} ({})",
                          vkResultName(result),
                          static_cast<int>(result));
            }
        }

        PFN_vkVoidFunction loadImGuiVulkanFunction(const char* functionName, void* userData) {
            const auto* loaderData = static_cast<const ImGuiVulkanLoaderData*>(userData);
            if (!loaderData) {
                return nullptr;
            }

            PFN_vkVoidFunction function = vkGetInstanceProcAddr(loaderData->instance, functionName);
            if (!function && loaderData->device != VK_NULL_HANDLE) {
                function = vkGetDeviceProcAddr(loaderData->device, functionName);
            }
            return function;
        }
    } // namespace

    VulkanImGuiBackend::~VulkanImGuiBackend() {
        shutdown();
    }

    bool VulkanImGuiBackend::initialize(VulkanDevice& device, const VulkanImGuiBackendDesc& desc) {
        if (m_Initialized) {
            return true;
        }

        if (!desc.glfwWindow || desc.colorFormat == Format::Unknown) {
            ARK_ERROR("VulkanImGuiBackend requires GLFW window and swapchain color format");
            return false;
        }

        ImGuiVulkanLoaderData loaderData{};
        loaderData.instance = device.getInstance();
        loaderData.device = device.getDevice();
        if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, loadImGuiVulkanFunction, &loaderData)) {
            ARK_ERROR("VulkanImGuiBackend failed to load Vulkan functions");
            return false;
        }

        if (!ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(desc.glfwWindow),
                                          desc.installGlfwCallbacks)) {
            ARK_ERROR("VulkanImGuiBackend failed to initialize GLFW backend");
            return false;
        }

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_3;
        initInfo.Instance = device.getInstance();
        initInfo.PhysicalDevice = device.getPhysicalDevice();
        initInfo.Device = device.getDevice();
        initInfo.QueueFamily = device.getGraphicsQueueFamily();
        initInfo.Queue = device.getGraphicsQueue();
        initInfo.DescriptorPoolSize = 64;
        initInfo.MinImageCount = std::max<u32>(2, desc.imageCount);
        initInfo.ImageCount = std::max<u32>(initInfo.MinImageCount, desc.imageCount);
        initInfo.UseDynamicRendering = true;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
        m_ColorFormat = toVkFormat(desc.colorFormat);
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_ColorFormat;
        initInfo.CheckVkResultFn = checkImGuiVkResult;

        if (!ImGui_ImplVulkan_Init(&initInfo)) {
            ImGui_ImplGlfw_Shutdown();
            ARK_ERROR("VulkanImGuiBackend failed to initialize Vulkan backend");
            return false;
        }

        m_Initialized = true;
        return true;
    }

    void VulkanImGuiBackend::shutdown() {
        if (!m_Initialized) {
            return;
        }

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        m_ColorFormat = VK_FORMAT_UNDEFINED;
        m_Initialized = false;
    }

    void VulkanImGuiBackend::newFrame() {
        if (!m_Initialized) {
            return;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
    }

    bool VulkanImGuiBackend::renderDrawData(VulkanCommandContext&,
                                            VulkanFrameResource& frameResource,
                                            ImDrawData* drawData) {
        if (!m_Initialized || !drawData) {
            return true;
        }

        const VkCommandBuffer commandBuffer = frameResource.getCommandBuffer();
        if (commandBuffer == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanImGuiBackend requires an active Vulkan command buffer");
            return false;
        }

        ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
        return true;
    }

    void VulkanImGuiBackend::setMinImageCount(u32 imageCount) {
        if (!m_Initialized) {
            return;
        }

        ImGui_ImplVulkan_SetMinImageCount(std::max<u32>(2, imageCount));
    }
} // namespace ark::rhi::vulkan
