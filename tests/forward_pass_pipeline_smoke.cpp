#include "asset/MeshData.h"
#include "core/Memory.h"
#include "renderer/FrameContext.h"
#include "renderer/MeshResource.h"
#include "renderer/RenderQueue.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/TextureCache.h"
#include "renderer/material/MaterialResource.h"
#include "renderer/passes/ForwardPass.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DescriptorSetLayout.h"
#include "rhi/DeviceContext.h"
#include "rhi/Fence.h"
#include "rhi/FrameResource.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RenderDevice.h"
#include "rhi/Shader.h"
#include "rhi/SwapChain.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <cstdlib>
#include <iostream>
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
            return ark::makeScope<FakeTexture>(desc);
        }

        ark::Scope<ark::rhi::TextureView> createTextureView(ark::rhi::Texture& texture,
                                                            const ark::rhi::TextureViewDesc& desc) override {
            return ark::makeScope<FakeTextureView>(texture, desc);
        }

        ark::Scope<ark::rhi::Sampler> createSampler(const ark::rhi::SamplerDesc& desc) override {
            return ark::makeScope<FakeSampler>(desc);
        }

        ark::Scope<ark::rhi::Shader> createShader(const ark::rhi::ShaderDesc& desc) override {
            return ark::makeScope<FakeShader>(desc);
        }

        ark::Scope<ark::rhi::PipelineLayout> createPipelineLayout(const ark::rhi::PipelineLayoutDesc& desc) override {
            return ark::makeScope<FakePipelineLayout>(desc);
        }

        ark::Scope<ark::rhi::PipelineState> createGraphicsPipeline(const ark::rhi::GraphicsPipelineDesc& desc) override {
            pipelineDescs.push_back(desc);
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

        std::vector<ark::rhi::GraphicsPipelineDesc> pipelineDescs;

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
            ++pipelineBinds;
        }

        void bindDescriptorSet(ark::u32, ark::rhi::DescriptorSet&) override {
            ++descriptorBinds;
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
            ++indexedDraws;
        }

        void pipelineBarrier(std::span<const ark::rhi::ResourceBarrier>) override {
        }

        void clearRenderTarget(ark::rhi::TextureView&, const ark::rhi::ClearColor&) override {
        }

        ark::rhi::FrameResource frame{};
        int pipelineBinds = 0;
        int descriptorBinds = 0;
        int indexedDraws = 0;
    };

    class FakeSwapChain final : public ark::rhi::SwapChain {
    public:
        const ark::rhi::SwapChainDesc& getDesc() const override {
            return m_Desc;
        }

        ark::u32 getBackBufferCount() const override {
            return 1;
        }

        ark::u32 getCurrentBackBufferIndex() const override {
            return 0;
        }

        ark::rhi::TextureView* getCurrentBackBufferView() override {
            return nullptr;
        }

        ark::rhi::TextureView* getDepthBufferView() override {
            return nullptr;
        }

        ark::rhi::AcquireResult acquireNextImage(ark::rhi::FrameResource&) override {
            return {};
        }

        ark::rhi::SwapChainStatus present(ark::rhi::FrameResource&) override {
            return ark::rhi::SwapChainStatus::Ready;
        }

        ark::rhi::SwapChainStatus resize(ark::rhi::Extent2D extent) override {
            m_Desc.extent = extent;
            return ark::rhi::SwapChainStatus::Ready;
        }

    private:
        ark::rhi::SwapChainDesc m_Desc{
            .extent = ark::rhi::Extent2D{800, 600},
            .colorFormat = ark::rhi::Format::BGRA8Unorm,
            .depthFormat = ark::rhi::Format::D32Float,
            .imageCount = 1,
            .enableVSync = false,
        };
    };

    ark::asset::MeshPrimitiveData makeTriangle() {
        ark::asset::MeshVertex v0{};
        v0.position[0] = -0.5f;
        v0.position[1] = -0.5f;
        v0.normal[2] = 1.0f;

        ark::asset::MeshVertex v1 = v0;
        v1.position[0] = 0.5f;
        v1.uv0[0] = 1.0f;

        ark::asset::MeshVertex v2 = v0;
        v2.position[1] = 0.5f;
        v2.uv0[0] = 0.5f;
        v2.uv0[1] = 1.0f;

        ark::asset::MeshPrimitiveData mesh{};
        mesh.debugName = "ForwardPipelineTriangle";
        mesh.vertices = {v0, v1, v2};
        mesh.indices = {0, 1, 2};
        return mesh;
    }

    bool createMaterial(FakeRenderDevice& device,
                        ark::TextureCache& textureCache,
                        ark::MaterialResource& materialResource,
                        ark::asset::AlphaMode alphaMode,
                        bool doubleSided) {
        ark::MaterialTextureSet textures{};
        textures.baseColor = textureCache.getOrCreateFallback(device, ark::FallbackTextureKind::White);
        textures.normal = textureCache.getOrCreateFallback(device, ark::FallbackTextureKind::FlatNormal);
        textures.metallicRoughness =
            textureCache.getOrCreateFallback(device, ark::FallbackTextureKind::MetallicRoughnessDefault);
        textures.occlusion = textureCache.getOrCreateFallback(device, ark::FallbackTextureKind::OcclusionDefault);
        textures.emissive = textureCache.getOrCreateFallback(device, ark::FallbackTextureKind::Black);
        if (!textures.baseColor || !textures.normal || !textures.metallicRoughness ||
            !textures.occlusion || !textures.emissive) {
            return false;
        }

        ark::asset::MaterialData material{};
        material.debugName = "ForwardPipelineMaterial";
        material.baseColorTexturePath = "forward_pass_pipeline_dummy.png";
        material.alphaMode = alphaMode;
        material.doubleSided = doubleSided;
        return materialResource.create(material, textures);
    }

    bool capturePipelineDesc(ark::asset::AlphaMode alphaMode,
                             bool doubleSided,
                             ark::rhi::GraphicsPipelineDesc& pipelineDesc,
                             FakeDeviceContext& context) {
        FakeRenderDevice device{};
        FakeSwapChain swapChain{};
        ark::TextureCache textureCache{};
        ark::MeshResource mesh{};
        ark::MaterialResource material{};

        if (!mesh.create(device, makeTriangle()) ||
            !createMaterial(device, textureCache, material, alphaMode, doubleSided)) {
            std::cerr << "Failed to create ForwardPass pipeline smoke resources\n";
            return false;
        }

        ark::RenderScene scene{};
        scene.addObject(mesh, material, glm::mat4{1.0f}, "ForwardPipelineDraw");
        ark::RenderQueue queue{};
        queue.build(scene);

        ark::RenderView view{};
        view.setDefaultPerspective(swapChain.getDesc().extent);

        ark::ForwardPass pass{};
        pass.setup(device);

        context.frame.frameSlot = 0;
        context.frame.frameIndex = 0;

        ark::FrameContext frameContext{};
        frameContext.frameIndex = 0;
        frameContext.scene = &scene;
        frameContext.view = &view;
        frameContext.queue = &queue;
        frameContext.device = &device;
        frameContext.context = &context;
        frameContext.swapChain = &swapChain;
        frameContext.frameResource = &context.frame;
        frameContext.extent = swapChain.getDesc().extent;

        if (!pass.prepare(frameContext) || !pass.execute(frameContext)) {
            std::cerr << "ForwardPass pipeline smoke failed to execute\n";
            return false;
        }

        if (device.pipelineDescs.size() != 1 || context.pipelineBinds != 1 || context.indexedDraws != 1) {
            std::cerr << "ForwardPass pipeline smoke did not create and draw exactly one pipeline\n";
            return false;
        }

        pipelineDesc = device.pipelineDescs.front();
        return true;
    }

    bool validateDoubleSidedCullModes() {
        FakeDeviceContext singleSidedContext{};
        ark::rhi::GraphicsPipelineDesc singleSidedDesc{};
        if (!capturePipelineDesc(ark::asset::AlphaMode::Opaque, false, singleSidedDesc, singleSidedContext)) {
            return false;
        }

        if (singleSidedDesc.rasterState.cullMode != ark::rhi::CullMode::Back ||
            singleSidedDesc.rasterState.frontFace != ark::rhi::FrontFace::CounterClockwise ||
            !singleSidedDesc.depthStencilState.enableDepthWrite ||
            singleSidedDesc.blendState.colorAttachment.enableBlend) {
            std::cerr << "Single-sided opaque ForwardPass pipeline state is invalid\n";
            return false;
        }

        FakeDeviceContext doubleSidedContext{};
        ark::rhi::GraphicsPipelineDesc doubleSidedDesc{};
        if (!capturePipelineDesc(ark::asset::AlphaMode::Mask, true, doubleSidedDesc, doubleSidedContext)) {
            return false;
        }

        if (doubleSidedDesc.rasterState.cullMode != ark::rhi::CullMode::None ||
            doubleSidedDesc.rasterState.frontFace != ark::rhi::FrontFace::CounterClockwise ||
            !doubleSidedDesc.depthStencilState.enableDepthWrite ||
            doubleSidedDesc.blendState.colorAttachment.enableBlend) {
            std::cerr << "Double-sided mask ForwardPass pipeline state is invalid\n";
            return false;
        }

        FakeDeviceContext blendContext{};
        ark::rhi::GraphicsPipelineDesc blendDesc{};
        if (!capturePipelineDesc(ark::asset::AlphaMode::Blend, false, blendDesc, blendContext)) {
            return false;
        }

        if (blendDesc.rasterState.cullMode != ark::rhi::CullMode::Back ||
            blendDesc.rasterState.frontFace != ark::rhi::FrontFace::CounterClockwise ||
            blendDesc.depthStencilState.enableDepthWrite ||
            !blendDesc.blendState.colorAttachment.enableBlend) {
            std::cerr << "Single-sided blend ForwardPass pipeline state is invalid\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateDoubleSidedCullModes() ? EXIT_SUCCESS : EXIT_FAILURE;
}
