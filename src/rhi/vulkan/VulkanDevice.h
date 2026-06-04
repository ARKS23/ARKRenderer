#pragma once

#include "rhi/RenderDevice.h"

namespace ark::rhi::vulkan {
    class VulkanDevice final : public RenderDevice {
    public:
        void waitIdle() override;

        [[nodiscard]] const RenderDeviceCaps& getCaps() const override;

        [[nodiscard]] BufferPtr createBuffer(const BufferDesc& desc) override;
        [[nodiscard]] TexturePtr createTexture(const TextureDesc& desc) override;
        [[nodiscard]] TextureViewPtr createTextureView(Texture& texture, const TextureViewDesc& desc) override;
        [[nodiscard]] SamplerPtr createSampler(const SamplerDesc& desc) override;
        [[nodiscard]] ShaderPtr createShader(const ShaderDesc& desc) override;
        [[nodiscard]] PipelineLayoutPtr createPipelineLayout(const PipelineLayoutDesc& desc) override;
        [[nodiscard]] PipelineStatePtr createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
        [[nodiscard]] DescriptorSetLayoutPtr createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) override;
        [[nodiscard]] DescriptorSetPtr createDescriptorSet(const DescriptorSetLayout& layout) override;
        [[nodiscard]] FencePtr createFence() override;

    private:
        RenderDeviceCaps m_Caps;
    };
} // namespace ark::rhi::vulkan
