#include "renderer/RenderQueue.h"
#include "renderer/SceneResource.h"
#include "rhi/Buffer.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DescriptorSetLayout.h"
#include "rhi/Fence.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RenderDevice.h"
#include "rhi/Sampler.h"
#include "rhi/Shader.h"
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

    bool validateExplicitModelAndProceduralEnvironment() {
        FakeRenderDevice device{};

        ark::SceneResourceLoadDesc desc{};
        desc.modelPath = "assets/models/material_ball_validation_fixture.gltf";
        desc.modelFallback = ark::SceneModelFallbackPolicy::None;
        desc.environmentFallback = ark::SceneEnvironmentFallbackPolicy::ProceduralOnly;
        desc.modelName = "MaterialBallSceneResource";

        ark::SceneResource sceneResource{};
        if (!sceneResource.load(device, desc)) {
            std::cerr << "SceneResource failed to load explicit material ball scene\n";
            return false;
        }

        const ark::SceneResourceLoadReport& report = sceneResource.report();
        if (!report.modelLoaded || !report.environmentLoaded ||
            report.modelSource != ark::SceneModelSource::RequestedPath ||
            report.environmentSource != ark::SceneEnvironmentSource::Procedural ||
            report.resolvedModelPath.filename() != "material_ball_validation_fixture.gltf" ||
            sceneResource.scene().size() != 1 ||
            !sceneResource.scene().environment().isEnabled() ||
            !sceneResource.model() ||
            !sceneResource.environment()) {
            std::cerr << "SceneResource explicit model report or scene state is invalid\n";
            return false;
        }

        ark::RenderQueue queue{};
        queue.build(sceneResource.scene());
        if (queue.size() != 15) {
            std::cerr << "SceneResource material ball render queue size is invalid\n";
            return false;
        }

        sceneResource.resetImmediate();
        if (sceneResource.hasScene() || sceneResource.scene().environment().isEnabled() ||
            sceneResource.model() || sceneResource.environment()) {
            std::cerr << "SceneResource reset state is invalid\n";
            return false;
        }

        return true;
    }

    bool validateMissingExplicitModelFallsBack() {
        FakeRenderDevice device{};

        ark::SceneResourceLoadDesc desc{};
        desc.modelPath = "assets/models/not_a_real_scene_resource_fixture.gltf";
        desc.modelFallback = ark::SceneModelFallbackPolicy::DefaultSandboxModel;
        desc.environmentFallback = ark::SceneEnvironmentFallbackPolicy::ProceduralOnly;

        ark::SceneResource sceneResource{};
        if (!sceneResource.load(device, desc)) {
            std::cerr << "SceneResource failed to load default model fallback\n";
            return false;
        }

        const ark::SceneResourceLoadReport& report = sceneResource.report();
        if (!report.modelLoaded || report.modelSource != ark::SceneModelSource::DefaultFallback ||
            report.resolvedModelPath.empty() || !sceneResource.hasScene()) {
            std::cerr << "SceneResource default model fallback report is invalid\n";
            return false;
        }

        ark::RenderQueue queue{};
        queue.build(sceneResource.scene());
        if (queue.size() == 0) {
            std::cerr << "SceneResource default model fallback produced an empty queue\n";
            return false;
        }

        return true;
    }

    bool validateDebugOrientationEnvironment() {
        FakeRenderDevice device{};

        ark::SceneResourceLoadDesc desc{};
        desc.modelPath = "assets/models/forward_multinode_fixture.gltf";
        desc.modelFallback = ark::SceneModelFallbackPolicy::None;
        desc.environmentFallback = ark::SceneEnvironmentFallbackPolicy::DebugOrientation;

        ark::SceneResource sceneResource{};
        if (!sceneResource.load(device, desc)) {
            std::cerr << "SceneResource failed to load debug orientation environment\n";
            return false;
        }

        const ark::SceneResourceLoadReport& report = sceneResource.report();
        if (!report.environmentLoaded ||
            report.environmentSource != ark::SceneEnvironmentSource::DebugOrientation ||
            !report.resolvedEnvironmentPath.empty() ||
            !sceneResource.scene().environment().isEnabled()) {
            std::cerr << "SceneResource debug orientation environment report is invalid\n";
            return false;
        }

        return true;
    }

    bool validateMissingEnvironmentFallback() {
        FakeRenderDevice device{};

        ark::SceneResourceLoadDesc desc{};
        desc.modelPath = "assets/models/forward_multinode_fixture.gltf";
        desc.modelFallback = ark::SceneModelFallbackPolicy::None;
        desc.environmentPath = "assets/HDR/not_a_real_environment.hdr";
        desc.environmentFallback = ark::SceneEnvironmentFallbackPolicy::DefaultHdrThenProcedural;

        ark::SceneResource sceneResource{};
        if (!sceneResource.load(device, desc)) {
            std::cerr << "SceneResource failed to load missing environment fallback\n";
            return false;
        }

        const ark::SceneResourceLoadReport& report = sceneResource.report();
        const bool allowedFallback = report.environmentSource == ark::SceneEnvironmentSource::DefaultHdr ||
                                     report.environmentSource == ark::SceneEnvironmentSource::Procedural;
        if (!report.environmentLoaded || !allowedFallback || !sceneResource.scene().environment().isEnabled()) {
            std::cerr << "SceneResource missing environment fallback report is invalid\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateExplicitModelAndProceduralEnvironment() &&
                   validateMissingExplicitModelFallsBack() &&
                   validateDebugOrientationEnvironment() &&
                   validateMissingEnvironmentFallback()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
