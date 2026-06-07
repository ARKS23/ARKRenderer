#pragma once

#include "core/Memory.h"
#include "renderer/RenderPass.h"
#include "rhi/Buffer.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DescriptorSetLayout.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RHICommon.h"
#include "rhi/RenderDevice.h"
#include "rhi/Shader.h"

#include <array>

namespace ark {
    // CubePass 用于 Phase 0.5 验证 uniform buffer、descriptor set、index buffer 和 drawIndexed 闭环。
    class CubePass final : public RenderPass {
    public:
        ~CubePass() override;

        void setup(rhi::RenderDevice& device) override;
        bool execute(FrameContext& frameContext) override;

    private:
        static constexpr u32 FramesInFlight = 2;

        bool createPipelineResources();
        bool ensurePipeline(FrameContext& frameContext);
        bool updateCameraUniform(FrameContext& frameContext, u32 frameSlot);

        rhi::RenderDevice* m_Device = nullptr;
        Scope<rhi::Buffer> m_VertexBuffer;
        Scope<rhi::Buffer> m_IndexBuffer;
        std::array<Scope<rhi::Buffer>, FramesInFlight> m_CameraBuffers;
        Scope<rhi::DescriptorSetLayout> m_DescriptorSetLayout;
        std::array<Scope<rhi::DescriptorSet>, FramesInFlight> m_DescriptorSets;
        Scope<rhi::Shader> m_VertexShader;
        Scope<rhi::Shader> m_FragmentShader;
        Scope<rhi::PipelineLayout> m_PipelineLayout;
        Scope<rhi::PipelineState> m_Pipeline;
        rhi::Format m_PipelineColorFormat = rhi::Format::Unknown;
    };
} // namespace ark
