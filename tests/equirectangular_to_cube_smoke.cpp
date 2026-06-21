#include "asset/TextureLoader.h"
#include "core/Memory.h"
#include "renderer/effects/ibl/EnvironmentCubeConverter.h"
#include "renderer/EnvironmentCubeResource.h"
#include "renderer/EnvironmentResource.h"
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
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {
    struct alignas(16) CapturedConversionUniform {
        ark::u32 faceIndex = 0;
        float outputResolution = 0.0f;
        float padding0 = 0.0f;
        float padding1 = 0.0f;
    };

    static_assert(sizeof(CapturedConversionUniform) == 16);

    struct DescriptorSetCapture {
        int uniformBufferUpdates = 0;
        int sampledImageUpdates = 0;
        int samplerUpdates = 0;
        ark::u32 lastUniformBinding = 0;
        ark::u64 lastUniformRange = 0;
        ark::u32 lastSampledImageBinding = 0;
        ark::u32 lastSamplerBinding = 0;
    };

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
            return m_State;
        }

        void setState(ark::rhi::ResourceState state) {
            m_State = state;
        }

    private:
        ark::rhi::TextureDesc m_Desc;
        ark::rhi::ResourceState m_State = ark::rhi::ResourceState::Undefined;
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
        void updateUniformBuffer(ark::u32 binding, const ark::rhi::BufferDescriptor& buffer) override {
            ++capture.uniformBufferUpdates;
            capture.lastUniformBinding = binding;
            capture.lastUniformRange = buffer.range;
        }

        void updateSampledImage(ark::u32 binding, const ark::rhi::SampledImageDescriptor& image) override {
            ++capture.sampledImageUpdates;
            capture.lastSampledImageBinding = binding;
            sourceImageView = image.view;
        }

        void updateSampler(ark::u32 binding, const ark::rhi::SamplerDescriptor& sampler) override {
            ++capture.samplerUpdates;
            capture.lastSamplerBinding = binding;
            sourceSampler = sampler.sampler;
        }

        DescriptorSetCapture capture{};
        ark::rhi::TextureView* sourceImageView = nullptr;
        ark::rhi::Sampler* sourceSampler = nullptr;
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
            if (desc.bytecode.empty()) {
                return nullptr;
            }

            shaderDescs.push_back(desc);
            return ark::makeScope<FakeShader>(desc);
        }

        ark::Scope<ark::rhi::PipelineLayout> createPipelineLayout(const ark::rhi::PipelineLayoutDesc& desc) override {
            pipelineLayoutDescs.push_back(desc);
            return ark::makeScope<FakePipelineLayout>(desc);
        }

        ark::Scope<ark::rhi::PipelineState> createGraphicsPipeline(const ark::rhi::GraphicsPipelineDesc& desc) override {
            pipelineDescs.push_back(desc);
            return ark::makeScope<FakePipelineState>(desc);
        }

        ark::Scope<ark::rhi::DescriptorSetLayout>
        createDescriptorSetLayout(const ark::rhi::DescriptorSetLayoutDesc& desc) override {
            descriptorSetLayoutDesc = desc;
            return ark::makeScope<FakeDescriptorSetLayout>(desc);
        }

        ark::Scope<ark::rhi::DescriptorSet> createDescriptorSet(const ark::rhi::DescriptorSetLayout&) override {
            auto descriptorSet = ark::makeScope<FakeDescriptorSet>();
            descriptorSets.push_back(descriptorSet.get());
            return descriptorSet;
        }

        ark::Scope<ark::rhi::Fence> createFence() override {
            return ark::makeScope<FakeFence>();
        }

        std::vector<ark::rhi::BufferDesc> bufferDescs;
        std::vector<ark::rhi::TextureDesc> textureDescs;
        std::vector<ark::rhi::TextureViewDesc> textureViewDescs;
        std::vector<ark::rhi::SamplerDesc> samplerDescs;
        std::vector<ark::rhi::ShaderDesc> shaderDescs;
        std::vector<ark::rhi::PipelineLayoutDesc> pipelineLayoutDescs;
        std::vector<ark::rhi::GraphicsPipelineDesc> pipelineDescs;
        ark::rhi::DescriptorSetLayoutDesc descriptorSetLayoutDesc{};
        std::vector<FakeDescriptorSet*> descriptorSets;

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

        bool beginRendering(const ark::rhi::RenderingDesc& desc) override {
            renderingDescs.push_back(desc);
            ++beginRenderingCount;
            return true;
        }

        void endRendering() override {
            ++endRenderingCount;
        }

        void setViewport(const ark::rhi::Viewport& viewport) override {
            viewports.push_back(viewport);
        }

        void setScissorRect(const ark::rhi::ScissorRect& rect) override {
            scissors.push_back(rect);
        }

        void setPipeline(ark::rhi::PipelineState&) override {
            ++pipelineBinds;
        }

        void bindDescriptorSet(ark::u32, ark::rhi::DescriptorSet&) override {
            ++descriptorBinds;
        }

        bool updateBuffer(ark::rhi::Buffer& buffer, const void* data, ark::u64 size, ark::u64 offset = 0) override {
            if (buffer.getDesc().debugName == "EquirectToCubeUniformBuffer") {
                if (size != sizeof(CapturedConversionUniform) || offset != 0) {
                    return false;
                }

                CapturedConversionUniform uniform{};
                std::memcpy(&uniform, data, sizeof(uniform));
                conversionUniforms.push_back(uniform);
            }

            return true;
        }

        bool uploadTextureData(const ark::rhi::TextureUploadDesc& desc) override {
            textureUploads.push_back(desc);
            if (auto* texture = dynamic_cast<FakeTexture*>(desc.texture)) {
                texture->setState(ark::rhi::ResourceState::ShaderResource);
            }
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

        void draw(const ark::rhi::DrawDesc& desc) override {
            drawDescs.push_back(desc);
            ++draws;
        }

        void drawIndexed(const ark::rhi::DrawIndexedDesc&) override {
        }

        void pipelineBarrier(std::span<const ark::rhi::ResourceBarrier> barriers) override {
            for (const ark::rhi::ResourceBarrier& barrier : barriers) {
                resourceBarriers.push_back(barrier);
                if (auto* texture = dynamic_cast<FakeTexture*>(barrier.texture)) {
                    texture->setState(barrier.after);
                }
            }
        }

        void clearRenderTarget(ark::rhi::TextureView&, const ark::rhi::ClearColor&) override {
        }

        ark::rhi::FrameResource frame{};
        std::vector<ark::rhi::TextureUploadDesc> textureUploads;
        std::vector<ark::rhi::ResourceBarrier> resourceBarriers;
        std::vector<ark::rhi::RenderingDesc> renderingDescs;
        std::vector<ark::rhi::Viewport> viewports;
        std::vector<ark::rhi::ScissorRect> scissors;
        std::vector<ark::rhi::DrawDesc> drawDescs;
        std::vector<CapturedConversionUniform> conversionUniforms;
        int beginRenderingCount = 0;
        int endRenderingCount = 0;
        int pipelineBinds = 0;
        int descriptorBinds = 0;
        int draws = 0;
        int deferredBuffers = 0;
    };

    ark::asset::ImageData makeHdrEnvironmentImage() {
        constexpr ark::u32 Width = 2;
        constexpr ark::u32 Height = 1;
        constexpr ark::u32 BytesPerPixel = 16;
        const float pixels[Width * Height * 4]{
            1.0f, 0.25f, 0.25f, 1.0f,
            0.25f, 0.5f, 1.0f, 1.0f,
        };

        ark::asset::ImageData image{};
        image.width = Width;
        image.height = Height;
        image.format = ark::asset::ImageFormat::Rgba32Float;
        image.bytesPerPixel = BytesPerPixel;
        image.pixels.resize(sizeof(pixels));
        std::memcpy(image.pixels.data(), pixels, sizeof(pixels));
        image.debugName = "EquirectToCubeSource";
        return image;
    }

    bool validateDescriptorLayout(const ark::rhi::DescriptorSetLayoutDesc& desc) {
        if (desc.debugName != "EquirectToCubeDescriptorSetLayout" || desc.bindings.size() != 3) {
            std::cerr << "EnvironmentCubeConverter descriptor layout shape is invalid\n";
            return false;
        }

        const ark::rhi::DescriptorBindingDesc& uniformBinding = desc.bindings[0];
        const ark::rhi::DescriptorBindingDesc& imageBinding = desc.bindings[1];
        const ark::rhi::DescriptorBindingDesc& samplerBinding = desc.bindings[2];
        if (uniformBinding.binding != 0 ||
            uniformBinding.type != ark::rhi::DescriptorType::UniformBuffer ||
            !ark::rhi::hasShaderStage(uniformBinding.stages, ark::rhi::ShaderStageFlags::Fragment) ||
            imageBinding.binding != 1 ||
            imageBinding.type != ark::rhi::DescriptorType::SampledImage ||
            !ark::rhi::hasShaderStage(imageBinding.stages, ark::rhi::ShaderStageFlags::Fragment) ||
            samplerBinding.binding != 2 ||
            samplerBinding.type != ark::rhi::DescriptorType::Sampler ||
            !ark::rhi::hasShaderStage(samplerBinding.stages, ark::rhi::ShaderStageFlags::Fragment)) {
            std::cerr << "EnvironmentCubeConverter descriptor bindings are invalid\n";
            return false;
        }

        return true;
    }

    bool validateResourceSetup(const FakeRenderDevice& device) {
        constexpr std::size_t FaceCount = ark::EnvironmentCubeResource::FaceCount;

        std::size_t conversionUniformBuffers = 0;
        for (const ark::rhi::BufferDesc& bufferDesc : device.bufferDescs) {
            if (bufferDesc.debugName == "EquirectToCubeUniformBuffer") {
                ++conversionUniformBuffers;
                if (bufferDesc.size != sizeof(CapturedConversionUniform) ||
                    bufferDesc.usage != ark::rhi::BufferUsage::Uniform ||
                    bufferDesc.memoryUsage != ark::rhi::MemoryUsage::CpuToGpu) {
                    std::cerr << "EnvironmentCubeConverter uniform buffer desc is invalid\n";
                    return false;
                }
            }
        }

        if (conversionUniformBuffers != FaceCount || device.descriptorSets.size() != FaceCount) {
            std::cerr << "EnvironmentCubeConverter did not create one descriptor set and uniform per face\n";
            return false;
        }

        if (device.shaderDescs.size() != 2 || device.pipelineLayoutDescs.size() != 1) {
            std::cerr << "EnvironmentCubeConverter shader or pipeline layout resources are invalid\n";
            return false;
        }

        if (!validateDescriptorLayout(device.descriptorSetLayoutDesc)) {
            return false;
        }

        return true;
    }

    bool validateExecution(const FakeRenderDevice& device,
                           const FakeDeviceContext& context,
                           const ark::EnvironmentCubeResource& target) {
        constexpr std::size_t FaceCount = ark::EnvironmentCubeResource::FaceCount;

        if (context.resourceBarriers.size() != 2 ||
            context.resourceBarriers[0].after != ark::rhi::ResourceState::RenderTarget ||
            context.resourceBarriers[1].after != ark::rhi::ResourceState::ShaderResource) {
            std::cerr << "EnvironmentCubeConverter did not transition target cubemap correctly\n";
            return false;
        }

        if (context.beginRenderingCount != static_cast<int>(FaceCount) ||
            context.endRenderingCount != static_cast<int>(FaceCount) ||
            context.pipelineBinds != static_cast<int>(FaceCount) ||
            context.descriptorBinds != static_cast<int>(FaceCount) ||
            context.draws != static_cast<int>(FaceCount) ||
            context.drawDescs.size() != FaceCount ||
            context.renderingDescs.size() != FaceCount ||
            context.viewports.size() != FaceCount ||
            context.scissors.size() != FaceCount ||
            context.conversionUniforms.size() != FaceCount) {
            std::cerr << "EnvironmentCubeConverter did not render exactly six faces\n";
            return false;
        }

        for (std::size_t faceIndex = 0; faceIndex < FaceCount; ++faceIndex) {
            if (context.drawDescs[faceIndex].vertexCount != 3 ||
                context.renderingDescs[faceIndex].extent.width != 32 ||
                context.renderingDescs[faceIndex].extent.height != 32 ||
                context.renderingDescs[faceIndex].colorAttachment.view !=
                    target.faceRenderTargetView(static_cast<ark::u32>(faceIndex)) ||
                context.viewports[faceIndex].width != 32.0f ||
                context.viewports[faceIndex].height != 32.0f ||
                context.scissors[faceIndex].width != 32 ||
                context.scissors[faceIndex].height != 32 ||
                context.conversionUniforms[faceIndex].faceIndex != faceIndex ||
                context.conversionUniforms[faceIndex].outputResolution != 32.0f) {
                std::cerr << "EnvironmentCubeConverter per-face command data is invalid\n";
                return false;
            }
        }

        if (device.pipelineDescs.size() != 1) {
            std::cerr << "EnvironmentCubeConverter did not create exactly one pipeline\n";
            return false;
        }

        const ark::rhi::GraphicsPipelineDesc& pipelineDesc = device.pipelineDescs.front();
        if (pipelineDesc.colorFormat != ark::rhi::Format::RGBA16Float ||
            pipelineDesc.depthFormat != ark::rhi::Format::Unknown ||
            pipelineDesc.topology != ark::rhi::PrimitiveTopology::TriangleList ||
            pipelineDesc.rasterState.cullMode != ark::rhi::CullMode::None ||
            pipelineDesc.depthStencilState.enableDepthTest ||
            pipelineDesc.depthStencilState.enableDepthWrite ||
            pipelineDesc.blendState.colorAttachment.enableBlend) {
            std::cerr << "EnvironmentCubeConverter pipeline state is invalid\n";
            return false;
        }

        for (const FakeDescriptorSet* descriptorSet : device.descriptorSets) {
            if (descriptorSet->capture.uniformBufferUpdates != 1 ||
                descriptorSet->capture.lastUniformBinding != 0 ||
                descriptorSet->capture.lastUniformRange != sizeof(CapturedConversionUniform) ||
                descriptorSet->capture.sampledImageUpdates != 1 ||
                descriptorSet->capture.lastSampledImageBinding != 1 ||
                descriptorSet->capture.samplerUpdates != 1 ||
                descriptorSet->capture.lastSamplerBinding != 2 ||
                !descriptorSet->sourceImageView ||
                !descriptorSet->sourceSampler) {
                std::cerr << "EnvironmentCubeConverter descriptor set updates are invalid\n";
                return false;
            }
        }

        return true;
    }

    bool validateEquirectangularToCubeConversion() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};

        ark::EnvironmentResource source{};
        ark::EnvironmentResourceDesc sourceDesc{};
        sourceDesc.debugName = "EquirectToCubeSource";
        if (!source.create(device, makeHdrEnvironmentImage(), sourceDesc) || !source.upload(context)) {
            std::cerr << "Failed to create or upload source environment\n";
            return false;
        }

        ark::EnvironmentCubeResource target{};
        ark::EnvironmentCubeResourceDesc targetDesc{};
        targetDesc.debugName = "EquirectToCubeTarget";
        targetDesc.faceExtent = ark::rhi::Extent2D{32, 32};
        targetDesc.format = ark::rhi::Format::RGBA16Float;
        targetDesc.mipLevels = 1;
        if (!target.create(device, targetDesc)) {
            std::cerr << "Failed to create target cubemap\n";
            return false;
        }

        ark::EnvironmentCubeConverter converter{};
        converter.setup(device);

        if (!validateResourceSetup(device)) {
            return false;
        }

        ark::EnvironmentCubeConversionDesc conversionDesc{};
        conversionDesc.source = &source;
        conversionDesc.target = &target;
        conversionDesc.debugName = "EquirectToCubeSmoke";
        if (!converter.convert(context, conversionDesc)) {
            std::cerr << "EnvironmentCubeConverter conversion failed\n";
            return false;
        }

        if (!validateExecution(device, context, target)) {
            return false;
        }

        converter.resetImmediate();
        target.resetImmediate();
        source.resetImmediate();
        return true;
    }
} // namespace

int main() {
    return validateEquirectangularToCubeConversion() ? EXIT_SUCCESS : EXIT_FAILURE;
}
