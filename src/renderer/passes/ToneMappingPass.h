#pragma once

#include "core/Memory.h"
#include "renderer/RenderPass.h"
#include "rhi/RHICommon.h"

#include <array>

namespace ark {
    namespace rhi {
        class DescriptorSet;
        class DescriptorSetLayout;
        class PipelineLayout;
        class PipelineState;
        class RenderDevice;
        class Sampler;
        class Shader;
        class TextureView;
    } // namespace rhi

    class ToneMappingPass : public RenderPass {
    public:
        ~ToneMappingPass() override;

        void setup(rhi::RenderDevice& device) override;
        bool execute(FrameContext& frameContext) override;

    private:
        static constexpr u32 FramesInFlight = 2;

        bool createDescriptorResources();
        bool createShaderResources();
        bool createPipelineResources();
        rhi::PipelineState* getOrCreatePipeline(FrameContext& frameContext);

        rhi::RenderDevice* m_Device = nullptr;
        Scope<rhi::DescriptorSetLayout> m_DescriptorSetLayout;
        std::array<Scope<rhi::DescriptorSet>, FramesInFlight> m_DescriptorSets;
        std::array<rhi::TextureView*, FramesInFlight> m_BoundSceneColorViews{};
        Scope<rhi::Sampler> m_Sampler;
        Scope<rhi::Shader> m_VertexShader;
        Scope<rhi::Shader> m_FragmentShader;
        Scope<rhi::PipelineLayout> m_PipelineLayout;
        Scope<rhi::PipelineState> m_Pipeline;
        rhi::Format m_PipelineColorFormat = rhi::Format::Unknown;
    };
} // namespace ark
