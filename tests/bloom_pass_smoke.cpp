#include "core/Memory.h"
#include "renderer/core/FrameContext.h"
#include "renderer/RenderView.h"
#include "renderer/effects/bloom/BloomPass.h"
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
#include <string>
#include <string_view>
#include <vector>

namespace {
    struct alignas(16) CapturedBloomUniform {
        ark::u32 mode = 0;
        float intensity = 0.0f;
        float scatter = 0.0f;
        float threshold = 0.0f;
        float softKnee = 0.0f;
        float texelSizeX = 0.0f;
        float texelSizeY = 0.0f;
        float padding0 = 0.0f;
    };

    static_assert(sizeof(CapturedBloomUniform) == 32);

    struct DescriptorSetCapture {
        int uniformBufferUpdates = 0;
        int sampledImage0Updates = 0;
        int sampledImage1Updates = 0;
        int samplerUpdates = 0;
        ark::u32 lastUniformBinding = 0;
        ark::u64 lastUniformRange = 0;
        ark::rhi::TextureView* sampledImage0 = nullptr;
        ark::rhi::TextureView* sampledImage1 = nullptr;
        ark::rhi::Sampler* sampler = nullptr;
    };

    bool near(float lhs, float rhs, float epsilon = 0.0001f) {
        return std::fabs(lhs - rhs) <= epsilon;
    }

    bool hasUsage(ark::rhi::TextureUsage value, ark::rhi::TextureUsage usage) {
        return ark::rhi::hasTextureUsage(value, usage);
    }

    bool hasPrefix(std::string_view text, std::string_view prefix) {
        return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
    }

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
            if (binding == 0) {
                ++capture.sampledImage0Updates;
                capture.sampledImage0 = image.view;
            } else if (binding == 1) {
                ++capture.sampledImage1Updates;
                capture.sampledImage1 = image.view;
            }
        }

        void updateSampler(ark::u32 binding, const ark::rhi::SamplerDescriptor& sampler) override {
            if (binding == 2) {
                ++capture.samplerUpdates;
                capture.sampler = sampler.sampler;
            }
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
            if (hasPrefix(buffer.getDesc().debugName, "BloomUniformBuffer.")) {
                if (size != sizeof(CapturedBloomUniform) || offset != 0) {
                    return false;
                }

                CapturedBloomUniform uniform{};
                std::memcpy(&uniform, data, sizeof(uniform));
                uniforms.push_back(uniform);
            }

            return true;
        }

        bool uploadTextureData(const ark::rhi::TextureUploadDesc& desc) override {
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
            return true;
        }

        bool deferReleaseTexture(ark::Scope<ark::rhi::Texture>& texture) override {
            ++deferredTextureReleases;
            texture.reset();
            return true;
        }

        bool deferReleaseTextureView(ark::Scope<ark::rhi::TextureView>& textureView) override {
            ++deferredTextureViewReleases;
            textureView.reset();
            return true;
        }

        bool deferReleaseSampler(ark::Scope<ark::rhi::Sampler>& sampler) override {
            ++deferredSamplerReleases;
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
        std::vector<ark::rhi::ResourceBarrier> resourceBarriers;
        std::vector<ark::rhi::RenderingDesc> renderingDescs;
        std::vector<ark::rhi::Viewport> viewports;
        std::vector<ark::rhi::ScissorRect> scissors;
        std::vector<ark::rhi::DrawDesc> drawDescs;
        std::vector<CapturedBloomUniform> uniforms;
        int beginRenderingCount = 0;
        int endRenderingCount = 0;
        int pipelineBinds = 0;
        int descriptorBinds = 0;
        int draws = 0;
        int deferredTextureReleases = 0;
        int deferredTextureViewReleases = 0;
        int deferredSamplerReleases = 0;
    };

    bool validateDescriptorLayout(const ark::rhi::DescriptorSetLayoutDesc& desc) {
        if (desc.debugName != "BloomDescriptorSetLayout" || desc.bindings.size() != 4) {
            std::cerr << "BloomPass descriptor layout shape is invalid\n";
            return false;
        }

        const std::array<ark::rhi::DescriptorType, 4> expectedTypes{
            ark::rhi::DescriptorType::SampledImage,
            ark::rhi::DescriptorType::SampledImage,
            ark::rhi::DescriptorType::Sampler,
            ark::rhi::DescriptorType::UniformBuffer,
        };
        for (ark::usize bindingIndex = 0; bindingIndex < expectedTypes.size(); ++bindingIndex) {
            const ark::rhi::DescriptorBindingDesc& binding = desc.bindings[bindingIndex];
            if (binding.binding != bindingIndex ||
                binding.type != expectedTypes[bindingIndex] ||
                binding.count != 1 ||
                !ark::rhi::hasShaderStage(binding.stages, ark::rhi::ShaderStageFlags::Fragment)) {
                std::cerr << "BloomPass descriptor binding " << bindingIndex << " is invalid\n";
                return false;
            }
        }

        return true;
    }

    bool validateDisabledPath() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};

        ark::rhi::TextureDesc sourceDesc{};
        sourceDesc.extent = ark::rhi::Extent2D{128, 64};
        sourceDesc.format = ark::rhi::Format::RGBA16Float;
        sourceDesc.usage = ark::rhi::TextureUsage::RenderTarget | ark::rhi::TextureUsage::ShaderResource;
        FakeTexture sourceTexture{sourceDesc};
        sourceTexture.setState(ark::rhi::ResourceState::ShaderResource);

        ark::rhi::TextureViewDesc sourceViewDesc{};
        sourceViewDesc.format = sourceDesc.format;
        FakeTextureView sourceView{sourceTexture, sourceViewDesc};

        ark::RenderView view{};
        view.setDefaultPerspective(sourceDesc.extent);

        ark::BloomPass pass{};
        pass.setup(device);

        ark::FrameContext frameContext{};
        frameContext.view = &view;
        frameContext.context = &context;
        frameContext.frameResource = &context.frame;
        frameContext.sceneColorView = &sourceView;
        frameContext.extent = sourceDesc.extent;
        frameContext.colorFormat = ark::rhi::Format::RGBA16Float;

        if (!pass.prepare(frameContext) || !pass.execute(frameContext)) {
            std::cerr << "BloomPass disabled path failed\n";
            return false;
        }

        if (frameContext.sceneColorView != &sourceView ||
            !device.textureDescs.empty() ||
            !device.bufferDescs.empty() ||
            !device.descriptorSets.empty() ||
            !device.pipelineDescs.empty() ||
            context.beginRenderingCount != 0 ||
            context.draws != 0 ||
            !context.uniforms.empty() ||
            !context.resourceBarriers.empty()) {
            std::cerr << "BloomPass disabled path should not allocate targets or record draw work\n";
            return false;
        }

        return validateDescriptorLayout(device.descriptorSetLayoutDesc);
    }

    bool validateResourceSetup(const FakeRenderDevice& device) {
        if (device.shaderDescs.size() != 2 ||
            device.pipelineLayoutDescs.size() != 1 ||
            device.samplerDescs.size() != 1) {
            std::cerr << "BloomPass shader, pipeline layout or sampler setup is invalid\n";
            return false;
        }

        const ark::rhi::SamplerDesc& samplerDesc = device.samplerDescs.front();
        if (samplerDesc.debugName != "BloomLinearClampSampler" ||
            samplerDesc.minFilter != ark::rhi::FilterMode::Linear ||
            samplerDesc.magFilter != ark::rhi::FilterMode::Linear ||
            samplerDesc.addressU != ark::rhi::AddressMode::ClampToEdge ||
            samplerDesc.addressV != ark::rhi::AddressMode::ClampToEdge ||
            samplerDesc.addressW != ark::rhi::AddressMode::ClampToEdge) {
            std::cerr << "BloomPass sampler contract is invalid\n";
            return false;
        }

        return validateDescriptorLayout(device.descriptorSetLayoutDesc);
    }

    bool validateEnabledResources(const FakeRenderDevice& device) {
        constexpr ark::usize ExpectedBloomTextures = 9;
        constexpr ark::usize ExpectedBloomDraws = 8;
        if (device.textureDescs.size() != ExpectedBloomTextures ||
            device.textureViewDescs.size() != ExpectedBloomTextures ||
            device.bufferDescs.size() != ExpectedBloomDraws ||
            device.descriptorSets.size() != ExpectedBloomDraws) {
            std::cerr << "BloomPass did not create expected target and per-draw resources\n";
            return false;
        }

        std::size_t fullExtentTargets = 0;
        std::size_t firstMipTargets = 0;
        for (const ark::rhi::TextureDesc& desc : device.textureDescs) {
            if (desc.format != ark::rhi::Format::RGBA16Float ||
                desc.mipLevels != 1 ||
                desc.arrayLayers != 1 ||
                !hasUsage(desc.usage, ark::rhi::TextureUsage::RenderTarget) ||
                !hasUsage(desc.usage, ark::rhi::TextureUsage::ShaderResource)) {
                std::cerr << "BloomPass target texture desc is invalid\n";
                return false;
            }

            if (desc.extent.width == 128 && desc.extent.height == 64) {
                ++fullExtentTargets;
            }
            if (desc.extent.width == 64 && desc.extent.height == 32) {
                ++firstMipTargets;
            }
        }

        if (fullExtentTargets != 1 || firstMipTargets != 2) {
            std::cerr << "BloomPass target extent chain is invalid\n";
            return false;
        }

        for (const ark::rhi::BufferDesc& desc : device.bufferDescs) {
            if (!hasPrefix(desc.debugName, "BloomUniformBuffer.") ||
                desc.size != sizeof(CapturedBloomUniform) ||
                desc.usage != ark::rhi::BufferUsage::Uniform ||
                desc.memoryUsage != ark::rhi::MemoryUsage::CpuToGpu) {
                std::cerr << "BloomPass uniform buffer desc is invalid\n";
                return false;
            }
        }

        return true;
    }

    bool validateEnabledCommands(const FakeRenderDevice& device,
                                 const FakeDeviceContext& context,
                                 const ark::FrameContext& frameContext,
                                 const ark::rhi::TextureView& originalSceneColorView) {
        constexpr int ExpectedBloomDraws = 8;
        constexpr std::array<ark::u32, ExpectedBloomDraws> ExpectedModes{0u, 1u, 1u, 1u, 2u, 2u, 2u, 3u};

        if (context.beginRenderingCount != ExpectedBloomDraws ||
            context.endRenderingCount != ExpectedBloomDraws ||
            context.pipelineBinds != ExpectedBloomDraws ||
            context.descriptorBinds != ExpectedBloomDraws ||
            context.draws != ExpectedBloomDraws ||
            context.drawDescs.size() != ExpectedBloomDraws ||
            context.renderingDescs.size() != ExpectedBloomDraws ||
            context.viewports.size() != ExpectedBloomDraws ||
            context.scissors.size() != ExpectedBloomDraws ||
            context.uniforms.size() != ExpectedBloomDraws ||
            context.resourceBarriers.size() != ExpectedBloomDraws * 2) {
            std::cerr << "BloomPass did not record the expected fullscreen pass sequence\n";
            return false;
        }

        for (int drawIndex = 0; drawIndex < ExpectedBloomDraws; ++drawIndex) {
            if (context.drawDescs[drawIndex].vertexCount != 3 ||
                context.resourceBarriers[drawIndex * 2].after != ark::rhi::ResourceState::RenderTarget ||
                context.resourceBarriers[drawIndex * 2 + 1].after != ark::rhi::ResourceState::ShaderResource ||
                context.uniforms[drawIndex].mode != ExpectedModes[drawIndex] ||
                !near(context.uniforms[drawIndex].intensity, 0.2f) ||
                !near(context.uniforms[drawIndex].scatter, 0.7f) ||
                !near(context.uniforms[drawIndex].threshold, 1.4f) ||
                !near(context.uniforms[drawIndex].softKnee, 0.35f)) {
                std::cerr << "BloomPass fullscreen draw " << drawIndex << " captured invalid command data\n";
                return false;
            }
        }

        if (!near(context.uniforms.front().texelSizeX, 1.0f / 128.0f) ||
            !near(context.uniforms.front().texelSizeY, 1.0f / 64.0f) ||
            !near(context.uniforms.back().texelSizeX, 1.0f / 64.0f) ||
            !near(context.uniforms.back().texelSizeY, 1.0f / 32.0f)) {
            std::cerr << "BloomPass source texel size uniforms are invalid\n";
            return false;
        }

        if (frameContext.sceneColorView == &originalSceneColorView ||
            !frameContext.sceneColorView ||
            !frameContext.sceneColorView->getTexture() ||
            frameContext.sceneColorView->getTexture()->getDesc().extent.width != 128 ||
            frameContext.sceneColorView->getTexture()->getDesc().extent.height != 64 ||
            frameContext.sceneColorView->getTexture()->getState() != ark::rhi::ResourceState::ShaderResource) {
            std::cerr << "BloomPass did not replace sceneColorView with a shader-readable HDR composite\n";
            return false;
        }

        if (device.pipelineDescs.size() != 1) {
            std::cerr << "BloomPass should create exactly one graphics pipeline\n";
            return false;
        }

        const ark::rhi::GraphicsPipelineDesc& pipelineDesc = device.pipelineDescs.front();
        if (pipelineDesc.debugName != "BloomPipeline" ||
            pipelineDesc.colorFormat != ark::rhi::Format::RGBA16Float ||
            pipelineDesc.depthFormat != ark::rhi::Format::Unknown ||
            pipelineDesc.topology != ark::rhi::PrimitiveTopology::TriangleList ||
            pipelineDesc.rasterState.cullMode != ark::rhi::CullMode::None ||
            pipelineDesc.depthStencilState.enableDepthTest ||
            pipelineDesc.depthStencilState.enableDepthWrite ||
            pipelineDesc.blendState.colorAttachment.enableBlend) {
            std::cerr << "BloomPass pipeline state is invalid\n";
            return false;
        }

        for (const FakeDescriptorSet* descriptorSet : device.descriptorSets) {
            const DescriptorSetCapture& capture = descriptorSet->capture;
            if (capture.uniformBufferUpdates != 1 ||
                capture.lastUniformBinding != 3 ||
                capture.lastUniformRange != sizeof(CapturedBloomUniform) ||
                capture.sampledImage0Updates != 1 ||
                capture.sampledImage1Updates != 1 ||
                capture.samplerUpdates != 1 ||
                !capture.sampledImage0 ||
                !capture.sampledImage1 ||
                !capture.sampler) {
                std::cerr << "BloomPass descriptor set update contract is invalid\n";
                return false;
            }
        }

        return true;
    }

    bool validateEnabledPath() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};
        context.frame.frameSlot = 1;
        context.frame.frameIndex = 17;

        ark::rhi::TextureDesc sourceDesc{};
        sourceDesc.extent = ark::rhi::Extent2D{128, 64};
        sourceDesc.format = ark::rhi::Format::RGBA16Float;
        sourceDesc.usage = ark::rhi::TextureUsage::RenderTarget | ark::rhi::TextureUsage::ShaderResource;
        FakeTexture sourceTexture{sourceDesc};
        sourceTexture.setState(ark::rhi::ResourceState::ShaderResource);

        ark::rhi::TextureViewDesc sourceViewDesc{};
        sourceViewDesc.format = sourceDesc.format;
        FakeTextureView sourceView{sourceTexture, sourceViewDesc};

        ark::PostProcessingSettings postProcessing{};
        postProcessing.bloom.enabled = true;
        postProcessing.bloom.intensity = 0.2f;
        postProcessing.bloom.scatter = 0.7f;
        postProcessing.bloom.threshold = 1.4f;
        postProcessing.bloom.softKnee = 0.35f;
        postProcessing.bloom.maxMipCount = 4;

        ark::RenderView view{};
        view.setDefaultPerspective(sourceDesc.extent);
        view.setPostProcessingSettings(postProcessing);

        ark::BloomPass pass{};
        pass.setup(device);
        if (!validateResourceSetup(device)) {
            return false;
        }

        ark::FrameContext frameContext{};
        frameContext.view = &view;
        frameContext.context = &context;
        frameContext.frameResource = &context.frame;
        frameContext.sceneColorView = &sourceView;
        frameContext.extent = sourceDesc.extent;
        frameContext.colorFormat = ark::rhi::Format::RGBA16Float;

        if (!pass.prepare(frameContext) || !pass.execute(frameContext)) {
            std::cerr << "BloomPass enabled path failed\n";
            return false;
        }

        if (!validateEnabledResources(device)) {
            return false;
        }

        if (!validateEnabledCommands(device, context, frameContext, sourceView)) {
            return false;
        }

        if (sourceTexture.getState() != ark::rhi::ResourceState::ShaderResource) {
            std::cerr << "BloomPass should leave original scene color shader-readable\n";
            return false;
        }

        return true;
    }

    bool validateRuntimeParameterUpdates() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};
        context.frame.frameSlot = 0;

        ark::rhi::TextureDesc sourceDesc{};
        sourceDesc.extent = ark::rhi::Extent2D{128, 64};
        sourceDesc.format = ark::rhi::Format::RGBA16Float;
        sourceDesc.usage = ark::rhi::TextureUsage::RenderTarget | ark::rhi::TextureUsage::ShaderResource;
        FakeTexture sourceTexture{sourceDesc};
        sourceTexture.setState(ark::rhi::ResourceState::ShaderResource);

        ark::rhi::TextureViewDesc sourceViewDesc{};
        sourceViewDesc.format = sourceDesc.format;
        FakeTextureView sourceView{sourceTexture, sourceViewDesc};

        ark::PostProcessingSettings postProcessing{};
        postProcessing.bloom.enabled = true;
        postProcessing.bloom.intensity = 0.2f;
        postProcessing.bloom.scatter = 0.7f;
        postProcessing.bloom.threshold = 1.4f;
        postProcessing.bloom.softKnee = 0.35f;
        postProcessing.bloom.maxMipCount = 4;

        ark::RenderView view{};
        view.setDefaultPerspective(sourceDesc.extent);
        view.setPostProcessingSettings(postProcessing);

        ark::BloomPass pass{};
        pass.setup(device);

        ark::FrameContext frameContext{};
        frameContext.view = &view;
        frameContext.context = &context;
        frameContext.frameResource = &context.frame;
        frameContext.sceneColorView = &sourceView;
        frameContext.extent = sourceDesc.extent;
        frameContext.colorFormat = ark::rhi::Format::RGBA16Float;

        if (!pass.prepare(frameContext) || !pass.execute(frameContext)) {
            std::cerr << "BloomPass runtime parameter setup failed\n";
            return false;
        }

        const ark::usize initialTextureCount = device.textureDescs.size();
        const int initialDeferredTextures = context.deferredTextureReleases;
        const int initialDeferredViews = context.deferredTextureViewReleases;

        postProcessing.bloom.intensity = 0.55f;
        postProcessing.bloom.threshold = 2.0f;
        view.setPostProcessingSettings(postProcessing);
        frameContext.sceneColorView = &sourceView;
        if (!pass.prepare(frameContext) || !pass.execute(frameContext)) {
            std::cerr << "BloomPass runtime uniform update failed\n";
            return false;
        }

        if (device.textureDescs.size() != initialTextureCount ||
            context.deferredTextureReleases != initialDeferredTextures ||
            context.deferredTextureViewReleases != initialDeferredViews ||
            context.deferredSamplerReleases != 0 ||
            context.uniforms.empty() ||
            !near(context.uniforms.back().intensity, 0.55f) ||
            !near(context.uniforms.back().threshold, 2.0f)) {
            std::cerr << "BloomPass uniform-only UI changes should not rebuild render targets\n";
            return false;
        }

        postProcessing.bloom.maxMipCount = 3;
        view.setPostProcessingSettings(postProcessing);
        frameContext.sceneColorView = &sourceView;
        if (!pass.prepare(frameContext) || !pass.execute(frameContext)) {
            std::cerr << "BloomPass runtime target rebuild failed\n";
            return false;
        }

        if (context.deferredTextureReleases != initialDeferredTextures + 9 ||
            context.deferredTextureViewReleases != initialDeferredViews + 9 ||
            context.deferredSamplerReleases != 0 ||
            device.textureDescs.size() != initialTextureCount + 7) {
            std::cerr << "BloomPass max-mip UI changes should defer-release old render targets before rebuild\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateDisabledPath() && validateEnabledPath() && validateRuntimeParameterUpdates()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
