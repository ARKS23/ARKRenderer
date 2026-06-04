#pragma once

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

#include <memory>
#include <string>

namespace ark::rhi {
    struct RenderDeviceDesc {
        bool enableValidation = false;
        NativeWindowHandle nativeWindow;
    };

    struct RenderDeviceCaps {
        std::string gpuName;
        u32 apiVersion = 0;
        u32 graphicsQueueFamily = 0;
        u32 presentQueueFamily = 0;
    };

    using BufferPtr = std::unique_ptr<Buffer>;
    using DescriptorSetLayoutPtr = std::unique_ptr<DescriptorSetLayout>;
    using DescriptorSetPtr = std::unique_ptr<DescriptorSet>;
    using FencePtr = std::unique_ptr<Fence>;
    using PipelineLayoutPtr = std::unique_ptr<PipelineLayout>;
    using PipelineStatePtr = std::unique_ptr<PipelineState>;
    using SamplerPtr = std::unique_ptr<Sampler>;
    using ShaderPtr = std::unique_ptr<Shader>;
    using TexturePtr = std::unique_ptr<Texture>;
    using TextureViewPtr = std::unique_ptr<TextureView>;

    class RenderDevice {
    public:
        virtual ~RenderDevice() = default;

        virtual void waitIdle() = 0;

        [[nodiscard]] virtual const RenderDeviceCaps& getCaps() const = 0;

        // RenderDevice 只负责创建 GPU 对象和维护设备能力，不参与每帧 draw/submit。
        [[nodiscard]] virtual BufferPtr createBuffer(const BufferDesc& desc) = 0;
        [[nodiscard]] virtual TexturePtr createTexture(const TextureDesc& desc) = 0;
        [[nodiscard]] virtual TextureViewPtr createTextureView(Texture& texture, const TextureViewDesc& desc) = 0;
        [[nodiscard]] virtual SamplerPtr createSampler(const SamplerDesc& desc) = 0;
        [[nodiscard]] virtual ShaderPtr createShader(const ShaderDesc& desc) = 0;
        [[nodiscard]] virtual PipelineLayoutPtr createPipelineLayout(const PipelineLayoutDesc& desc) = 0;
        [[nodiscard]] virtual PipelineStatePtr createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
        [[nodiscard]] virtual DescriptorSetLayoutPtr createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) = 0;
        [[nodiscard]] virtual DescriptorSetPtr createDescriptorSet(const DescriptorSetLayout& layout) = 0;
        [[nodiscard]] virtual FencePtr createFence() = 0;
    };

    using RenderDevicePtr = std::unique_ptr<RenderDevice>;
} // namespace ark::rhi
