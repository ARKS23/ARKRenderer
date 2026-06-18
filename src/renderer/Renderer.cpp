#include "renderer/Renderer.h"

#include "core/Log.h"
#include "core/Memory.h"
#include "renderer/EnvironmentBrdfLutGenerator.h"
#include "renderer/EnvironmentBrdfLutResource.h"
#include "renderer/EnvironmentCubeConverter.h"
#include "renderer/EnvironmentCubeResource.h"
#include "renderer/EnvironmentIrradianceGenerator.h"
#include "renderer/EnvironmentSpecularPrefilterGenerator.h"
#include "renderer/FrameContext.h"
#include "renderer/FrameOverlay.h"
#include "renderer/FrameRenderer.h"
#include "renderer/Frustum.h"
#include "renderer/RenderQueue.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/SceneResource.h"
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

        const char* toString(SceneModelSource source) {
            switch (source) {
            case SceneModelSource::None:
                return "None";
            case SceneModelSource::RequestedPath:
                return "RequestedPath";
            case SceneModelSource::DefaultFallback:
                return "DefaultFallback";
            }

            return "Unknown";
        }

        const char* toString(SceneEnvironmentSource source) {
            switch (source) {
            case SceneEnvironmentSource::None:
                return "None";
            case SceneEnvironmentSource::RequestedHdr:
                return "RequestedHdr";
            case SceneEnvironmentSource::DefaultHdr:
                return "DefaultHdr";
            case SceneEnvironmentSource::Procedural:
                return "Procedural";
            case SceneEnvironmentSource::DebugOrientation:
                return "DebugOrientation";
            }

            return "Unknown";
        }

        class DefaultRenderer final : public Renderer {
        public:
            explicit DefaultRenderer(const RendererDesc& desc)
                : m_Quality(sanitizeRendererQualityDesc(desc.quality)),
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
                m_EnvironmentCubeConverter.setup(m_Backend->device());
                m_EnvironmentIrradianceGenerator.setup(m_Backend->device());
                m_EnvironmentSpecularPrefilterGenerator.setup(m_Backend->device());
                m_EnvironmentBrdfLutGenerator.setup(m_Backend->device());

                createDefaultSceneResource(desc);
                ARK_INFO("Renderer initialized");
            }

            ~DefaultRenderer() override {
                if (m_Backend) {
                    m_Backend->device().waitIdle();
                }

                m_FrameRenderer.reset();
                m_DefaultSceneResource.resetImmediate();
                m_EnvironmentBrdfLutGenerator.resetImmediate();
                m_EnvironmentSpecularPrefilterGenerator.resetImmediate();
                m_EnvironmentIrradianceGenerator.resetImmediate();
                m_EnvironmentCubeConverter.resetImmediate();
                m_DefaultBrdfLut.resetImmediate();
                m_DefaultSpecularCube.resetImmediate();
                m_DefaultIrradianceCube.resetImmediate();
                m_DefaultEnvironmentCube.resetImmediate();
                m_Backend.reset();
                ARK_INFO("Renderer shutdown");
            }

            void render(RenderScene& scene, const RenderView& view, FrameOverlay* overlay = nullptr) override {
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

                RenderScene& renderScene =
                    scene.empty() && m_DefaultSceneResource.hasScene() ? m_DefaultSceneResource.scene() : scene;
                prepareDefaultEnvironmentCube(context, renderScene);
                prepareDefaultIrradianceCube(context, renderScene);
                prepareDefaultSpecularCube(context, renderScene);
                prepareDefaultBrdfLut(context, renderScene);
                // Phase 0.9 起 Renderer 负责把 scene 扁平化为本帧 draw queue。
                m_RenderQueue.build(renderScene, view.cameraPosition());
                RenderQueue* forwardQueue = nullptr;
                if (view.visibilitySettings().enableFrustumCulling) {
                    const Frustum cameraFrustum =
                        Frustum::fromViewProjection(view.projectionMatrix() * view.viewMatrix());
                    RenderQueueBuildDesc forwardQueueDesc{};
                    forwardQueueDesc.scene = &renderScene;
                    forwardQueueDesc.cameraPosition = view.cameraPosition();
                    forwardQueueDesc.cameraFrustum = &cameraFrustum;
                    forwardQueueDesc.enableFrustumCulling = true;
                    m_ForwardRenderQueue.build(forwardQueueDesc);
                    forwardQueue = &m_ForwardRenderQueue;
                } else {
                    m_ForwardRenderQueue.clear();
                }

                FrameContext frameContext{};
                frameContext.frameIndex = frame.frameIndex;
                frameContext.scene = &renderScene;
                frameContext.view = &view;
                frameContext.queue = &m_RenderQueue;
                frameContext.forwardQueue = forwardQueue;
                frameContext.device = &m_Backend->device();
                frameContext.context = &context;
                frameContext.swapChain = &swapChain;
                frameContext.frameResource = &frame;
                frameContext.backBufferView = backBufferView;
                frameContext.extent = m_Extent;
                frameContext.clearColor = m_ClearColor;
                frameContext.environmentCube = resolveFrameEnvironmentCube(renderScene);
                frameContext.irradianceCube = resolveFrameIrradianceCube(renderScene);
                frameContext.prefilteredSpecularCube = resolveFramePrefilteredSpecularCube(renderScene);
                frameContext.brdfLut = resolveFrameBrdfLut(renderScene);

                if (!m_FrameRenderer->render(frameContext, overlay)) {
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

            bool createDefaultSceneResource(const RendererDesc& desc) {
                if (!m_DefaultSceneResource.load(m_Backend->device(), desc.defaultScene)) {
                    ARK_WARN("Renderer default scene resource was not loaded");
                    return false;
                }

                // 默认 sandbox scene 由 renderer 持有 GPU model，避免 app 层直接管理 RHI 生命周期。
                const SceneResourceLoadReport& report = m_DefaultSceneResource.report();
                ARK_INFO("Renderer default scene loaded: modelSource={}, loadedModels={}, environmentSource={}",
                         toString(report.modelSource),
                         report.loadedModelCount,
                         toString(report.environmentSource));

                return createDefaultEnvironmentBakeTargets();
            }

            bool createDefaultEnvironmentBakeTargets() {
                if (!m_DefaultSceneResource.environment()) {
                    ARK_WARN("Renderer default environment was not loaded; skipping default IBL targets");
                    return false;
                }

                const EnvironmentBakeQualityDesc& bake = m_Quality.environmentBake;

                EnvironmentCubeResourceDesc cubeDesc{};
                cubeDesc.debugName = "DefaultSandboxEnvironmentCube";
                cubeDesc.faceExtent = bake.environmentCubeFaceExtent;
                cubeDesc.format = rhi::Format::RGBA16Float;
                cubeDesc.mipLevels = 1;
                if (bake.enableEnvironmentCube && !m_DefaultEnvironmentCube.create(m_Backend->device(), cubeDesc)) {
                    ARK_WARN("Renderer failed to create default sandbox environment cubemap conversion target");
                    m_DefaultEnvironmentCube.resetImmediate();
                }

                EnvironmentCubeResourceDesc irradianceDesc{};
                irradianceDesc.debugName = "DefaultSandboxIrradianceCube";
                irradianceDesc.faceExtent = bake.irradianceCubeFaceExtent;
                irradianceDesc.format = rhi::Format::RGBA16Float;
                irradianceDesc.mipLevels = 1;
                if (bake.enableIrradiance && !m_DefaultIrradianceCube.create(m_Backend->device(), irradianceDesc)) {
                    ARK_WARN("Renderer failed to create default sandbox irradiance cubemap target");
                    m_DefaultIrradianceCube.resetImmediate();
                }

                EnvironmentCubeResourceDesc specularDesc{};
                specularDesc.debugName = "DefaultSandboxSpecularCube";
                specularDesc.faceExtent = bake.specularCubeFaceExtent;
                specularDesc.format = rhi::Format::RGBA16Float;
                specularDesc.mipLevels = rhi::calculateMipLevelCount(bake.specularCubeFaceExtent);
                if (bake.enableSpecularPrefilter && !m_DefaultSpecularCube.create(m_Backend->device(), specularDesc)) {
                    ARK_WARN("Renderer failed to create default sandbox specular cubemap target");
                    m_DefaultSpecularCube.resetImmediate();
                }

                EnvironmentBrdfLutResourceDesc brdfLutDesc{};
                brdfLutDesc.debugName = "DefaultSandboxBrdfLut";
                brdfLutDesc.extent = bake.brdfLutExtent;
                brdfLutDesc.format = rhi::Format::RGBA16Float;
                if (bake.enableBrdfLut && !m_DefaultBrdfLut.create(m_Backend->device(), brdfLutDesc)) {
                    ARK_WARN("Renderer failed to create default sandbox BRDF LUT target");
                    m_DefaultBrdfLut.resetImmediate();
                }

                return true;
            }

            void prepareDefaultEnvironmentCube(rhi::DeviceContext& context, RenderScene& renderScene) {
                if (!m_Quality.environmentBake.enableEnvironmentCube ||
                    m_DefaultEnvironmentCubeConversionAttempted ||
                    m_DefaultEnvironmentCubeConverted) {
                    return;
                }

                EnvironmentResource* defaultEnvironment = m_DefaultSceneResource.environment();
                const SceneEnvironment& environment = renderScene.environment();
                if (!defaultEnvironment || environment.environment != defaultEnvironment) {
                    return;
                }

                m_DefaultEnvironmentCubeConversionAttempted = true;
                if (!m_DefaultEnvironmentCube.isValid()) {
                    return;
                }

                if (!defaultEnvironment->upload(context)) {
                    ARK_WARN("Renderer failed to upload default sandbox environment before cubemap conversion");
                    return;
                }

                EnvironmentCubeConversionDesc conversionDesc{};
                conversionDesc.source = defaultEnvironment;
                conversionDesc.target = &m_DefaultEnvironmentCube;
                conversionDesc.debugName = "DefaultSandboxEnvironmentCubeConversion";
                if (!m_EnvironmentCubeConverter.convert(context, conversionDesc)) {
                    ARK_WARN("Renderer failed to convert default sandbox environment to cubemap; keeping "
                             "equirectangular ForwardPass path");
                    return;
                }

                m_DefaultEnvironmentCubeConverted = true;
                ARK_INFO("Converted default sandbox environment to cubemap");
            }

            void prepareDefaultIrradianceCube(rhi::DeviceContext& context, RenderScene& renderScene) {
                if (!m_Quality.environmentBake.enableIrradiance ||
                    m_DefaultIrradianceCubeGenerationAttempted ||
                    m_DefaultIrradianceCubeGenerated) {
                    return;
                }

                EnvironmentResource* defaultEnvironment = m_DefaultSceneResource.environment();
                const SceneEnvironment& environment = renderScene.environment();
                if (!defaultEnvironment || environment.environment != defaultEnvironment) {
                    return;
                }

                if (!m_DefaultEnvironmentCubeConverted) {
                    return;
                }

                m_DefaultIrradianceCubeGenerationAttempted = true;
                if (!m_DefaultEnvironmentCube.isValid() || !m_DefaultIrradianceCube.isValid()) {
                    return;
                }

                EnvironmentIrradianceGenerationDesc generationDesc{};
                generationDesc.source = &m_DefaultEnvironmentCube;
                generationDesc.target = &m_DefaultIrradianceCube;
                generationDesc.sampleDelta = m_Quality.environmentBake.irradianceSampleDelta;
                generationDesc.debugName = "DefaultSandboxIrradianceGeneration";
                if (!m_EnvironmentIrradianceGenerator.generate(context, generationDesc)) {
                    ARK_WARN("Renderer failed to generate default sandbox irradiance cubemap; keeping "
                             "equirectangular ForwardPass path");
                    return;
                }

                m_DefaultIrradianceCubeGenerated = true;
                ARK_INFO("Generated default sandbox irradiance cubemap");
            }

            void prepareDefaultSpecularCube(rhi::DeviceContext& context, RenderScene& renderScene) {
                if (!m_Quality.environmentBake.enableSpecularPrefilter ||
                    m_DefaultSpecularCubeGenerationAttempted ||
                    m_DefaultSpecularCubeGenerated) {
                    return;
                }

                EnvironmentResource* defaultEnvironment = m_DefaultSceneResource.environment();
                const SceneEnvironment& environment = renderScene.environment();
                if (!defaultEnvironment || environment.environment != defaultEnvironment) {
                    return;
                }

                if (!m_DefaultEnvironmentCubeConverted) {
                    return;
                }

                m_DefaultSpecularCubeGenerationAttempted = true;
                if (!m_DefaultEnvironmentCube.isValid() || !m_DefaultSpecularCube.isValid()) {
                    return;
                }

                EnvironmentSpecularPrefilterDesc prefilterDesc{};
                prefilterDesc.source = &m_DefaultEnvironmentCube;
                prefilterDesc.target = &m_DefaultSpecularCube;
                prefilterDesc.sampleCount = m_Quality.environmentBake.specularPrefilterSampleCount;
                prefilterDesc.debugName = "DefaultSandboxSpecularPrefilter";
                if (!m_EnvironmentSpecularPrefilterGenerator.generate(context, prefilterDesc)) {
                    ARK_WARN("Renderer failed to generate default sandbox specular cubemap; keeping diffuse IBL path");
                    return;
                }

                m_DefaultSpecularCubeGenerated = true;
                ARK_INFO("Generated default sandbox specular cubemap");
            }

            void prepareDefaultBrdfLut(rhi::DeviceContext& context, RenderScene& renderScene) {
                if (!m_Quality.environmentBake.enableBrdfLut ||
                    m_DefaultBrdfLutGenerationAttempted ||
                    m_DefaultBrdfLutGenerated) {
                    return;
                }

                EnvironmentResource* defaultEnvironment = m_DefaultSceneResource.environment();
                const SceneEnvironment& environment = renderScene.environment();
                if (!defaultEnvironment || environment.environment != defaultEnvironment) {
                    return;
                }

                m_DefaultBrdfLutGenerationAttempted = true;
                if (!m_DefaultBrdfLut.isValid()) {
                    return;
                }

                EnvironmentBrdfLutGenerationDesc brdfLutDesc{};
                brdfLutDesc.target = &m_DefaultBrdfLut;
                brdfLutDesc.sampleCount = m_Quality.environmentBake.brdfLutSampleCount;
                brdfLutDesc.debugName = "DefaultSandboxBrdfLutGeneration";
                if (!m_EnvironmentBrdfLutGenerator.generate(context, brdfLutDesc)) {
                    ARK_WARN("Renderer failed to generate default sandbox BRDF LUT; keeping diffuse IBL path");
                    return;
                }

                m_DefaultBrdfLutGenerated = true;
                ARK_INFO("Generated default sandbox BRDF LUT");
            }

            EnvironmentCubeResource* resolveFrameEnvironmentCube(RenderScene& renderScene) {
                if (!m_Quality.environmentBake.enableEnvironmentCube) {
                    return nullptr;
                }

                EnvironmentResource* defaultEnvironment = m_DefaultSceneResource.environment();
                const SceneEnvironment& environment = renderScene.environment();
                if (m_DefaultEnvironmentCubeConverted &&
                    defaultEnvironment &&
                    environment.environment == defaultEnvironment &&
                    m_DefaultEnvironmentCube.isValid()) {
                    return &m_DefaultEnvironmentCube;
                }

                return nullptr;
            }

            EnvironmentCubeResource* resolveFrameIrradianceCube(RenderScene& renderScene) {
                if (!m_Quality.environmentBake.enableIrradiance) {
                    return nullptr;
                }

                EnvironmentResource* defaultEnvironment = m_DefaultSceneResource.environment();
                const SceneEnvironment& environment = renderScene.environment();
                if (m_DefaultIrradianceCubeGenerated &&
                    defaultEnvironment &&
                    environment.environment == defaultEnvironment &&
                    m_DefaultIrradianceCube.isValid()) {
                    return &m_DefaultIrradianceCube;
                }

                return nullptr;
            }

            EnvironmentCubeResource* resolveFramePrefilteredSpecularCube(RenderScene& renderScene) {
                if (!m_Quality.environmentBake.enableSpecularPrefilter) {
                    return nullptr;
                }

                EnvironmentResource* defaultEnvironment = m_DefaultSceneResource.environment();
                const SceneEnvironment& environment = renderScene.environment();
                if (m_DefaultSpecularCubeGenerated &&
                    defaultEnvironment &&
                    environment.environment == defaultEnvironment &&
                    m_DefaultSpecularCube.isValid()) {
                    return &m_DefaultSpecularCube;
                }

                return nullptr;
            }

            EnvironmentBrdfLutResource* resolveFrameBrdfLut(RenderScene& renderScene) {
                if (!m_Quality.environmentBake.enableBrdfLut) {
                    return nullptr;
                }

                EnvironmentResource* defaultEnvironment = m_DefaultSceneResource.environment();
                const SceneEnvironment& environment = renderScene.environment();
                if (m_DefaultBrdfLutGenerated &&
                    defaultEnvironment &&
                    environment.environment == defaultEnvironment &&
                    m_DefaultBrdfLut.isValid()) {
                    return &m_DefaultBrdfLut;
                }

                return nullptr;
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
            SceneResource m_DefaultSceneResource;
            EnvironmentCubeResource m_DefaultEnvironmentCube;
            EnvironmentCubeResource m_DefaultIrradianceCube;
            EnvironmentCubeResource m_DefaultSpecularCube;
            EnvironmentBrdfLutResource m_DefaultBrdfLut;
            EnvironmentCubeConverter m_EnvironmentCubeConverter;
            EnvironmentIrradianceGenerator m_EnvironmentIrradianceGenerator;
            EnvironmentSpecularPrefilterGenerator m_EnvironmentSpecularPrefilterGenerator;
            EnvironmentBrdfLutGenerator m_EnvironmentBrdfLutGenerator;
            RenderQueue m_RenderQueue;
            RenderQueue m_ForwardRenderQueue;
            RendererQualityDesc m_Quality;
            rhi::Extent2D m_Extent{};
            rhi::ClearColor m_ClearColor{};
            bool m_DefaultEnvironmentCubeConversionAttempted = false;
            bool m_DefaultEnvironmentCubeConverted = false;
            bool m_DefaultIrradianceCubeGenerationAttempted = false;
            bool m_DefaultIrradianceCubeGenerated = false;
            bool m_DefaultSpecularCubeGenerationAttempted = false;
            bool m_DefaultSpecularCubeGenerated = false;
            bool m_DefaultBrdfLutGenerationAttempted = false;
            bool m_DefaultBrdfLutGenerated = false;
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
