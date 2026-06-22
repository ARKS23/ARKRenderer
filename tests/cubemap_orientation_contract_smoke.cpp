#include "renderer/effects/ibl/CubemapOrientation.h"
#include "renderer/resources/EnvironmentCubeResource.h"
#include "renderer/effects/sky/SandboxEnvironment.h"
#include "rhi/Buffer.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DescriptorSetLayout.h"
#include "rhi/DeviceContext.h"
#include "rhi/Fence.h"
#include "rhi/FrameResource.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RenderDevice.h"
#include "rhi/Sampler.h"
#include "rhi/Shader.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace {
    class FakeBuffer final : public ark::rhi::Buffer {
    public:
        explicit FakeBuffer(const ark::rhi::BufferDesc& desc) : m_Desc(desc) {
        }

        const ark::rhi::BufferDesc& getDesc() const override {
            return m_Desc;
        }

    private:
        ark::rhi::BufferDesc m_Desc;
    };

    class FakeTexture final : public ark::rhi::Texture {
    public:
        explicit FakeTexture(const ark::rhi::TextureDesc& desc) : m_Desc(desc) {
        }

        const ark::rhi::TextureDesc& getDesc() const override {
            return m_Desc;
        }

        ark::rhi::ResourceState getState() const override {
            return ark::rhi::ResourceState::ShaderResource;
        }

    private:
        ark::rhi::TextureDesc m_Desc;
    };

    class FakeTextureView final : public ark::rhi::TextureView {
    public:
        FakeTextureView(ark::rhi::Texture& texture, const ark::rhi::TextureViewDesc& desc)
            : m_Texture(&texture), m_Desc(desc) {
        }

        ark::rhi::Texture* getTexture() const override {
            return m_Texture;
        }

        const ark::rhi::TextureViewDesc& getDesc() const override {
            return m_Desc;
        }

    private:
        ark::rhi::Texture* m_Texture = nullptr;
        ark::rhi::TextureViewDesc m_Desc;
    };

    class FakeSampler final : public ark::rhi::Sampler {
    public:
        explicit FakeSampler(const ark::rhi::SamplerDesc& desc) : m_Desc(desc) {
        }

        const ark::rhi::SamplerDesc& getDesc() const override {
            return m_Desc;
        }

    private:
        ark::rhi::SamplerDesc m_Desc;
    };

    class FakeShader final : public ark::rhi::Shader {
    public:
        explicit FakeShader(const ark::rhi::ShaderDesc& desc) : m_Desc(desc) {
        }

        const ark::rhi::ShaderDesc& getDesc() const override {
            return m_Desc;
        }

    private:
        ark::rhi::ShaderDesc m_Desc;
    };

    class FakePipelineLayout final : public ark::rhi::PipelineLayout {
    public:
        explicit FakePipelineLayout(const ark::rhi::PipelineLayoutDesc& desc) : m_Desc(desc) {
        }

        const ark::rhi::PipelineLayoutDesc& getDesc() const override {
            return m_Desc;
        }

    private:
        ark::rhi::PipelineLayoutDesc m_Desc;
    };

    class FakePipelineState final : public ark::rhi::PipelineState {
    public:
        explicit FakePipelineState(const ark::rhi::GraphicsPipelineDesc& desc) : m_Desc(desc) {
        }

        const ark::rhi::GraphicsPipelineDesc& getDesc() const override {
            return m_Desc;
        }

    private:
        ark::rhi::GraphicsPipelineDesc m_Desc;
    };

    class FakeDescriptorSetLayout final : public ark::rhi::DescriptorSetLayout {
    public:
        explicit FakeDescriptorSetLayout(const ark::rhi::DescriptorSetLayoutDesc& desc) : m_Desc(desc) {
        }

        const ark::rhi::DescriptorSetLayoutDesc& getDesc() const override {
            return m_Desc;
        }

    private:
        ark::rhi::DescriptorSetLayoutDesc m_Desc;
    };

    class FakeDescriptorSet final : public ark::rhi::DescriptorSet {
    public:
        void updateUniformBuffer(ark::u32, const ark::rhi::BufferDescriptor&) override {
        }

        void updateSampledImage(ark::u32, const ark::rhi::SampledImageDescriptor&) override {
        }

        void updateSampler(ark::u32, const ark::rhi::SamplerDescriptor&) override {
        }
    };

    class FakeFence final : public ark::rhi::Fence {
    };

    class FakeRenderDevice final : public ark::rhi::RenderDevice {
    public:
        void waitIdle() override {
        }

        ark::rhi::RenderBackendType getBackendType() const override {
            return ark::rhi::RenderBackendType::Vulkan;
        }

        const ark::rhi::RenderDeviceCaps& getCaps() const override {
            return m_Caps;
        }

        ark::Scope<ark::rhi::Buffer> createBuffer(const ark::rhi::BufferDesc& desc) override {
            return ark::makeScope<FakeBuffer>(desc);
        }

        ark::Scope<ark::rhi::Texture> createTexture(const ark::rhi::TextureDesc& desc) override {
            textureDescs.push_back(desc);
            return ark::makeScope<FakeTexture>(desc);
        }

        ark::Scope<ark::rhi::TextureView> createTextureView(ark::rhi::Texture& texture,
                                                            const ark::rhi::TextureViewDesc& desc) override {
            textureViewDescs.push_back(desc);
            return ark::makeScope<FakeTextureView>(texture, desc);
        }

        ark::Scope<ark::rhi::Sampler> createSampler(const ark::rhi::SamplerDesc& desc) override {
            samplerDescs.push_back(desc);
            return ark::makeScope<FakeSampler>(desc);
        }

        ark::Scope<ark::rhi::Shader> createShader(const ark::rhi::ShaderDesc& desc) override {
            return ark::makeScope<FakeShader>(desc);
        }

        ark::Scope<ark::rhi::PipelineLayout> createPipelineLayout(const ark::rhi::PipelineLayoutDesc& desc) override {
            return ark::makeScope<FakePipelineLayout>(desc);
        }

        ark::Scope<ark::rhi::PipelineState> createGraphicsPipeline(const ark::rhi::GraphicsPipelineDesc& desc) override {
            return ark::makeScope<FakePipelineState>(desc);
        }

        ark::Scope<ark::rhi::DescriptorSetLayout>
        createDescriptorSetLayout(const ark::rhi::DescriptorSetLayoutDesc& desc) override {
            return ark::makeScope<FakeDescriptorSetLayout>(desc);
        }

        ark::Scope<ark::rhi::DescriptorSet> createDescriptorSet(const ark::rhi::DescriptorSetLayout&) override {
            return ark::makeScope<FakeDescriptorSet>();
        }

        ark::Scope<ark::rhi::Fence> createFence() override {
            return ark::makeScope<FakeFence>();
        }

        std::vector<ark::rhi::TextureDesc> textureDescs;
        std::vector<ark::rhi::TextureViewDesc> textureViewDescs;
        std::vector<ark::rhi::SamplerDesc> samplerDescs;

    private:
        ark::rhi::RenderDeviceCaps m_Caps{};
    };

    class FakeDeviceContext final : public ark::rhi::DeviceContext {
    public:
        ark::rhi::FrameResource& beginFrame() override {
            return frame;
        }

        bool begin(ark::rhi::FrameResource&) override {
            return true;
        }

        bool end() override {
            return true;
        }

        bool submit(const ark::rhi::SubmitDesc&) override {
            return true;
        }

        void advanceFrame() override {
        }

        bool beginRendering(const ark::rhi::RenderingDesc&) override {
            return true;
        }

        void endRendering() override {
        }

        void setViewport(const ark::rhi::Viewport&) override {
        }

        void setScissorRect(const ark::rhi::ScissorRect&) override {
        }

        void setPipeline(ark::rhi::PipelineState&) override {
        }

        void bindDescriptorSet(ark::u32, ark::rhi::DescriptorSet&) override {
        }

        bool updateBuffer(ark::rhi::Buffer&, const void*, ark::u64, ark::u64 = 0) override {
            return true;
        }

        bool uploadTextureData(const ark::rhi::TextureUploadDesc&) override {
            return true;
        }

        bool generateTextureMips(ark::rhi::Texture&) override {
            return true;
        }

        bool uploadBufferData(const ark::rhi::BufferUploadDesc&) override {
            return true;
        }

        bool deferReleaseBuffer(ark::Scope<ark::rhi::Buffer>& buffer) override {
            buffer.reset();
            return true;
        }

        bool deferReleaseTexture(ark::Scope<ark::rhi::Texture>& texture) override {
            texture.reset();
            return true;
        }

        bool deferReleaseTextureView(ark::Scope<ark::rhi::TextureView>& textureView) override {
            textureView.reset();
            return true;
        }

        bool deferReleaseSampler(ark::Scope<ark::rhi::Sampler>& sampler) override {
            sampler.reset();
            return true;
        }

        void setVertexBuffer(ark::u32, ark::rhi::Buffer&, ark::u64 = 0) override {
        }

        void setIndexBuffer(ark::rhi::Buffer&, ark::rhi::IndexType = ark::rhi::IndexType::UInt32, ark::u64 = 0) override {
        }

        void draw(const ark::rhi::DrawDesc&) override {
        }

        void drawIndexed(const ark::rhi::DrawIndexedDesc&) override {
        }

        void pipelineBarrier(std::span<const ark::rhi::ResourceBarrier>) override {
        }

        void clearRenderTarget(ark::rhi::TextureView&, const ark::rhi::ClearColor&) override {
        }

        ark::rhi::FrameResource frame{};
    };

    bool near(float a, float b, float epsilon = 0.0001f) {
        return std::fabs(a - b) <= epsilon;
    }

    bool nearColor(ark::LinearColor a, ark::LinearColor b) {
        return near(a.r, b.r) && near(a.g, b.g) && near(a.b, b.b) && near(a.a, b.a);
    }

    ark::LinearColor readPixel(const ark::asset::ImageData& image, ark::u32 x, ark::u32 y) {
        const auto* pixels = reinterpret_cast<const float*>(image.pixels.data());
        const ark::usize pixelOffset = (static_cast<ark::usize>(y) * image.width + x) * 4;
        return ark::LinearColor{
            pixels[pixelOffset + 0],
            pixels[pixelOffset + 1],
            pixels[pixelOffset + 2],
            pixels[pixelOffset + 3],
        };
    }

    bool validateFaceContract() {
        if (ark::CubemapFaceContracts.size() != ark::CubemapFaceCount ||
            ark::EnvironmentCubeResource::FaceCount != ark::CubemapFaceCount) {
            std::cerr << "Cubemap face count contract is invalid\n";
            return false;
        }

        const std::array<ark::CubemapFace, ark::CubemapFaceCount> expectedFaces{
            ark::CubemapFace::PositiveX,
            ark::CubemapFace::NegativeX,
            ark::CubemapFace::PositiveY,
            ark::CubemapFace::NegativeY,
            ark::CubemapFace::PositiveZ,
            ark::CubemapFace::NegativeZ,
        };

        for (ark::u32 faceIndex = 0; faceIndex < ark::CubemapFaceCount; ++faceIndex) {
            const ark::CubemapFaceContract& contract = ark::CubemapFaceContracts[faceIndex];
            if (contract.face != expectedFaces[faceIndex] ||
                static_cast<ark::u32>(contract.face) != faceIndex ||
                !nearColor(ark::debugOrientationColorForDirection(contract.axis), contract.debugColor)) {
                std::cerr << "Cubemap face order or debug color contract is invalid\n";
                return false;
            }
        }

        return true;
    }

    bool validateEnvironmentCubeFaceViews() {
        FakeRenderDevice device{};

        ark::EnvironmentCubeResourceDesc desc{};
        desc.debugName = "OrientationContractCube";
        desc.faceExtent = ark::rhi::Extent2D{16, 16};
        desc.format = ark::rhi::Format::RGBA16Float;
        desc.mipLevels = 1;

        ark::EnvironmentCubeResource cube{};
        if (!cube.create(device, desc)) {
            std::cerr << "Failed to create orientation contract cube\n";
            return false;
        }

        if (device.textureViewDescs.size() != ark::CubemapFaceCount + 1) {
            std::cerr << "EnvironmentCubeResource did not create cube view plus six face views\n";
            return false;
        }

        const ark::rhi::TextureViewDesc& cubeView = device.textureViewDescs.front();
        if (cubeView.type != ark::rhi::TextureViewType::Cube ||
            cubeView.baseArrayLayer != 0 ||
            cubeView.arrayLayerCount != ark::CubemapFaceCount) {
            std::cerr << "Cubemap sampled view does not cover six layers\n";
            return false;
        }

        for (ark::u32 faceIndex = 0; faceIndex < ark::CubemapFaceCount; ++faceIndex) {
            const ark::rhi::TextureViewDesc& faceView = device.textureViewDescs[faceIndex + 1];
            if (faceView.type != ark::rhi::TextureViewType::Texture2D ||
                faceView.baseArrayLayer != faceIndex ||
                faceView.arrayLayerCount != 1 ||
                faceView.baseMipLevel != 0 ||
                faceView.mipLevelCount != 1 ||
                cube.faceRenderTargetView(faceIndex) == nullptr) {
                std::cerr << "Cubemap face view layer does not match face contract\n";
                return false;
            }
        }

        cube.resetImmediate();
        return true;
    }

    bool validateDebugOrientationImage() {
        const ark::asset::ImageData image = ark::makeDebugOrientationEnvironmentImage();
        if (image.empty() ||
            image.width != 64 ||
            image.height != 32 ||
            image.format != ark::asset::ImageFormat::Rgba32Float ||
            image.bytesPerPixel != 16 ||
            image.debugName != "DebugOrientationEnvironment") {
            std::cerr << "Debug orientation environment image metadata is invalid\n";
            return false;
        }

        const ark::LinearColor negativeX = readPixel(image, 0, image.height / 2);
        const ark::LinearColor negativeZ = readPixel(image, image.width / 4, image.height / 2);
        const ark::LinearColor positiveX = readPixel(image, image.width / 2, image.height / 2);
        const ark::LinearColor positiveZ = readPixel(image, (image.width * 3) / 4, image.height / 2);
        const ark::LinearColor positiveY = readPixel(image, image.width / 4, 0);
        const ark::LinearColor negativeY = readPixel(image, image.width / 4, image.height - 1);

        if (!nearColor(positiveX, ark::cubemapFaceContract(ark::CubemapFace::PositiveX).debugColor) ||
            !nearColor(positiveZ, ark::cubemapFaceContract(ark::CubemapFace::PositiveZ).debugColor) ||
            !nearColor(negativeX, ark::cubemapFaceContract(ark::CubemapFace::NegativeX).debugColor) ||
            !nearColor(negativeZ, ark::cubemapFaceContract(ark::CubemapFace::NegativeZ).debugColor) ||
            !nearColor(positiveY, ark::cubemapFaceContract(ark::CubemapFace::PositiveY).debugColor) ||
            !nearColor(negativeY, ark::cubemapFaceContract(ark::CubemapFace::NegativeY).debugColor)) {
            std::cerr << "Debug orientation environment colors do not match face contract\n";
            return false;
        }

        return true;
    }

    bool validateProceduralEnvironmentStillAvailable() {
        const ark::asset::ImageData image = ark::makeProceduralSandboxEnvironmentImage();
        if (image.empty() ||
            image.width != 64 ||
            image.height != 32 ||
            image.format != ark::asset::ImageFormat::Rgba32Float ||
            image.bytesPerPixel != 16 ||
            image.debugName != "ProceduralSandboxEnvironment") {
            std::cerr << "Procedural sandbox environment image metadata is invalid\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateFaceContract() &&
                   validateEnvironmentCubeFaceViews() &&
                   validateDebugOrientationImage() &&
                   validateProceduralEnvironmentStillAvailable()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
