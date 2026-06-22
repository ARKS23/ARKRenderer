#include "core/Memory.h"
#include "renderer/FrameContext.h"
#include "renderer/RenderView.h"
#include "renderer/effects/ssao/SsaoPass.h"
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
#include <string_view>
#include <vector>

namespace {
    struct CapturedSsaoUniform {
        float projection[16]{};
        float inverseProjection[16]{};
        float parameters0[4]{};
        float parameters1[4]{};
        float texelSize[4]{};
    };

    static_assert(sizeof(CapturedSsaoUniform) == 176);

    bool hasPrefix(std::string_view text, std::string_view prefix) {
        return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
    }

    bool hasUsage(ark::rhi::TextureUsage value, ark::rhi::TextureUsage usage) {
        return ark::rhi::hasTextureUsage(value, usage);
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
        void updateUniformBuffer(ark::u32, const ark::rhi::BufferDescriptor&) override {
            ++uniformUpdates;
        }

        void updateSampledImage(ark::u32, const ark::rhi::SampledImageDescriptor& image) override {
            ++sampledImageUpdates;
            if (image.view) {
                sampledImages.push_back(image.view);
            }
        }

        void updateSampler(ark::u32, const ark::rhi::SamplerDescriptor& sampler) override {
            ++samplerUpdates;
            lastSampler = sampler.sampler;
        }

        int uniformUpdates = 0;
        int sampledImageUpdates = 0;
        int samplerUpdates = 0;
        ark::rhi::Sampler* lastSampler = nullptr;
        std::vector<ark::rhi::TextureView*> sampledImages;
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
            descriptorSetLayoutDescs.push_back(desc);
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
        std::vector<ark::rhi::DescriptorSetLayoutDesc> descriptorSetLayoutDescs;
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

        void setViewport(const ark::rhi::Viewport&) override {
            ++viewportCount;
        }

        void setScissorRect(const ark::rhi::ScissorRect&) override {
            ++scissorCount;
        }

        void setPipeline(ark::rhi::PipelineState&) override {
            ++pipelineBinds;
        }

        void bindDescriptorSet(ark::u32, ark::rhi::DescriptorSet&) override {
            ++descriptorBinds;
        }

        bool updateBuffer(ark::rhi::Buffer& buffer, const void* data, ark::u64 size, ark::u64 offset = 0) override {
            if (hasPrefix(buffer.getDesc().debugName, "SsaoFullscreenUniformBuffer.")) {
                if (size != sizeof(CapturedSsaoUniform) || offset != 0) {
                    return false;
                }

                CapturedSsaoUniform uniform{};
                std::memcpy(&uniform, data, sizeof(uniform));
                fullscreenUniforms.push_back(uniform);
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
            ++indexedDraws;
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
        std::vector<ark::rhi::DrawDesc> drawDescs;
        std::vector<CapturedSsaoUniform> fullscreenUniforms;
        int beginRenderingCount = 0;
        int endRenderingCount = 0;
        int viewportCount = 0;
        int scissorCount = 0;
        int pipelineBinds = 0;
        int descriptorBinds = 0;
        int draws = 0;
        int indexedDraws = 0;
        int deferredTextureReleases = 0;
        int deferredTextureViewReleases = 0;
        int deferredSamplerReleases = 0;
    };

    struct FrameFixture {
        ark::rhi::TextureDesc sceneDesc{};
        FakeTexture sceneTexture;
        FakeTextureView sceneView;
        ark::rhi::TextureDesc depthDesc{};
        FakeTexture depthTexture;
        FakeTextureView depthView;

        explicit FrameFixture(ark::rhi::Extent2D extent)
            : sceneDesc{extent,
                        ark::rhi::Format::RGBA16Float,
                        1,
                        1,
                        ark::rhi::TextureUsage::RenderTarget | ark::rhi::TextureUsage::ShaderResource,
                        ark::rhi::TextureType::Texture2D},
              sceneTexture(sceneDesc),
              sceneView(sceneTexture, ark::rhi::TextureViewDesc{ark::rhi::Format::RGBA16Float}),
              depthDesc{extent,
                        ark::rhi::Format::D32Float,
                        1,
                        1,
                        ark::rhi::TextureUsage::DepthStencil,
                        ark::rhi::TextureType::Texture2D},
              depthTexture(depthDesc),
              depthView(depthTexture, ark::rhi::TextureViewDesc{ark::rhi::Format::D32Float}) {
            sceneTexture.setState(ark::rhi::ResourceState::ShaderResource);
            depthTexture.setState(ark::rhi::ResourceState::DepthStencilWrite);
        }
    };

    ark::FrameContext makeFrameContext(ark::RenderView& view,
                                       FakeDeviceContext& context,
                                       FrameFixture& fixture) {
        ark::FrameContext frameContext{};
        frameContext.view = &view;
        frameContext.context = &context;
        frameContext.frameResource = &context.frame;
        frameContext.sceneColorView = &fixture.sceneView;
        frameContext.depthBufferView = &fixture.depthView;
        frameContext.extent = fixture.sceneDesc.extent;
        frameContext.colorFormat = ark::rhi::Format::RGBA16Float;
        frameContext.depthFormat = ark::rhi::Format::D32Float;
        return frameContext;
    }

    bool validateDisabledPath() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};
        FrameFixture fixture{ark::rhi::Extent2D{128, 64}};

        ark::RenderView view{};
        view.setDefaultPerspective(fixture.sceneDesc.extent);

        ark::SsaoPass pass{};
        pass.setup(device);

        ark::FrameContext frameContext = makeFrameContext(view, context, fixture);
        if (!pass.prepare(frameContext) || !pass.execute(frameContext)) {
            std::cerr << "SsaoPass disabled path failed\n";
            return false;
        }

        if (frameContext.sceneColorView != &fixture.sceneView ||
            frameContext.ssaoNormalDepthView ||
            frameContext.ssaoOcclusionView ||
            frameContext.ssaoCompositeView ||
            !device.textureDescs.empty() ||
            !device.bufferDescs.empty() ||
            !device.descriptorSets.empty() ||
            context.beginRenderingCount != 0 ||
            context.draws != 0 ||
            context.indexedDraws != 0) {
            std::cerr << "SsaoPass disabled path should not allocate targets or record draw work\n";
            return false;
        }

        return device.descriptorSetLayoutDescs.size() == 2 &&
               device.shaderDescs.size() == 4 &&
               device.pipelineLayoutDescs.size() == 2 &&
               device.pipelineDescs.size() == 2 &&
               device.samplerDescs.size() == 1;
    }

    bool validateEnabledResources(const FakeRenderDevice& device) {
        if (device.textureDescs.size() != 5 ||
            device.textureViewDescs.size() != 5 ||
            device.bufferDescs.size() != 3 ||
            device.descriptorSets.size() != 3) {
            std::cerr << "SsaoPass did not create expected resources for empty-queue path\n";
            return false;
        }

        for (ark::usize index = 0; index < device.textureDescs.size(); ++index) {
            const ark::rhi::TextureDesc& desc = device.textureDescs[index];
            if (index == 1) {
                if (desc.format != ark::rhi::Format::D32Float ||
                    !hasUsage(desc.usage, ark::rhi::TextureUsage::DepthStencil)) {
                    std::cerr << "SsaoPass normal-depth depth texture desc is invalid\n";
                    return false;
                }
                continue;
            }

            if (desc.format != ark::rhi::Format::RGBA16Float ||
                !hasUsage(desc.usage, ark::rhi::TextureUsage::RenderTarget) ||
                !hasUsage(desc.usage, ark::rhi::TextureUsage::ShaderResource)) {
                std::cerr << "SsaoPass target texture desc is invalid\n";
                return false;
            }
        }

        if (device.textureDescs[0].extent.width != 64 ||
            device.textureDescs[0].extent.height != 32 ||
            device.textureDescs[1].extent.width != 64 ||
            device.textureDescs[1].extent.height != 32 ||
            device.textureDescs[4].extent.width != 128 ||
            device.textureDescs[4].extent.height != 64) {
            std::cerr << "SsaoPass target extents do not match resolution scale contract\n";
            return false;
        }

        if (device.pipelineDescs.size() < 2 ||
            !device.pipelineDescs[0].depthStencilState.enableDepthTest ||
            !device.pipelineDescs[0].depthStencilState.enableDepthWrite ||
            device.pipelineDescs[0].depthFormat != ark::rhi::Format::D32Float) {
            std::cerr << "SsaoPass normal-depth pipeline should own and write its private depth target\n";
            return false;
        }

        for (const ark::rhi::BufferDesc& desc : device.bufferDescs) {
            if (!hasPrefix(desc.debugName, "SsaoFullscreenUniformBuffer.") ||
                desc.size != sizeof(CapturedSsaoUniform) ||
                desc.usage != ark::rhi::BufferUsage::Uniform ||
                desc.memoryUsage != ark::rhi::MemoryUsage::CpuToGpu) {
                std::cerr << "SsaoPass fullscreen uniform buffer desc is invalid\n";
                return false;
            }
        }

        return true;
    }

    bool validateEnabledCommands(const FakeDeviceContext& context,
                                 const ark::FrameContext& frameContext,
                                 const ark::rhi::TextureView& originalSceneColorView) {
        if (context.beginRenderingCount != 4 ||
            context.endRenderingCount != 4 ||
            context.pipelineBinds != 3 ||
            context.descriptorBinds != 3 ||
            context.draws != 3 ||
            context.indexedDraws != 0 ||
            context.fullscreenUniforms.size() != 3 ||
            context.resourceBarriers.size() != 9) {
            std::cerr << "SsaoPass did not record expected normal-depth/fullscreen command sequence\n";
            return false;
        }

        constexpr std::array<float, 3> ExpectedModes{0.0f, 1.0f, 2.0f};
        for (ark::usize index = 0; index < ExpectedModes.size(); ++index) {
            if (context.drawDescs[index].vertexCount != 3 ||
                context.fullscreenUniforms[index].parameters1[3] != ExpectedModes[index]) {
                std::cerr << "SsaoPass fullscreen draw mode is invalid\n";
                return false;
            }
        }

        if (context.renderingDescs.empty() ||
            context.renderingDescs[0].extent.width != 64 ||
            context.renderingDescs[0].extent.height != 32 ||
            !context.renderingDescs[0].depthStencilAttachment.view ||
            context.renderingDescs[0].depthStencilAttachment.loadOp != ark::rhi::LoadOp::Clear ||
            context.renderingDescs[0].depthStencilAttachment.storeOp != ark::rhi::StoreOp::DontCare) {
            std::cerr << "SsaoPass normal-depth prepass should clear and own a private depth attachment\n";
            return false;
        }

        if (frameContext.sceneColorView == &originalSceneColorView ||
            !frameContext.sceneColorView ||
            !frameContext.ssaoNormalDepthView ||
            !frameContext.ssaoOcclusionView ||
            !frameContext.ssaoCompositeView ||
            frameContext.ssaoExtent.width != 64 ||
            frameContext.ssaoExtent.height != 32) {
            std::cerr << "SsaoPass did not publish expected frame bindings\n";
            return false;
        }

        return true;
    }

    bool validateEnabledPathAndRebuild() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};
        FrameFixture fixture{ark::rhi::Extent2D{128, 64}};

        ark::PostProcessingSettings postProcessing{};
        postProcessing.ssao.enabled = true;
        postProcessing.ssao.radius = 1.1f;
        postProcessing.ssao.intensity = 0.9f;
        postProcessing.ssao.sampleCount = 16;
        postProcessing.ssao.resolutionScale = 0.5f;

        ark::RenderView view{};
        view.setDefaultPerspective(fixture.sceneDesc.extent);
        view.setPostProcessingSettings(postProcessing);

        ark::SsaoPass pass{};
        pass.setup(device);

        ark::FrameContext frameContext = makeFrameContext(view, context, fixture);
        if (!pass.prepare(frameContext) || !pass.execute(frameContext)) {
            std::cerr << "SsaoPass enabled path failed\n";
            return false;
        }

        if (!validateEnabledResources(device) ||
            !validateEnabledCommands(context, frameContext, fixture.sceneView)) {
            return false;
        }

        const int initialDeferredTextures = context.deferredTextureReleases;
        const int initialDeferredViews = context.deferredTextureViewReleases;
        const ark::usize initialTextureCount = device.textureDescs.size();

        postProcessing.ssao.resolutionScale = 1.0f;
        view.setPostProcessingSettings(postProcessing);
        frameContext = makeFrameContext(view, context, fixture);
        if (!pass.prepare(frameContext) || !pass.execute(frameContext)) {
            std::cerr << "SsaoPass runtime target rebuild failed\n";
            return false;
        }

        if (context.deferredTextureReleases != initialDeferredTextures + 5 ||
            context.deferredTextureViewReleases != initialDeferredViews + 5 ||
            device.textureDescs.size() != initialTextureCount + 5 ||
            frameContext.ssaoExtent.width != 128 ||
            frameContext.ssaoExtent.height != 64) {
            std::cerr << "SsaoPass resolution-scale changes should defer-release and rebuild targets\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateDisabledPath() && validateEnabledPathAndRebuild()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
