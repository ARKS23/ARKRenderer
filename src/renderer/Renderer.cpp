#include "renderer/Renderer.h"

#include "asset/GltfLoader.h"
#include "asset/TextureLoader.h"
#include "core/FileSystem.h"
#include "core/Log.h"
#include "core/Memory.h"
#include "renderer/EnvironmentCubeConverter.h"
#include "renderer/EnvironmentCubeResource.h"
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
#include <cstring>
#include <glm/mat4x4.hpp>
#include <stdexcept>
#include <vector>

namespace ark {
    namespace {
        constexpr const char* DefaultSandboxModelAssetPath = "assets/models/forward_multinode_fixture.gltf";
        constexpr const char* DefaultSandboxEnvironmentAssetPath = "assets/HDR/2k.hdr";
        constexpr rhi::Extent2D DefaultEnvironmentCubeFaceExtent{512, 512};

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
            const Path requestedPath =
                overridePath.empty() ? Path{DefaultSandboxEnvironmentAssetPath} : overridePath;

            if (requestedPath.is_absolute()) {
                return fileExists(requestedPath) ? requestedPath : Path{};
            }

            const std::array<Path, 3> overrideCandidates{
                requestedPath,
                Path{"../"} / requestedPath,
                Path{"../../"} / requestedPath,
            };
            return findFirstExistingPath(overrideCandidates);
        }

        asset::ImageData makeProceduralSandboxEnvironmentImage() {
            constexpr u32 Width = 64;
            constexpr u32 Height = 32;
            constexpr u32 BytesPerPixel = 16;

            std::vector<float> pixels(Width * Height * 4);
            for (u32 y = 0; y < Height; ++y) {
                const float v = Height > 1 ? static_cast<float>(y) / static_cast<float>(Height - 1) : 0.0f;
                const float sky = 1.0f - v;
                const float horizon = 1.0f - (v > 0.5f ? (v - 0.5f) * 2.0f : (0.5f - v) * 2.0f);
                for (u32 x = 0; x < Width; ++x) {
                    const float u = Width > 1 ? static_cast<float>(x) / static_cast<float>(Width - 1) : 0.0f;
                    float r = 0.04f + sky * 0.12f + horizon * 0.35f;
                    float g = 0.08f + sky * 0.20f + horizon * 0.30f;
                    float b = 0.18f + sky * 0.55f + horizon * 0.18f;

                    const float sunU = u - 0.12f;
                    const float sunV = v - 0.42f;
                    const float sunDistanceSquared = sunU * sunU + sunV * sunV;
                    if (sunDistanceSquared < 0.0025f) {
                        r += 6.0f;
                        g += 4.0f;
                        b += 1.4f;
                    }

                    const usize pixelOffset = (static_cast<usize>(y) * Width + x) * 4;
                    pixels[pixelOffset + 0] = r;
                    pixels[pixelOffset + 1] = g;
                    pixels[pixelOffset + 2] = b;
                    pixels[pixelOffset + 3] = 1.0f;
                }
            }

            asset::ImageData image{};
            image.width = Width;
            image.height = Height;
            image.format = asset::ImageFormat::Rgba32Float;
            image.bytesPerPixel = BytesPerPixel;
            image.pixels.resize(pixels.size() * sizeof(float));
            std::memcpy(image.pixels.data(), pixels.data(), image.pixels.size());
            image.debugName = "ProceduralSandboxEnvironment";
            return image;
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
                m_EnvironmentCubeConverter.setup(m_Backend->device());

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
                m_EnvironmentCubeConverter.resetImmediate();
                m_DefaultEnvironmentCube.resetImmediate();
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
                prepareDefaultEnvironmentCube(context, renderScene);
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
                frameContext.environmentCube = resolveFrameEnvironmentCube(renderScene);

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
                const Path environmentPath = findSandboxEnvironmentFile(m_DefaultEnvironmentPath);
                asset::ImageData environmentImage{};
                if (!environmentPath.empty()) {
                    ARK_INFO("Using sandbox environment: {}", environmentPath.string());
                    environmentImage = asset::loadImageHdrRgba32F(environmentPath);
                } else if (!m_DefaultEnvironmentPath.empty()) {
                    ARK_WARN("Requested sandbox environment was not found: {}", m_DefaultEnvironmentPath.string());
                } else {
                    ARK_INFO("Default sandbox environment was not found: {}", DefaultSandboxEnvironmentAssetPath);
                }

                if (environmentImage.empty()) {
                    ARK_INFO("Using procedural sandbox environment");
                    environmentImage = makeProceduralSandboxEnvironmentImage();
                }

                EnvironmentResourceDesc environmentDesc{};
                environmentDesc.debugName = "DefaultSandboxEnvironment";
                if (!m_DefaultEnvironment.create(m_Backend->device(), environmentImage, environmentDesc)) {
                    ARK_ERROR("Renderer failed to create default sandbox environment");
                    return false;
                }

                EnvironmentCubeResourceDesc cubeDesc{};
                cubeDesc.debugName = "DefaultSandboxEnvironmentCube";
                cubeDesc.faceExtent = DefaultEnvironmentCubeFaceExtent;
                cubeDesc.format = rhi::Format::RGBA16Float;
                cubeDesc.mipLevels = 1;
                if (!m_DefaultEnvironmentCube.create(m_Backend->device(), cubeDesc)) {
                    ARK_WARN("Renderer failed to create default sandbox environment cubemap conversion target");
                    m_DefaultEnvironmentCube.resetImmediate();
                }

                SceneEnvironment environment{};
                environment.environment = &m_DefaultEnvironment;
                environment.intensity = 1.0f;
                m_DefaultScene.setEnvironment(environment);
                return true;
            }

            void prepareDefaultEnvironmentCube(rhi::DeviceContext& context, RenderScene& renderScene) {
                if (m_DefaultEnvironmentCubeConversionAttempted || m_DefaultEnvironmentCubeConverted) {
                    return;
                }

                const SceneEnvironment& environment = renderScene.environment();
                if (environment.environment != &m_DefaultEnvironment) {
                    return;
                }

                m_DefaultEnvironmentCubeConversionAttempted = true;
                if (!m_DefaultEnvironmentCube.isValid()) {
                    return;
                }

                if (!m_DefaultEnvironment.upload(context)) {
                    ARK_WARN("Renderer failed to upload default sandbox environment before cubemap conversion");
                    return;
                }

                EnvironmentCubeConversionDesc conversionDesc{};
                conversionDesc.source = &m_DefaultEnvironment;
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

            EnvironmentCubeResource* resolveFrameEnvironmentCube(RenderScene& renderScene) {
                const SceneEnvironment& environment = renderScene.environment();
                if (m_DefaultEnvironmentCubeConverted &&
                    environment.environment == &m_DefaultEnvironment &&
                    m_DefaultEnvironmentCube.isValid()) {
                    return &m_DefaultEnvironmentCube;
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
            ModelResource m_DefaultModel;
            EnvironmentResource m_DefaultEnvironment;
            EnvironmentCubeResource m_DefaultEnvironmentCube;
            EnvironmentCubeConverter m_EnvironmentCubeConverter;
            RenderScene m_DefaultScene;
            RenderQueue m_RenderQueue;
            Path m_DefaultModelPath;
            Path m_DefaultEnvironmentPath;
            rhi::Extent2D m_Extent{};
            rhi::ClearColor m_ClearColor{};
            bool m_DefaultEnvironmentCubeConversionAttempted = false;
            bool m_DefaultEnvironmentCubeConverted = false;
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
