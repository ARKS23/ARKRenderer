#pragma once

#include "core/Memory.h"
#include "renderer/RenderPass.h"
#include "rhi/Buffer.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DescriptorSetLayout.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RHICommon.h"
#include "rhi/Sampler.h"
#include "rhi/Shader.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <glm/mat4x4.hpp>

#include <array>

namespace ark {
    class ShadowPass : public RenderPass {
    public:
        ~ShadowPass() override;

        void setup(rhi::RenderDevice& device) override;
        bool prepare(FrameContext& frameContext) override;
        bool execute(FrameContext& frameContext) override;

    private:
        static constexpr u32 FramesInFlight = 2;

        bool createDescriptorResources();
        bool createShaderResources();
        bool createPipelineResources();
        bool ensureShadowTarget(rhi::Extent2D extent);
        bool updateShadowUniform(FrameContext& frameContext, u32 frameSlot);
        bool beginShadowRendering(FrameContext& frameContext);
        void setViewportAndScissor(FrameContext& frameContext);

        rhi::RenderDevice* m_Device = nullptr;
        std::array<Scope<rhi::Buffer>, FramesInFlight> m_ShadowBuffers;
        std::array<Scope<rhi::DescriptorSet>, FramesInFlight> m_DescriptorSets;
        Scope<rhi::DescriptorSetLayout> m_DescriptorSetLayout;
        Scope<rhi::PipelineLayout> m_PipelineLayout;
        Scope<rhi::PipelineState> m_Pipeline;
        Scope<rhi::Shader> m_VertexShader;
        Scope<rhi::Shader> m_FragmentShader;
        Scope<rhi::Texture> m_ShadowMap;
        Scope<rhi::TextureView> m_ShadowMapView;
        Scope<rhi::Sampler> m_ShadowSampler;
        rhi::Extent2D m_ShadowExtent{};
    };
} // namespace ark
