#include "core/Memory.h"
#include "renderer/effects/ibl/EnvironmentBrdfLutGenerator.h"
#include "renderer/EnvironmentBrdfLutResource.h"
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
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace {
    struct alignas(16) CapturedBrdfLutUniform {
        ark::u32 sampleCount = 0;
        ark::u32 padding0 = 0;
        ark::u32 padding1 = 0;
        ark::u32 padding2 = 0;
    };

    static_assert(sizeof(CapturedBrdfLutUniform) == 16);

    struct DescriptorSetCapture {
        int uniformBufferUpdates = 0;
        ark::u32 lastUniformBinding = 0;
        ark::u64 lastUniformRange = 0;
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

        void updateSampledImage(ark::u32, const ark::rhi::SampledImageDescriptor&) override {
        }

        void updateSampler(ark::u32, const ark::rhi::SamplerDescriptor&) override {
        }

        DescriptorSetCapture capture{};
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
            if (buffer.getDesc().debugName == "BrdfLutUniformBuffer") {
                if (size != sizeof(CapturedBrdfLutUniform) || offset != 0) {
                    return false;
                }

                CapturedBrdfLutUniform uniform{};
                std::memcpy(&uniform, data, sizeof(uniform));
                uniforms.push_back(uniform);
            }

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
        std::vector<ark::rhi::ResourceBarrier> resourceBarriers;
        std::vector<ark::rhi::RenderingDesc> renderingDescs;
        std::vector<ark::rhi::Viewport> viewports;
        std::vector<ark::rhi::ScissorRect> scissors;
        std::vector<ark::rhi::DrawDesc> drawDescs;
        std::vector<CapturedBrdfLutUniform> uniforms;
        int beginRenderingCount = 0;
        int endRenderingCount = 0;
        int pipelineBinds = 0;
        int descriptorBinds = 0;
        int draws = 0;
        int deferredBuffers = 0;
        int deferredTextures = 0;
        int deferredTextureViews = 0;
        int deferredSamplers = 0;
    };

    bool validateBrdfLutResourceCreateRelease() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};

        ark::EnvironmentBrdfLutResourceDesc desc{};
        desc.debugName = "SmokeBrdfLut";
        desc.extent = ark::rhi::Extent2D{256, 128};

        ark::EnvironmentBrdfLutResource lut{};
        if (!lut.create(device, desc)) {
            std::cerr << "EnvironmentBrdfLutResource create failed\n";
            return false;
        }

        if (!lut.isValid() || !lut.texture() || !lut.textureView() || !lut.renderTargetView() || !lut.sampler() ||
            lut.extent().width != 256 || lut.extent().height != 128 ||
            lut.format() != ark::rhi::Format::RGBA16Float) {
            std::cerr << "EnvironmentBrdfLutResource create state is invalid\n";
            return false;
        }

        if (device.textureDescs.size() != 1 || device.textureViewDescs.size() != 2 ||
            device.samplerDescs.size() != 1) {
            std::cerr << "EnvironmentBrdfLutResource did not create expected RHI objects\n";
            return false;
        }

        const ark::rhi::TextureDesc& textureDesc = device.textureDescs.front();
        if (textureDesc.type != ark::rhi::TextureType::Texture2D ||
            textureDesc.extent.width != 256 ||
            textureDesc.extent.height != 128 ||
            textureDesc.format != ark::rhi::Format::RGBA16Float ||
            textureDesc.mipLevels != 1 ||
            textureDesc.arrayLayers != 1 ||
            !ark::rhi::hasTextureUsage(textureDesc.usage, ark::rhi::TextureUsage::RenderTarget) ||
            !ark::rhi::hasTextureUsage(textureDesc.usage, ark::rhi::TextureUsage::ShaderResource)) {
            std::cerr << "EnvironmentBrdfLutResource texture desc is invalid\n";
            return false;
        }

        for (const ark::rhi::TextureViewDesc& viewDesc : device.textureViewDescs) {
            if (viewDesc.type != ark::rhi::TextureViewType::Texture2D ||
                viewDesc.format != ark::rhi::Format::RGBA16Float ||
                viewDesc.baseMipLevel != 0 ||
                viewDesc.mipLevelCount != 1 ||
                viewDesc.baseArrayLayer != 0 ||
                viewDesc.arrayLayerCount != 1) {
                std::cerr << "EnvironmentBrdfLutResource texture view desc is invalid\n";
                return false;
            }
        }

        const ark::rhi::SamplerDesc& samplerDesc = device.samplerDescs.front();
        if (samplerDesc.debugName != "SmokeBrdfLut.Sampler" ||
            samplerDesc.addressU != ark::rhi::AddressMode::ClampToEdge ||
            samplerDesc.addressV != ark::rhi::AddressMode::ClampToEdge ||
            samplerDesc.addressW != ark::rhi::AddressMode::ClampToEdge ||
            samplerDesc.minFilter != ark::rhi::FilterMode::Linear ||
            samplerDesc.magFilter != ark::rhi::FilterMode::Linear ||
            samplerDesc.mipFilter != ark::rhi::FilterMode::Nearest) {
            std::cerr << "EnvironmentBrdfLutResource sampler desc is invalid\n";
            return false;
        }

        if (!lut.releaseDeferred(context) ||
            context.deferredTextureViews != 2 ||
            context.deferredSamplers != 1 ||
            context.deferredTextures != 1 ||
            context.deferredBuffers != 0 ||
            lut.isValid() ||
            lut.format() != ark::rhi::Format::Unknown) {
            std::cerr << "EnvironmentBrdfLutResource deferred release path is invalid\n";
            return false;
        }

        return true;
    }

    bool validateBrdfLutResourceRejectsInvalidDescAndSupportsSamplerOverride() {
        FakeRenderDevice device{};

        ark::EnvironmentBrdfLutResourceDesc invalidExtentDesc{};
        invalidExtentDesc.extent = {};
        ark::EnvironmentBrdfLutResource invalidExtentLut{};
        if (invalidExtentLut.create(device, invalidExtentDesc) || !device.textureDescs.empty()) {
            std::cerr << "EnvironmentBrdfLutResource accepted invalid extent\n";
            return false;
        }

        ark::EnvironmentBrdfLutResourceDesc unsupportedFormatDesc{};
        unsupportedFormatDesc.format = ark::rhi::Format::RGBA8Unorm;
        ark::EnvironmentBrdfLutResource unsupportedFormatLut{};
        if (unsupportedFormatLut.create(device, unsupportedFormatDesc) || !device.textureDescs.empty()) {
            std::cerr << "EnvironmentBrdfLutResource accepted unsupported format\n";
            return false;
        }

        ark::EnvironmentBrdfLutResourceDesc overrideDesc{};
        overrideDesc.debugName = "OverrideBrdfLut";
        overrideDesc.hasSamplerOverride = true;
        overrideDesc.sampler.addressU = ark::rhi::AddressMode::Repeat;
        overrideDesc.sampler.addressV = ark::rhi::AddressMode::MirroredRepeat;
        overrideDesc.sampler.addressW = ark::rhi::AddressMode::ClampToEdge;
        overrideDesc.sampler.minFilter = ark::rhi::FilterMode::Nearest;

        ark::EnvironmentBrdfLutResource overrideLut{};
        if (!overrideLut.create(device, overrideDesc)) {
            std::cerr << "EnvironmentBrdfLutResource rejected sampler override\n";
            return false;
        }

        const ark::rhi::SamplerDesc& samplerDesc = device.samplerDescs.back();
        if (samplerDesc.debugName != "OverrideBrdfLut.Sampler" ||
            samplerDesc.addressU != ark::rhi::AddressMode::Repeat ||
            samplerDesc.addressV != ark::rhi::AddressMode::MirroredRepeat ||
            samplerDesc.minFilter != ark::rhi::FilterMode::Nearest) {
            std::cerr << "EnvironmentBrdfLutResource sampler override is invalid\n";
            return false;
        }

        overrideLut.resetImmediate();
        if (overrideLut.isValid() || overrideLut.format() != ark::rhi::Format::Unknown) {
            std::cerr << "EnvironmentBrdfLutResource immediate reset is invalid\n";
            return false;
        }

        return true;
    }

    bool validateDescriptorLayout(const ark::rhi::DescriptorSetLayoutDesc& desc) {
        if (desc.debugName != "BrdfLutDescriptorSetLayout" || desc.bindings.size() != 1) {
            std::cerr << "EnvironmentBrdfLutGenerator descriptor layout shape is invalid\n";
            return false;
        }

        const ark::rhi::DescriptorBindingDesc& uniformBinding = desc.bindings[0];
        if (uniformBinding.binding != 0 ||
            uniformBinding.type != ark::rhi::DescriptorType::UniformBuffer ||
            !ark::rhi::hasShaderStage(uniformBinding.stages, ark::rhi::ShaderStageFlags::Fragment)) {
            std::cerr << "EnvironmentBrdfLutGenerator descriptor binding is invalid\n";
            return false;
        }

        return true;
    }

    bool validateGeneratorSetup(const FakeRenderDevice& device) {
        if (device.shaderDescs.size() != 2 ||
            device.pipelineLayoutDescs.size() != 1 ||
            device.bufferDescs.size() != 1 ||
            device.descriptorSets.size() != 1) {
            std::cerr << "EnvironmentBrdfLutGenerator setup resources are invalid\n";
            return false;
        }

        const ark::rhi::BufferDesc& bufferDesc = device.bufferDescs.front();
        if (bufferDesc.debugName != "BrdfLutUniformBuffer" ||
            bufferDesc.size != sizeof(CapturedBrdfLutUniform) ||
            bufferDesc.usage != ark::rhi::BufferUsage::Uniform ||
            bufferDesc.memoryUsage != ark::rhi::MemoryUsage::CpuToGpu) {
            std::cerr << "EnvironmentBrdfLutGenerator uniform buffer desc is invalid\n";
            return false;
        }

        const FakeDescriptorSet* descriptorSet = device.descriptorSets.front();
        if (descriptorSet->capture.uniformBufferUpdates != 1 ||
            descriptorSet->capture.lastUniformBinding != 0 ||
            descriptorSet->capture.lastUniformRange != sizeof(CapturedBrdfLutUniform)) {
            std::cerr << "EnvironmentBrdfLutGenerator uniform descriptor update is invalid\n";
            return false;
        }

        return validateDescriptorLayout(device.descriptorSetLayoutDesc);
    }

    bool validateGenerationExecution(const FakeRenderDevice& device,
                                     const FakeDeviceContext& context,
                                     const ark::EnvironmentBrdfLutResource& target) {
        if (context.resourceBarriers.size() != 2 ||
            context.resourceBarriers[0].texture != target.texture() ||
            context.resourceBarriers[0].after != ark::rhi::ResourceState::RenderTarget ||
            context.resourceBarriers[1].texture != target.texture() ||
            context.resourceBarriers[1].after != ark::rhi::ResourceState::ShaderResource) {
            std::cerr << "EnvironmentBrdfLutGenerator did not transition target correctly\n";
            return false;
        }

        if (context.beginRenderingCount != 1 ||
            context.endRenderingCount != 1 ||
            context.pipelineBinds != 1 ||
            context.descriptorBinds != 1 ||
            context.draws != 1 ||
            context.drawDescs.size() != 1 ||
            context.renderingDescs.size() != 1 ||
            context.viewports.size() != 1 ||
            context.scissors.size() != 1 ||
            context.uniforms.size() != 1) {
            std::cerr << "EnvironmentBrdfLutGenerator command counts are invalid\n";
            return false;
        }

        const ark::rhi::Extent2D extent = target.extent();
        if (context.drawDescs.front().vertexCount != 3 ||
            context.renderingDescs.front().extent.width != extent.width ||
            context.renderingDescs.front().extent.height != extent.height ||
            context.renderingDescs.front().colorAttachment.view != target.renderTargetView() ||
            context.viewports.front().width != static_cast<float>(extent.width) ||
            context.viewports.front().height != static_cast<float>(extent.height) ||
            context.scissors.front().width != extent.width ||
            context.scissors.front().height != extent.height ||
            context.uniforms.front().sampleCount != 16) {
            std::cerr << "EnvironmentBrdfLutGenerator command data is invalid\n";
            return false;
        }

        if (device.pipelineDescs.size() != 1) {
            std::cerr << "EnvironmentBrdfLutGenerator did not create exactly one pipeline\n";
            return false;
        }

        const ark::rhi::GraphicsPipelineDesc& pipelineDesc = device.pipelineDescs.front();
        if (pipelineDesc.debugName != "BrdfLutPipeline" ||
            pipelineDesc.colorFormat != ark::rhi::Format::RGBA16Float ||
            pipelineDesc.depthFormat != ark::rhi::Format::Unknown ||
            pipelineDesc.topology != ark::rhi::PrimitiveTopology::TriangleList ||
            pipelineDesc.rasterState.cullMode != ark::rhi::CullMode::None ||
            pipelineDesc.depthStencilState.enableDepthTest ||
            pipelineDesc.depthStencilState.enableDepthWrite ||
            pipelineDesc.blendState.colorAttachment.enableBlend) {
            std::cerr << "EnvironmentBrdfLutGenerator pipeline state is invalid\n";
            return false;
        }

        return true;
    }

    bool validateBrdfLutGeneration() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};

        ark::EnvironmentBrdfLutResource target{};
        ark::EnvironmentBrdfLutResourceDesc targetDesc{};
        targetDesc.debugName = "GeneratedBrdfLut";
        targetDesc.extent = ark::rhi::Extent2D{128, 128};
        if (!target.create(device, targetDesc)) {
            std::cerr << "Failed to create generated BRDF LUT target\n";
            return false;
        }

        ark::EnvironmentBrdfLutGenerator generator{};
        generator.setup(device);
        if (!validateGeneratorSetup(device)) {
            return false;
        }

        ark::EnvironmentBrdfLutGenerationDesc nullDesc{};
        nullDesc.debugName = "InvalidNullBrdfLutGeneration";
        if (generator.generate(context, nullDesc)) {
            std::cerr << "EnvironmentBrdfLutGenerator should reject null target\n";
            return false;
        }

        context = FakeDeviceContext{};
        ark::EnvironmentBrdfLutGenerationDesc generationDesc{};
        generationDesc.target = &target;
        generationDesc.sampleCount = 8;
        generationDesc.debugName = "BrdfLutSmoke";
        if (!generator.generate(context, generationDesc)) {
            std::cerr << "EnvironmentBrdfLutGenerator generation failed\n";
            return false;
        }

        if (!validateGenerationExecution(device, context, target)) {
            return false;
        }

        generator.resetImmediate();
        target.resetImmediate();
        return true;
    }
} // namespace

int main() {
    return validateBrdfLutResourceCreateRelease() &&
                   validateBrdfLutResourceRejectsInvalidDescAndSupportsSamplerOverride() &&
                   validateBrdfLutGeneration()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
