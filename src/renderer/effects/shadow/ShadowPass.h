#pragma once

#include "core/Memory.h"
#include "renderer/core/RenderPass.h"
#include "renderer/settings/ShadowConstants.h"
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
#include <vector>

namespace ark {
    struct MaterialRenderState;
    class MaterialResource;
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

        struct ShadowDrawResources {
            Scope<rhi::Buffer> uniformBuffer;
            Scope<rhi::Buffer> materialBuffer;
            Scope<rhi::DescriptorSet> descriptorSet;
        };

        bool createDescriptorResources();
        bool createShaderResources();
        bool createPipelineResources();
        static ShadowTargetDesc makeShadowTargetDesc(const ShadowSettings& settings);
        bool ensureShadowTarget(FrameContext& frameContext, const ShadowTargetDesc& targetDesc);
        bool releaseShadowTargetDeferred(FrameContext& frameContext);
        bool ensureDrawResources(u32 frameSlot, usize requiredCount);
        bool updateDrawResources(FrameContext& frameContext,
                                 ShadowDrawResources& drawResources,
                                 const MaterialResource& material,
                                 const glm::mat4& lightViewProjection,
                                 const glm::mat4& modelMatrix);
        rhi::PipelineState* selectPipeline(const MaterialRenderState& renderState) const;
        rhi::TextureView* shadowRenderTargetView(u32 layerIndex) const;
        bool beginShadowRendering(FrameContext& frameContext, rhi::TextureView& depthView);
        void setViewportAndScissor(FrameContext& frameContext);
        bool renderShadowLayer(FrameContext& frameContext,
                               u32 frameSlot,
                               usize layerResourceBase,
                               rhi::TextureView& depthView,
                               const glm::mat4& lightViewProjection);

        rhi::RenderDevice* m_Device = nullptr;
        std::array<std::vector<ShadowDrawResources>, FramesInFlight> m_DrawResources;
        Scope<rhi::DescriptorSetLayout> m_DescriptorSetLayout;
        Scope<rhi::PipelineLayout> m_PipelineLayout;
        Scope<rhi::PipelineState> m_SingleSidedPipeline;
        Scope<rhi::PipelineState> m_DoubleSidedPipeline;
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
