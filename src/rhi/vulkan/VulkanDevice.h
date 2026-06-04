#pragma once

#include "rhi/RenderDevice.h"
#include "rhi/vulkan/VulkanCommon.h"

namespace ark::rhi::vulkan {
    class VulkanDevice final : public RenderDevice {
    public:
        explicit VulkanDevice(const RenderDeviceCreateInfo& createInfo);
        ~VulkanDevice() override;

        void waitIdle() override;
        RenderBackendType getBackendType() const override;

        const RenderDeviceCaps& getCaps() const override;

        VkInstance getInstance() const;
        VkPhysicalDevice getPhysicalDevice() const;
        VkDevice getDevice() const;
        VkSurfaceKHR getSurface() const;
        VkQueue getGraphicsQueue() const;
        VkQueue getPresentQueue() const;
        u32 getGraphicsQueueFamily() const;
        u32 getPresentQueueFamily() const;

        Scope<Buffer> createBuffer(const BufferDesc& desc) override;
        Scope<Texture> createTexture(const TextureDesc& desc) override;
        Scope<TextureView> createTextureView(Texture& texture, const TextureViewDesc& desc) override;
        Scope<Sampler> createSampler(const SamplerDesc& desc) override;
        Scope<Shader> createShader(const ShaderDesc& desc) override;
        Scope<PipelineLayout> createPipelineLayout(const PipelineLayoutDesc& desc) override;
        Scope<PipelineState> createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
        Scope<DescriptorSetLayout> createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) override;
        Scope<DescriptorSet> createDescriptorSet(const DescriptorSetLayout& layout) override;
        Scope<Fence> createFence() override;

    private:
        void createInstance(const RenderDeviceCreateInfo& createInfo);
        void createSurface(NativeWindowHandle nativeWindow);
        void createDevice();
        void destroy();

        RenderDeviceDesc m_Desc;
        RenderDeviceCaps m_Caps;

        VkInstance m_Instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
        VkQueue m_PresentQueue = VK_NULL_HANDLE;
    };
} // namespace ark::rhi::vulkan
