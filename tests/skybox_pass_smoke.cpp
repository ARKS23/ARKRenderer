#include "core/Memory.h"
#include "renderer/EnvironmentCubeResource.h"
#include "renderer/FrameContext.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/effects/sky/SkyboxPass.h"
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

#include <glm/glm.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace {
    struct alignas(16) CapturedSkyboxUniform {
        glm::mat4 inverseProjection{1.0f};
        glm::mat4 inverseViewRotation{1.0f};
        glm::vec4 settings{1.0f, 0.0f, 0.0f, 0.0f};
    };

    static_assert(sizeof(CapturedSkyboxUniform) == 144);

    struct DescriptorSetCapture {
        int uniformBufferUpdates = 0;
        int sampledImageUpdates = 0;
        int samplerUpdates = 0;
        ark::u32 lastUniformBinding = 0;
        ark::u64 lastUniformRange = 0;
        ark::u32 lastSampledImageBinding = 0;
        ark::u32 lastSamplerBinding = 0;
        ark::rhi::TextureView* sampledImageView = nullptr;
        ark::rhi::Sampler* sampler = nullptr;
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
            ++capture.uniformBufferUpdates;
            capture.lastUniformBinding = binding;
            capture.lastUniformRange = buffer.range;
        }

        void updateSampledImage(ark::u32 binding, const ark::rhi::SampledImageDescriptor& image) override {
            ++capture.sampledImageUpdates;
            capture.lastSampledImageBinding = binding;
            capture.sampledImageView = image.view;
        }

        void updateSampler(ark::u32 binding, const ark::rhi::SamplerDescriptor& samplerDescriptor) override {
            ++capture.samplerUpdates;
            capture.lastSamplerBinding = binding;
            capture.sampler = samplerDescriptor.sampler;
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
            if (buffer.getDesc().debugName == "SkyboxUniformBuffer") {
                if (size != sizeof(CapturedSkyboxUniform) || offset != 0) {
                    return false;
                }

                std::memcpy(&lastSkyboxUniform, data, sizeof(CapturedSkyboxUniform));
                ++skyboxUniformUpdates;
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
        CapturedSkyboxUniform lastSkyboxUniform{};
        int skyboxUniformUpdates = 0;
        int pipelineBinds = 0;
        int descriptorBinds = 0;
        int draws = 0;
        ark::rhi::DrawDesc lastDrawDesc{};
    };

    bool validateDescriptorLayout(const ark::rhi::DescriptorSetLayoutDesc& desc) {
        if (desc.debugName != "SkyboxDescriptorSetLayout" || desc.bindings.size() != 3) {
            std::cerr << "SkyboxPass descriptor layout shape is invalid\n";
            return false;
        }

        const ark::rhi::DescriptorBindingDesc& uniformBinding = desc.bindings[0];
        const ark::rhi::DescriptorBindingDesc& cubeBinding = desc.bindings[1];
        const ark::rhi::DescriptorBindingDesc& samplerBinding = desc.bindings[2];
        if (uniformBinding.binding != 0 ||
            uniformBinding.type != ark::rhi::DescriptorType::UniformBuffer ||
            !ark::rhi::hasShaderStage(uniformBinding.stages, ark::rhi::ShaderStageFlags::Fragment) ||
            cubeBinding.binding != 1 ||
            cubeBinding.type != ark::rhi::DescriptorType::SampledImage ||
            !ark::rhi::hasShaderStage(cubeBinding.stages, ark::rhi::ShaderStageFlags::Fragment) ||
            samplerBinding.binding != 2 ||
            samplerBinding.type != ark::rhi::DescriptorType::Sampler ||
            !ark::rhi::hasShaderStage(samplerBinding.stages, ark::rhi::ShaderStageFlags::Fragment)) {
            std::cerr << "SkyboxPass descriptor bindings are invalid\n";
            return false;
        }

        return true;
    }

    bool validateSetupResources(const FakeRenderDevice& device) {
        if (!validateDescriptorLayout(device.descriptorSetLayoutDesc)) {
            return false;
        }

        if (device.bufferDescs.size() != 2 || device.descriptorSets.size() != 2 ||
            device.shaderDescs.size() != 2 || device.pipelineLayoutDescs.size() != 1) {
            std::cerr << "SkyboxPass setup did not create expected resources\n";
            return false;
        }

        for (const ark::rhi::BufferDesc& bufferDesc : device.bufferDescs) {
            if (bufferDesc.debugName != "SkyboxUniformBuffer" ||
                bufferDesc.size != sizeof(CapturedSkyboxUniform) ||
                bufferDesc.usage != ark::rhi::BufferUsage::Uniform ||
                bufferDesc.memoryUsage != ark::rhi::MemoryUsage::CpuToGpu) {
                std::cerr << "SkyboxPass uniform buffer desc is invalid\n";
                return false;
            }
        }

        for (const FakeDescriptorSet* descriptorSet : device.descriptorSets) {
            if (descriptorSet->capture.uniformBufferUpdates != 1 ||
                descriptorSet->capture.lastUniformBinding != 0 ||
                descriptorSet->capture.lastUniformRange != sizeof(CapturedSkyboxUniform)) {
                std::cerr << "SkyboxPass descriptor set uniform binding is invalid\n";
                return false;
            }
        }

        return true;
    }

    bool validateNoCubemapPath() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};

        ark::SkyboxPass pass{};
        pass.setup(device);

        ark::FrameContext frameContext{};
        frameContext.context = &context;
        frameContext.colorFormat = ark::rhi::Format::RGBA16Float;
        frameContext.depthFormat = ark::rhi::Format::D32Float;
        frameContext.frameResource = &context.frame;

        if (!pass.execute(frameContext)) {
            std::cerr << "SkyboxPass no-cubemap path failed\n";
            return false;
        }

        if (!validateSetupResources(device)) {
            return false;
        }

        if (!device.pipelineDescs.empty() || context.skyboxUniformUpdates != 0 ||
            context.pipelineBinds != 0 || context.descriptorBinds != 0 || context.draws != 0) {
            std::cerr << "SkyboxPass should no-op when no cubemap is provided\n";
            return false;
        }

        return true;
    }

    bool validateCubemapDrawPath() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};

        ark::EnvironmentCubeResource cube{};
        ark::EnvironmentCubeResourceDesc cubeDesc{};
        cubeDesc.debugName = "SkyboxSmokeCube";
        cubeDesc.faceExtent = ark::rhi::Extent2D{32, 32};
        cubeDesc.format = ark::rhi::Format::RGBA16Float;
        if (!cube.create(device, cubeDesc)) {
            std::cerr << "Failed to create skybox smoke cubemap\n";
            return false;
        }

        ark::RenderView view{};
        view.setDefaultPerspective(ark::rhi::Extent2D{800, 600});

        ark::SkyboxPass pass{};
        pass.setup(device);

        context.frame.frameSlot = 1;
        context.frame.frameIndex = 1;

        ark::FrameContext frameContext{};
        frameContext.context = &context;
        frameContext.view = &view;
        frameContext.environmentCube = &cube;
        frameContext.colorFormat = ark::rhi::Format::RGBA16Float;
        frameContext.depthFormat = ark::rhi::Format::D32Float;
        frameContext.frameResource = &context.frame;

        if (!pass.execute(frameContext)) {
            std::cerr << "SkyboxPass cubemap path failed\n";
            return false;
        }

        if (!validateSetupResources(device)) {
            return false;
        }

        if (device.pipelineDescs.size() != 1 ||
            context.skyboxUniformUpdates != 1 ||
            context.pipelineBinds != 1 ||
            context.descriptorBinds != 1 ||
            context.draws != 1 ||
            context.lastDrawDesc.vertexCount != 3 ||
            context.lastDrawDesc.instanceCount != 1) {
            std::cerr << "SkyboxPass did not bind and draw exactly once\n";
            return false;
        }

        const ark::rhi::GraphicsPipelineDesc& pipelineDesc = device.pipelineDescs.front();
        if (pipelineDesc.colorFormat != ark::rhi::Format::RGBA16Float ||
            pipelineDesc.depthFormat != ark::rhi::Format::D32Float ||
            pipelineDesc.topology != ark::rhi::PrimitiveTopology::TriangleList ||
            pipelineDesc.rasterState.cullMode != ark::rhi::CullMode::None ||
            pipelineDesc.depthStencilState.enableDepthTest ||
            pipelineDesc.depthStencilState.enableDepthWrite ||
            pipelineDesc.blendState.colorAttachment.enableBlend) {
            std::cerr << "SkyboxPass pipeline state is invalid\n";
            return false;
        }

        const FakeDescriptorSet* selectedDescriptorSet = device.descriptorSets[1];
        if (selectedDescriptorSet->capture.sampledImageUpdates != 1 ||
            selectedDescriptorSet->capture.lastSampledImageBinding != 1 ||
            selectedDescriptorSet->capture.sampledImageView != cube.textureView() ||
            selectedDescriptorSet->capture.samplerUpdates != 1 ||
            selectedDescriptorSet->capture.lastSamplerBinding != 2 ||
            selectedDescriptorSet->capture.sampler != cube.sampler()) {
            std::cerr << "SkyboxPass did not bind cubemap descriptors correctly\n";
            return false;
        }

        if (context.lastSkyboxUniform.settings.x != 1.0f) {
            std::cerr << "SkyboxPass uniform settings are invalid\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateNoCubemapPath() && validateCubemapDrawPath() ? EXIT_SUCCESS : EXIT_FAILURE;
}
