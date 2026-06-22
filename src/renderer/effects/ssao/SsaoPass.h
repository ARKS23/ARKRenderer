#pragma once

#include "core/Memory.h"
#include "renderer/settings/PostProcessingSettings.h"
#include "renderer/core/RenderPass.h"
#include "rhi/RHICommon.h"

#include <glm/mat4x4.hpp>

#include <array>
#include <vector>

namespace ark {
    struct MaterialRenderState;
    class MaterialResource;
    class MeshResource;

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

    class SsaoPass final : public RenderPass {
    public:
        ~SsaoPass() override;

        void setup(rhi::RenderDevice& device) override;
        bool prepare(FrameContext& frameContext) override;
        bool execute(FrameContext& frameContext) override;

    private:
        static constexpr u32 FramesInFlight = 2;

        enum class FullscreenMode : u32 {
            Evaluate = 0,
            Blur = 1,
            Composite = 2,
        };

        struct Target {
            Scope<rhi::Texture> texture;
            Scope<rhi::TextureView> view;
            rhi::Extent2D extent{};
        };

        struct GeometryDrawResources {
            Scope<rhi::Buffer> uniformBuffer;
            Scope<rhi::Buffer> materialBuffer;
            Scope<rhi::DescriptorSet> descriptorSet;
        };

        struct FullscreenDrawResources {
            Scope<rhi::Buffer> uniformBuffer;
            Scope<rhi::DescriptorSet> descriptorSet;
        };

        bool createGeometryDescriptorResources();
        bool createFullscreenDescriptorResources();
        bool createShaderResources();
        bool createPipelineResources();
        bool ensureTargets(FrameContext& frameContext, const SsaoSettings& settings);
        bool ensureGeometryDrawResources(u32 frameSlot, usize drawCount);
        bool ensureFullscreenDrawResources(u32 frameSlot, usize drawCount);
        bool createTarget(Target& target,
                          rhi::Extent2D extent,
                          rhi::Format format,
                          const char* debugName);
        bool createDepthTarget(Target& target,
                               rhi::Extent2D extent,
                               const char* debugName);
        bool releaseTargetDeferred(rhi::DeviceContext& context, Target& target);
        bool releaseTargetsDeferred(FrameContext& frameContext);
        bool updateGeometryDrawResources(FrameContext& frameContext,
                                         GeometryDrawResources& resources,
                                         const MaterialResource& material,
                                         const glm::mat4& modelMatrix);
        rhi::PipelineState* selectGeometryPipeline(const MaterialRenderState& renderState) const;
        rhi::PipelineState* getOrCreateFullscreenPipeline();
        bool recordNormalDepthPass(FrameContext& frameContext, u32 frameSlot);
        bool recordFullscreenPass(FrameContext& frameContext,
                                  u32 frameSlot,
                                  usize drawIndex,
                                  FullscreenMode mode,
                                  rhi::TextureView& source0,
                                  rhi::TextureView& source1,
                                  Target& target,
                                  const SsaoSettings& settings);
        void publishFrameBindings(FrameContext& frameContext);
        void clearFrameBindings(FrameContext& frameContext);

        rhi::RenderDevice* m_Device = nullptr;
        Scope<rhi::DescriptorSetLayout> m_GeometryDescriptorSetLayout;
        Scope<rhi::DescriptorSetLayout> m_FullscreenDescriptorSetLayout;
        Scope<rhi::PipelineLayout> m_GeometryPipelineLayout;
        Scope<rhi::PipelineLayout> m_FullscreenPipelineLayout;
        Scope<rhi::PipelineState> m_GeometrySingleSidedPipeline;
        Scope<rhi::PipelineState> m_GeometryDoubleSidedPipeline;
        Scope<rhi::PipelineState> m_FullscreenPipeline;
        Scope<rhi::Shader> m_GeometryVertexShader;
        Scope<rhi::Shader> m_GeometryFragmentShader;
        Scope<rhi::Shader> m_FullscreenVertexShader;
        Scope<rhi::Shader> m_FullscreenFragmentShader;
        Scope<rhi::Sampler> m_LinearSampler;
        Scope<rhi::Sampler> m_PointSampler;
        std::array<std::vector<GeometryDrawResources>, FramesInFlight> m_GeometryDrawResources;
        std::array<std::vector<FullscreenDrawResources>, FramesInFlight> m_FullscreenDrawResources;
        Target m_NormalDepthTarget;
        Target m_NormalDepthDepthTarget;
        Target m_OcclusionTarget;
        Target m_BlurTarget;
        Target m_CompositeTarget;
        rhi::Extent2D m_SsaoExtent{};
        rhi::Extent2D m_FrameExtent{};
    };
} // namespace ark
