#include "renderer/FrameRenderer.h"

#include "core/Log.h"
#include "core/Memory.h"
#include "renderer/FrameContext.h"
#include "renderer/FrameOverlay.h"
#include "renderer/RenderPass.h"
#include "renderer/effects/bloom/BloomPass.h"
#include "renderer/effects/ssao/SsaoPass.h"
#include "renderer/passes/ClearPass.h"
#include "renderer/passes/ForwardPass.h"
#include "renderer/effects/shadow/ShadowPass.h"
#include "renderer/effects/sky/SkyboxPass.h"
#include "renderer/effects/tone_mapping/ToneMappingPass.h"
#include "rhi/DeviceContext.h"
#include "rhi/RenderDevice.h"
#include "rhi/ResourceBarrier.h"
#include "rhi/SwapChain.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <array>

namespace ark {
    namespace {
        constexpr rhi::Format SceneColorFormat = rhi::Format::RGBA16Float;

        class DefaultFrameRenderer final : public FrameRenderer {
        public:
            DefaultFrameRenderer()
                : m_ShadowPass(makeScope<ShadowPass>()), m_ClearPass(makeScope<ClearPass>()),
                  m_ForwardPass(makeScope<ForwardPass>()),
                  m_SkyboxPass(makeScope<SkyboxPass>()), m_BloomPass(makeScope<BloomPass>()),
                  m_SsaoPass(makeScope<SsaoPass>()),
                  m_ToneMappingPass(makeScope<ToneMappingPass>()),
                  m_ScenePasses{m_ClearPass.get(), m_SkyboxPass.get(), m_ForwardPass.get()},
                  m_PostPasses{m_ToneMappingPass.get()} {
            }

            void setup(rhi::RenderDevice& device) override {
                m_Device = &device;

                // pass 私有 GPU 资源在 setup 阶段创建，避免每帧重复创建 buffer / shader / pipeline。
                m_ShadowPass->setup(device);
                for (RenderPass* pass : m_ScenePasses) {
                    pass->setup(device);
                }
                m_SsaoPass->setup(device);
                m_BloomPass->setup(device);
                for (RenderPass* pass : m_PostPasses) {
                    pass->setup(device);
                }
            }

            bool render(FrameContext& frameContext) override {
                return render(frameContext, nullptr);
            }

            bool render(FrameContext& frameContext, FrameOverlay* overlay) override {
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
                if (!ensureSceneColor(frameContext)) {
                    return false;
                }
                rhi::Texture* sceneColor = m_SceneColor.get();

                if (!m_ShadowPass->prepare(frameContext) || !m_ShadowPass->execute(frameContext)) {
                    return false;
                }

                // Scene pass 写入 HDR scene color，tone mapping pass 再把它映射到 swapchain backbuffer。
                const std::array<rhi::ResourceBarrier, 2> toRenderTarget{{
                    rhi::ResourceBarrier{
                        .texture = sceneColor,
                        .before = sceneColor->getState(),
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
                for (RenderPass* pass : m_ScenePasses) {
                    if (!pass->prepare(frameContext)) {
                        return false;
                    }
                }
                for (RenderPass* pass : m_PostPasses) {
                    if (!pass->prepare(frameContext)) {
                        return false;
                    }
                }

                frameContext.sceneColorView = m_SceneColorView.get();
                frameContext.depthBufferView = depthBufferView;
                frameContext.colorFormat = SceneColorFormat;
                frameContext.depthFormat = frameContext.swapChain->getDesc().depthFormat;
                if (!beginSceneRendering(frameContext, *depthBufferView)) {
                    return false;
                }

                setViewportAndScissor(frameContext);

                for (RenderPass* pass : m_ScenePasses) {
                    if (!pass->execute(frameContext)) {
                        frameContext.context->endRendering();
                        return false;
                    }
                }

                frameContext.context->endRendering();

                const std::array<rhi::ResourceBarrier, 1> sceneColorToShaderResource{{
                    rhi::ResourceBarrier{
                        .texture = sceneColor,
                        .before = sceneColor->getState(),
                        .after = rhi::ResourceState::ShaderResource,
                    },
                }};
                frameContext.context->pipelineBarrier(sceneColorToShaderResource);

                frameContext.colorFormat = SceneColorFormat;
                frameContext.depthFormat = rhi::Format::Unknown;
                if (!m_SsaoPass->prepare(frameContext) || !m_SsaoPass->execute(frameContext)) {
                    return false;
                }

                if (!m_BloomPass->prepare(frameContext) || !m_BloomPass->execute(frameContext)) {
                    return false;
                }

                const std::array<rhi::ResourceBarrier, 1> backBufferToRenderTarget{{
                    rhi::ResourceBarrier{
                        .texture = backBuffer,
                        .before = backBuffer->getState(),
                        .after = rhi::ResourceState::RenderTarget,
                    },
                }};
                frameContext.context->pipelineBarrier(backBufferToRenderTarget);

                frameContext.colorFormat = frameContext.swapChain->getDesc().colorFormat;
                frameContext.depthFormat = rhi::Format::Unknown;
                if (!beginToneMappingRendering(frameContext)) {
                    return false;
                }

                setViewportAndScissor(frameContext);

                for (RenderPass* pass : m_PostPasses) {
                    if (!pass->execute(frameContext)) {
                        frameContext.context->endRendering();
                        return false;
                    }
                }

                frameContext.context->endRendering();

                if (overlay && overlay->isEnabled()) {
                    frameContext.colorFormat = frameContext.swapChain->getDesc().colorFormat;
                    frameContext.depthFormat = rhi::Format::Unknown;
                    if (!beginOverlayRendering(frameContext)) {
                        return false;
                    }

                    setViewportAndScissor(frameContext);
                    if (!overlay->render(frameContext)) {
                        frameContext.context->endRendering();
                        return false;
                    }

                    frameContext.context->endRendering();
                }

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
            bool ensureSceneColor(FrameContext& frameContext) {
                if (!m_Device) {
                    ARK_ERROR("FrameRenderer requires RenderDevice before creating scene color");
                    return false;
                }

                if (!frameContext.context) {
                    ARK_ERROR("FrameRenderer requires DeviceContext before creating scene color");
                    return false;
                }

                const rhi::Extent2D extent = frameContext.extent;
                if (!rhi::isValidExtent(extent)) {
                    ARK_ERROR("FrameRenderer requires a valid scene color extent");
                    return false;
                }

                if (m_SceneColor && m_SceneColorView && m_SceneColor->getDesc().extent.width == extent.width &&
                    m_SceneColor->getDesc().extent.height == extent.height) {
                    return true;
                }

                if (!releaseSceneColorDeferred(frameContext)) {
                    return false;
                }

                m_Extent = extent;

                rhi::TextureDesc textureDesc{};
                textureDesc.extent = extent;
                textureDesc.format = SceneColorFormat;
                textureDesc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
                m_SceneColor = m_Device->createTexture(textureDesc);
                if (!m_SceneColor) {
                    return false;
                }

                rhi::TextureViewDesc viewDesc{};
                viewDesc.format = SceneColorFormat;
                m_SceneColorView = m_Device->createTextureView(*m_SceneColor, viewDesc);
                return m_SceneColorView != nullptr;
            }

            bool releaseSceneColorDeferred(FrameContext& frameContext) {
                if (!frameContext.context) {
                    ARK_ERROR("FrameRenderer requires DeviceContext for deferred scene color release");
                    return false;
                }

                // scene color 可能仍被上一帧 GPU 命令读取；resize 时也必须走延迟释放。
                if (m_SceneColorView && !frameContext.context->deferReleaseTextureView(m_SceneColorView)) {
                    return false;
                }
                if (m_SceneColor && !frameContext.context->deferReleaseTexture(m_SceneColor)) {
                    return false;
                }

                m_Extent = {};
                return true;
            }

            bool beginSceneRendering(FrameContext& frameContext, rhi::TextureView& depthBufferView) {
                rhi::RenderingDesc renderingDesc{};
                renderingDesc.extent = frameContext.extent;
                renderingDesc.colorAttachment.view = m_SceneColorView.get();
                renderingDesc.colorAttachment.loadOp = rhi::LoadOp::Clear;
                renderingDesc.colorAttachment.storeOp = rhi::StoreOp::Store;
                renderingDesc.colorAttachment.clearColor = frameContext.clearColor;
                renderingDesc.depthStencilAttachment.view = &depthBufferView;
                renderingDesc.depthStencilAttachment.loadOp = rhi::LoadOp::Clear;
                renderingDesc.depthStencilAttachment.storeOp = rhi::StoreOp::DontCare;
                renderingDesc.depthStencilAttachment.clearDepth = 1.0f;
                renderingDesc.depthStencilAttachment.clearStencil = 0;
                return frameContext.context->beginRendering(renderingDesc);
            }

            bool beginToneMappingRendering(FrameContext& frameContext) {
                rhi::RenderingDesc renderingDesc{};
                renderingDesc.extent = frameContext.extent;
                renderingDesc.colorAttachment.view = frameContext.backBufferView;
                renderingDesc.colorAttachment.loadOp = rhi::LoadOp::Clear;
                renderingDesc.colorAttachment.storeOp = rhi::StoreOp::Store;
                renderingDesc.colorAttachment.clearColor = frameContext.clearColor;
                return frameContext.context->beginRendering(renderingDesc);
            }

            bool beginOverlayRendering(FrameContext& frameContext) {
                rhi::RenderingDesc renderingDesc{};
                renderingDesc.extent = frameContext.extent;
                renderingDesc.colorAttachment.view = frameContext.backBufferView;
                renderingDesc.colorAttachment.loadOp = rhi::LoadOp::Load;
                renderingDesc.colorAttachment.storeOp = rhi::StoreOp::Store;
                return frameContext.context->beginRendering(renderingDesc);
            }

            void setViewportAndScissor(FrameContext& frameContext) {
                rhi::Viewport viewport{};
                viewport.width = static_cast<float>(frameContext.extent.width);
                viewport.height = static_cast<float>(frameContext.extent.height);
                frameContext.context->setViewport(viewport);

                rhi::ScissorRect scissor{};
                scissor.width = frameContext.extent.width;
                scissor.height = frameContext.extent.height;
                frameContext.context->setScissorRect(scissor);
            }

            rhi::RenderDevice* m_Device = nullptr;
            Scope<ShadowPass> m_ShadowPass;
            Scope<ClearPass> m_ClearPass;
            Scope<ForwardPass> m_ForwardPass;
            Scope<SkyboxPass> m_SkyboxPass;
            Scope<BloomPass> m_BloomPass;
            Scope<SsaoPass> m_SsaoPass;
            Scope<ToneMappingPass> m_ToneMappingPass;
            std::array<RenderPass*, 3> m_ScenePasses{};
            std::array<RenderPass*, 1> m_PostPasses{};
            Scope<rhi::Texture> m_SceneColor;
            Scope<rhi::TextureView> m_SceneColorView;
            rhi::Extent2D m_Extent{};
        };
    } // namespace

    Scope<FrameRenderer> createFrameRenderer() {
        return makeScope<DefaultFrameRenderer>();
    }
} // namespace ark
