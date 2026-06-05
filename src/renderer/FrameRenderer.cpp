#include "renderer/FrameRenderer.h"

#include "core/Log.h"
#include "core/Memory.h"
#include "renderer/FrameContext.h"
#include "renderer/RenderPass.h"
#include "renderer/passes/ClearPass.h"
#include "renderer/passes/TrianglePass.h"
#include "rhi/DeviceContext.h"
#include "rhi/ResourceBarrier.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <array>

namespace ark {
    namespace {
        class DefaultFrameRenderer final : public FrameRenderer {
        public:
            DefaultFrameRenderer()
                : m_ClearPass(makeScope<ClearPass>()), m_TrianglePass(makeScope<TrianglePass>()),
                  m_Passes{m_ClearPass.get(), m_TrianglePass.get()} {
            }

            void setup(rhi::RenderDevice& device) override {
                for (RenderPass* pass : m_Passes) {
                    pass->setup(device);
                }
            }

            bool render(FrameContext& frameContext) override {
                if (!frameContext.context || !frameContext.backBufferView ||
                    !frameContext.backBufferView->getTexture()) {
                    ARK_ERROR("FrameRenderer requires DeviceContext and backbuffer");
                    return false;
                }

                rhi::Texture* backBuffer = frameContext.backBufferView->getTexture();

                // backbuffer 进入渲染附件写入状态；清屏由 beginRendering 的 loadOp=Clear 表达。
                const std::array<rhi::ResourceBarrier, 1> toRenderTarget{{
                    rhi::ResourceBarrier{
                        .texture = backBuffer,
                        .before = backBuffer->getState(),
                        .after = rhi::ResourceState::RenderTarget,
                    },
                }};
                frameContext.context->pipelineBarrier(toRenderTarget);

                rhi::RenderingDesc renderingDesc{};
                renderingDesc.extent = frameContext.extent;
                renderingDesc.colorAttachment.view = frameContext.backBufferView;
                renderingDesc.colorAttachment.loadOp = rhi::LoadOp::Clear;
                renderingDesc.colorAttachment.storeOp = rhi::StoreOp::Store;
                renderingDesc.colorAttachment.clearColor = frameContext.clearColor;

                if (!frameContext.context->beginRendering(renderingDesc)) {
                    return false;
                }

                for (RenderPass* pass : m_Passes) {
                    if (!pass->execute(frameContext)) {
                        frameContext.context->endRendering();
                        return false;
                    }
                }

                frameContext.context->endRendering();

                // present 前必须把 backbuffer 转回 Present 状态。
                const std::array<rhi::ResourceBarrier, 1> toPresent{{
                    rhi::ResourceBarrier{
                        .texture = backBuffer,
                        .before = backBuffer->getState(),
                        .after = rhi::ResourceState::Present,
                    },
                }};
                frameContext.context->pipelineBarrier(toPresent);

                return true;
            }

            void resize(rhi::Extent2D extent) override {
                m_Extent = extent;
            }

        private:
            Scope<ClearPass> m_ClearPass;
            Scope<TrianglePass> m_TrianglePass;
            std::array<RenderPass*, 2> m_Passes{};
            rhi::Extent2D m_Extent{};
        };
    } // namespace

    Scope<FrameRenderer> createFrameRenderer() {
        return makeScope<DefaultFrameRenderer>();
    }
} // namespace ark
