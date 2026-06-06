#pragma once

#include "core/Memory.h"
#include "renderer/RenderPass.h"
#include "rhi/Buffer.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RHICommon.h"
#include "rhi/RenderDevice.h"
#include "rhi/Shader.h"

namespace ark {
    // TrianglePass 是 Phase 0.4 的几何绘制落点，只负责最小三角形验证，不承载最终 ForwardPass 的完整职责。
    class TrianglePass final : public RenderPass {
    public:
        ~TrianglePass() override;

        // setup 创建 pass 级长期资源；execute 只录制本帧 draw 命令。
        void setup(rhi::RenderDevice& device) override;
        bool execute(FrameContext& frameContext) override;

    private:
        bool ensurePipeline(FrameContext& frameContext);

        // TrianglePass 不拥有 RenderDevice，只借用它在 swapchain format 变化时重建 pipeline。
        rhi::RenderDevice* m_Device = nullptr;
        Scope<rhi::Shader> m_VertexShader;
        Scope<rhi::Shader> m_FragmentShader;
        Scope<rhi::PipelineLayout> m_PipelineLayout;
        Scope<rhi::Buffer> m_VertexBuffer;

        // Pipeline 依赖 swapchain color format；format 变化时 ensurePipeline() 会重建。
        Scope<rhi::PipelineState> m_Pipeline;
        rhi::Format m_PipelineColorFormat = rhi::Format::Unknown;
    };
} // namespace ark
