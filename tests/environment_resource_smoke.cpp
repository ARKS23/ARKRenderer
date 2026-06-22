#include "asset/TextureLoader.h"
#include "core/Memory.h"
#include "renderer/resources/EnvironmentResource.h"
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
            bufferDescs.push_back(desc);
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

        std::vector<ark::rhi::BufferDesc> bufferDescs;
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

        bool uploadTextureData(const ark::rhi::TextureUploadDesc& desc) override {
            lastUploadDesc = desc;
            lastUploadTextureFormat = desc.texture ? desc.texture->getDesc().format : ark::rhi::Format::Unknown;
            lastUploadTextureMipLevels = desc.texture ? desc.texture->getDesc().mipLevels : 0;
            ++textureUploads;
            return true;
        }

        bool generateTextureMips(ark::rhi::Texture&) override {
            ++generatedMips;
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
        ark::rhi::TextureUploadDesc lastUploadDesc{};
        ark::rhi::Format lastUploadTextureFormat = ark::rhi::Format::Unknown;
        ark::u32 lastUploadTextureMipLevels = 0;
        int textureUploads = 0;
        int generatedMips = 0;
        int deferredBuffers = 0;
        int deferredTextures = 0;
        int deferredTextureViews = 0;
        int deferredSamplers = 0;
    };

    ark::asset::ImageData makeHdrImage() {
        ark::asset::ImageData image{};
        image.width = 4;
        image.height = 2;
        image.format = ark::asset::ImageFormat::Rgba32Float;
        image.bytesPerPixel = 16;
        image.debugName = "EnvironmentSmokeImage";
        image.pixels.resize(static_cast<ark::usize>(image.width) * image.height * image.bytesPerPixel, 0);
        return image;
    }

    bool validateEnvironmentResourceCreateUploadRelease() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};
        const ark::asset::ImageData image = makeHdrImage();

        ark::EnvironmentResourceDesc desc{};
        desc.debugName = "SmokeEnvironment";

        ark::EnvironmentResource environment{};
        if (!environment.create(device, image, desc)) {
            std::cerr << "EnvironmentResource create failed\n";
            return false;
        }

        if (environment.isReady() || environment.format() != ark::rhi::Format::RGBA32Float ||
            environment.mipLevels() != 1 || !environment.textureView() || !environment.sampler()) {
            std::cerr << "EnvironmentResource create state is invalid\n";
            return false;
        }

        if (device.bufferDescs.size() != 1 || device.textureDescs.size() != 1 ||
            device.textureViewDescs.size() != 1 || device.samplerDescs.size() != 1) {
            std::cerr << "EnvironmentResource did not create expected RHI objects\n";
            return false;
        }

        const ark::rhi::BufferDesc& bufferDesc = device.bufferDescs.front();
        if (bufferDesc.debugName != "SmokeEnvironment.StagingBuffer" ||
            bufferDesc.size != image.byteSize() ||
            bufferDesc.usage != ark::rhi::BufferUsage::TransferSrc ||
            bufferDesc.memoryUsage != ark::rhi::MemoryUsage::CpuToGpu ||
            bufferDesc.initialData != image.pixels.data()) {
            std::cerr << "EnvironmentResource staging buffer desc is invalid\n";
            return false;
        }

        const ark::rhi::TextureDesc& textureDesc = device.textureDescs.front();
        if (textureDesc.extent.width != image.width ||
            textureDesc.extent.height != image.height ||
            textureDesc.format != ark::rhi::Format::RGBA32Float ||
            textureDesc.mipLevels != 1 ||
            textureDesc.arrayLayers != 1 ||
            !ark::rhi::hasTextureUsage(textureDesc.usage, ark::rhi::TextureUsage::ShaderResource) ||
            !ark::rhi::hasTextureUsage(textureDesc.usage, ark::rhi::TextureUsage::TransferDst) ||
            ark::rhi::hasTextureUsage(textureDesc.usage, ark::rhi::TextureUsage::TransferSrc)) {
            std::cerr << "EnvironmentResource texture desc is invalid\n";
            return false;
        }

        const ark::rhi::TextureViewDesc& viewDesc = device.textureViewDescs.front();
        if (viewDesc.format != ark::rhi::Format::RGBA32Float || viewDesc.mipLevelCount != 1) {
            std::cerr << "EnvironmentResource texture view desc is invalid\n";
            return false;
        }

        const ark::rhi::SamplerDesc& samplerDesc = device.samplerDescs.front();
        if (samplerDesc.addressU != ark::rhi::AddressMode::Repeat ||
            samplerDesc.addressV != ark::rhi::AddressMode::ClampToEdge ||
            samplerDesc.addressW != ark::rhi::AddressMode::ClampToEdge ||
            samplerDesc.minFilter != ark::rhi::FilterMode::Linear ||
            samplerDesc.magFilter != ark::rhi::FilterMode::Linear ||
            samplerDesc.mipFilter != ark::rhi::FilterMode::Nearest) {
            std::cerr << "EnvironmentResource sampler desc is invalid\n";
            return false;
        }

        if (!environment.upload(context) ||
            context.textureUploads != 1 ||
            context.lastUploadTextureFormat != ark::rhi::Format::RGBA32Float ||
            context.lastUploadTextureMipLevels != 1 ||
            context.lastUploadDesc.extent.width != image.width ||
            context.lastUploadDesc.extent.height != image.height ||
            context.lastUploadDesc.rowPitch != image.width * image.bytesPerPixel ||
            context.lastUploadDesc.bytesPerPixel != image.bytesPerPixel ||
            context.generatedMips != 0 ||
            context.deferredBuffers != 1 ||
            !environment.isReady()) {
            std::cerr << "EnvironmentResource upload path is invalid\n";
            return false;
        }

        if (!environment.upload(context) || context.textureUploads != 1) {
            std::cerr << "EnvironmentResource re-upload should be a no-op\n";
            return false;
        }

        if (!environment.releaseDeferred(context) ||
            context.deferredTextureViews != 1 ||
            context.deferredSamplers != 1 ||
            context.deferredTextures != 1 ||
            environment.isReady() ||
            environment.format() != ark::rhi::Format::Unknown) {
            std::cerr << "EnvironmentResource deferred release path is invalid\n";
            return false;
        }

        return true;
    }

    bool validateEnvironmentResourceRejectsInvalidImageAndSupportsSamplerOverride() {
        FakeRenderDevice device{};
        ark::asset::ImageData invalidImage = makeHdrImage();
        invalidImage.format = ark::asset::ImageFormat::Rgba8Unorm;
        invalidImage.bytesPerPixel = 4;

        ark::EnvironmentResourceDesc desc{};
        desc.debugName = "InvalidEnvironment";
        ark::EnvironmentResource invalidEnvironment{};
        if (invalidEnvironment.create(device, invalidImage, desc)) {
            std::cerr << "EnvironmentResource accepted invalid image format\n";
            return false;
        }

        ark::EnvironmentResourceDesc overrideDesc{};
        overrideDesc.debugName = "OverrideEnvironment";
        overrideDesc.hasSamplerOverride = true;
        overrideDesc.sampler.addressU = ark::rhi::AddressMode::MirroredRepeat;
        overrideDesc.sampler.addressV = ark::rhi::AddressMode::Repeat;
        overrideDesc.sampler.addressW = ark::rhi::AddressMode::ClampToEdge;
        overrideDesc.sampler.mipFilter = ark::rhi::FilterMode::Linear;

        ark::EnvironmentResource overrideEnvironment{};
        if (!overrideEnvironment.create(device, makeHdrImage(), overrideDesc)) {
            std::cerr << "EnvironmentResource rejected sampler override\n";
            return false;
        }

        const ark::rhi::SamplerDesc& samplerDesc = device.samplerDescs.back();
        if (samplerDesc.debugName != "OverrideEnvironment.Sampler" ||
            samplerDesc.addressU != ark::rhi::AddressMode::MirroredRepeat ||
            samplerDesc.addressV != ark::rhi::AddressMode::Repeat ||
            samplerDesc.mipFilter != ark::rhi::FilterMode::Linear) {
            std::cerr << "EnvironmentResource sampler override is invalid\n";
            return false;
        }

        overrideEnvironment.resetImmediate();
        if (overrideEnvironment.isReady() || overrideEnvironment.format() != ark::rhi::Format::Unknown) {
            std::cerr << "EnvironmentResource immediate reset is invalid\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateEnvironmentResourceCreateUploadRelease() &&
                   validateEnvironmentResourceRejectsInvalidImageAndSupportsSamplerOverride()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
