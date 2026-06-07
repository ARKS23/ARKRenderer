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
#include "rhi/Sampler.h"
#include "rhi/Shader.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <array>

namespace ark {
    // CubePass 用于验证 uniform buffer、descriptor set、index buffer、texture sampling 和 drawIndexed 闭环。
    class CubePass final : public RenderPass {
    public:
        ~CubePass() override;

        void setup(rhi::RenderDevice& device) override;
        bool prepare(FrameContext& frameContext) override;
        bool execute(FrameContext& frameContext) override;

    private:
        static constexpr u32 FramesInFlight = 2;

        bool createPipelineResources();
        bool createTextureResources();
        bool ensurePipeline(FrameContext& frameContext);
        bool updateCameraUniform(FrameContext& frameContext, u32 frameSlot);
        bool uploadTexture(FrameContext& frameContext);

        rhi::RenderDevice* m_Device = nullptr;
        Scope<rhi::Buffer> m_VertexBuffer;
        Scope<rhi::Buffer> m_IndexBuffer;
        Scope<rhi::Buffer> m_TextureStagingBuffer;
        Scope<rhi::Texture> m_Texture;
        Scope<rhi::TextureView> m_TextureView;
        Scope<rhi::Sampler> m_Sampler;
        std::array<Scope<rhi::Buffer>, FramesInFlight> m_CameraBuffers;
        Scope<rhi::DescriptorSetLayout> m_DescriptorSetLayout;
        std::array<Scope<rhi::DescriptorSet>, FramesInFlight> m_DescriptorSets;
        Scope<rhi::Shader> m_VertexShader;
        Scope<rhi::Shader> m_FragmentShader;
        Scope<rhi::PipelineLayout> m_PipelineLayout;
        Scope<rhi::PipelineState> m_Pipeline;
        rhi::Format m_PipelineColorFormat = rhi::Format::Unknown;
        rhi::Format m_PipelineDepthFormat = rhi::Format::Unknown;
        bool m_TextureUploaded = false;
    };
} // namespace ark
