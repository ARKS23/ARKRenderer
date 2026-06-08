#include "renderer/FrameRenderer.h"

#include "core/Log.h"
#include "core/Memory.h"
#include "renderer/FrameContext.h"
#include "renderer/RenderPass.h"
#include "renderer/passes/ClearPass.h"
#include "renderer/passes/ForwardPass.h"
#include "rhi/DeviceContext.h"
#include "rhi/ResourceBarrier.h"
#include "rhi/SwapChain.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <array>

namespace ark {
    namespace {
        class DefaultFrameRenderer final : public FrameRenderer {
        public:
            DefaultFrameRenderer()
                : m_ClearPass(makeScope<ClearPass>()), m_ForwardPass(makeScope<ForwardPass>()),
                  m_Passes{m_ClearPass.get(), m_ForwardPass.get()} {
            }

            void setup(rhi::RenderDevice& device) override {
                // pass 私有 GPU 资源在 setup 阶段创建，避免每帧重复创建 buffer / shader / pipeline。
                for (RenderPass* pass : m_Passes) {
                    pass->setup(device);
                }
            }

            bool render(FrameContext& frameContext) override {
                if (!frameContext.context || !frameContext.swapChain || !frameContext.backBufferView ||
                    !frameContext.backBufferView->getTexture()) {
                    ARK_ERROR("FrameRenderer requires DeviceContext, SwapChain and backbuffer");
                    return false;
                }

                rhi::Texture* backBuffer = frameContext.backBufferView->getTexture();
                rhi::TextureView* depthBufferView = frameContext.swapChain->getDepthBufferView();
                if (!depthBufferView || !depthBufferView->getTexture()) {
                    ARK_ERROR("FrameRenderer requires swapchain depth buffer");
                    return false;
                }
                rhi::Texture* depthBuffer = depthBufferView->getTexture();

                // FrameRenderer 负责一帧内的 render scope：资源状态转换 -> beginRendering -> pass -> endRendering。
                // 这样 Renderer 只保留 acquire / submit / present 外壳，后续可以用 RenderGraph 替换这里的手动调度。
                // color/depth attachment 进入可写状态；清屏由 beginRendering 的 loadOp=Clear 表达。
                const std::array<rhi::ResourceBarrier, 2> toRenderTarget{{
                    rhi::ResourceBarrier{
                        .texture = backBuffer,
                        .before = backBuffer->getState(),
                        .after = rhi::ResourceState::RenderTarget,
                    },
                    rhi::ResourceBarrier{
                        .texture = depthBuffer,
                        .before = depthBuffer->getState(),
                        .after = rhi::ResourceState::DepthStencilWrite,
                    },
                }};
                frameContext.context->pipelineBarrier(toRenderTarget);

                // prepare 阶段专门记录 upload/copy 等 render scope 外命令，避免把 texture copy 放进 dynamic rendering。
                for (RenderPass* pass : m_Passes) {
                    if (!pass->prepare(frameContext)) {
                        return false;
                    }
                }

                rhi::RenderingDesc renderingDesc{};
                renderingDesc.extent = frameContext.extent;
                renderingDesc.colorAttachment.view = frameContext.backBufferView;
                renderingDesc.colorAttachment.loadOp = rhi::LoadOp::Clear;
                renderingDesc.colorAttachment.storeOp = rhi::StoreOp::Store;
                renderingDesc.colorAttachment.clearColor = frameContext.clearColor;
                renderingDesc.depthStencilAttachment.view = depthBufferView;
                renderingDesc.depthStencilAttachment.loadOp = rhi::LoadOp::Clear;
                renderingDesc.depthStencilAttachment.storeOp = rhi::StoreOp::DontCare;
                renderingDesc.depthStencilAttachment.clearDepth = 1.0f;
                renderingDesc.depthStencilAttachment.clearStencil = 0;

                if (!frameContext.context->beginRendering(renderingDesc)) {
                    return false;
                }

                // viewport / scissor 属于每帧动态状态，跟随当前 backbuffer 尺寸设置，避免 resize 后沿用旧状态。
                rhi::Viewport viewport{};
                viewport.width = static_cast<float>(frameContext.extent.width);
                viewport.height = static_cast<float>(frameContext.extent.height);
                frameContext.context->setViewport(viewport);

                rhi::ScissorRect scissor{};
                scissor.width = frameContext.extent.width;
                scissor.height = frameContext.extent.height;
                frameContext.context->setScissorRect(scissor);

                // 目前 pass 顺序固定；RenderGraph 落地后，这里会变成按图调度 pass 和资源 barrier。
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
            Scope<ForwardPass> m_ForwardPass;
            std::array<RenderPass*, 2> m_Passes{};
            rhi::Extent2D m_Extent{};
        };
    } // namespace

    Scope<FrameRenderer> createFrameRenderer() {
        return makeScope<DefaultFrameRenderer>();
    }
} // namespace ark
