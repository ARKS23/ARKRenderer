#pragma once

#include "core/Memory.h"
#include "renderer/RenderPass.h"
#include "renderer/effects/shadow/ShadowConstants.h"
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
    struct ShadowSettings;

    class ShadowPass : public RenderPass {
    public:
        ~ShadowPass() override;

        void setup(rhi::RenderDevice& device) override;
        bool prepare(FrameContext& frameContext) override;
        bool execute(FrameContext& frameContext) override;

    private:
        static constexpr u32 FramesInFlight = 2;

        struct ShadowTargetDesc {
            rhi::Extent2D extent{};
            u32 layerCount = 1;
            bool useTextureArray = false;
        };

        bool createDescriptorResources();
        bool createShaderResources();
        bool createPipelineResources();
        static ShadowTargetDesc makeShadowTargetDesc(const ShadowSettings& settings);
        bool ensureShadowTarget(FrameContext& frameContext, const ShadowTargetDesc& targetDesc);
        bool releaseShadowTargetDeferred(FrameContext& frameContext);
        bool updateShadowUniform(FrameContext& frameContext, u32 frameSlot);
        rhi::TextureView* shadowRenderTargetView(u32 layerIndex) const;
        bool beginShadowRendering(FrameContext& frameContext, rhi::TextureView& depthView);
        void setViewportAndScissor(FrameContext& frameContext);
        bool renderShadowLayer(FrameContext& frameContext, u32 frameSlot, rhi::TextureView& depthView, const glm::mat4& lightViewProjection);

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
        std::array<Scope<rhi::TextureView>, MaxShadowCascadeCount> m_ShadowCascadeViews;
        Scope<rhi::Sampler> m_ShadowSampler;
        rhi::Extent2D m_ShadowExtent{};
        u32 m_ShadowLayerCount = 0;
        bool m_ShadowUsesTextureArray = false;
    };
} // namespace ark
