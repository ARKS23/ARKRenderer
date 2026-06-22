#include "asset/MeshData.h"
#include "core/Memory.h"
#include "renderer/FrameContext.h"
#include "renderer/MeshResource.h"
#include "renderer/RenderQueue.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/TextureCache.h"
#include "renderer/material/MaterialResource.h"
#include "renderer/effects/shadow/ShadowPass.h"
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

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
    struct alignas(16) CapturedShadowUniform {
        glm::mat4 lightViewProjection{1.0f};
        glm::mat4 model{1.0f};
    };

    static_assert(sizeof(CapturedShadowUniform) == 128);

    bool near(float lhs, float rhs, float epsilon = 0.0001f) {
        return std::abs(lhs - rhs) <= epsilon;
    }

    glm::vec3 projectPoint(const glm::mat4& matrix, const glm::vec3& point) {
        const glm::vec4 clip = matrix * glm::vec4{point, 1.0f};
        return glm::vec3{clip} / clip.w;
    }

    glm::vec2 projectShadowTexel(const glm::mat4& matrix,
                                 const glm::vec3& point,
                                 ark::u32 mapExtent) {
        const glm::vec3 ndc = projectPoint(matrix, point);
        return (glm::vec2{ndc.x, ndc.y} * 0.5f + glm::vec2{0.5f}) *
               static_cast<float>(mapExtent);
    }

    bool isProjectedPointTexelAligned(const glm::mat4& matrix,
                                      const glm::vec3& point,
                                      ark::u32 mapExtent,
                                      float epsilon = 0.001f) {
        const glm::vec2 shadowTexel = projectShadowTexel(matrix, point, mapExtent);
        return near(shadowTexel.x, std::round(shadowTexel.x), epsilon) &&
               near(shadowTexel.y, std::round(shadowTexel.y), epsilon);
    }

    bool containsBinding(const std::vector<ark::rhi::DescriptorBindingDesc>& bindings,
                         ark::u32 binding,
                         ark::rhi::DescriptorType type,
                         ark::rhi::ShaderStageFlags stages) {
        for (const ark::rhi::DescriptorBindingDesc& desc : bindings) {
            if (desc.binding == binding && desc.type == type && desc.count == 1 && desc.stages == stages) {
                return true;
            }
        }

        return false;
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
            uniformBindings.push_back(binding);
            if (binding == 0) {
                shadowUniformBuffer = buffer.buffer;
                shadowUniformRange = buffer.range;
            }
        }

        void updateSampledImage(ark::u32, const ark::rhi::SampledImageDescriptor&) override {
        }

        void updateSampler(ark::u32, const ark::rhi::SamplerDescriptor&) override {
        }

        std::vector<ark::u32> uniformBindings;
        ark::rhi::Buffer* shadowUniformBuffer = nullptr;
        ark::u64 shadowUniformRange = 0;
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
            ++beginRenderingCalls;
            lastRenderingDesc = desc;
            depthAttachmentViews.push_back(desc.depthStencilAttachment.view);
            return true;
        }

        void endRendering() override {
            ++endRenderingCalls;
        }

        void setViewport(const ark::rhi::Viewport& viewport) override {
            lastViewport = viewport;
        }

        void setScissorRect(const ark::rhi::ScissorRect& rect) override {
            lastScissor = rect;
        }

        void setPipeline(ark::rhi::PipelineState&) override {
            ++pipelineBinds;
        }

        void bindDescriptorSet(ark::u32 setIndex, ark::rhi::DescriptorSet&) override {
            lastDescriptorSetIndex = setIndex;
            ++descriptorBinds;
        }

        bool updateBuffer(ark::rhi::Buffer& buffer, const void* data, ark::u64 size, ark::u64 = 0) override {
            const std::string& debugName = buffer.getDesc().debugName;
            if (debugName.rfind("ShadowUniformBuffer", 0) == 0 &&
                size == sizeof(CapturedShadowUniform)) {
                std::memcpy(&lastShadowUniform, data, sizeof(CapturedShadowUniform));
                ++shadowUniformUpdates;
            }

            return true;
        }

        bool uploadTextureData(const ark::rhi::TextureUploadDesc&) override {
            ++textureUploads;
            return true;
        }

        bool generateTextureMips(ark::rhi::Texture&) override {
            return true;
        }

        bool uploadBufferData(const ark::rhi::BufferUploadDesc&) override {
            ++bufferUploads;
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
            ++vertexBufferBinds;
        }

        void setIndexBuffer(ark::rhi::Buffer&, ark::rhi::IndexType = ark::rhi::IndexType::UInt32, ark::u64 = 0) override {
            ++indexBufferBinds;
        }

        void draw(const ark::rhi::DrawDesc&) override {
        }

        void drawIndexed(const ark::rhi::DrawIndexedDesc& desc) override {
            lastDrawIndexed = desc;
            ++indexedDraws;
        }

        void pipelineBarrier(std::span<const ark::rhi::ResourceBarrier> barriers) override {
            for (const ark::rhi::ResourceBarrier& barrier : barriers) {
                barrierAfterStates.push_back(barrier.after);
                if (auto* texture = dynamic_cast<FakeTexture*>(barrier.texture)) {
                    texture->setState(barrier.after);
                }
            }
        }

        void clearRenderTarget(ark::rhi::TextureView&, const ark::rhi::ClearColor&) override {
        }

        ark::rhi::FrameResource frame{};
        ark::rhi::RenderingDesc lastRenderingDesc{};
        ark::rhi::Viewport lastViewport{};
        ark::rhi::ScissorRect lastScissor{};
        ark::rhi::DrawIndexedDesc lastDrawIndexed{};
        CapturedShadowUniform lastShadowUniform{};
        std::vector<ark::rhi::TextureView*> depthAttachmentViews;
        std::vector<ark::rhi::ResourceState> barrierAfterStates;
        int beginRenderingCalls = 0;
        int endRenderingCalls = 0;
        int pipelineBinds = 0;
        int descriptorBinds = 0;
        int shadowUniformUpdates = 0;
        int textureUploads = 0;
        int bufferUploads = 0;
        int deferredTextureReleases = 0;
        int deferredTextureViewReleases = 0;
        int deferredSamplerReleases = 0;
        int vertexBufferBinds = 0;
        int indexBufferBinds = 0;
        int indexedDraws = 0;
        ark::u32 lastDescriptorSetIndex = 99;
    };

    ark::asset::MeshPrimitiveData makeTriangle() {
        ark::asset::MeshVertex v0{};
        v0.position[0] = -0.5f;
        v0.position[1] = -0.5f;
        v0.normal[1] = 1.0f;

        ark::asset::MeshVertex v1 = v0;
        v1.position[0] = 0.5f;

        ark::asset::MeshVertex v2 = v0;
        v2.position[1] = 0.5f;

        ark::asset::MeshPrimitiveData mesh{};
        mesh.debugName = "ShadowPassTriangle";
        mesh.vertices = {v0, v1, v2};
        mesh.indices = {0, 1, 2};
        return mesh;
    }

    bool createMaterial(FakeRenderDevice& device,
                        ark::TextureCache& textureCache,
                        ark::MaterialResource& material,
                        ark::asset::AlphaMode alphaMode = ark::asset::AlphaMode::Opaque) {
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

        ark::asset::MaterialData materialData{};
        materialData.debugName = "ShadowPassMaterial";
        materialData.baseColorTexturePath = "shadow_pass_dummy.png";
        materialData.alphaMode = alphaMode;
        return material.create(materialData, textures);
    }

    bool validateShadowPassDisabledPath() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};
        ark::ShadowPass pass{};
        pass.setup(device);

        ark::RenderView view{};
        ark::FrameContext frameContext{};
        frameContext.view = &view;
        frameContext.context = &context;
        frameContext.frameResource = &context.frame;
        frameContext.shadowMapView = reinterpret_cast<ark::rhi::TextureView*>(0x1);
        frameContext.shadowSampler = reinterpret_cast<ark::rhi::Sampler*>(0x1);
        frameContext.shadowStrength = 1.0f;
        frameContext.shadowBias = 0.2f;
        frameContext.shadowFilterMode = static_cast<float>(ark::ShadowFilterMode::Pcf5x5);
        frameContext.shadowFilterRadiusTexels = 4.0f;
        frameContext.cascadeShadows.enabled = true;
        frameContext.cascadeShadows.cascadeCount = 4;

        if (!pass.prepare(frameContext) || !pass.execute(frameContext)) {
            std::cerr << "ShadowPass disabled path failed\n";
            return false;
        }

        if (frameContext.shadowMapView || frameContext.shadowSampler ||
            frameContext.shadowStrength != 0.0f ||
            !near(frameContext.shadowBias, ark::ShadowSettings{}.bias) ||
            frameContext.shadowFilterMode != static_cast<float>(ark::ShadowFilterMode::Hard) ||
            frameContext.shadowFilterRadiusTexels != 0.0f ||
            frameContext.cascadeShadows.isEnabled() ||
            context.beginRenderingCalls != 0 ||
            context.indexedDraws != 0) {
            std::cerr << "ShadowPass disabled path should clear frame shadow bindings without rendering\n";
            return false;
        }

        return true;
    }

    bool validateShadowPassDepthRender() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};
        ark::TextureCache textureCache{};
        ark::MeshResource mesh{};
        ark::MaterialResource material{};
        ark::RenderScene scene{};
        ark::RenderQueue queue{};
        ark::RenderView view{};
        ark::ShadowPass pass{};

        if (!mesh.create(device, makeTriangle()) ||
            !createMaterial(device, textureCache, material)) {
            std::cerr << "Failed to create ShadowPass smoke draw resources\n";
            return false;
        }

        scene.addObject(mesh, material, glm::mat4{1.0f}, "ShadowPassDraw");
        queue.build(scene);

        view.setDefaultPerspective(ark::rhi::Extent2D{1280, 720});
        ark::ShadowSettings shadows{};
        shadows.enabled = true;
        shadows.strength = 0.55f;
        shadows.bias = 0.003f;
        shadows.mapExtent = 512;
        shadows.orthographicHalfExtent = 18.0f;
        shadows.lightDistance = 32.0f;
        shadows.filterMode = ark::ShadowFilterMode::Pcf5x5;
        shadows.filterRadiusTexels = 2.0f;
        view.setShadowSettings(shadows);

        pass.setup(device);

        context.frame.frameSlot = 0;
        context.frame.frameIndex = 0;

        ark::FrameContext frameContext{};
        frameContext.scene = &scene;
        frameContext.view = &view;
        frameContext.queue = &queue;
        frameContext.device = &device;
        frameContext.context = &context;
        frameContext.frameResource = &context.frame;

        if (!pass.prepare(frameContext) || !pass.execute(frameContext)) {
            std::cerr << "ShadowPass failed to prepare or execute\n";
            return false;
        }

        if (device.descriptorSetLayoutDescs.empty() ||
            !containsBinding(device.descriptorSetLayoutDescs.front().bindings,
                             0,
                             ark::rhi::DescriptorType::UniformBuffer,
                             ark::rhi::ShaderStageFlags::Vertex)) {
            std::cerr << "ShadowPass descriptor layout is invalid\n";
            return false;
        }

        if (device.pipelineDescs.empty()) {
            std::cerr << "ShadowPass did not create a depth pipeline\n";
            return false;
        }

        const ark::rhi::GraphicsPipelineDesc& pipelineDesc = device.pipelineDescs.front();
        if (pipelineDesc.colorFormat != ark::rhi::Format::Unknown ||
            pipelineDesc.depthFormat != ark::rhi::Format::D32Float ||
            !pipelineDesc.depthStencilState.enableDepthTest ||
            !pipelineDesc.depthStencilState.enableDepthWrite ||
            pipelineDesc.depthStencilState.depthCompareOp != ark::rhi::CompareOp::Less ||
            pipelineDesc.vertexInput.buffers.size() != 1 ||
            pipelineDesc.vertexInput.buffers.front().attributes.size() != 1 ||
            pipelineDesc.vertexInput.buffers.front().attributes.front().location != 0) {
            std::cerr << "ShadowPass pipeline state is invalid\n";
            return false;
        }

        if (device.textureDescs.empty()) {
            std::cerr << "ShadowPass did not create a shadow map texture\n";
            return false;
        }

        const ark::rhi::TextureDesc& shadowTextureDesc = device.textureDescs.back();
        if (shadowTextureDesc.extent.width != 512 ||
            shadowTextureDesc.extent.height != 512 ||
            shadowTextureDesc.arrayLayers != 1 ||
            shadowTextureDesc.format != ark::rhi::Format::D32Float ||
            !ark::rhi::hasTextureUsage(shadowTextureDesc.usage, ark::rhi::TextureUsage::DepthStencil) ||
            !ark::rhi::hasTextureUsage(shadowTextureDesc.usage, ark::rhi::TextureUsage::ShaderResource)) {
            std::cerr << "ShadowPass shadow map texture is invalid\n";
            return false;
        }

        if (device.textureViewDescs.empty() ||
            device.textureViewDescs.back().type != ark::rhi::TextureViewType::Texture2D ||
            device.textureViewDescs.back().arrayLayerCount != 1) {
            std::cerr << "ShadowPass single shadow map view is invalid\n";
            return false;
        }

        if (!frameContext.shadowMapView || !frameContext.shadowSampler ||
            !near(frameContext.shadowStrength, 0.55f) ||
            !near(frameContext.shadowBias, 0.003f) ||
            frameContext.shadowFilterMode != static_cast<float>(ark::ShadowFilterMode::Pcf5x5) ||
            !near(frameContext.shadowFilterRadiusTexels, 2.0f) ||
            frameContext.cascadeShadows.isEnabled()) {
            std::cerr << "ShadowPass did not publish frame shadow resources\n";
            return false;
        }

        if (device.samplerDescs.empty()) {
            std::cerr << "ShadowPass did not create a shadow sampler\n";
            return false;
        }

        const ark::rhi::SamplerDesc& shadowSamplerDesc = device.samplerDescs.back();
        if (shadowSamplerDesc.debugName != "ShadowMapSampler" ||
            shadowSamplerDesc.minFilter != ark::rhi::FilterMode::Nearest ||
            shadowSamplerDesc.magFilter != ark::rhi::FilterMode::Nearest ||
            shadowSamplerDesc.mipFilter != ark::rhi::FilterMode::Nearest ||
            shadowSamplerDesc.addressU != ark::rhi::AddressMode::ClampToEdge ||
            shadowSamplerDesc.addressV != ark::rhi::AddressMode::ClampToEdge) {
            std::cerr << "ShadowPass shadow sampler should expose raw-depth taps for shader PCF\n";
            return false;
        }

        if (context.beginRenderingCalls != 1 ||
            context.endRenderingCalls != 1 ||
            context.lastRenderingDesc.colorAttachment.view != nullptr ||
            context.lastRenderingDesc.depthStencilAttachment.view != frameContext.shadowMapView ||
            context.depthAttachmentViews.size() != 1 ||
            context.depthAttachmentViews.front() != frameContext.shadowMapView ||
            context.lastRenderingDesc.extent.width != 512 ||
            context.lastRenderingDesc.depthStencilAttachment.clearDepth != 1.0f ||
            context.lastViewport.width != 512.0f ||
            context.lastScissor.width != 512) {
            std::cerr << "ShadowPass did not begin a depth-only rendering scope\n";
            return false;
        }

        if (context.pipelineBinds != 1 ||
            context.descriptorBinds != 1 ||
            context.lastDescriptorSetIndex != 0 ||
            context.shadowUniformUpdates != 1 ||
            context.vertexBufferBinds != 1 ||
            context.indexBufferBinds != 1 ||
            context.indexedDraws != 1 ||
            context.lastDrawIndexed.indexCount != 3) {
            std::cerr << "ShadowPass did not issue the expected shadow draw commands\n";
            return false;
        }

        if (context.barrierAfterStates.size() != 2 ||
            context.barrierAfterStates[0] != ark::rhi::ResourceState::DepthStencilWrite ||
            context.barrierAfterStates[1] != ark::rhi::ResourceState::ShaderResource) {
            std::cerr << "ShadowPass did not transition the shadow map correctly\n";
            return false;
        }

        if (device.descriptorSets.empty() ||
            !device.descriptorSets.front()->shadowUniformBuffer ||
            device.descriptorSets.front()->shadowUniformRange != sizeof(CapturedShadowUniform)) {
            std::cerr << "ShadowPass descriptor set did not bind the uniform buffer\n";
            return false;
        }

        if (near(context.lastShadowUniform.lightViewProjection[0][0], 1.0f) &&
            near(context.lastShadowUniform.lightViewProjection[1][1], 1.0f) &&
            near(context.lastShadowUniform.lightViewProjection[2][2], 1.0f) &&
            near(context.lastShadowUniform.lightViewProjection[3][3], 1.0f)) {
            std::cerr << "ShadowPass did not write a light view-projection matrix\n";
            return false;
        }

        return true;
    }

    bool validateShadowPassCascadeRenderPath() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};
        ark::TextureCache textureCache{};
        ark::MeshResource mesh{};
        ark::MaterialResource material{};
        ark::RenderScene scene{};
        ark::RenderQueue queue{};
        ark::RenderView view{};
        ark::ShadowPass pass{};

        if (!mesh.create(device, makeTriangle()) ||
            !createMaterial(device, textureCache, material)) {
            std::cerr << "Failed to create ShadowPass CSM smoke draw resources\n";
            return false;
        }

        scene.addObject(mesh, material, glm::mat4{1.0f}, "ShadowPassCsmDraw");
        queue.build(scene);

        view.setDefaultPerspective(ark::rhi::Extent2D{1280, 720});
        ark::ShadowSettings shadows{};
        shadows.enabled = true;
        shadows.strength = 0.8f;
        shadows.mapExtent = 512;
        shadows.cascades.enabled = true;
        shadows.cascades.cascadeCount = 4;
        shadows.cascades.splitLambda = 0.5f;
        shadows.cascades.maxDistance = 64.0f;
        shadows.cascades.cascadeExtent = 1024;
        view.setShadowSettings(shadows);

        pass.setup(device);

        context.frame.frameSlot = 0;
        context.frame.frameIndex = 0;

        ark::FrameContext frameContext{};
        frameContext.scene = &scene;
        frameContext.view = &view;
        frameContext.queue = &queue;
        frameContext.device = &device;
        frameContext.context = &context;
        frameContext.frameResource = &context.frame;

        if (!pass.prepare(frameContext) || !pass.execute(frameContext)) {
            std::cerr << "ShadowPass CSM path failed to prepare or execute\n";
            return false;
        }

        if (device.textureDescs.empty()) {
            std::cerr << "ShadowPass CSM path did not create a shadow texture\n";
            return false;
        }

        const ark::rhi::TextureDesc& shadowTextureDesc = device.textureDescs.back();
        if (shadowTextureDesc.extent.width != 1024 ||
            shadowTextureDesc.extent.height != 1024 ||
            shadowTextureDesc.arrayLayers != 4 ||
            shadowTextureDesc.format != ark::rhi::Format::D32Float) {
            std::cerr << "ShadowPass CSM texture array desc is invalid\n";
            return false;
        }

        if (!frameContext.shadowMapView ||
            frameContext.shadowMapView->getDesc().type != ark::rhi::TextureViewType::Texture2DArray ||
            frameContext.shadowMapView->getDesc().arrayLayerCount != 4 ||
            frameContext.shadowMapView->getDesc().baseArrayLayer != 0) {
            std::cerr << "ShadowPass CSM sampled shadow view should cover the whole texture array\n";
            return false;
        }

        if (device.textureViewDescs.size() < 5) {
            std::cerr << "ShadowPass CSM path did not create sampled and per-layer views\n";
            return false;
        }

        const ark::usize shadowViewBase = device.textureViewDescs.size() - 5;
        const ark::rhi::TextureViewDesc& sampledViewDesc = device.textureViewDescs[shadowViewBase];
        if (sampledViewDesc.type != ark::rhi::TextureViewType::Texture2DArray ||
            sampledViewDesc.arrayLayerCount != 4 ||
            sampledViewDesc.baseArrayLayer != 0) {
            std::cerr << "ShadowPass CSM sampled view desc is invalid\n";
            return false;
        }

        for (ark::u32 cascadeIndex = 0; cascadeIndex < 4; ++cascadeIndex) {
            const ark::rhi::TextureViewDesc& layerViewDesc =
                device.textureViewDescs[shadowViewBase + 1 + cascadeIndex];
            if (layerViewDesc.type != ark::rhi::TextureViewType::Texture2D ||
                layerViewDesc.baseArrayLayer != cascadeIndex ||
                layerViewDesc.arrayLayerCount != 1) {
                std::cerr << "ShadowPass CSM per-layer view desc is invalid\n";
                return false;
            }
        }

        if (!frameContext.cascadeShadows.isEnabled() ||
            frameContext.cascadeShadows.cascadeCount != 4 ||
            frameContext.cascadeShadows.cascadeExtent != 1024 ||
            !near(frameContext.cascadeShadows.cascades[0].nearDistance, view.cameraNearPlane()) ||
            frameContext.cascadeShadows.cascades[0].farDistance <=
                frameContext.cascadeShadows.cascades[0].nearDistance) {
            std::cerr << "ShadowPass CSM path did not publish cascade frame data\n";
            return false;
        }

        if (context.beginRenderingCalls != 4 ||
            context.endRenderingCalls != 4 ||
            context.depthAttachmentViews.size() != 4 ||
            context.lastRenderingDesc.colorAttachment.view != nullptr ||
            context.lastRenderingDesc.extent.width != 1024 ||
            context.lastViewport.width != 1024.0f ||
            context.lastScissor.width != 1024) {
            std::cerr << "ShadowPass CSM path did not render every cascade layer\n";
            return false;
        }

        for (ark::u32 cascadeIndex = 0; cascadeIndex < 4; ++cascadeIndex) {
            const ark::rhi::TextureView* depthView = context.depthAttachmentViews[cascadeIndex];
            if (!depthView ||
                depthView == frameContext.shadowMapView ||
                depthView->getDesc().type != ark::rhi::TextureViewType::Texture2D ||
                depthView->getDesc().baseArrayLayer != cascadeIndex ||
                depthView->getDesc().arrayLayerCount != 1) {
                std::cerr << "ShadowPass CSM path used the wrong render target layer\n";
                return false;
            }
        }

        if (context.pipelineBinds != 4 ||
            context.descriptorBinds != 4 ||
            context.shadowUniformUpdates != 4 ||
            context.vertexBufferBinds != 4 ||
            context.indexBufferBinds != 4 ||
            context.indexedDraws != 4 ||
            context.lastDrawIndexed.indexCount != 3) {
            std::cerr << "ShadowPass CSM path did not issue one shadow draw per cascade\n";
            return false;
        }

        if (context.barrierAfterStates.size() != 2 ||
            context.barrierAfterStates[0] != ark::rhi::ResourceState::DepthStencilWrite ||
            context.barrierAfterStates[1] != ark::rhi::ResourceState::ShaderResource) {
            std::cerr << "ShadowPass CSM path should transition the array texture once per pass\n";
            return false;
        }

        if (near(context.lastShadowUniform.lightViewProjection[0][0], 1.0f) &&
            near(context.lastShadowUniform.lightViewProjection[1][1], 1.0f) &&
            near(context.lastShadowUniform.lightViewProjection[2][2], 1.0f) &&
            near(context.lastShadowUniform.lightViewProjection[3][3], 1.0f)) {
            std::cerr << "ShadowPass CSM path did not write cascade light view-projection matrices\n";
            return false;
        }

        return true;
    }

    bool validateShadowPassSceneBoundsFitting() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};
        ark::TextureCache textureCache{};
        ark::MeshResource mesh{};
        ark::MaterialResource material{};
        ark::RenderScene scene{};
        ark::RenderQueue queue{};
        ark::RenderView fittedView{};
        ark::RenderView manualView{};
        ark::ShadowPass pass{};

        if (!mesh.create(device, makeTriangle()) ||
            !createMaterial(device, textureCache, material)) {
            std::cerr << "Failed to create ShadowPass fitting resources\n";
            return false;
        }

        ark::SceneLighting lighting{};
        lighting.mainLight.direction = glm::vec3{0.0f, -1.0f, 0.0f};
        scene.setLighting(lighting);
        scene.addObject(mesh,
                        material,
                        glm::translate(glm::mat4{1.0f}, glm::vec3{23.37f, 0.0f, 0.41f}),
                        "FittedShadowObject");
        queue.build(scene);
        if (!scene.hasBounds()) {
            std::cerr << "ShadowPass fitting test requires scene bounds\n";
            return false;
        }

        ark::ShadowSettings shadows{};
        shadows.enabled = true;
        shadows.strength = 1.0f;
        shadows.mapExtent = 256;
        shadows.orthographicHalfExtent = 4.0f;
        shadows.farPlane = 32.0f;
        shadows.lightDistance = 8.0f;
        shadows.fitSceneBounds = true;
        fittedView.setShadowSettings(shadows);

        pass.setup(device);

        context.frame.frameSlot = 0;
        context.frame.frameIndex = 0;

        ark::FrameContext frameContext{};
        frameContext.scene = &scene;
        frameContext.view = &fittedView;
        frameContext.queue = &queue;
        frameContext.device = &device;
        frameContext.context = &context;
        frameContext.frameResource = &context.frame;

        if (!pass.prepare(frameContext)) {
            std::cerr << "ShadowPass scene-fit prepare failed\n";
            return false;
        }

        const glm::vec3 fittedCenter = projectPoint(frameContext.lightViewProjection, scene.bounds().center());
        const glm::mat4 stabilizedLightViewProjection = frameContext.lightViewProjection;
        if (!isProjectedPointTexelAligned(stabilizedLightViewProjection,
                                          glm::vec3{0.0f},
                                          shadows.mapExtent)) {
            const glm::vec2 texel = projectShadowTexel(stabilizedLightViewProjection,
                                                       glm::vec3{0.0f},
                                                       shadows.mapExtent);
            std::cerr << "ShadowPass stabilized scene fitting did not align stable reference to texel grid: "
                      << texel.x << ", " << texel.y << "\n";
            return false;
        }

        const float halfTexelNdc = 1.0f / static_cast<float>(shadows.mapExtent);
        if (!near(fittedCenter.x, 0.0f, halfTexelNdc) ||
            !near(fittedCenter.y, 0.0f, halfTexelNdc) ||
            fittedCenter.z < 0.0f ||
            fittedCenter.z > 1.0f) {
            std::cerr << "ShadowPass scene fitting did not center scene bounds: "
                      << fittedCenter.x << ", "
                      << fittedCenter.y << ", "
                      << fittedCenter.z << "\n";
            return false;
        }

        shadows.stabilizeProjection = false;
        fittedView.setShadowSettings(shadows);
        frameContext.view = &fittedView;
        if (!pass.prepare(frameContext)) {
            std::cerr << "ShadowPass unstabilized scene-fit prepare failed\n";
            return false;
        }

        const glm::mat4 unstabilizedLightViewProjection = frameContext.lightViewProjection;
        if (isProjectedPointTexelAligned(unstabilizedLightViewProjection,
                                         glm::vec3{0.0f},
                                         shadows.mapExtent,
                                         0.00001f)) {
            std::cerr << "ShadowPass unstabilized scene fitting should not force stable reference to texel grid\n";
            return false;
        }

        shadows.fitSceneBounds = false;
        shadows.stabilizeProjection = true;
        manualView.setShadowSettings(shadows);
        frameContext.view = &manualView;
        if (!pass.prepare(frameContext)) {
            std::cerr << "ShadowPass manual prepare failed\n";
            return false;
        }

        const glm::vec3 manualCenter = projectPoint(frameContext.lightViewProjection, scene.bounds().center());
        if (std::abs(manualCenter.x) < 1.0f) {
            std::cerr << "ShadowPass manual bounds should not auto-center scene bounds\n";
            return false;
        }

        return true;
    }

    bool validateShadowMapRuntimeResize() {
        FakeRenderDevice device{};
        FakeDeviceContext context{};
        ark::RenderView view{};
        ark::ShadowPass pass{};

        ark::ShadowSettings shadows{};
        shadows.enabled = true;
        shadows.strength = 1.0f;
        shadows.mapExtent = 512;
        view.setShadowSettings(shadows);

        pass.setup(device);

        ark::FrameContext frameContext{};
        frameContext.view = &view;
        frameContext.context = &context;
        frameContext.frameResource = &context.frame;

        if (!pass.prepare(frameContext) || !frameContext.shadowMapView || !frameContext.shadowSampler) {
            std::cerr << "ShadowPass runtime resize setup failed\n";
            return false;
        }

        const ark::usize initialTextureCount = device.textureDescs.size();
        shadows.mapExtent = 1024;
        view.setShadowSettings(shadows);
        if (!pass.prepare(frameContext)) {
            std::cerr << "ShadowPass runtime resize prepare failed\n";
            return false;
        }

        if (context.deferredTextureViewReleases != 1 ||
            context.deferredSamplerReleases != 1 ||
            context.deferredTextureReleases != 1 ||
            device.textureDescs.size() != initialTextureCount + 1 ||
            device.textureDescs.back().extent.width != 1024 ||
            device.textureDescs.back().extent.height != 1024 ||
            !frameContext.shadowMapView ||
            !frameContext.shadowSampler) {
            std::cerr << "ShadowPass map extent change should defer-release old shadow target before rebuild\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateShadowPassDisabledPath() && validateShadowPassDepthRender() &&
                   validateShadowPassCascadeRenderPath() && validateShadowPassSceneBoundsFitting() &&
                   validateShadowMapRuntimeResize()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
