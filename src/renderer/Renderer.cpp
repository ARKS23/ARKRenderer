#include "renderer/Renderer.h"

#include "asset/GltfLoader.h"
#include "asset/TextureLoader.h"
#include "core/FileSystem.h"
#include "core/Log.h"
#include "core/Memory.h"
#include "renderer/EnvironmentResource.h"
#include "renderer/FrameContext.h"
#include "renderer/FrameRenderer.h"
#include "renderer/ModelResource.h"
#include "renderer/RenderQueue.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "rhi/RenderBackend.h"
#include "rhi/TextureView.h"

#include <array>
#include <glm/mat4x4.hpp>
#include <stdexcept>

namespace ark {
    namespace {
        constexpr const char* DefaultSandboxModelAssetPath = "assets/models/forward_multinode_fixture.gltf";

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

        Path findSandboxModelFile(const Path& overridePath) {
            if (!overridePath.empty()) {
                if (overridePath.is_absolute()) {
                    return fileExists(overridePath) ? overridePath : Path{};
                }

                const std::array<Path, 3> overrideCandidates{
                    overridePath,
                    Path{"../"} / overridePath,
                    Path{"../../"} / overridePath,
                };
                return findFirstExistingPath(overrideCandidates);
            }

            const Path relative = Path{DefaultSandboxModelAssetPath};
            const std::array<Path, 3> candidates{
                relative,
                Path{"../"} / relative,
                Path{"../../"} / relative,
            };

            return findFirstExistingPath(candidates);
        }

        Path findSandboxEnvironmentFile(const Path& overridePath) {
            if (overridePath.empty()) {
                return {};
            }

            if (overridePath.is_absolute()) {
                return fileExists(overridePath) ? overridePath : Path{};
            }

            const std::array<Path, 3> overrideCandidates{
                overridePath,
                Path{"../"} / overridePath,
                Path{"../../"} / overridePath,
            };
            return findFirstExistingPath(overrideCandidates);
        }

        class DefaultRenderer final : public Renderer {
        public:
            explicit DefaultRenderer(const RendererDesc& desc)
                : m_DefaultModelPath(desc.defaultModelPath),
                  m_DefaultEnvironmentPath(desc.defaultEnvironmentPath),
                  m_Extent(desc.extent) {
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

                createDefaultScene();
                createDefaultEnvironment();
                ARK_INFO("Renderer initialized");
            }

            ~DefaultRenderer() override {
                if (m_Backend) {
                    m_Backend->device().waitIdle();
                }

                m_FrameRenderer.reset();
                m_DefaultScene.clear();
                m_DefaultEnvironment.resetImmediate();
                m_DefaultModel.reset();
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

                // Phase 0.9 起 Renderer 负责把 scene 扁平化为本帧 draw queue，pass 只消费 queue。

                RenderScene& renderScene = scene.empty() && !m_DefaultScene.empty() ? m_DefaultScene : scene;
                // Phase 0.9 起 Renderer 负责把 scene 扁平化为本帧 draw queue。
                m_RenderQueue.build(renderScene);

                FrameContext frameContext{};
                frameContext.frameIndex = frame.frameIndex;
                frameContext.scene = &renderScene;
                frameContext.view = &view;
                frameContext.queue = &m_RenderQueue;
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

            bool createDefaultScene() {
                const Path modelPath = findSandboxModelFile(m_DefaultModelPath);
                if (modelPath.empty()) {
                    if (m_DefaultModelPath.empty()) {
                        ARK_WARN("Default sandbox model fixture was not found: {}", DefaultSandboxModelAssetPath);
                    } else {
                        ARK_WARN("Requested sandbox model was not found: {}", m_DefaultModelPath.string());
                    }
                    return false;
                }

                ARK_INFO("Using sandbox model: {}", modelPath.string());
                asset::ModelData modelData = asset::loadGltfModel(modelPath);
                if (modelData.empty()) {
                    ARK_WARN("Default sandbox model fixture is empty: {}", modelPath.string());
                    return false;
                }

                if (!m_DefaultModel.create(m_Backend->device(), modelData)) {
                    ARK_ERROR("Renderer failed to create default sandbox model");
                    return false;
                }

                // 默认 sandbox scene 由 renderer 持有 GPU model，避免 app 层直接管理 RHI 生命周期。
                m_DefaultScene.clear();
                m_DefaultScene.addModel(m_DefaultModel, glm::mat4{1.0f}, "DefaultSandboxModel");
                return true;
            }

            bool createDefaultEnvironment() {
                if (m_DefaultEnvironmentPath.empty()) {
                    return true;
                }

                const Path environmentPath = findSandboxEnvironmentFile(m_DefaultEnvironmentPath);
                if (environmentPath.empty()) {
                    ARK_WARN("Requested sandbox environment was not found: {}", m_DefaultEnvironmentPath.string());
                    return false;
                }

                ARK_INFO("Using sandbox environment: {}", environmentPath.string());
                asset::ImageData environmentImage = asset::loadImageHdrRgba32F(environmentPath);
                if (environmentImage.empty()) {
                    ARK_WARN("Sandbox environment image is empty: {}", environmentPath.string());
                    return false;
                }

                EnvironmentResourceDesc environmentDesc{};
                environmentDesc.debugName = "DefaultSandboxEnvironment";
                if (!m_DefaultEnvironment.create(m_Backend->device(), environmentImage, environmentDesc)) {
                    ARK_ERROR("Renderer failed to create default sandbox environment");
                    return false;
                }

                SceneEnvironment environment{};
                environment.environment = &m_DefaultEnvironment;
                environment.intensity = 1.0f;
                m_DefaultScene.setEnvironment(environment);
                return true;
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
            ModelResource m_DefaultModel;
            EnvironmentResource m_DefaultEnvironment;
            RenderScene m_DefaultScene;
            RenderQueue m_RenderQueue;
            Path m_DefaultModelPath;
            Path m_DefaultEnvironmentPath;
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
