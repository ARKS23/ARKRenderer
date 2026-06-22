#include "core/Memory.h"
#include "renderer/core/FrameContext.h"
#include "renderer/RenderView.h"
#include "renderer/effects/tone_mapping/ToneMappingPass.h"
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
#include "rhi/SwapChain.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace {
    struct alignas(16) CapturedToneMappingUniform {
        float exposure = 0.0f;
        float inverseOutputGamma = 0.0f;
        float operatorType = 0.0f;
        float padding1 = 0.0f;
    };

    static_assert(sizeof(CapturedToneMappingUniform) == 16);

    struct DescriptorSetCapture {
        int uniformBufferUpdates = 0;
        int sampledImageUpdates = 0;
        int samplerUpdates = 0;
        ark::u32 lastUniformBinding = 0;
        ark::u64 lastUniformRange = 0;
        ark::u32 lastSampledImageBinding = 0;
        ark::u32 lastSamplerBinding = 0;
    };

    struct ExecutionCapture {
        CapturedToneMappingUniform uniform{};
        int toneMappingUniformUpdates = 0;
        int pipelineBinds = 0;
        int descriptorBinds = 0;
        int draws = 0;
        ark::rhi::DrawDesc lastDrawDesc{};
        std::vector<ark::rhi::BufferDesc> bufferDescs;
        ark::rhi::DescriptorSetLayoutDesc descriptorSetLayoutDesc{};
        std::vector<DescriptorSetCapture> descriptorSets;
        std::vector<ark::rhi::GraphicsPipelineDesc> pipelineDescs;
        ark::rhi::SamplerDesc samplerDesc{};
        bool samplerCreated = false;
    };

    bool near(float a, float b, float epsilon = 0.0001f) {
        return std::fabs(a - b) <= epsilon;
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
        void updateUniformBuffer(ark::u32 binding, const ark::rhi::BufferDescriptor& buffer) override {
            ++m_Capture.uniformBufferUpdates;
            m_Capture.lastUniformBinding = binding;
            m_Capture.lastUniformRange = buffer.range;
        }

        void updateSampledImage(ark::u32 binding, const ark::rhi::SampledImageDescriptor&) override {
            ++m_Capture.sampledImageUpdates;
            m_Capture.lastSampledImageBinding = binding;
        }

        void updateSampler(ark::u32 binding, const ark::rhi::SamplerDescriptor&) override {
            ++m_Capture.samplerUpdates;
            m_Capture.lastSamplerBinding = binding;
        }

        const DescriptorSetCapture& capture() const {
            return m_Capture;
        }

    private:
        DescriptorSetCapture m_Capture{};
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
            return ark::makeScope<FakeTexture>(desc);
        }

        ark::Scope<ark::rhi::TextureView> createTextureView(ark::rhi::Texture& texture,
                                                            const ark::rhi::TextureViewDesc& desc) override {
            return ark::makeScope<FakeTextureView>(texture, desc);
        }

        ark::Scope<ark::rhi::Sampler> createSampler(const ark::rhi::SamplerDesc& desc) override {
            samplerDesc = desc;
            samplerCreated = true;
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
        ark::rhi::DescriptorSetLayoutDesc descriptorSetLayoutDesc{};
        std::vector<FakeDescriptorSet*> descriptorSets;
        std::vector<ark::rhi::ShaderDesc> shaderDescs;
        std::vector<ark::rhi::GraphicsPipelineDesc> pipelineDescs;
        ark::rhi::SamplerDesc samplerDesc{};
        bool samplerCreated = false;

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

        bool updateBuffer(ark::rhi::Buffer& buffer, const void* data, ark::u64 size, ark::u64 offset = 0) override {
            if (buffer.getDesc().debugName == "ToneMappingUniformBuffer") {
                if (size != sizeof(CapturedToneMappingUniform) || offset != 0) {
                    return false;
                }

                std::memcpy(&lastToneMappingUniform, data, sizeof(CapturedToneMappingUniform));
                ++toneMappingUniformUpdates;
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
            lastDrawDesc = desc;
            ++draws;
        }

        void drawIndexed(const ark::rhi::DrawIndexedDesc&) override {
        }

        void pipelineBarrier(std::span<const ark::rhi::ResourceBarrier>) override {
        }

        void clearRenderTarget(ark::rhi::TextureView&, const ark::rhi::ClearColor&) override {
        }

        ark::rhi::FrameResource frame{};
        CapturedToneMappingUniform lastToneMappingUniform{};
        int toneMappingUniformUpdates = 0;
        int pipelineBinds = 0;
        int descriptorBinds = 0;
        int draws = 0;
        ark::rhi::DrawDesc lastDrawDesc{};
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

    bool runToneMappingPass(const ark::ToneMappingSettings* settings, ark::u32 frameSlot, ExecutionCapture& capture) {
        FakeRenderDevice device{};
        FakeDeviceContext context{};
        FakeSwapChain swapChain{};

        ark::rhi::TextureDesc sceneColorDesc{};
        sceneColorDesc.extent = swapChain.getDesc().extent;
        sceneColorDesc.format = ark::rhi::Format::RGBA16Float;
        sceneColorDesc.usage = ark::rhi::TextureUsage::RenderTarget | ark::rhi::TextureUsage::ShaderResource;
        FakeTexture sceneColorTexture{sceneColorDesc};

        ark::rhi::TextureViewDesc sceneColorViewDesc{};
        sceneColorViewDesc.format = ark::rhi::Format::RGBA16Float;
        FakeTextureView sceneColorView{sceneColorTexture, sceneColorViewDesc};

        ark::RenderView view{};
        view.setDefaultPerspective(swapChain.getDesc().extent);
        if (settings) {
            view.setToneMappingSettings(*settings);
        }

        ark::ToneMappingPass pass{};
        pass.setup(device);

        context.frame.frameSlot = frameSlot;
        context.frame.frameIndex = frameSlot;

        ark::FrameContext frameContext{};
        frameContext.frameIndex = frameSlot;
        frameContext.view = settings ? &view : nullptr;
        frameContext.device = &device;
        frameContext.context = &context;
        frameContext.swapChain = &swapChain;
        frameContext.frameResource = &context.frame;
        frameContext.sceneColorView = &sceneColorView;
        frameContext.extent = swapChain.getDesc().extent;
        frameContext.colorFormat = ark::rhi::Format::BGRA8Unorm;
        frameContext.depthFormat = ark::rhi::Format::Unknown;

        if (!pass.execute(frameContext)) {
            std::cerr << "ToneMappingPass smoke failed to execute\n";
            return false;
        }

        capture.uniform = context.lastToneMappingUniform;
        capture.toneMappingUniformUpdates = context.toneMappingUniformUpdates;
        capture.pipelineBinds = context.pipelineBinds;
        capture.descriptorBinds = context.descriptorBinds;
        capture.draws = context.draws;
        capture.lastDrawDesc = context.lastDrawDesc;
        capture.bufferDescs = device.bufferDescs;
        capture.descriptorSetLayoutDesc = device.descriptorSetLayoutDesc;
        capture.pipelineDescs = device.pipelineDescs;
        capture.samplerDesc = device.samplerDesc;
        capture.samplerCreated = device.samplerCreated;
        capture.descriptorSets.clear();
        for (const FakeDescriptorSet* descriptorSet : device.descriptorSets) {
            capture.descriptorSets.push_back(descriptorSet->capture());
        }

        return true;
    }

    bool validateDescriptorLayout(const ark::rhi::DescriptorSetLayoutDesc& desc) {
        if (desc.debugName != "ToneMappingDescriptorSetLayout" || desc.bindings.size() != 3) {
            std::cerr << "ToneMappingPass descriptor layout shape is invalid\n";
            return false;
        }

        const ark::rhi::DescriptorBindingDesc& sceneColorBinding = desc.bindings[0];
        const ark::rhi::DescriptorBindingDesc& samplerBinding = desc.bindings[1];
        const ark::rhi::DescriptorBindingDesc& uniformBinding = desc.bindings[2];
        if (sceneColorBinding.binding != 0 ||
            sceneColorBinding.type != ark::rhi::DescriptorType::SampledImage ||
            !ark::rhi::hasShaderStage(sceneColorBinding.stages, ark::rhi::ShaderStageFlags::Fragment) ||
            samplerBinding.binding != 1 ||
            samplerBinding.type != ark::rhi::DescriptorType::Sampler ||
            !ark::rhi::hasShaderStage(samplerBinding.stages, ark::rhi::ShaderStageFlags::Fragment) ||
            uniformBinding.binding != 2 ||
            uniformBinding.type != ark::rhi::DescriptorType::UniformBuffer ||
            !ark::rhi::hasShaderStage(uniformBinding.stages, ark::rhi::ShaderStageFlags::Fragment)) {
            std::cerr << "ToneMappingPass descriptor bindings are invalid\n";
            return false;
        }

        return true;
    }

    bool validateCommonExecution(const ExecutionCapture& capture, ark::u32 frameSlot) {
        if (capture.toneMappingUniformUpdates != 1 ||
            capture.pipelineBinds != 1 ||
            capture.descriptorBinds != 1 ||
            capture.draws != 1 ||
            capture.lastDrawDesc.vertexCount != 3 ||
            capture.lastDrawDesc.instanceCount != 1) {
            std::cerr << "ToneMappingPass did not update, bind, and draw exactly once\n";
            return false;
        }

        if (capture.bufferDescs.size() != 2) {
            std::cerr << "ToneMappingPass did not create one uniform buffer per frame slot\n";
            return false;
        }

        for (const ark::rhi::BufferDesc& bufferDesc : capture.bufferDescs) {
            if (bufferDesc.debugName != "ToneMappingUniformBuffer" ||
                bufferDesc.size != sizeof(CapturedToneMappingUniform) ||
                bufferDesc.usage != ark::rhi::BufferUsage::Uniform ||
                bufferDesc.memoryUsage != ark::rhi::MemoryUsage::CpuToGpu) {
                std::cerr << "ToneMappingPass uniform buffer desc is invalid\n";
                return false;
            }
        }

        if (!validateDescriptorLayout(capture.descriptorSetLayoutDesc)) {
            return false;
        }

        if (capture.descriptorSets.size() != 2) {
            std::cerr << "ToneMappingPass did not create one descriptor set per frame slot\n";
            return false;
        }

        const ark::u32 selectedFrameSlot = frameSlot % 2;
        const DescriptorSetCapture& selectedDescriptorSet = capture.descriptorSets[selectedFrameSlot];
        if (selectedDescriptorSet.uniformBufferUpdates != 1 ||
            selectedDescriptorSet.lastUniformBinding != 2 ||
            selectedDescriptorSet.lastUniformRange != sizeof(CapturedToneMappingUniform) ||
            selectedDescriptorSet.sampledImageUpdates != 1 ||
            selectedDescriptorSet.lastSampledImageBinding != 0 ||
            selectedDescriptorSet.samplerUpdates != 1 ||
            selectedDescriptorSet.lastSamplerBinding != 1) {
            std::cerr << "ToneMappingPass did not update the selected frame descriptor set correctly\n";
            return false;
        }

        if (!capture.samplerCreated ||
            capture.samplerDesc.addressU != ark::rhi::AddressMode::ClampToEdge ||
            capture.samplerDesc.addressV != ark::rhi::AddressMode::ClampToEdge ||
            capture.samplerDesc.addressW != ark::rhi::AddressMode::ClampToEdge) {
            std::cerr << "ToneMappingPass scene color sampler is invalid\n";
            return false;
        }

        if (capture.pipelineDescs.size() != 1) {
            std::cerr << "ToneMappingPass did not create exactly one pipeline\n";
            return false;
        }

        const ark::rhi::GraphicsPipelineDesc& pipelineDesc = capture.pipelineDescs.front();
        if (pipelineDesc.colorFormat != ark::rhi::Format::BGRA8Unorm ||
            pipelineDesc.depthFormat != ark::rhi::Format::Unknown ||
            pipelineDesc.topology != ark::rhi::PrimitiveTopology::TriangleList ||
            pipelineDesc.rasterState.cullMode != ark::rhi::CullMode::None ||
            pipelineDesc.depthStencilState.enableDepthTest ||
            pipelineDesc.depthStencilState.enableDepthWrite ||
            pipelineDesc.blendState.colorAttachment.enableBlend) {
            std::cerr << "ToneMappingPass pipeline state is invalid\n";
            return false;
        }

        return true;
    }

    bool validateToneMappingSettingsUniform() {
        ark::ToneMappingSettings settings{};
        settings.exposure = 1.75f;
        settings.outputGamma = 2.0f;
        settings.operatorType = ark::ToneMappingOperator::ACES;

        ExecutionCapture capture{};
        if (!runToneMappingPass(&settings, 1, capture)) {
            return false;
        }

        if (!validateCommonExecution(capture, 1)) {
            return false;
        }

        if (!near(capture.uniform.exposure, 1.75f) ||
            !near(capture.uniform.inverseOutputGamma, 0.5f) ||
            !near(capture.uniform.operatorType, 2.0f) ||
            !near(capture.uniform.padding1, 0.0f)) {
            std::cerr << "ToneMappingPass uniform did not preserve RenderView tone mapping settings\n";
            return false;
        }

        return true;
    }

    bool validateToneMappingSettingsFallbacks() {
        ark::ToneMappingSettings settings{};
        settings.exposure = -5.0f;
        settings.outputGamma = 0.0f;
        settings.operatorType = ark::ToneMappingOperator::Linear;

        ExecutionCapture capture{};
        if (!runToneMappingPass(&settings, 0, capture)) {
            return false;
        }

        if (!validateCommonExecution(capture, 0)) {
            return false;
        }

        if (!near(capture.uniform.exposure, 0.0f) ||
            !near(capture.uniform.inverseOutputGamma, 1.0f / 2.2f) ||
            !near(capture.uniform.operatorType, 1.0f)) {
            std::cerr << "ToneMappingPass uniform did not clamp invalid tone mapping settings\n";
            return false;
        }

        ExecutionCapture defaultCapture{};
        if (!runToneMappingPass(nullptr, 0, defaultCapture)) {
            return false;
        }

        if (!near(defaultCapture.uniform.exposure, 1.0f) ||
            !near(defaultCapture.uniform.inverseOutputGamma, 1.0f / 2.2f) ||
            !near(defaultCapture.uniform.operatorType, 0.0f)) {
            std::cerr << "ToneMappingPass uniform did not use default settings when FrameContext has no view\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateToneMappingSettingsUniform() && validateToneMappingSettingsFallbacks() ? EXIT_SUCCESS
                                                                                          : EXIT_FAILURE;
}
