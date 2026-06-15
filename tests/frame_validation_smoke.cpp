#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif

#include "asset/GltfLoader.h"
#include "core/FileSystem.h"
#include "core/Memory.h"
#include "renderer/EnvironmentCubeConverter.h"
#include "renderer/EnvironmentCubeResource.h"
#include "renderer/EnvironmentResource.h"
#include "renderer/FrameContext.h"
#include "renderer/ModelResource.h"
#include "renderer/RenderQueue.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/SandboxEnvironment.h"
#include "renderer/passes/ForwardPass.h"
#include "renderer/passes/SkyboxPass.h"
#include "rhi/Buffer.h"
#include "rhi/DeviceContext.h"
#include "rhi/RenderBackend.h"
#include "rhi/RenderDevice.h"
#include "rhi/ResourceBarrier.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <GLFW/glfw3.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
    constexpr ark::rhi::Extent2D FrameExtent{256, 144};
    constexpr ark::u32 FrameBytesPerPixel = 8;
    constexpr ark::u64 FrameByteSize =
        static_cast<ark::u64>(FrameExtent.width) * FrameExtent.height * FrameBytesPerPixel;

    class HiddenGlfwWindow final {
    public:
        HiddenGlfwWindow() {
            if (glfwInit() != GLFW_TRUE) {
                throw std::runtime_error("glfwInit failed");
            }

            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

            m_Window = glfwCreateWindow(64, 64, "ARK frame validation smoke", nullptr, nullptr);
            if (!m_Window) {
                glfwTerminate();
                throw std::runtime_error("glfwCreateWindow failed");
            }
        }

        ~HiddenGlfwWindow() {
            if (m_Window) {
                glfwDestroyWindow(m_Window);
                m_Window = nullptr;
            }
            glfwTerminate();
        }

        ark::rhi::NativeWindowHandle nativeHandle() const {
            return ark::rhi::NativeWindowHandle{
                .type = ark::rhi::NativeWindowType::GLFW,
                .handle = m_Window,
            };
        }

    private:
        GLFWwindow* m_Window = nullptr;
    };

    struct FrameColorStats {
        ark::u64 pixelCount = 0;
        ark::u64 finitePixelCount = 0;
        ark::u64 nonBlackPixelCount = 0;
        glm::vec3 minRgb{std::numeric_limits<float>::max()};
        glm::vec3 maxRgb{std::numeric_limits<float>::lowest()};
        glm::vec3 meanRgb{0.0f};
        float meanLuminance = 0.0f;
        float maxLuminance = 0.0f;
    };

    float halfToFloat(ark::u16 value) {
        const ark::u32 sign = static_cast<ark::u32>(value & 0x8000u) << 16u;
        const ark::u32 exponent = (value >> 10u) & 0x1fu;
        const ark::u32 mantissa = value & 0x03ffu;

        if (exponent == 0u) {
            if (mantissa == 0u) {
                return sign != 0u ? -0.0f : 0.0f;
            }

            const float magnitude = std::ldexp(static_cast<float>(mantissa), -24);
            return sign != 0u ? -magnitude : magnitude;
        }

        if (exponent == 0x1fu) {
            if (mantissa == 0u) {
                return sign != 0u ? -std::numeric_limits<float>::infinity()
                                  : std::numeric_limits<float>::infinity();
            }

            return std::numeric_limits<float>::quiet_NaN();
        }

        const float magnitude =
            std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                       static_cast<int>(exponent) - 15);
        return sign != 0u ? -magnitude : magnitude;
    }

    ark::u16 readU16LE(const std::vector<ark::u8>& bytes, ark::usize offset) {
        return static_cast<ark::u16>(bytes[offset]) |
               static_cast<ark::u16>(static_cast<ark::u16>(bytes[offset + 1]) << 8u);
    }

    FrameColorStats computeFrameColorStats(const std::vector<ark::u8>& bytes, ark::rhi::Extent2D extent) {
        FrameColorStats stats{};
        stats.pixelCount = static_cast<ark::u64>(extent.width) * extent.height;
        glm::vec3 sumRgb{0.0f};
        double luminanceSum = 0.0;

        for (ark::u64 pixelIndex = 0; pixelIndex < stats.pixelCount; ++pixelIndex) {
            const ark::usize offset = static_cast<ark::usize>(pixelIndex * FrameBytesPerPixel);
            const float r = halfToFloat(readU16LE(bytes, offset + 0));
            const float g = halfToFloat(readU16LE(bytes, offset + 2));
            const float b = halfToFloat(readU16LE(bytes, offset + 4));
            const float a = halfToFloat(readU16LE(bytes, offset + 6));
            const glm::vec3 rgb{r, g, b};

            if (std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a)) {
                ++stats.finitePixelCount;
            }

            stats.minRgb = glm::min(stats.minRgb, rgb);
            stats.maxRgb = glm::max(stats.maxRgb, rgb);
            sumRgb += rgb;

            const float luminance = glm::dot(rgb, glm::vec3{0.2126f, 0.7152f, 0.0722f});
            if (luminance > 1.0e-5f) {
                ++stats.nonBlackPixelCount;
            }
            stats.maxLuminance = std::max(stats.maxLuminance, luminance);
            luminanceSum += luminance;
        }

        if (stats.pixelCount > 0) {
            const float invPixelCount = 1.0f / static_cast<float>(stats.pixelCount);
            stats.meanRgb = sumRgb * invPixelCount;
            stats.meanLuminance = static_cast<float>(luminanceSum / static_cast<double>(stats.pixelCount));
        }

        return stats;
    }

    void printStats(const FrameColorStats& stats) {
        const double nonBlackRatio = stats.pixelCount == 0
                                         ? 0.0
                                         : static_cast<double>(stats.nonBlackPixelCount) /
                                               static_cast<double>(stats.pixelCount);
        std::cerr << "Frame stats: pixels=" << stats.pixelCount
                  << " finite=" << stats.finitePixelCount
                  << " nonBlackRatio=" << nonBlackRatio
                  << " meanRgb=(" << stats.meanRgb.r << ", " << stats.meanRgb.g << ", " << stats.meanRgb.b << ")"
                  << " minRgb=(" << stats.minRgb.r << ", " << stats.minRgb.g << ", " << stats.minRgb.b << ")"
                  << " maxRgb=(" << stats.maxRgb.r << ", " << stats.maxRgb.g << ", " << stats.maxRgb.b << ")"
                  << " meanLum=" << stats.meanLuminance
                  << " maxLum=" << stats.maxLuminance << '\n';
    }

    bool validateStats(const FrameColorStats& stats) {
        if (stats.pixelCount == 0 || stats.finitePixelCount != stats.pixelCount) {
            std::cerr << "Frame validation read non-finite pixels\n";
            printStats(stats);
            return false;
        }

        const double nonBlackRatio =
            static_cast<double>(stats.nonBlackPixelCount) / static_cast<double>(stats.pixelCount);
        if (nonBlackRatio < 0.05) {
            std::cerr << "Frame validation output is too close to black\n";
            printStats(stats);
            return false;
        }

        if (stats.maxLuminance < 0.01f || stats.meanLuminance <= 0.001f ||
            stats.meanLuminance > 20.0f) {
            std::cerr << "Frame validation luminance is outside the expected smoke range\n";
            printStats(stats);
            return false;
        }

        const glm::vec3 channelRange = stats.maxRgb - stats.minRgb;
        if (std::max({channelRange.r, channelRange.g, channelRange.b}) < 0.01f) {
            std::cerr << "Frame validation output has insufficient color variation\n";
            printStats(stats);
            return false;
        }

        return true;
    }

    ark::Scope<ark::rhi::Texture> createSceneColorTexture(ark::rhi::RenderDevice& device) {
        ark::rhi::TextureDesc desc{};
        desc.extent = FrameExtent;
        desc.format = ark::rhi::Format::RGBA16Float;
        desc.usage = ark::rhi::TextureUsage::RenderTarget |
                     ark::rhi::TextureUsage::ShaderResource |
                     ark::rhi::TextureUsage::TransferSrc;
        return device.createTexture(desc);
    }

    ark::Scope<ark::rhi::Texture> createDepthTexture(ark::rhi::RenderDevice& device) {
        ark::rhi::TextureDesc desc{};
        desc.extent = FrameExtent;
        desc.format = ark::rhi::Format::D32Float;
        desc.usage = ark::rhi::TextureUsage::DepthStencil;
        return device.createTexture(desc);
    }

    ark::Scope<ark::rhi::Buffer> createReadbackBuffer(ark::rhi::RenderDevice& device) {
        ark::rhi::BufferDesc desc{};
        desc.debugName = "FrameValidationReadbackBuffer";
        desc.size = FrameByteSize;
        desc.usage = ark::rhi::BufferUsage::TransferDst;
        desc.memoryUsage = ark::rhi::MemoryUsage::GpuToCpu;
        return device.createBuffer(desc);
    }

    ark::Path findFixturePath(const ark::Path& relative) {
        const std::array<ark::Path, 3> candidates{
            relative,
            ark::Path{"../"} / relative,
            ark::Path{"../../"} / relative,
        };

        return ark::findFirstExistingPath(candidates);
    }

    bool renderValidationFrame(ark::rhi::RenderDevice& device,
                               ark::rhi::DeviceContext& context,
                               ark::rhi::FrameResource& frame,
                               ark::rhi::Texture& sceneColor,
                               ark::rhi::TextureView& sceneColorView,
                               ark::rhi::Texture& depth,
                               ark::rhi::TextureView& depthView,
                               ark::EnvironmentCubeResource& skyboxCube,
                               ark::EnvironmentResource& environment,
                               ark::ModelResource& model) {
        ark::SkyboxPass skyboxPass{};
        ark::ForwardPass forwardPass{};
        skyboxPass.setup(device);
        forwardPass.setup(device);

        ark::RenderScene scene{};
        scene.setEnvironment(ark::SceneEnvironment{
            .environment = &environment,
            .intensity = 1.0f,
        });
        ark::SceneLighting lighting{};
        lighting.mainLight.direction = glm::vec3{-0.25f, -0.55f, -0.80f};
        lighting.mainLight.color = glm::vec3{1.0f, 0.97f, 0.90f};
        lighting.ambientColor = glm::vec3{0.04f, 0.05f, 0.06f};
        scene.setLighting(lighting);
        scene.addModel(model, glm::mat4{1.0f}, "FrameValidationSpecularFixture");

        ark::RenderQueue queue{};
        queue.build(scene);
        if (queue.empty()) {
            std::cerr << "Frame validation render queue is empty\n";
            return false;
        }

        ark::RenderView view{};
        const glm::vec3 cameraPosition{0.0f, 0.0f, 4.0f};
        const glm::mat4 viewMatrix =
            glm::lookAt(cameraPosition, glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
        glm::mat4 projection =
            glm::perspectiveRH_ZO(glm::radians(45.0f),
                                  static_cast<float>(FrameExtent.width) / static_cast<float>(FrameExtent.height),
                                  0.1f,
                                  100.0f);
        projection[1][1] *= -1.0f;
        view.setMatrices(viewMatrix, projection, cameraPosition);

        ark::FrameContext frameContext{};
        frameContext.frameIndex = frame.frameIndex;
        frameContext.scene = &scene;
        frameContext.view = &view;
        frameContext.queue = &queue;
        frameContext.device = &device;
        frameContext.context = &context;
        frameContext.frameResource = &frame;
        frameContext.sceneColorView = &sceneColorView;
        frameContext.environmentCube = &skyboxCube;
        frameContext.extent = FrameExtent;
        frameContext.colorFormat = ark::rhi::Format::RGBA16Float;
        frameContext.depthFormat = ark::rhi::Format::D32Float;
        frameContext.clearColor = ark::rhi::ClearColor{0.0f, 0.0f, 0.0f, 1.0f};

        if (!skyboxPass.prepare(frameContext) || !forwardPass.prepare(frameContext)) {
            std::cerr << "Frame validation pass prepare failed\n";
            return false;
        }

        const std::array<ark::rhi::ResourceBarrier, 2> toRenderTarget{{
            ark::rhi::ResourceBarrier{
                .texture = &sceneColor,
                .before = sceneColor.getState(),
                .after = ark::rhi::ResourceState::RenderTarget,
            },
            ark::rhi::ResourceBarrier{
                .texture = &depth,
                .before = depth.getState(),
                .after = ark::rhi::ResourceState::DepthStencilWrite,
            },
        }};
        context.pipelineBarrier(toRenderTarget);

        ark::rhi::RenderingDesc renderingDesc{};
        renderingDesc.extent = FrameExtent;
        renderingDesc.colorAttachment.view = &sceneColorView;
        renderingDesc.colorAttachment.loadOp = ark::rhi::LoadOp::Clear;
        renderingDesc.colorAttachment.storeOp = ark::rhi::StoreOp::Store;
        renderingDesc.colorAttachment.clearColor = frameContext.clearColor;
        renderingDesc.depthStencilAttachment.view = &depthView;
        renderingDesc.depthStencilAttachment.loadOp = ark::rhi::LoadOp::Clear;
        renderingDesc.depthStencilAttachment.storeOp = ark::rhi::StoreOp::DontCare;
        renderingDesc.depthStencilAttachment.clearDepth = 1.0f;

        if (!context.beginRendering(renderingDesc)) {
            std::cerr << "Frame validation beginRendering failed\n";
            return false;
        }

        ark::rhi::Viewport viewport{};
        viewport.width = static_cast<float>(FrameExtent.width);
        viewport.height = static_cast<float>(FrameExtent.height);
        context.setViewport(viewport);

        ark::rhi::ScissorRect scissor{};
        scissor.width = FrameExtent.width;
        scissor.height = FrameExtent.height;
        context.setScissorRect(scissor);

        if (!skyboxPass.execute(frameContext) || !forwardPass.execute(frameContext)) {
            context.endRendering();
            std::cerr << "Frame validation pass execute failed\n";
            return false;
        }

        context.endRendering();
        return true;
    }

    bool validateFrameColorReadback() {
        HiddenGlfwWindow window{};

        ark::rhi::RenderBackendDesc backendDesc{};
        backendDesc.device.desc.applicationName = "ARK Frame Validation Smoke";
        backendDesc.device.nativeWindow = window.nativeHandle();
        backendDesc.swapChain.extent = FrameExtent;
        backendDesc.swapChain.enableVSync = true;

        ark::Scope<ark::rhi::RenderBackend> backend = ark::rhi::createRenderBackend(backendDesc);
        if (!backend) {
            std::cerr << "Failed to create render backend\n";
            return false;
        }

        ark::rhi::RenderDevice& device = backend->device();
        ark::rhi::DeviceContext& context = backend->context();

        ark::Scope<ark::rhi::Texture> sceneColor = createSceneColorTexture(device);
        ark::Scope<ark::rhi::Texture> depth = createDepthTexture(device);
        ark::Scope<ark::rhi::Buffer> readbackBuffer = createReadbackBuffer(device);
        if (!sceneColor || !depth || !readbackBuffer) {
            std::cerr << "Failed to create frame validation resources\n";
            return false;
        }

        ark::rhi::TextureViewDesc sceneColorViewDesc{};
        sceneColorViewDesc.format = ark::rhi::Format::RGBA16Float;
        ark::Scope<ark::rhi::TextureView> sceneColorView =
            device.createTextureView(*sceneColor, sceneColorViewDesc);

        ark::rhi::TextureViewDesc depthViewDesc{};
        depthViewDesc.format = ark::rhi::Format::D32Float;
        ark::Scope<ark::rhi::TextureView> depthView = device.createTextureView(*depth, depthViewDesc);
        if (!sceneColorView || !depthView) {
            std::cerr << "Failed to create frame validation texture views\n";
            return false;
        }

        const ark::Path modelPath =
            findFixturePath(ark::Path{"assets/models/specular_ibl_validation_fixture.gltf"});
        if (modelPath.empty()) {
            std::cerr << "Failed to find specular IBL validation fixture\n";
            return false;
        }

        const ark::asset::ModelData modelData = ark::asset::loadGltfModel(modelPath);
        if (modelData.empty()) {
            std::cerr << "Failed to load specular IBL validation fixture\n";
            return false;
        }

        ark::ModelResource model{};
        if (!model.create(device, modelData)) {
            std::cerr << "Failed to create specular IBL validation model resource\n";
            return false;
        }

        const ark::asset::ImageData environmentImage = ark::makeProceduralSandboxEnvironmentImage();
        ark::EnvironmentResource environment{};
        ark::EnvironmentResourceDesc environmentDesc{};
        environmentDesc.debugName = "FrameValidationProceduralEnvironment";
        if (!environment.create(device, environmentImage, environmentDesc)) {
            std::cerr << "Failed to create frame validation environment\n";
            return false;
        }

        ark::EnvironmentCubeResource skyboxCube{};
        ark::EnvironmentCubeResourceDesc cubeDesc{};
        cubeDesc.debugName = "FrameValidationSkyboxCube";
        cubeDesc.faceExtent = ark::rhi::Extent2D{32, 32};
        cubeDesc.format = ark::rhi::Format::RGBA16Float;
        cubeDesc.mipLevels = 1;
        if (!skyboxCube.create(device, cubeDesc)) {
            std::cerr << "Failed to create frame validation skybox cube\n";
            return false;
        }

        ark::EnvironmentCubeConverter converter{};
        converter.setup(device);

        ark::rhi::FrameResource& frame = context.beginFrame();
        if (!context.begin(frame)) {
            std::cerr << "Failed to begin frame validation command recording\n";
            return false;
        }

        if (!environment.upload(context)) {
            std::cerr << "Failed to upload frame validation environment\n";
            return false;
        }

        ark::EnvironmentCubeConversionDesc conversionDesc{};
        conversionDesc.source = &environment;
        conversionDesc.target = &skyboxCube;
        conversionDesc.debugName = "FrameValidationSkyboxConversion";
        if (!converter.convert(context, conversionDesc)) {
            std::cerr << "Failed to convert frame validation environment to cubemap\n";
            return false;
        }

        if (!renderValidationFrame(device,
                                   context,
                                   frame,
                                   *sceneColor,
                                   *sceneColorView,
                                   *depth,
                                   *depthView,
                                   skyboxCube,
                                   environment,
                                   model)) {
            return false;
        }

        const std::array<ark::rhi::ResourceBarrier, 1> toCopySrc{{
            ark::rhi::ResourceBarrier{
                .texture = sceneColor.get(),
                .before = sceneColor->getState(),
                .after = ark::rhi::ResourceState::CopySrc,
            },
        }};
        context.pipelineBarrier(toCopySrc);

        ark::rhi::TextureReadbackDesc readbackDesc{};
        readbackDesc.texture = sceneColor.get();
        readbackDesc.destinationBuffer = readbackBuffer.get();
        readbackDesc.extent = FrameExtent;
        readbackDesc.bytesPerPixel = FrameBytesPerPixel;
        if (!context.copyTextureToBuffer(readbackDesc)) {
            std::cerr << "Failed to copy frame validation scene color to readback buffer\n";
            return false;
        }

        ark::rhi::SubmitDesc submitDesc{};
        submitDesc.frameResource = &frame;
        submitDesc.waitForSwapChainImage = false;
        submitDesc.signalRenderFinished = false;
        if (!context.end() || !context.submit(submitDesc)) {
            std::cerr << "Failed to submit frame validation command buffer\n";
            return false;
        }

        device.waitIdle();

        std::vector<ark::u8> bytes(static_cast<ark::usize>(FrameByteSize));
        if (!readbackBuffer->readData(bytes.data(), FrameByteSize)) {
            std::cerr << "Failed to read frame validation bytes\n";
            return false;
        }

        const FrameColorStats stats = computeFrameColorStats(bytes, FrameExtent);
        return validateStats(stats);
    }
} // namespace

int main() {
    try {
        return validateFrameColorReadback() ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
