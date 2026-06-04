#pragma once

#include "core/Memory.h"
#include "rhi/Buffer.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DescriptorSetLayout.h"
#include "rhi/Fence.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RHICommon.h"
#include "rhi/Sampler.h"
#include "rhi/Shader.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <string>

namespace ark::rhi {
    // 设备描述只保存后端选择和设备级配置，不持有窗口或 surface 等外部对象。
    struct RenderDeviceDesc {
        RenderBackendType backend = RenderBackendType::Vulkan;
        bool enableValidation = false;
        std::string applicationName = "ARKRenderer";
        u32 applicationVersion = 0;
        u32 preferredApiVersion = 0;
    };

    // 创建设备时需要窗口句柄来建立 surface，并据此选择支持 present 的物理设备。
    struct RenderDeviceCreateInfo {
        RenderDeviceDesc desc;
        NativeWindowHandle nativeWindow;
    };

    struct RenderDeviceCaps {
        std::string gpuName;
        u32 apiVersion = 0;
        u32 graphicsQueueFamily = 0;
        u32 presentQueueFamily = 0;
    };

    class RenderDevice {
    public:
        virtual ~RenderDevice() = default;

        virtual void waitIdle() = 0;
        virtual RenderBackendType getBackendType() const = 0;

        virtual const RenderDeviceCaps& getCaps() const = 0;

        // RenderDevice 只负责创建 GPU 对象和维护设备能力，不参与每帧 draw/submit。
        virtual Scope<Buffer> createBuffer(const BufferDesc& desc) = 0;
        virtual Scope<Texture> createTexture(const TextureDesc& desc) = 0;
        virtual Scope<TextureView> createTextureView(Texture& texture, const TextureViewDesc& desc) = 0;
        virtual Scope<Sampler> createSampler(const SamplerDesc& desc) = 0;
        virtual Scope<Shader> createShader(const ShaderDesc& desc) = 0;
        virtual Scope<PipelineLayout> createPipelineLayout(const PipelineLayoutDesc& desc) = 0;
        virtual Scope<PipelineState> createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
        virtual Scope<DescriptorSetLayout> createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) = 0;
        virtual Scope<DescriptorSet> createDescriptorSet(const DescriptorSetLayout& layout) = 0;
        virtual Scope<Fence> createFence() = 0;
    };
} // namespace ark::rhi
