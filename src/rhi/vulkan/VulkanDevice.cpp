#include "rhi/vulkan/VulkanDevice.h"

#include "core/Log.h"
#include "core/Memory.h"
#include "core/ScopeExit.h"
#include "rhi/vulkan/VulkanBuffer.h"
#include "rhi/vulkan/VulkanDescriptorSet.h"
#include "rhi/vulkan/VulkanDescriptorSetLayout.h"
#include "rhi/vulkan/VulkanPipelineLayout.h"
#include "rhi/vulkan/VulkanPipelineState.h"
#include "rhi/vulkan/VulkanSampler.h"
#include "rhi/vulkan/VulkanShader.h"
#include "rhi/vulkan/VulkanTexture.h"
#include "rhi/vulkan/VulkanTextureView.h"

#include <VkBootstrap.h>
#include <GLFW/glfw3.h>

#include <fmt/core.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace ark::rhi::vulkan {
    namespace {
        std::string makeVkbErrorMessage(const char* operation, const auto& result) {
            std::string message = fmt::format("{} failed: {}", operation, result.error().message());

            const VkResult vkResult = result.vk_result();
            if (vkResult != VK_SUCCESS) {
                message += fmt::format(" ({}: {})", vkResultName(vkResult), static_cast<int>(vkResult));
            }

            const std::vector<std::string>& reasons = result.detailed_failure_reasons();
            if (!reasons.empty()) {
                message += " | Reasons:";
                for (const std::string& reason : reasons) {
                    message += " ";
                    message += reason;
                }
            }

            return message;
        }

        std::vector<const char*> getRequiredGlfwInstanceExtensions() {
            u32 extensionCount = 0;
            const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);   // 暂时GLFW Hardcode
            if (!extensions || extensionCount == 0) {
                throw std::runtime_error("glfwGetRequiredInstanceExtensions failed");
            }

            return std::vector<const char*>(extensions, extensions + extensionCount);
        }

        u32 chooseApiVersion(const RenderDeviceDesc& desc) {
            if (desc.preferredApiVersion != 0) {
                return desc.preferredApiVersion;
            }

            return VK_API_VERSION_1_3;
        }

        void throwUnsupportedFactory(const char* functionName) {
            throw std::logic_error(fmt::format("{} is not implemented in Phase 0.2", functionName));
        }
    } // namespace

    VulkanDevice::VulkanDevice(const RenderDeviceCreateInfo& createInfo) : m_Desc(createInfo.desc) {
        if (createInfo.nativeWindow.type != NativeWindowType::GLFW || !createInfo.nativeWindow.handle) {
            throw std::runtime_error("VulkanDevice requires a valid GLFW native window handle");
        }

        if (!ARK_VK_CHECK(volkInitialize())) {
            throw std::runtime_error("volkInitialize failed");
        }

        auto cleanupOnFailure = makeScopeExit([this]() {
            destroy();
        });

        createInstance(createInfo);
        createSurface(createInfo.nativeWindow);
        createDevice();
        cleanupOnFailure.release();

        ARK_INFO("Vulkan device initialized: GPU='{}', API={}, graphicsQueue={}, presentQueue={}", m_Caps.gpuName,
                 vulkanVersionToString(m_Caps.apiVersion), m_Caps.graphicsQueueFamily, m_Caps.presentQueueFamily);
    }

    VulkanDevice::~VulkanDevice() {
        destroy();
    }

    void VulkanDevice::waitIdle() {
        if (m_Device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_Device);
        }
    }

    RenderBackendType VulkanDevice::getBackendType() const {
        return m_Desc.backend;
    }

    const RenderDeviceCaps& VulkanDevice::getCaps() const {
        return m_Caps;
    }

    VkInstance VulkanDevice::getInstance() const {
        return m_Instance;
    }

    VkPhysicalDevice VulkanDevice::getPhysicalDevice() const {
        return m_PhysicalDevice;
    }

    VkDevice VulkanDevice::getDevice() const {
        return m_Device;
    }

    VkSurfaceKHR VulkanDevice::getSurface() const {
        return m_Surface;
    }

    VkQueue VulkanDevice::getGraphicsQueue() const {
        return m_GraphicsQueue;
    }

    VkQueue VulkanDevice::getPresentQueue() const {
        return m_PresentQueue;
    }

    u32 VulkanDevice::getGraphicsQueueFamily() const {
        return m_Caps.graphicsQueueFamily;
    }

    u32 VulkanDevice::getPresentQueueFamily() const {
        return m_Caps.presentQueueFamily;
    }

    VmaAllocator VulkanDevice::getAllocator() const {
        return m_Allocator ? m_Allocator->getHandle() : VK_NULL_HANDLE;
    }

    Scope<Buffer> VulkanDevice::createBuffer(const BufferDesc& desc) {
        return makeScope<VulkanBuffer>(getAllocator(), desc);
    }

    Scope<Texture> VulkanDevice::createTexture(const TextureDesc& desc) {
        return makeScope<VulkanTexture>(getAllocator(), desc);
    }

    Scope<TextureView> VulkanDevice::createTextureView(Texture& texture, const TextureViewDesc& desc) {
        VulkanTexture* vulkanTexture = dynamic_cast<VulkanTexture*>(&texture);
        if (!vulkanTexture || vulkanTexture->getHandle() == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanDevice::createTextureView requires VulkanTexture");
        }

        return makeScope<VulkanTextureView>(m_Device, *vulkanTexture, desc);
    }

    Scope<Sampler> VulkanDevice::createSampler(const SamplerDesc& desc) {
        return makeScope<VulkanSampler>(m_Device, desc);
    }

    Scope<Shader> VulkanDevice::createShader(const ShaderDesc& desc) {
        return makeScope<VulkanShader>(m_Device, desc);
    }

    Scope<PipelineLayout> VulkanDevice::createPipelineLayout(const PipelineLayoutDesc& desc) {
        return makeScope<VulkanPipelineLayout>(m_Device, desc);
    }

    Scope<PipelineState> VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
        return makeScope<VulkanPipelineState>(m_Device, desc);
    }

    Scope<DescriptorSetLayout> VulkanDevice::createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) {
        return makeScope<VulkanDescriptorSetLayout>(m_Device, desc);
    }

    Scope<DescriptorSet> VulkanDevice::createDescriptorSet(const DescriptorSetLayout& layout) {
        const VulkanDescriptorSetLayout* vulkanLayout = dynamic_cast<const VulkanDescriptorSetLayout*>(&layout);
        if (!vulkanLayout || vulkanLayout->getHandle() == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanDevice::createDescriptorSet requires VulkanDescriptorSetLayout");
        }

        if (!m_DescriptorManager) {
            throw std::runtime_error("VulkanDevice::createDescriptorSet requires VulkanDescriptorManager");
        }

        // Descriptor set 的底层分配来自 device 级 descriptor manager，wrapper 不拥有 descriptor pool。
        VkDescriptorSet descriptorSet = m_DescriptorManager->allocateDescriptorSet(*vulkanLayout);
        return makeScope<VulkanDescriptorSet>(m_Device, descriptorSet, *vulkanLayout);
    }

    Scope<Fence> VulkanDevice::createFence() {
        throwUnsupportedFactory("VulkanDevice::createFence");
        return {};
    }

    void VulkanDevice::createInstance(const RenderDeviceCreateInfo& createInfo) {
        // GLFW 决定平台 surface 所需的 instance extension，VulkanDevice 只借用窗口句柄。
        std::vector<const char*> instanceExtensions = getRequiredGlfwInstanceExtensions();
        const u32 apiVersion = chooseApiVersion(createInfo.desc);

        vkb::InstanceBuilder builder;
        builder.set_app_name(createInfo.desc.applicationName.c_str())
            .set_app_version(createInfo.desc.applicationVersion)
            .require_api_version(apiVersion)
            .enable_extensions(instanceExtensions)
            .request_validation_layers(createInfo.desc.enableValidation);

        if (createInfo.desc.enableValidation) {
            builder.use_default_debug_messenger();
        }

        auto instanceResult = builder.build();
        if (!instanceResult) {
            throw std::runtime_error(makeVkbErrorMessage("vkb::InstanceBuilder::build", instanceResult));
        }

        const vkb::Instance instance = instanceResult.value();
        m_Instance = instance.instance;
        m_DebugMessenger = instance.debug_messenger;
        volkLoadInstance(m_Instance);

        ARK_INFO("Vulkan instance created: requestedApi={}, validation={}", vulkanVersionToString(apiVersion),
                 createInfo.desc.enableValidation ? "enabled" : "disabled");
    }

    void VulkanDevice::createSurface(NativeWindowHandle nativeWindow) {
        if (!ARK_VK_CHECK(
                glfwCreateWindowSurface(m_Instance, static_cast<GLFWwindow*>(nativeWindow.handle), nullptr, &m_Surface))) {
            throw std::runtime_error("glfwCreateWindowSurface failed");
        }

        ARK_INFO("Vulkan surface created");
    }

    void VulkanDevice::createDevice() {
        // physical device 选择必须带 surface，否则无法确认 present queue 支持。
        vkb::Instance instance{};
        instance.instance = m_Instance;

        vkb::PhysicalDeviceSelector selector(instance);
        selector.set_surface(m_Surface).prefer_gpu_device_type(vkb::PreferredDeviceType::discrete);

        auto physicalDeviceResult = selector.select();
        if (!physicalDeviceResult) {
            throw std::runtime_error(makeVkbErrorMessage("vkb::PhysicalDeviceSelector::select", physicalDeviceResult));
        }

        vkb::PhysicalDevice selectedPhysicalDevice = physicalDeviceResult.value();
        vkb::DeviceBuilder deviceBuilder(selectedPhysicalDevice);
        VkPhysicalDeviceVulkan13Features supportedVulkan13Features{};
        supportedVulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2 supportedFeatures{};
        supportedFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supportedFeatures.pNext = &supportedVulkan13Features;
        vkGetPhysicalDeviceFeatures2(selectedPhysicalDevice.physical_device, &supportedFeatures);

        if (supportedVulkan13Features.shaderDemoteToHelperInvocation != VK_TRUE) {
            throw std::runtime_error("Vulkan device does not support shaderDemoteToHelperInvocation");
        }

        VkPhysicalDeviceVulkan13Features vulkan13Features{};
        vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        vulkan13Features.dynamicRendering = VK_TRUE;
        // mesh alpha mask 的 discard 会编译成 DemoteToHelperInvocation，必须在 device feature 中显式开启。
        vulkan13Features.shaderDemoteToHelperInvocation = VK_TRUE;

        deviceBuilder.add_pNext(&vulkan13Features);

        auto deviceResult = deviceBuilder.build();
        if (!deviceResult) {
            throw std::runtime_error(makeVkbErrorMessage("vkb::DeviceBuilder::build", deviceResult));
        }

        vkb::Device device = deviceResult.value();
        m_PhysicalDevice = selectedPhysicalDevice.physical_device;
        m_Device = device.device;
        volkLoadDevice(m_Device);

        auto graphicsQueueResult = device.get_queue(vkb::QueueType::graphics);
        auto presentQueueResult = device.get_queue(vkb::QueueType::present);
        auto graphicsQueueFamilyResult = device.get_queue_index(vkb::QueueType::graphics);
        auto presentQueueFamilyResult = device.get_queue_index(vkb::QueueType::present);

        if (!graphicsQueueResult || !presentQueueResult || !graphicsQueueFamilyResult || !presentQueueFamilyResult) {
            throw std::runtime_error("Failed to get Vulkan graphics/present queues");
        }

        m_GraphicsQueue = graphicsQueueResult.value();
        m_PresentQueue = presentQueueResult.value();
        m_Caps.graphicsQueueFamily = graphicsQueueFamilyResult.value();
        m_Caps.presentQueueFamily = presentQueueFamilyResult.value();

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(m_PhysicalDevice, &properties);

        m_Caps.gpuName = properties.deviceName;
        m_Caps.apiVersion = properties.apiVersion;
        m_Allocator = makeScope<VulkanAllocator>(m_Instance, m_PhysicalDevice, m_Device, m_Caps.apiVersion);
        // Descriptor pool 属于设备级资源，后续 pass 只通过 RenderDevice 分配 descriptor set。
        m_DescriptorManager = makeScope<VulkanDescriptorManager>(m_Device);
    }

    void VulkanDevice::destroy() {
        // Vulkan 对象按依赖反序销毁：device -> surface/debug messenger -> instance。
        m_DescriptorManager.reset();
        m_Allocator.reset();

        if (m_Device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_Device);
            vkDestroyDevice(m_Device, nullptr);
            m_Device = VK_NULL_HANDLE;
        }

        if (m_Surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
            m_Surface = VK_NULL_HANDLE;
        }

        if (m_DebugMessenger != VK_NULL_HANDLE) {
            vkDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
            m_DebugMessenger = VK_NULL_HANDLE;
        }

        if (m_Instance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_Instance, nullptr);
            m_Instance = VK_NULL_HANDLE;
        }

        m_PhysicalDevice = VK_NULL_HANDLE;
        m_GraphicsQueue = VK_NULL_HANDLE;
        m_PresentQueue = VK_NULL_HANDLE;
    }
} // namespace ark::rhi::vulkan
