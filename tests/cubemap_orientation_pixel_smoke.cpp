#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif

#include "asset/TextureLoader.h"
#include "core/Memory.h"
#include "renderer/effects/ibl/CubemapOrientation.h"
#include "renderer/effects/ibl/EnvironmentCubeConverter.h"
#include "renderer/EnvironmentCubeResource.h"
#include "renderer/EnvironmentResource.h"
#include "renderer/effects/sky/SandboxEnvironment.h"
#include "rhi/Buffer.h"
#include "rhi/DeviceContext.h"
#include "rhi/RenderBackend.h"
#include "rhi/ResourceBarrier.h"
#include "rhi/Texture.h"

#include <GLFW/glfw3.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <iostream>

namespace {
    constexpr ark::u32 FaceExtent = 16;
    constexpr ark::u32 BytesPerPixel = 16;
    constexpr ark::u64 FaceByteSize = FaceExtent * FaceExtent * BytesPerPixel;

    class HiddenGlfwWindow final {
    public:
        HiddenGlfwWindow() {
            if (glfwInit() != GLFW_TRUE) {
                throw std::runtime_error("glfwInit failed");
            }

            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

            m_Window = glfwCreateWindow(64, 64, "ARK cubemap orientation pixel smoke", nullptr, nullptr);
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

    struct ReadbackColor {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 0.0f;
    };

    bool near(float a, float b, float epsilon = 0.08f) {
        return std::abs(a - b) <= epsilon;
    }

    bool nearColor(ReadbackColor actual, ark::LinearColor expected) {
        return near(actual.r, expected.r) &&
               near(actual.g, expected.g) &&
               near(actual.b, expected.b) &&
               near(actual.a, expected.a);
    }

    void printColorMismatch(ark::u32 faceIndex, ReadbackColor actual, ark::LinearColor expected) {
        const ark::CubemapFaceContract& contract =
            ark::cubemapFaceContract(static_cast<ark::CubemapFace>(faceIndex));
        std::cerr << "Cubemap face " << faceIndex << " " << contract.name
                  << " center color mismatch. expected=("
                  << expected.r << ", " << expected.g << ", " << expected.b << ", " << expected.a
                  << ") actual=("
                  << actual.r << ", " << actual.g << ", " << actual.b << ", " << actual.a << ")\n";
    }

    bool createDebugEnvironment(ark::rhi::RenderDevice& device,
                                ark::rhi::DeviceContext& context,
                                ark::EnvironmentResource& environment) {
        const ark::asset::ImageData image = ark::makeDebugOrientationEnvironmentImage();

        ark::EnvironmentResourceDesc desc{};
        desc.debugName = "OrientationPixelSourceEnvironment";
        if (!environment.create(device, image, desc)) {
            std::cerr << "Failed to create debug orientation EnvironmentResource\n";
            return false;
        }

        if (!environment.upload(context)) {
            std::cerr << "Failed to upload debug orientation EnvironmentResource\n";
            return false;
        }

        return true;
    }

    bool readbackFaceCenter(ark::rhi::RenderDevice& device,
                            ark::rhi::DeviceContext& context,
                            ark::EnvironmentCubeResource& cube,
                            ark::u32 faceIndex,
                            ReadbackColor& color) {
        ark::rhi::BufferDesc bufferDesc{};
        bufferDesc.debugName = "CubemapFaceReadbackBuffer";
        bufferDesc.size = FaceByteSize;
        bufferDesc.usage = ark::rhi::BufferUsage::TransferDst;
        bufferDesc.memoryUsage = ark::rhi::MemoryUsage::GpuToCpu;
        ark::Scope<ark::rhi::Buffer> readbackBuffer = device.createBuffer(bufferDesc);
        if (!readbackBuffer) {
            std::cerr << "Failed to create cubemap face readback buffer\n";
            return false;
        }

        const std::array<ark::rhi::ResourceBarrier, 1> toCopySrc{{
            ark::rhi::ResourceBarrier{
                .texture = cube.texture(),
                .before = cube.texture()->getState(),
                .after = ark::rhi::ResourceState::CopySrc,
            },
        }};
        context.pipelineBarrier(toCopySrc);

        ark::rhi::TextureReadbackDesc readbackDesc{};
        readbackDesc.texture = cube.texture();
        readbackDesc.destinationBuffer = readbackBuffer.get();
        readbackDesc.extent = cube.faceExtent();
        readbackDesc.bytesPerPixel = BytesPerPixel;
        readbackDesc.arrayLayer = faceIndex;

        if (!context.copyTextureToBuffer(readbackDesc)) {
            std::cerr << "Failed to record cubemap face readback copy\n";
            return false;
        }

        const std::array<ark::rhi::ResourceBarrier, 1> toShaderResource{{
            ark::rhi::ResourceBarrier{
                .texture = cube.texture(),
                .before = cube.texture()->getState(),
                .after = ark::rhi::ResourceState::ShaderResource,
            },
        }};
        context.pipelineBarrier(toShaderResource);

        ark::rhi::SubmitDesc submitDesc{};
        submitDesc.frameResource = nullptr;
        submitDesc.waitForSwapChainImage = false;
        submitDesc.signalRenderFinished = false;
        if (!context.end() || !context.submit(submitDesc)) {
            std::cerr << "Failed to submit cubemap face readback copy\n";
            return false;
        }

        device.waitIdle();

        std::array<float, FaceExtent * FaceExtent * 4> pixels{};
        if (!readbackBuffer->readData(pixels.data(), FaceByteSize)) {
            std::cerr << "Failed to read cubemap face bytes\n";
            return false;
        }

        const ark::u32 centerX = FaceExtent / 2;
        const ark::u32 centerY = FaceExtent / 2;
        const ark::usize centerOffset = (static_cast<ark::usize>(centerY) * FaceExtent + centerX) * 4;
        color = ReadbackColor{
            pixels[centerOffset + 0],
            pixels[centerOffset + 1],
            pixels[centerOffset + 2],
            pixels[centerOffset + 3],
        };

        ark::rhi::FrameResource& frame = context.beginFrame();
        if (!context.begin(frame)) {
            std::cerr << "Failed to begin next frame after readback\n";
            return false;
        }

        return true;
    }

    bool validateCubemapOrientationPixels() {
        HiddenGlfwWindow window{};

        ark::rhi::RenderBackendDesc backendDesc{};
        backendDesc.device.desc.applicationName = "ARK Cubemap Orientation Pixel Smoke";
        backendDesc.device.nativeWindow = window.nativeHandle();
        backendDesc.swapChain.extent = ark::rhi::Extent2D{64, 64};
        backendDesc.swapChain.enableVSync = true;

        ark::Scope<ark::rhi::RenderBackend> backend = ark::rhi::createRenderBackend(backendDesc);
        if (!backend) {
            std::cerr << "Failed to create render backend\n";
            return false;
        }

        ark::rhi::RenderDevice& device = backend->device();
        ark::rhi::DeviceContext& context = backend->context();

        ark::rhi::FrameResource& initialFrame = context.beginFrame();
        if (!context.begin(initialFrame)) {
            std::cerr << "Failed to begin initial command recording\n";
            return false;
        }

        ark::EnvironmentResource environment{};
        if (!createDebugEnvironment(device, context, environment)) {
            return false;
        }

        ark::EnvironmentCubeResourceDesc cubeDesc{};
        cubeDesc.debugName = "OrientationPixelCube";
        cubeDesc.faceExtent = ark::rhi::Extent2D{FaceExtent, FaceExtent};
        cubeDesc.format = ark::rhi::Format::RGBA32Float;
        cubeDesc.mipLevels = 1;
        cubeDesc.allowReadback = true;

        ark::EnvironmentCubeResource cube{};
        if (!cube.create(device, cubeDesc)) {
            std::cerr << "Failed to create readback-enabled EnvironmentCubeResource\n";
            return false;
        }

        ark::EnvironmentCubeConverter converter{};
        converter.setup(device);

        ark::EnvironmentCubeConversionDesc conversionDesc{};
        conversionDesc.source = &environment;
        conversionDesc.target = &cube;
        conversionDesc.debugName = "OrientationPixelConversion";
        if (!converter.convert(context, conversionDesc)) {
            std::cerr << "Failed to convert debug orientation environment to cubemap\n";
            return false;
        }

        for (ark::u32 faceIndex = 0; faceIndex < ark::CubemapFaceCount; ++faceIndex) {
            ReadbackColor actual{};
            if (!readbackFaceCenter(device, context, cube, faceIndex, actual)) {
                return false;
            }

            const ark::LinearColor expected =
                ark::cubemapFaceContract(static_cast<ark::CubemapFace>(faceIndex)).debugColor;
            if (!nearColor(actual, expected)) {
                printColorMismatch(faceIndex, actual, expected);
                return false;
            }
        }

        if (!context.end()) {
            std::cerr << "Failed to end trailing command recording\n";
            return false;
        }

        device.waitIdle();
        return true;
    }
} // namespace

int main() {
    try {
        return validateCubemapOrientationPixels() ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
