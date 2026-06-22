#pragma once

#include "core/Memory.h"
#include "renderer/core/RenderPass.h"
#include "renderer/settings/PostProcessingSettings.h"
#include "rhi/RHICommon.h"

#include <array>
#include <vector>

namespace ark {
    namespace rhi {
        class Buffer;
        class DescriptorSet;
        class DescriptorSetLayout;
        class DeviceContext;
        class PipelineLayout;
        class PipelineState;
        class RenderDevice;
        class Sampler;
        class Shader;
        class Texture;
        class TextureView;
    } // namespace rhi

    class BloomPass final : public RenderPass {
    public:
        ~BloomPass() override;

        void setup(rhi::RenderDevice& device) override;
        bool prepare(FrameContext& frameContext) override;
        bool execute(FrameContext& frameContext) override;

    private:
        static constexpr u32 FramesInFlight = 2;

        enum class Mode : u32 {
            Prefilter = 0,
            Downsample = 1,
            Upsample = 2,
            Composite = 3,
        };

        struct Target {
            Scope<rhi::Texture> texture;
            Scope<rhi::TextureView> view;
            rhi::Extent2D extent{};
        };

        struct DrawResources {
            Scope<rhi::Buffer> uniformBuffer;
            Scope<rhi::DescriptorSet> descriptorSet;
        };

        bool createDescriptorResources();
        bool createShaderResources();
        bool createPipelineResources();
        bool ensureTargets(FrameContext& frameContext, rhi::Extent2D extent, const BloomSettings& settings);
        bool ensureDrawResources(u32 frameSlot, usize drawCount);
        bool createTarget(Target& target, rhi::Extent2D extent, const char* debugName);
        bool releaseTargetDeferred(rhi::DeviceContext& context, Target& target);
        bool releaseTargetsDeferred(FrameContext& frameContext);
        bool recordFullscreenPass(FrameContext& frameContext,
                                  u32 frameSlot,
                                  usize drawIndex,
                                  Mode mode,
                                  rhi::TextureView& source0,
                                  rhi::TextureView& source1,
                                  Target& target,
                                  const BloomSettings& settings);
        rhi::PipelineState* getOrCreatePipeline();

        rhi::RenderDevice* m_Device = nullptr;
        Scope<rhi::DescriptorSetLayout> m_DescriptorSetLayout;
        Scope<rhi::Sampler> m_Sampler;
        Scope<rhi::Shader> m_VertexShader;
        Scope<rhi::Shader> m_FragmentShader;
        Scope<rhi::PipelineLayout> m_PipelineLayout;
        Scope<rhi::PipelineState> m_Pipeline;
        std::array<std::vector<DrawResources>, FramesInFlight> m_DrawResources;
        std::vector<Target> m_DownsampleTargets;
        std::vector<Target> m_UpsampleTargets;
        Target m_CompositeTarget;
        rhi::Extent2D m_Extent{};
        u32 m_LevelCount = 0;
    };
} // namespace ark
