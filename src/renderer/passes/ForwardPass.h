#pragma once

#include "asset/MeshData.h"
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

#include <array>

namespace ark {
    // ForwardPass 是 Phase 0.8 的最小资产渲染 pass，消费 MeshResource / MaterialResource 后执行 indexed draw。
    class ForwardPass final : public RenderPass {
    public:
        ~ForwardPass() override;

        void setup(rhi::RenderDevice& device) override;
        bool prepare(FrameContext& frameContext) override;
        bool execute(FrameContext& frameContext) override;

    private:
        static constexpr u32 FramesInFlight = 2;

        bool createMeshResource();
        bool createMaterialResource();
        bool createDescriptorResources();
        bool createShaderResources();
        bool createPipelineResources();
        bool ensurePipeline(FrameContext& frameContext);
        bool updateCameraUniform(FrameContext& frameContext, u32 frameSlot);

        rhi::RenderDevice* m_Device = nullptr;
        asset::ModelData m_ModelData;
        MeshResource m_Mesh;
        MaterialResource m_Material;
        std::array<Scope<rhi::Buffer>, FramesInFlight> m_CameraBuffers;
        Scope<rhi::DescriptorSetLayout> m_DescriptorSetLayout;
        std::array<Scope<rhi::DescriptorSet>, FramesInFlight> m_DescriptorSets;
        Scope<rhi::Shader> m_VertexShader;
        Scope<rhi::Shader> m_FragmentShader;
        Scope<rhi::PipelineLayout> m_PipelineLayout;
        Scope<rhi::PipelineState> m_Pipeline;
        rhi::Format m_PipelineColorFormat = rhi::Format::Unknown;
        rhi::Format m_PipelineDepthFormat = rhi::Format::Unknown;
    };
} // namespace ark
