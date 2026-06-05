#include "renderer/Renderer.h"

#include "core/Log.h"
#include "core/Memory.h"
#include "renderer/FrameContext.h"
#include "renderer/FrameRenderer.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "rhi/RenderBackend.h"
#include "rhi/TextureView.h"

#include <stdexcept>

namespace ark {
    namespace {
        // 第一版只提供默认 swapchain 配置，后续可以把 vsync、format 等暴露到 RendererDesc。
        rhi::SwapChainDesc makeDefaultSwapChainDesc(rhi::Extent2D extent) {
            rhi::SwapChainDesc desc{};
            desc.extent = extent;
            desc.colorFormat = rhi::Format::BGRA8Unorm;
            desc.depthFormat = rhi::Format::D32Float;
            desc.imageCount = 2;
            desc.enableVSync = true;
            return desc;
        }

        class DefaultRenderer final : public Renderer {
        public:
            explicit DefaultRenderer(const RendererDesc& desc) : m_Extent(desc.extent) {
                // Renderer 只组装公共 RHI 描述，具体后端对象由内部工厂创建。
                rhi::RenderBackendDesc backendDesc{};
                backendDesc.device.desc.backend = rhi::RenderBackendType::Vulkan;
                backendDesc.device.desc.enableValidation = desc.enableValidation;
                backendDesc.device.nativeWindow = desc.nativeWindow;
                backendDesc.swapChain = makeDefaultSwapChainDesc(desc.extent);

                m_Backend = rhi::createRenderBackend(backendDesc);
                if (!m_Backend->swapChain()) {
                    ARK_WARN("Renderer created without swapchain because window extent is zero");
                    m_RenderingPaused = true;
                }

                // FrameRenderer 负责一帧内部 pass 顺序；Renderer 只保留 acquire/submit/present 外壳。
                m_FrameRenderer = createFrameRenderer();
                m_FrameRenderer->setup(m_Backend->device());
                m_FrameRenderer->resize(m_Extent);

                ARK_INFO("Renderer initialized");
            }

            ~DefaultRenderer() override {
                m_FrameRenderer.reset();
                m_Backend.reset();
                ARK_INFO("Renderer shutdown");
            }

            void render(RenderScene& scene, const RenderView& view) override {
                // 窗口最小化或 swapchain 尚未创建时跳过当前帧，避免用 0 尺寸重建 swapchain。
                if (m_RenderingPaused || !m_Backend->swapChain()) {
                    return;
                }

                // 一帧的底层对象由 DeviceContext 管理，Renderer 只负责组织执行顺序。
                rhi::DeviceContext& context = m_Backend->context();
                rhi::SwapChain& swapChain = *m_Backend->swapChain();
                rhi::FrameResource& frame = context.beginFrame();

                // acquire 失败后不能继续录制命令，否则会向无效 backbuffer 写入。
                const rhi::AcquireResult acquireResult = swapChain.acquireNextImage(frame);
                if (!canRenderAfterAcquire(acquireResult.status)) {
                    handleSwapChainStatus(acquireResult.status);
                    return;
                }
                const bool acquireSuboptimal = acquireResult.status == rhi::SwapChainStatus::Suboptimal;

                rhi::TextureView* backBufferView = swapChain.getCurrentBackBufferView();
                if (!backBufferView || !backBufferView->getTexture()) {
                    ARK_ERROR("Renderer failed to get current backbuffer");
                    return;
                }

                if (!context.begin(frame)) {
                    return;
                }

                FrameContext frameContext{};
                frameContext.frameIndex = frame.frameIndex;
                frameContext.scene = &scene;
                frameContext.view = &view;
                frameContext.device = &m_Backend->device();
                frameContext.context = &context;
                frameContext.swapChain = &swapChain;
                frameContext.frameResource = &frame;
                frameContext.backBufferView = backBufferView;
                frameContext.extent = m_Extent;
                frameContext.clearColor = m_ClearColor;

                if (!m_FrameRenderer->render(frameContext)) {
                    context.end();
                    return;
                }

                if (!context.end()) {
                    return;
                }

                rhi::SubmitDesc submitDesc{};
                submitDesc.frameResource = &frame;
                if (!context.submit(submitDesc)) {
                    return;
                }

                const rhi::SwapChainStatus presentStatus = swapChain.present(frame);
                context.advanceFrame();

                // acquire suboptimal 也需要保守 resize，即使 present 暂时返回 Ready。
                handleSwapChainStatus(acquireSuboptimal && presentStatus == rhi::SwapChainStatus::Ready
                                          ? rhi::SwapChainStatus::Suboptimal
                                          : presentStatus);
            }

            void resize(unsigned width, unsigned height) override {
                rhi::Extent2D extent{static_cast<u32>(width), static_cast<u32>(height)};
                m_Extent = extent;

                // extent 为 0 通常表示窗口最小化；此时暂停渲染，等待窗口恢复有效尺寸。
                if (!rhi::isValidExtent(extent)) {
                    m_RenderingPaused = true;
                    return;
                }

                m_RenderingPaused = false;
                m_FrameRenderer->resize(extent);
                if (m_Backend->swapChain()) {
                    m_Backend->swapChain()->resize(extent);
                    return;
                }

                // 从最小化恢复时，如果之前没有 swapchain，则重新创建。
                m_Backend->recreateSwapChain(makeDefaultSwapChainDesc(extent));
            }

        private:
            static bool canRenderAfterAcquire(rhi::SwapChainStatus status) {
                return status == rhi::SwapChainStatus::Ready || status == rhi::SwapChainStatus::Suboptimal;
            }

            void handleSwapChainStatus(rhi::SwapChainStatus status) {
                // Swapchain 状态集中在这里处理，避免 render 主流程被错误分支打散。
                switch (status) {
                case rhi::SwapChainStatus::Ready:
                    return;
                case rhi::SwapChainStatus::Suboptimal:
                case rhi::SwapChainStatus::OutOfDate:
                    resize(m_Extent.width, m_Extent.height);
                    return;
                case rhi::SwapChainStatus::SurfaceLost:
                    ARK_ERROR("Swapchain surface lost");
                    return;
                case rhi::SwapChainStatus::DeviceLost:
                    ARK_ERROR("Vulkan device lost");
                    return;
                case rhi::SwapChainStatus::Error:
                    ARK_ERROR("Swapchain operation failed");
                    return;
                }
            }

            Scope<FrameRenderer> m_FrameRenderer;
            Scope<rhi::RenderBackend> m_Backend;
            rhi::Extent2D m_Extent{};
            rhi::ClearColor m_ClearColor{};
            bool m_RenderingPaused = false;
        };
    } // namespace

    Scope<Renderer> createRenderer(const RendererDesc& desc) {
        if (desc.nativeWindow.type != rhi::NativeWindowType::GLFW || !desc.nativeWindow.handle) {
            throw std::runtime_error("createRenderer requires a valid GLFW native window handle");
        }

        return makeScope<DefaultRenderer>(desc);
    }
} // namespace ark
