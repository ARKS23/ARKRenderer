#include "asset/MeshData.h"
#include "asset/TextureLoader.h"
#include "core/Memory.h"
#include "renderer/EnvironmentBrdfLutResource.h"
#include "renderer/EnvironmentCubeResource.h"
#include "renderer/EnvironmentResource.h"
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

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace {
    struct alignas(16) CapturedLightingUniform {
        glm::vec4 lightDirection;
        glm::vec4 lightColor;
        glm::vec4 ambientColor;
        glm::vec4 cameraPosition;
        glm::vec4 environment;
        glm::vec4 environmentSpecular;
    };

    static_assert(sizeof(CapturedLightingUniform) == 96);

    struct alignas(16) CapturedMaterialUniform {
        glm::vec4 baseColorFactor;
        glm::vec4 emissiveFactor;
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        float normalScale = 1.0f;
        float occlusionStrength = 1.0f;
        float alphaCutoff = 0.5f;
        float alphaMode = 0.0f;
        float baseColorTexCoord = 0.0f;
        float normalTexCoord = 0.0f;
        float metallicRoughnessTexCoord = 0.0f;
        float occlusionTexCoord = 0.0f;
        float emissiveTexCoord = 0.0f;
        float padding = 0.0f;
        glm::vec4 baseColorUvTransform0;
        glm::vec4 baseColorUvTransform1;
        glm::vec4 normalUvTransform0;
        glm::vec4 normalUvTransform1;
        glm::vec4 metallicRoughnessUvTransform0;
        glm::vec4 metallicRoughnessUvTransform1;
        glm::vec4 occlusionUvTransform0;
        glm::vec4 occlusionUvTransform1;
        glm::vec4 emissiveUvTransform0;
        glm::vec4 emissiveUvTransform1;
    };

    static_assert(sizeof(CapturedMaterialUniform) == 240);

    bool near(float a, float b, float epsilon = 0.0001f) {
        return std::fabs(a - b) <= epsilon;
    }

    bool nearVec3(const glm::vec3& a, const glm::vec3& b, float epsilon = 0.0001f) {
        return near(a.x, b.x, epsilon) && near(a.y, b.y, epsilon) && near(a.z, b.z, epsilon);
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

    ark::asset::ImageData makeHdrEnvironmentImage() {
        constexpr ark::u32 Width = 2;
        constexpr ark::u32 Height = 1;
        constexpr ark::u32 BytesPerPixel = 16;
        const float pixels[Width * Height * 4]{
            0.25f, 0.5f, 1.0f, 1.0f,
            1.0f, 0.5f, 0.25f, 1.0f,
        };

        ark::asset::ImageData image{};
        image.width = Width;
        image.height = Height;
        image.format = ark::asset::ImageFormat::Rgba32Float;
        image.bytesPerPixel = BytesPerPixel;
        image.pixels.resize(sizeof(pixels));
        std::memcpy(image.pixels.data(), pixels, sizeof(pixels));
        image.debugName = "ForwardPassEnvironment";
        return image;
    }

    struct ForwardPassCapture {
        ark::rhi::GraphicsPipelineDesc pipelineDesc{};
        std::vector<ark::rhi::DescriptorBindingDesc> descriptorBindings;
        CapturedLightingUniform lightingUniform{};
        std::vector<CapturedMaterialUniform> materialUniforms;
        int lightingUniformUpdates = 0;
        int materialUniformUpdates = 0;
        int pipelineBinds = 0;
        int descriptorBinds = 0;
        int indexedDraws = 0;
        ark::usize textureUploadCount = 0;
        bool environmentImageBound = false;
        bool environmentSamplerBound = false;
        bool irradianceImageBound = false;
        bool irradianceSamplerBound = false;
        bool specularImageBound = false;
        bool specularSamplerBound = false;
        bool brdfLutImageBound = false;
        bool brdfLutSamplerBound = false;
        ark::rhi::TextureDesc environmentTextureDesc{};
        ark::rhi::SamplerDesc environmentSamplerDesc{};
        ark::rhi::TextureDesc irradianceTextureDesc{};
        ark::rhi::SamplerDesc irradianceSamplerDesc{};
        ark::rhi::TextureDesc specularTextureDesc{};
        ark::rhi::SamplerDesc specularSamplerDesc{};
        ark::rhi::TextureDesc brdfLutTextureDesc{};
        ark::rhi::SamplerDesc brdfLutSamplerDesc{};
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
        void updateUniformBuffer(ark::u32 binding, const ark::rhi::BufferDescriptor&) override {
            uniformBindings.push_back(binding);
        }

        void updateSampledImage(ark::u32 binding, const ark::rhi::SampledImageDescriptor& image) override {
            sampledImageBindings.push_back(binding);
            if (binding == 14) {
                environmentImageView = image.view;
            }
            if (binding == 16) {
                irradianceImageView = image.view;
            }
            if (binding == 18) {
                specularImageView = image.view;
            }
            if (binding == 20) {
                brdfLutImageView = image.view;
            }
        }

        void updateSampler(ark::u32 binding, const ark::rhi::SamplerDescriptor& sampler) override {
            samplerBindings.push_back(binding);
            if (binding == 15) {
                environmentSampler = sampler.sampler;
            }
            if (binding == 17) {
                irradianceSampler = sampler.sampler;
            }
            if (binding == 19) {
                specularSampler = sampler.sampler;
            }
            if (binding == 21) {
                brdfLutSampler = sampler.sampler;
            }
        }

        std::vector<ark::u32> uniformBindings;
        std::vector<ark::u32> sampledImageBindings;
        std::vector<ark::u32> samplerBindings;
        ark::rhi::TextureView* environmentImageView = nullptr;
        ark::rhi::Sampler* environmentSampler = nullptr;
        ark::rhi::TextureView* irradianceImageView = nullptr;
        ark::rhi::Sampler* irradianceSampler = nullptr;
        ark::rhi::TextureView* specularImageView = nullptr;
        ark::rhi::Sampler* specularSampler = nullptr;
        ark::rhi::TextureView* brdfLutImageView = nullptr;
        ark::rhi::Sampler* brdfLutSampler = nullptr;
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

        bool updateBuffer(ark::rhi::Buffer& buffer, const void* data, ark::u64 size, ark::u64 = 0) override {
            if (buffer.getDesc().debugName == "ForwardLightingUniformBuffer" &&
                size == sizeof(CapturedLightingUniform)) {
                std::memcpy(&lastLightingUniform, data, sizeof(CapturedLightingUniform));
                ++lightingUniformUpdates;
            }

            if (buffer.getDesc().debugName.rfind("ForwardMaterialUniformBuffer.", 0) == 0 &&
                size == sizeof(CapturedMaterialUniform)) {
                CapturedMaterialUniform materialUniform{};
                std::memcpy(&materialUniform, data, sizeof(CapturedMaterialUniform));
                materialUniforms.push_back(materialUniform);
                ++materialUniformUpdates;
            }

            return true;
        }

        bool uploadTextureData(const ark::rhi::TextureUploadDesc& desc) override {
            textureUploads.push_back(desc);
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
        CapturedLightingUniform lastLightingUniform{};
        std::vector<CapturedMaterialUniform> materialUniforms;
        std::vector<ark::rhi::TextureUploadDesc> textureUploads;
        int lightingUniformUpdates = 0;
        int materialUniformUpdates = 0;
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
                        bool doubleSided,
                        float metallicFactor = 1.0f,
                        float roughnessFactor = 1.0f) {
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
        material.metallicFactor = metallicFactor;
        material.roughnessFactor = roughnessFactor;
        return materialResource.create(material, textures);
    }

    bool captureForwardPass(ark::asset::AlphaMode alphaMode,
                            bool doubleSided,
                            ForwardPassCapture& capture,
                            const ark::SceneLighting* lighting = nullptr,
                            const ark::RenderView* customView = nullptr,
                            ark::rhi::Format colorFormat = ark::rhi::Format::Unknown,
                            ark::rhi::Format depthFormat = ark::rhi::Format::Unknown,
                            float environmentIntensity = 0.0f,
                            bool useIrradianceCube = false,
                            bool useSpecularResources = false) {
        FakeRenderDevice device{};
        FakeDeviceContext context{};
        FakeSwapChain swapChain{};
        ark::TextureCache textureCache{};
        ark::MeshResource mesh{};
        ark::MaterialResource material{};
        ark::EnvironmentResource environmentResource{};
        ark::EnvironmentCubeResource irradianceResource{};
        ark::EnvironmentCubeResource specularResource{};
        ark::EnvironmentBrdfLutResource brdfLutResource{};

        if (!mesh.create(device, makeTriangle()) ||
            !createMaterial(device, textureCache, material, alphaMode, doubleSided)) {
            std::cerr << "Failed to create ForwardPass pipeline smoke resources\n";
            return false;
        }

        if (environmentIntensity > 0.0f) {
            ark::EnvironmentResourceDesc environmentDesc{};
            environmentDesc.debugName = "ForwardPassCaptureEnvironment";
            if (!environmentResource.create(device, makeHdrEnvironmentImage(), environmentDesc)) {
                std::cerr << "Failed to create ForwardPass environment resource\n";
                return false;
            }
        }

        if (useIrradianceCube) {
            ark::EnvironmentCubeResourceDesc irradianceDesc{};
            irradianceDesc.debugName = "ForwardPassCaptureIrradiance";
            irradianceDesc.faceExtent = ark::rhi::Extent2D{4, 4};
            irradianceDesc.format = ark::rhi::Format::RGBA16Float;
            irradianceDesc.mipLevels = 1;
            if (!irradianceResource.create(device, irradianceDesc)) {
                std::cerr << "Failed to create ForwardPass irradiance resource\n";
                return false;
            }
        }

        if (useSpecularResources) {
            ark::EnvironmentCubeResourceDesc specularDesc{};
            specularDesc.debugName = "ForwardPassCaptureSpecular";
            specularDesc.faceExtent = ark::rhi::Extent2D{4, 4};
            specularDesc.format = ark::rhi::Format::RGBA16Float;
            specularDesc.mipLevels = 3;
            if (!specularResource.create(device, specularDesc)) {
                std::cerr << "Failed to create ForwardPass specular resource\n";
                return false;
            }

            ark::EnvironmentBrdfLutResourceDesc brdfLutDesc{};
            brdfLutDesc.debugName = "ForwardPassCaptureBrdfLut";
            brdfLutDesc.extent = ark::rhi::Extent2D{4, 4};
            brdfLutDesc.format = ark::rhi::Format::RGBA16Float;
            if (!brdfLutResource.create(device, brdfLutDesc)) {
                std::cerr << "Failed to create ForwardPass BRDF LUT resource\n";
                return false;
            }
        }

        ark::RenderScene scene{};
        if (lighting) {
            scene.setLighting(*lighting);
        }
        if (environmentIntensity > 0.0f) {
            ark::SceneEnvironment environment{};
            environment.environment = &environmentResource;
            environment.intensity = environmentIntensity;
            scene.setEnvironment(environment);
        }
        scene.addObject(mesh, material, glm::mat4{1.0f}, "ForwardPipelineDraw");
        ark::RenderQueue queue{};
        queue.build(scene);

        ark::RenderView view{};
        view.setDefaultPerspective(swapChain.getDesc().extent);
        const ark::RenderView& activeView = customView ? *customView : view;

        ark::ForwardPass pass{};
        pass.setup(device);

        context.frame.frameSlot = 0;
        context.frame.frameIndex = 0;

        ark::FrameContext frameContext{};
        frameContext.frameIndex = 0;
        frameContext.scene = &scene;
        frameContext.view = &activeView;
        frameContext.queue = &queue;
        frameContext.device = &device;
        frameContext.context = &context;
        frameContext.swapChain = &swapChain;
        frameContext.frameResource = &context.frame;
        frameContext.extent = swapChain.getDesc().extent;
        frameContext.colorFormat = colorFormat;
        frameContext.depthFormat = depthFormat;
        frameContext.irradianceCube = useIrradianceCube ? &irradianceResource : nullptr;
        frameContext.prefilteredSpecularCube = useSpecularResources ? &specularResource : nullptr;
        frameContext.brdfLut = useSpecularResources ? &brdfLutResource : nullptr;

        if (!pass.prepare(frameContext) || !pass.execute(frameContext)) {
            std::cerr << "ForwardPass pipeline smoke failed to execute\n";
            return false;
        }

        if (device.pipelineDescs.size() != 1 || context.pipelineBinds != 1 || context.indexedDraws != 1) {
            std::cerr << "ForwardPass pipeline smoke did not create and draw exactly one pipeline\n";
            return false;
        }

        capture.pipelineDesc = device.pipelineDescs.front();
        if (!device.descriptorSetLayoutDescs.empty()) {
            capture.descriptorBindings = device.descriptorSetLayoutDescs.front().bindings;
        }
        capture.lightingUniform = context.lastLightingUniform;
        capture.materialUniforms = context.materialUniforms;
        capture.lightingUniformUpdates = context.lightingUniformUpdates;
        capture.materialUniformUpdates = context.materialUniformUpdates;
        capture.pipelineBinds = context.pipelineBinds;
        capture.descriptorBinds = context.descriptorBinds;
        capture.indexedDraws = context.indexedDraws;
        capture.textureUploadCount = context.textureUploads.size();
        if (!device.descriptorSets.empty()) {
            ark::rhi::TextureView* environmentImageView = device.descriptorSets.front()->environmentImageView;
            ark::rhi::Sampler* environmentSampler = device.descriptorSets.front()->environmentSampler;
            ark::rhi::TextureView* irradianceImageView = device.descriptorSets.front()->irradianceImageView;
            ark::rhi::Sampler* irradianceSampler = device.descriptorSets.front()->irradianceSampler;
            ark::rhi::TextureView* specularImageView = device.descriptorSets.front()->specularImageView;
            ark::rhi::Sampler* specularSampler = device.descriptorSets.front()->specularSampler;
            ark::rhi::TextureView* brdfLutImageView = device.descriptorSets.front()->brdfLutImageView;
            ark::rhi::Sampler* brdfLutSampler = device.descriptorSets.front()->brdfLutSampler;
            capture.environmentImageBound = environmentImageView != nullptr;
            capture.environmentSamplerBound = environmentSampler != nullptr;
            capture.irradianceImageBound = irradianceImageView != nullptr;
            capture.irradianceSamplerBound = irradianceSampler != nullptr;
            capture.specularImageBound = specularImageView != nullptr;
            capture.specularSamplerBound = specularSampler != nullptr;
            capture.brdfLutImageBound = brdfLutImageView != nullptr;
            capture.brdfLutSamplerBound = brdfLutSampler != nullptr;
            if (environmentImageView && environmentImageView->getTexture()) {
                capture.environmentTextureDesc = environmentImageView->getTexture()->getDesc();
            }
            if (environmentSampler) {
                capture.environmentSamplerDesc = environmentSampler->getDesc();
            }
            if (irradianceImageView && irradianceImageView->getTexture()) {
                capture.irradianceTextureDesc = irradianceImageView->getTexture()->getDesc();
            }
            if (irradianceSampler) {
                capture.irradianceSamplerDesc = irradianceSampler->getDesc();
            }
            if (specularImageView && specularImageView->getTexture()) {
                capture.specularTextureDesc = specularImageView->getTexture()->getDesc();
            }
            if (specularSampler) {
                capture.specularSamplerDesc = specularSampler->getDesc();
            }
            if (brdfLutImageView && brdfLutImageView->getTexture()) {
                capture.brdfLutTextureDesc = brdfLutImageView->getTexture()->getDesc();
            }
            if (brdfLutSampler) {
                capture.brdfLutSamplerDesc = brdfLutSampler->getDesc();
            }
        }
        return true;
    }

    bool validateForwardPassSpecularIblMaterialGridUniforms() {
        constexpr std::size_t RowCount = 3;
        constexpr std::size_t ColumnCount = 5;
        constexpr std::size_t MaterialCount = RowCount * ColumnCount;
        constexpr float EnvironmentIntensity = 1.0f;
        const std::array<float, RowCount> metallicValues{0.0f, 0.5f, 1.0f};
        const std::array<float, ColumnCount> roughnessValues{0.05f, 0.25f, 0.5f, 0.75f, 1.0f};

        FakeRenderDevice device{};
        FakeDeviceContext context{};
        FakeSwapChain swapChain{};
        ark::TextureCache textureCache{};
        ark::MeshResource mesh{};
        std::array<ark::MaterialResource, MaterialCount> materials{};
        ark::EnvironmentResource environmentResource{};
        ark::EnvironmentCubeResource irradianceResource{};
        ark::EnvironmentCubeResource specularResource{};
        ark::EnvironmentBrdfLutResource brdfLutResource{};

        if (!mesh.create(device, makeTriangle())) {
            std::cerr << "Failed to create ForwardPass material grid mesh\n";
            return false;
        }

        for (std::size_t index = 0; index < MaterialCount; ++index) {
            const std::size_t row = index / ColumnCount;
            const std::size_t column = index % ColumnCount;
            if (!createMaterial(device,
                                textureCache,
                                materials[index],
                                ark::asset::AlphaMode::Opaque,
                                false,
                                metallicValues[row],
                                roughnessValues[column])) {
                std::cerr << "Failed to create ForwardPass material grid material\n";
                return false;
            }
        }

        ark::EnvironmentResourceDesc environmentDesc{};
        environmentDesc.debugName = "ForwardPassMaterialGridEnvironment";
        if (!environmentResource.create(device, makeHdrEnvironmentImage(), environmentDesc)) {
            std::cerr << "Failed to create ForwardPass material grid environment\n";
            return false;
        }

        ark::EnvironmentCubeResourceDesc irradianceDesc{};
        irradianceDesc.debugName = "ForwardPassMaterialGridIrradiance";
        irradianceDesc.faceExtent = ark::rhi::Extent2D{4, 4};
        irradianceDesc.format = ark::rhi::Format::RGBA16Float;
        irradianceDesc.mipLevels = 1;
        if (!irradianceResource.create(device, irradianceDesc)) {
            std::cerr << "Failed to create ForwardPass material grid irradiance\n";
            return false;
        }

        ark::EnvironmentCubeResourceDesc specularDesc{};
        specularDesc.debugName = "ForwardPassMaterialGridSpecular";
        specularDesc.faceExtent = ark::rhi::Extent2D{4, 4};
        specularDesc.format = ark::rhi::Format::RGBA16Float;
        specularDesc.mipLevels = 3;
        if (!specularResource.create(device, specularDesc)) {
            std::cerr << "Failed to create ForwardPass material grid specular resource\n";
            return false;
        }

        ark::EnvironmentBrdfLutResourceDesc brdfLutDesc{};
        brdfLutDesc.debugName = "ForwardPassMaterialGridBrdfLut";
        brdfLutDesc.extent = ark::rhi::Extent2D{4, 4};
        brdfLutDesc.format = ark::rhi::Format::RGBA16Float;
        if (!brdfLutResource.create(device, brdfLutDesc)) {
            std::cerr << "Failed to create ForwardPass material grid BRDF LUT\n";
            return false;
        }

        ark::RenderScene scene{};
        ark::SceneEnvironment environment{};
        environment.environment = &environmentResource;
        environment.intensity = EnvironmentIntensity;
        scene.setEnvironment(environment);

        for (std::size_t index = 0; index < MaterialCount; ++index) {
            const float x = (static_cast<float>(index % ColumnCount) - 2.0f) * 0.6f;
            const float y = (1.0f - static_cast<float>(index / ColumnCount)) * 0.6f;
            glm::mat4 transform{1.0f};
            transform[3][0] = x;
            transform[3][1] = y;
            scene.addObject(mesh, materials[index], transform, "ForwardMaterialGridDraw");
        }

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
        frameContext.irradianceCube = &irradianceResource;
        frameContext.prefilteredSpecularCube = &specularResource;
        frameContext.brdfLut = &brdfLutResource;

        if (!pass.prepare(frameContext) || !pass.execute(frameContext)) {
            std::cerr << "ForwardPass material grid smoke failed to execute\n";
            return false;
        }

        if (context.indexedDraws != static_cast<int>(MaterialCount) ||
            context.materialUniformUpdates != static_cast<int>(MaterialCount) ||
            context.materialUniforms.size() != MaterialCount ||
            context.lightingUniformUpdates != 1 ||
            !near(context.lastLightingUniform.environment.w, 1.0f) ||
            !near(context.lastLightingUniform.environmentSpecular.x, 2.0f)) {
            std::cerr << "ForwardPass material grid did not record expected draw or specular uniform data\n";
            return false;
        }

        for (std::size_t index = 0; index < MaterialCount; ++index) {
            const std::size_t row = index / ColumnCount;
            const std::size_t column = index % ColumnCount;
            const CapturedMaterialUniform& uniform = context.materialUniforms[index];
            if (!near(uniform.baseColorFactor.x, 1.0f) ||
                !near(uniform.baseColorFactor.y, 1.0f) ||
                !near(uniform.baseColorFactor.z, 1.0f) ||
                !near(uniform.baseColorFactor.w, 1.0f) ||
                !near(uniform.metallicFactor, metallicValues[row]) ||
                !near(uniform.roughnessFactor, roughnessValues[column]) ||
                !near(uniform.alphaMode, static_cast<float>(ark::asset::AlphaMode::Opaque))) {
                std::cerr << "ForwardPass material grid uniform factors are invalid\n";
                return false;
            }
        }

        return true;
    }

    bool validateForwardPassUsesFrameContextFormats() {
        ForwardPassCapture capture{};
        if (!captureForwardPass(ark::asset::AlphaMode::Opaque,
                                false,
                                capture,
                                nullptr,
                                nullptr,
                                ark::rhi::Format::RGBA16Float,
                                ark::rhi::Format::D32Float)) {
            return false;
        }

        if (capture.pipelineDesc.colorFormat != ark::rhi::Format::RGBA16Float ||
            capture.pipelineDesc.depthFormat != ark::rhi::Format::D32Float) {
            std::cerr << "ForwardPass pipeline did not use FrameContext attachment formats\n";
            return false;
        }

        return true;
    }

    bool validateSceneLightingAndRenderView() {
        ark::RenderScene scene{};
        const ark::SceneLighting& defaultLighting = scene.lighting();
        if (!nearVec3(defaultLighting.mainLight.direction, glm::vec3{-0.35f, -0.8f, -0.45f}) ||
            !nearVec3(defaultLighting.mainLight.color, glm::vec3{1.0f, 0.96f, 0.88f}) ||
            !nearVec3(defaultLighting.ambientColor, glm::vec3{0.08f, 0.09f, 0.11f})) {
            std::cerr << "RenderScene default lighting is invalid\n";
            return false;
        }

        ark::SceneLighting customLighting{};
        customLighting.mainLight.direction = glm::vec3{0.0f, -2.0f, 0.0f};
        customLighting.mainLight.color = glm::vec3{0.2f, 0.4f, 0.8f};
        customLighting.ambientColor = glm::vec3{0.01f, 0.02f, 0.03f};
        scene.setLighting(customLighting);
        if (!nearVec3(scene.lighting().mainLight.direction, customLighting.mainLight.direction) ||
            !nearVec3(scene.lighting().mainLight.color, customLighting.mainLight.color) ||
            !nearVec3(scene.lighting().ambientColor, customLighting.ambientColor)) {
            std::cerr << "RenderScene did not preserve custom lighting\n";
            return false;
        }

        ark::RenderView defaultView{};
        defaultView.setDefaultPerspective(ark::rhi::Extent2D{1280, 720});
        if (!nearVec3(defaultView.cameraPosition(), glm::vec3{0.0f, 0.0f, -4.0f})) {
            std::cerr << "RenderView default camera position is invalid\n";
            return false;
        }

        ark::RenderView customView{};
        const glm::vec3 customCameraPosition{1.5f, 2.5f, -6.0f};
        customView.setMatrices(glm::mat4{1.0f}, glm::mat4{1.0f}, customCameraPosition);
        if (!nearVec3(customView.cameraPosition(), customCameraPosition)) {
            std::cerr << "RenderView did not preserve explicit camera position\n";
            return false;
        }

        return true;
    }

    bool validateForwardPassLightingUniform() {
        ark::SceneLighting customLighting{};
        customLighting.mainLight.direction = glm::vec3{0.0f, -2.0f, 0.0f};
        customLighting.mainLight.color = glm::vec3{0.2f, 0.4f, 0.8f};
        customLighting.ambientColor = glm::vec3{0.01f, 0.02f, 0.03f};

        ark::RenderView view{};
        const glm::vec3 cameraPosition{1.5f, 2.5f, -6.0f};
        view.setMatrices(glm::mat4{1.0f}, glm::mat4{1.0f}, cameraPosition);

        ForwardPassCapture capture{};
        if (!captureForwardPass(ark::asset::AlphaMode::Opaque,
                                false,
                                capture,
                                &customLighting,
                                &view)) {
            return false;
        }

        if (capture.lightingUniformUpdates != 1) {
            std::cerr << "ForwardPass did not update lighting uniform exactly once\n";
            return false;
        }

        const CapturedLightingUniform& uniform = capture.lightingUniform;
        if (!nearVec3(glm::vec3{uniform.lightDirection}, glm::vec3{0.0f, -1.0f, 0.0f}) ||
            !near(uniform.lightDirection.w, 0.0f) ||
            !nearVec3(glm::vec3{uniform.lightColor}, customLighting.mainLight.color) ||
            !near(uniform.lightColor.w, 1.0f) ||
            !nearVec3(glm::vec3{uniform.ambientColor}, customLighting.ambientColor) ||
            !near(uniform.ambientColor.w, 1.0f) ||
            !nearVec3(glm::vec3{uniform.cameraPosition}, cameraPosition) ||
            !near(uniform.cameraPosition.w, 1.0f) ||
            !near(uniform.environment.x, 0.0f) ||
            !near(uniform.environment.y, 0.0f) ||
            !near(uniform.environment.w, 0.0f) ||
            !near(uniform.environmentSpecular.x, 0.0f)) {
            std::cerr << "ForwardPass lighting uniform did not use scene lighting and view camera position\n";
            return false;
        }

        return true;
    }

    bool validateForwardPassEnvironmentDescriptors() {
        ForwardPassCapture capture{};
        if (!captureForwardPass(ark::asset::AlphaMode::Opaque, false, capture)) {
            return false;
        }

        if (!containsBinding(capture.descriptorBindings,
                             14,
                             ark::rhi::DescriptorType::SampledImage,
                             ark::rhi::ShaderStageFlags::Fragment) ||
            !containsBinding(capture.descriptorBindings,
                             15,
                             ark::rhi::DescriptorType::Sampler,
                             ark::rhi::ShaderStageFlags::Fragment) ||
            !containsBinding(capture.descriptorBindings,
                             16,
                             ark::rhi::DescriptorType::SampledImage,
                             ark::rhi::ShaderStageFlags::Fragment) ||
            !containsBinding(capture.descriptorBindings,
                             17,
                             ark::rhi::DescriptorType::Sampler,
                             ark::rhi::ShaderStageFlags::Fragment) ||
            !containsBinding(capture.descriptorBindings,
                             18,
                             ark::rhi::DescriptorType::SampledImage,
                             ark::rhi::ShaderStageFlags::Fragment) ||
            !containsBinding(capture.descriptorBindings,
                             19,
                             ark::rhi::DescriptorType::Sampler,
                             ark::rhi::ShaderStageFlags::Fragment) ||
            !containsBinding(capture.descriptorBindings,
                             20,
                             ark::rhi::DescriptorType::SampledImage,
                             ark::rhi::ShaderStageFlags::Fragment) ||
            !containsBinding(capture.descriptorBindings,
                             21,
                             ark::rhi::DescriptorType::Sampler,
                             ark::rhi::ShaderStageFlags::Fragment)) {
            std::cerr << "ForwardPass descriptor layout does not expose environment IBL bindings\n";
            return false;
        }

        if (!capture.environmentImageBound || !capture.environmentSamplerBound ||
            !capture.irradianceImageBound || !capture.irradianceSamplerBound ||
            !capture.specularImageBound || !capture.specularSamplerBound ||
            !capture.brdfLutImageBound || !capture.brdfLutSamplerBound ||
            capture.textureUploadCount == 0) {
            std::cerr << "ForwardPass did not bind or upload fallback environment descriptors\n";
            return false;
        }

        if (capture.environmentTextureDesc.format != ark::rhi::Format::RGBA32Float ||
            capture.environmentTextureDesc.extent.width != 1 ||
            capture.environmentTextureDesc.extent.height != 1) {
            std::cerr << "ForwardPass fallback environment texture is invalid\n";
            return false;
        }

        if (capture.irradianceTextureDesc.type != ark::rhi::TextureType::Cube ||
            capture.irradianceTextureDesc.format != ark::rhi::Format::RGBA16Float ||
            capture.irradianceTextureDesc.extent.width != 1 ||
            capture.irradianceTextureDesc.extent.height != 1 ||
            capture.irradianceTextureDesc.arrayLayers != 6) {
            std::cerr << "ForwardPass fallback irradiance cubemap is invalid\n";
            return false;
        }

        if (capture.specularTextureDesc.type != ark::rhi::TextureType::Cube ||
            capture.specularTextureDesc.format != ark::rhi::Format::RGBA16Float ||
            capture.specularTextureDesc.extent.width != 1 ||
            capture.specularTextureDesc.extent.height != 1 ||
            capture.specularTextureDesc.arrayLayers != 6 ||
            capture.specularTextureDesc.mipLevels != 1) {
            std::cerr << "ForwardPass fallback specular cubemap is invalid\n";
            return false;
        }

        if (capture.brdfLutTextureDesc.type != ark::rhi::TextureType::Texture2D ||
            capture.brdfLutTextureDesc.format != ark::rhi::Format::RGBA16Float ||
            capture.brdfLutTextureDesc.extent.width != 1 ||
            capture.brdfLutTextureDesc.extent.height != 1 ||
            capture.brdfLutTextureDesc.arrayLayers != 1 ||
            capture.brdfLutTextureDesc.mipLevels != 1) {
            std::cerr << "ForwardPass fallback BRDF LUT is invalid\n";
            return false;
        }

        if (!near(capture.lightingUniform.environment.x, 0.0f) ||
            !near(capture.lightingUniform.environment.y, 0.0f) ||
            !near(capture.lightingUniform.environment.z, 0.0f) ||
            !near(capture.lightingUniform.environment.w, 0.0f) ||
            !near(capture.lightingUniform.environmentSpecular.x, 0.0f)) {
            std::cerr << "ForwardPass fallback environment should keep lighting disabled\n";
            return false;
        }

        return true;
    }

    bool validateForwardPassSceneEnvironmentUniform() {
        constexpr float EnvironmentIntensity = 1.75f;

        ForwardPassCapture capture{};
        if (!captureForwardPass(ark::asset::AlphaMode::Opaque,
                                false,
                                capture,
                                nullptr,
                                nullptr,
                                ark::rhi::Format::Unknown,
                                ark::rhi::Format::Unknown,
                                EnvironmentIntensity,
                                true)) {
            return false;
        }

        if (!near(capture.lightingUniform.environment.x, EnvironmentIntensity) ||
            !near(capture.lightingUniform.environment.y, 1.0f) ||
            !near(capture.lightingUniform.environment.z, 1.0f) ||
            !near(capture.lightingUniform.environment.w, 0.0f) ||
            !near(capture.lightingUniform.environmentSpecular.x, 0.0f)) {
            std::cerr << "ForwardPass did not write scene environment intensity/enabled/irradiance flags\n";
            return false;
        }

        if (!capture.environmentImageBound || !capture.environmentSamplerBound ||
            !capture.irradianceImageBound || !capture.irradianceSamplerBound ||
            !capture.specularImageBound || !capture.specularSamplerBound ||
            !capture.brdfLutImageBound || !capture.brdfLutSamplerBound) {
            std::cerr << "ForwardPass did not bind scene environment descriptors\n";
            return false;
        }

        if (capture.environmentTextureDesc.format != ark::rhi::Format::RGBA32Float ||
            capture.environmentTextureDesc.extent.width != 2 ||
            capture.environmentTextureDesc.extent.height != 1) {
            std::cerr << "ForwardPass scene environment texture binding is invalid\n";
            return false;
        }

        if (capture.irradianceTextureDesc.type != ark::rhi::TextureType::Cube ||
            capture.irradianceTextureDesc.format != ark::rhi::Format::RGBA16Float ||
            capture.irradianceTextureDesc.extent.width != 4 ||
            capture.irradianceTextureDesc.extent.height != 4 ||
            capture.irradianceTextureDesc.arrayLayers != 6) {
            std::cerr << "ForwardPass scene irradiance cubemap binding is invalid\n";
            return false;
        }

        if (capture.textureUploadCount < 2) {
            std::cerr << "ForwardPass should upload fallback and scene environment textures\n";
            return false;
        }

        return true;
    }

    bool validateForwardPassSceneSpecularResources() {
        constexpr float EnvironmentIntensity = 1.25f;

        ForwardPassCapture capture{};
        if (!captureForwardPass(ark::asset::AlphaMode::Opaque,
                                false,
                                capture,
                                nullptr,
                                nullptr,
                                ark::rhi::Format::Unknown,
                                ark::rhi::Format::Unknown,
                                EnvironmentIntensity,
                                true,
                                true)) {
            return false;
        }

        if (!near(capture.lightingUniform.environment.x, EnvironmentIntensity) ||
            !near(capture.lightingUniform.environment.y, 1.0f) ||
            !near(capture.lightingUniform.environment.z, 1.0f) ||
            !near(capture.lightingUniform.environment.w, 1.0f) ||
            !near(capture.lightingUniform.environmentSpecular.x, 2.0f)) {
            std::cerr << "ForwardPass did not enable scene specular IBL uniform data\n";
            return false;
        }

        if (!capture.specularImageBound || !capture.specularSamplerBound ||
            !capture.brdfLutImageBound || !capture.brdfLutSamplerBound) {
            std::cerr << "ForwardPass did not bind scene specular IBL descriptors\n";
            return false;
        }

        if (capture.specularTextureDesc.type != ark::rhi::TextureType::Cube ||
            capture.specularTextureDesc.format != ark::rhi::Format::RGBA16Float ||
            capture.specularTextureDesc.extent.width != 4 ||
            capture.specularTextureDesc.extent.height != 4 ||
            capture.specularTextureDesc.arrayLayers != 6 ||
            capture.specularTextureDesc.mipLevels != 3) {
            std::cerr << "ForwardPass scene specular cubemap binding is invalid\n";
            return false;
        }

        if (capture.brdfLutTextureDesc.type != ark::rhi::TextureType::Texture2D ||
            capture.brdfLutTextureDesc.format != ark::rhi::Format::RGBA16Float ||
            capture.brdfLutTextureDesc.extent.width != 4 ||
            capture.brdfLutTextureDesc.extent.height != 4 ||
            capture.brdfLutTextureDesc.arrayLayers != 1 ||
            capture.brdfLutTextureDesc.mipLevels != 1) {
            std::cerr << "ForwardPass scene BRDF LUT binding is invalid\n";
            return false;
        }

        return true;
    }

    bool validateDoubleSidedCullModes() {
        ForwardPassCapture singleSidedCapture{};
        if (!captureForwardPass(ark::asset::AlphaMode::Opaque, false, singleSidedCapture)) {
            return false;
        }

        const ark::rhi::GraphicsPipelineDesc& singleSidedDesc = singleSidedCapture.pipelineDesc;
        if (singleSidedDesc.rasterState.cullMode != ark::rhi::CullMode::Back ||
            singleSidedDesc.rasterState.frontFace != ark::rhi::FrontFace::CounterClockwise ||
            !singleSidedDesc.depthStencilState.enableDepthWrite ||
            singleSidedDesc.blendState.colorAttachment.enableBlend) {
            std::cerr << "Single-sided opaque ForwardPass pipeline state is invalid\n";
            return false;
        }

        ForwardPassCapture doubleSidedCapture{};
        if (!captureForwardPass(ark::asset::AlphaMode::Mask, true, doubleSidedCapture)) {
            return false;
        }

        const ark::rhi::GraphicsPipelineDesc& doubleSidedDesc = doubleSidedCapture.pipelineDesc;
        if (doubleSidedDesc.rasterState.cullMode != ark::rhi::CullMode::None ||
            doubleSidedDesc.rasterState.frontFace != ark::rhi::FrontFace::CounterClockwise ||
            !doubleSidedDesc.depthStencilState.enableDepthWrite ||
            doubleSidedDesc.blendState.colorAttachment.enableBlend) {
            std::cerr << "Double-sided mask ForwardPass pipeline state is invalid\n";
            return false;
        }

        ForwardPassCapture blendCapture{};
        if (!captureForwardPass(ark::asset::AlphaMode::Blend, false, blendCapture)) {
            return false;
        }

        const ark::rhi::GraphicsPipelineDesc& blendDesc = blendCapture.pipelineDesc;
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
    return validateSceneLightingAndRenderView() &&
                   validateForwardPassLightingUniform() &&
                   validateForwardPassEnvironmentDescriptors() &&
                   validateForwardPassSceneEnvironmentUniform() &&
                   validateForwardPassSceneSpecularResources() &&
                   validateForwardPassSpecularIblMaterialGridUniforms() &&
                   validateForwardPassUsesFrameContextFormats() &&
                   validateDoubleSidedCullModes()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
