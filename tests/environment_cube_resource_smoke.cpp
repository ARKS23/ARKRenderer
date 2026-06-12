#include "core/Memory.h"
#include "renderer/EnvironmentCubeResource.h"
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

#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

namespace {
    class FakeBuffer final : public ark::rhi::Buffer {
    public:
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
        const ark::rhi::ShaderDesc& getDesc() const override {
            return m_Desc;
        }

    private:
        ark::rhi::ShaderDesc m_Desc;
    };

    class FakePipelineLayout final : public ark::rhi::PipelineLayout {
    public:
        const ark::rhi::PipelineLayoutDesc& getDesc() const override {
            return m_Desc;
        }

    private:
        ark::rhi::PipelineLayoutDesc m_Desc;
    };

    class FakePipelineState final : public ark::rhi::PipelineState {
    public:
        const ark::rhi::GraphicsPipelineDesc& getDesc() const override {
            return m_Desc;
        }

    private:
        ark::rhi::GraphicsPipelineDesc m_Desc;
    };

    class FakeDescriptorSetLayout final : public ark::rhi::DescriptorSetLayout {
    public:
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

        ark::Scope<ark::rhi::Buffer> createBuffer(const ark::rhi::BufferDesc&) override {
            return ark::makeScope<FakeBuffer>();
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

        ark::Scope<ark::rhi::Shader> createShader(const ark::rhi::ShaderDesc&) override {
            return ark::makeScope<FakeShader>();
        }

        ark::Scope<ark::rhi::PipelineLayout> createPipelineLayout(const ark::rhi::PipelineLayoutDesc&) override {
            return ark::makeScope<FakePipelineLayout>();
        }

        ark::Scope<ark::rhi::PipelineState> createGraphicsPipeline(const ark::rhi::GraphicsPipelineDesc&) override {
            return ark::makeScope<FakePipelineState>();
        }

        ark::Scope<ark::rhi::DescriptorSetLayout>
        createDescriptorSetLayout(const ark::rhi::DescriptorSetLayoutDesc&) override {
            return ark::makeScope<FakeDescriptorSetLayout>();
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
            ++deferredBuffers;
            return true;
        }

        bool deferReleaseTexture(ark::Scope<ark::rhi::Texture>& texture) override {
            texture.reset();
            ++deferredTextures;
            return true;
        }

        bool deferReleaseTextureView(ark::Scope<ark::rhi::TextureView>& textureView) override {
            textureView.reset();
            ++deferredTextureViews;
            return true;
        }

        bool deferReleaseSampler(ark::Scope<ark::rhi::Sampler>& sampler) override {
            sampler.reset();
            ++deferredSamplers;
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
        int deferredBuffers = 0;
        int deferredTextures = 0;
        int deferredTextureViews = 0;
        int deferredSamplers = 0;
    };

    bool validateEnvironmentCubeResourceCreateRelease() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};

        ark::EnvironmentCubeResourceDesc desc{};
        desc.debugName = "SmokeEnvironmentCube";
        desc.faceExtent = ark::rhi::Extent2D{64, 64};
        desc.format = ark::rhi::Format::RGBA16Float;
        desc.mipLevels = 3;

        ark::EnvironmentCubeResource cube{};
        if (!cube.create(device, desc)) {
            std::cerr << "EnvironmentCubeResource create failed\n";
            return false;
        }

        if (!cube.isValid() || !cube.texture() || !cube.textureView() || !cube.sampler() ||
            cube.faceExtent().width != 64 || cube.faceExtent().height != 64 ||
            cube.format() != ark::rhi::Format::RGBA16Float || cube.mipLevels() != 3) {
            std::cerr << "EnvironmentCubeResource create state is invalid\n";
            return false;
        }

        if (device.textureDescs.size() != 1 || device.textureViewDescs.size() != 1 ||
            device.samplerDescs.size() != 1) {
            std::cerr << "EnvironmentCubeResource did not create expected RHI objects\n";
            return false;
        }

        const ark::rhi::TextureDesc& textureDesc = device.textureDescs.front();
        if (textureDesc.type != ark::rhi::TextureType::Cube ||
            textureDesc.extent.width != 64 ||
            textureDesc.extent.height != 64 ||
            textureDesc.format != ark::rhi::Format::RGBA16Float ||
            textureDesc.mipLevels != 3 ||
            textureDesc.arrayLayers != 6 ||
            textureDesc.usage != ark::rhi::TextureUsage::ShaderResource) {
            std::cerr << "EnvironmentCubeResource texture desc is invalid\n";
            return false;
        }

        const ark::rhi::TextureViewDesc& viewDesc = device.textureViewDescs.front();
        if (viewDesc.type != ark::rhi::TextureViewType::Cube ||
            viewDesc.format != ark::rhi::Format::RGBA16Float ||
            viewDesc.mipLevelCount != 3 ||
            viewDesc.baseArrayLayer != 0 ||
            viewDesc.arrayLayerCount != 6) {
            std::cerr << "EnvironmentCubeResource texture view desc is invalid\n";
            return false;
        }

        const ark::rhi::SamplerDesc& samplerDesc = device.samplerDescs.front();
        if (samplerDesc.debugName != "SmokeEnvironmentCube.Sampler" ||
            samplerDesc.addressU != ark::rhi::AddressMode::ClampToEdge ||
            samplerDesc.addressV != ark::rhi::AddressMode::ClampToEdge ||
            samplerDesc.addressW != ark::rhi::AddressMode::ClampToEdge ||
            samplerDesc.minFilter != ark::rhi::FilterMode::Linear ||
            samplerDesc.magFilter != ark::rhi::FilterMode::Linear ||
            samplerDesc.mipFilter != ark::rhi::FilterMode::Linear) {
            std::cerr << "EnvironmentCubeResource sampler desc is invalid\n";
            return false;
        }

        if (!cube.releaseDeferred(context) ||
            context.deferredTextureViews != 1 ||
            context.deferredSamplers != 1 ||
            context.deferredTextures != 1 ||
            context.deferredBuffers != 0 ||
            cube.isValid() ||
            cube.format() != ark::rhi::Format::Unknown) {
            std::cerr << "EnvironmentCubeResource deferred release path is invalid\n";
            return false;
        }

        return true;
    }

    bool validateEnvironmentCubeResourceRejectsInvalidDescAndSupportsSamplerOverride() {
        FakeRenderDevice device{};

        ark::EnvironmentCubeResourceDesc nonSquareDesc{};
        nonSquareDesc.faceExtent = ark::rhi::Extent2D{64, 32};
        ark::EnvironmentCubeResource nonSquareCube{};
        if (nonSquareCube.create(device, nonSquareDesc) || !device.textureDescs.empty()) {
            std::cerr << "EnvironmentCubeResource accepted non-square faces\n";
            return false;
        }

        ark::EnvironmentCubeResourceDesc invalidMipDesc{};
        invalidMipDesc.faceExtent = ark::rhi::Extent2D{16, 16};
        invalidMipDesc.mipLevels = 0;
        ark::EnvironmentCubeResource invalidMipCube{};
        if (invalidMipCube.create(device, invalidMipDesc) || !device.textureDescs.empty()) {
            std::cerr << "EnvironmentCubeResource accepted zero mip levels\n";
            return false;
        }

        ark::EnvironmentCubeResourceDesc unsupportedFormatDesc{};
        unsupportedFormatDesc.faceExtent = ark::rhi::Extent2D{16, 16};
        unsupportedFormatDesc.format = ark::rhi::Format::RGBA8Unorm;
        ark::EnvironmentCubeResource unsupportedFormatCube{};
        if (unsupportedFormatCube.create(device, unsupportedFormatDesc) || !device.textureDescs.empty()) {
            std::cerr << "EnvironmentCubeResource accepted unsupported format\n";
            return false;
        }

        ark::EnvironmentCubeResourceDesc overrideDesc{};
        overrideDesc.debugName = "OverrideEnvironmentCube";
        overrideDesc.faceExtent = ark::rhi::Extent2D{32, 32};
        overrideDesc.format = ark::rhi::Format::RGBA32Float;
        overrideDesc.hasSamplerOverride = true;
        overrideDesc.sampler.addressU = ark::rhi::AddressMode::Repeat;
        overrideDesc.sampler.addressV = ark::rhi::AddressMode::ClampToEdge;
        overrideDesc.sampler.addressW = ark::rhi::AddressMode::MirroredRepeat;
        overrideDesc.sampler.mipFilter = ark::rhi::FilterMode::Nearest;

        ark::EnvironmentCubeResource overrideCube{};
        if (!overrideCube.create(device, overrideDesc)) {
            std::cerr << "EnvironmentCubeResource rejected sampler override\n";
            return false;
        }

        const ark::rhi::SamplerDesc& samplerDesc = device.samplerDescs.back();
        if (samplerDesc.debugName != "OverrideEnvironmentCube.Sampler" ||
            samplerDesc.addressU != ark::rhi::AddressMode::Repeat ||
            samplerDesc.addressW != ark::rhi::AddressMode::MirroredRepeat ||
            samplerDesc.mipFilter != ark::rhi::FilterMode::Nearest) {
            std::cerr << "EnvironmentCubeResource sampler override is invalid\n";
            return false;
        }

        overrideCube.resetImmediate();
        if (overrideCube.isValid() || overrideCube.format() != ark::rhi::Format::Unknown) {
            std::cerr << "EnvironmentCubeResource immediate reset is invalid\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateEnvironmentCubeResourceCreateRelease() &&
                   validateEnvironmentCubeResourceRejectsInvalidDescAndSupportsSamplerOverride()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
