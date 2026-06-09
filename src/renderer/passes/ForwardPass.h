#pragma once

#include "core/Memory.h"
#include "renderer/MeshResource.h"
#include "renderer/RenderPass.h"
#include "renderer/material/MaterialResource.h"
#include "rhi/Buffer.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DescriptorSetLayout.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RHICommon.h"
#include "rhi/RenderDevice.h"
#include "rhi/Shader.h"

#include <glm/mat4x4.hpp>

#include <array>
#include <vector>

namespace ark {
    // ForwardPass 只消费 RenderQueue；空 queue 表示本帧没有 forward draw。
    class ForwardPass final : public RenderPass {
    public:
        ~ForwardPass() override;

        void setup(rhi::RenderDevice& device) override;
        bool prepare(FrameContext& frameContext) override;
        bool execute(FrameContext& frameContext) override;

    private:
        static constexpr u32 FramesInFlight = 2;

        bool createDescriptorResources();
        bool createShaderResources();
        bool createPipelineResources();
        usize drawItemCount(const FrameContext& frameContext) const;
        bool ensureDrawDescriptorResources(u32 frameSlot, usize drawCount);
        bool ensurePipeline(FrameContext& frameContext);
        bool updateCameraUniform(FrameContext& frameContext, u32 frameSlot);
        bool updateObjectUniform(FrameContext& frameContext,
                                 u32 frameSlot,
                                 usize drawIndex,
                                 const glm::mat4& modelMatrix);
        bool updateMaterialUniform(FrameContext& frameContext,
                                   u32 frameSlot,
                                   usize drawIndex,
                                   const MaterialResource& material);
        bool updateDrawDescriptorSet(u32 frameSlot, usize drawIndex, MaterialResource& material);
        bool drawMeshItem(FrameContext& frameContext,
                          u32 frameSlot,
                          usize drawIndex,
                          MeshResource& mesh,
                          MaterialResource& material,
                          const glm::mat4& modelMatrix);

        struct DrawDescriptorResources {
            Scope<rhi::Buffer> objectBuffer;
            Scope<rhi::Buffer> materialBuffer;
            Scope<rhi::DescriptorSet> descriptorSet;
        };

        rhi::RenderDevice* m_Device = nullptr;
        std::array<Scope<rhi::Buffer>, FramesInFlight> m_CameraBuffers;
        Scope<rhi::DescriptorSetLayout> m_DescriptorSetLayout;
        std::array<std::vector<DrawDescriptorResources>, FramesInFlight> m_DrawDescriptors;
        Scope<rhi::Shader> m_VertexShader;
        Scope<rhi::Shader> m_FragmentShader;
        Scope<rhi::PipelineLayout> m_PipelineLayout;
        Scope<rhi::PipelineState> m_Pipeline;
        rhi::Format m_PipelineColorFormat = rhi::Format::Unknown;
        rhi::Format m_PipelineDepthFormat = rhi::Format::Unknown;
    };
} // namespace ark
