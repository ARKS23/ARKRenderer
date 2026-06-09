#include "asset/GltfLoader.h"
#include "asset/MeshData.h"
#include "asset/TextureLoader.h"
#include "core/FileSystem.h"
#include "renderer/ModelResource.h"
#include "renderer/RenderQueue.h"
#include "renderer/RenderScene.h"
#include "renderer/TextureCache.h"
#include "renderer/TextureResource.h"
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

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <span>

namespace {
    bool near(float lhs, float rhs) {
        return std::fabs(lhs - rhs) < 0.0001f;
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
            ++bufferCount;
            return ark::makeScope<FakeBuffer>(desc);
        }

        ark::Scope<ark::rhi::Texture> createTexture(const ark::rhi::TextureDesc& desc) override {
            ++textureCount;
            lastTextureDesc = desc;
            lastTextureFormat = desc.format;
            return ark::makeScope<FakeTexture>(desc);
        }

        ark::Scope<ark::rhi::TextureView> createTextureView(ark::rhi::Texture& texture,
                                                            const ark::rhi::TextureViewDesc& desc) override {
            ++textureViewCount;
            lastTextureViewDesc = desc;
            return ark::makeScope<FakeTextureView>(texture, desc);
        }

        ark::Scope<ark::rhi::Sampler> createSampler(const ark::rhi::SamplerDesc& desc) override {
            ++samplerCount;
            lastSamplerDesc = desc;
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

        int bufferCount = 0;
        int textureCount = 0;
        int textureViewCount = 0;
        int samplerCount = 0;
        ark::rhi::TextureDesc lastTextureDesc{};
        ark::rhi::TextureViewDesc lastTextureViewDesc{};
        ark::rhi::SamplerDesc lastSamplerDesc{};
        ark::rhi::Format lastTextureFormat = ark::rhi::Format::Unknown;

    private:
        ark::rhi::RenderDeviceCaps m_Caps{};
    };

    class FakeDeviceContext final : public ark::rhi::DeviceContext {
    public:
        ark::rhi::FrameResource& beginFrame() override {
            return m_Frame;
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
        }

        void bindDescriptorSet(ark::u32, ark::rhi::DescriptorSet&) override {
        }

        bool updateBuffer(ark::rhi::Buffer&, const void*, ark::u64, ark::u64 = 0) override {
            ++bufferUpdates;
            return true;
        }

        bool uploadTextureData(const ark::rhi::TextureUploadDesc&) override {
            ++textureUploads;
            return true;
        }

        bool generateTextureMips(ark::rhi::Texture&) override {
            ++textureMipGenerations;
            return true;
        }

        bool uploadBufferData(const ark::rhi::BufferUploadDesc&) override {
            ++bufferUploads;
            return true;
        }

        bool deferReleaseBuffer(ark::Scope<ark::rhi::Buffer>& buffer) override {
            ++deferredBuffers;
            buffer.reset();
            return true;
        }

        bool deferReleaseTexture(ark::Scope<ark::rhi::Texture>& texture) override {
            ++deferredTextures;
            texture.reset();
            return true;
        }

        bool deferReleaseTextureView(ark::Scope<ark::rhi::TextureView>& textureView) override {
            ++deferredTextureViews;
            textureView.reset();
            return true;
        }

        bool deferReleaseSampler(ark::Scope<ark::rhi::Sampler>& sampler) override {
            ++deferredSamplers;
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
        }

        void pipelineBarrier(std::span<const ark::rhi::ResourceBarrier>) override {
        }

        void clearRenderTarget(ark::rhi::TextureView&, const ark::rhi::ClearColor&) override {
        }

        int bufferUploads = 0;
        int textureUploads = 0;
        int textureMipGenerations = 0;
        int deferredBuffers = 0;
        int deferredTextures = 0;
        int deferredTextureViews = 0;
        int deferredSamplers = 0;
        int bufferUpdates = 0;

    private:
        ark::rhi::FrameResource m_Frame{};
    };

    ark::Path findTexturePath() {
        const ark::Path relative = ark::Path{"assets/textures/xiaowei.png"};
        const std::array<ark::Path, 3> candidates{
            relative,
            ark::Path{"../"} / relative,
            ark::Path{"../../"} / relative,
        };

        return ark::findFirstExistingPath(candidates);
    }

    ark::Path findModelPath(const ark::Path& relative) {
        const std::array<ark::Path, 3> candidates{
            relative,
            ark::Path{"../"} / relative,
            ark::Path{"../../"} / relative,
        };

        return ark::findFirstExistingPath(candidates);
    }

    ark::asset::MeshPrimitiveData makeTriangle(const char* name, ark::u32 materialIndex, float xOffset) {
        ark::asset::MeshVertex v0{};
        v0.position[0] = -0.5f + xOffset;
        v0.position[1] = 0.0f;
        v0.normal[2] = 1.0f;
        v0.uv0[1] = 1.0f;

        ark::asset::MeshVertex v1 = v0;
        v1.position[0] = 0.5f + xOffset;
        v1.uv0[0] = 1.0f;

        ark::asset::MeshVertex v2 = v0;
        v2.position[1] = 1.0f;
        v2.uv0[0] = 0.5f;
        v2.uv0[1] = 0.0f;

        ark::asset::MeshPrimitiveData mesh{};
        mesh.debugName = name;
        mesh.vertices = {v0, v1, v2};
        mesh.indices = {0, 1, 2};
        mesh.materialIndex = materialIndex;
        return mesh;
    }

    bool validateTextureResourceDeferredRelease() {
        const ark::Path texturePath = findTexturePath();
        if (texturePath.empty()) {
            std::cerr << "Failed to find texture asset\n";
            return false;
        }

        ark::asset::ImageData image = ark::asset::loadImageRgba8(texturePath);
        if (image.empty()) {
            std::cerr << "Failed to load texture asset\n";
            return false;
        }

        FakeRenderDevice device{};
        ark::TextureResourceDesc desc{};
        desc.path = texturePath;
        desc.colorSpace = ark::TextureColorSpace::Srgb;
        desc.debugName = "DeferredReleaseTexture";

        ark::TextureResource texture{};
        if (!texture.create(device, image, desc)) {
            std::cerr << "TextureResource create failed\n";
            return false;
        }

        FakeDeviceContext context{};
        if (!texture.upload(context)) {
            std::cerr << "TextureResource upload failed\n";
            return false;
        }

        const int expectedMipGenerations = texture.mipLevels() > 1 ? 1 : 0;
        if (!texture.isReady() || context.textureUploads != 1 ||
            context.textureMipGenerations != expectedMipGenerations || context.deferredBuffers != 1) {
            std::cerr << "TextureResource upload did not prepare ready texture\n";
            return false;
        }

        if (!texture.releaseDeferred(context)) {
            std::cerr << "TextureResource deferred release failed\n";
            return false;
        }

        if (texture.isReady() || context.deferredTextures != 1 || context.deferredTextureViews != 1 ||
            context.deferredSamplers != 1 || context.deferredBuffers != 1) {
            std::cerr << "TextureResource deferred release counts are invalid\n";
            return false;
        }

        return true;
    }

    bool validateTextureResourceMipDesc() {
        if (ark::rhi::calculateMipLevelCount(ark::rhi::Extent2D{1, 1}) != 1 ||
            ark::rhi::calculateMipLevelCount(ark::rhi::Extent2D{7, 5}) != 3 ||
            ark::rhi::calculateMipLevelCount(ark::rhi::Extent2D{256, 128}) != 9 ||
            ark::rhi::calculateMipLevelCount(ark::rhi::Extent2D{}) != 0) {
            std::cerr << "Mip level count calculation is invalid\n";
            return false;
        }

        ark::asset::ImageData image{};
        image.width = 7;
        image.height = 5;
        image.format = ark::asset::ImageFormat::Rgba8Unorm;
        image.bytesPerPixel = 4;
        image.pixels.resize(static_cast<ark::usize>(image.width) * image.height * image.bytesPerPixel, 255);
        image.debugName = "MipDescImage";

        FakeRenderDevice device{};
        ark::TextureResourceDesc desc{};
        desc.colorSpace = ark::TextureColorSpace::Srgb;
        desc.generateMips = true;
        desc.debugName = "MipDescTexture";

        ark::TextureResource texture{};
        if (!texture.create(device, image, desc)) {
            std::cerr << "Mip TextureResource create failed\n";
            return false;
        }

        const ark::u32 expectedMipLevels = 3;
        if (texture.mipLevels() != expectedMipLevels || device.lastTextureDesc.mipLevels != expectedMipLevels ||
            device.lastTextureViewDesc.mipLevelCount != expectedMipLevels) {
            std::cerr << "Mip TextureResource did not create full mip range\n";
            return false;
        }

        if (!ark::rhi::hasTextureUsage(device.lastTextureDesc.usage, ark::rhi::TextureUsage::TransferSrc) ||
            !ark::rhi::hasTextureUsage(device.lastTextureDesc.usage, ark::rhi::TextureUsage::TransferDst) ||
            !ark::rhi::hasTextureUsage(device.lastTextureDesc.usage, ark::rhi::TextureUsage::ShaderResource)) {
            std::cerr << "Mip TextureResource usage is invalid\n";
            return false;
        }

        if (device.lastSamplerDesc.mipFilter != ark::rhi::FilterMode::Linear) {
            std::cerr << "Mip TextureResource sampler mip filter is invalid\n";
            return false;
        }

        FakeDeviceContext context{};
        if (!texture.upload(context)) {
            std::cerr << "Mip TextureResource upload failed\n";
            return false;
        }

        if (!texture.isReady() || context.textureUploads != 1 || context.textureMipGenerations != 1 ||
            context.deferredBuffers != 1) {
            std::cerr << "Mip TextureResource upload did not generate mips\n";
            return false;
        }

        return true;
    }

    bool validateTextureCacheDeferredClear() {
        const ark::Path texturePath = findTexturePath();
        if (texturePath.empty()) {
            std::cerr << "Failed to find texture asset\n";
            return false;
        }

        FakeRenderDevice device{};
        ark::TextureCache textureCache{};
        ark::TextureResourceDesc desc{};
        desc.path = texturePath;
        desc.colorSpace = ark::TextureColorSpace::Srgb;
        desc.debugName = "DeferredClearTexture";

        ark::TextureResource* first = textureCache.getOrCreate(device, desc);
        ark::TextureResource* second = textureCache.getOrCreate(device, desc);
        if (!first || first != second || textureCache.size() != 1 || device.textureCount != 1 ||
            device.textureViewCount != 1 || device.samplerCount != 1) {
            std::cerr << "TextureCache did not reuse texture before deferred clear\n";
            return false;
        }

        FakeDeviceContext context{};
        if (!textureCache.clearDeferred(context)) {
            std::cerr << "TextureCache deferred clear failed\n";
            return false;
        }

        if (textureCache.size() != 0 || context.deferredBuffers != 1 || context.deferredTextures != 1 ||
            context.deferredTextureViews != 1 || context.deferredSamplers != 1) {
            std::cerr << "TextureCache deferred clear counts are invalid\n";
            return false;
        }

        return true;
    }

    bool validateMeshResourceDeferredRelease() {
        FakeRenderDevice device{};
        ark::MeshResource meshResource{};
        if (!meshResource.create(device, makeTriangle("DeferredMesh", 0, 0.0f))) {
            std::cerr << "MeshResource create failed\n";
            return false;
        }

        FakeDeviceContext context{};
        if (!meshResource.upload(context)) {
            std::cerr << "MeshResource upload failed\n";
            return false;
        }

        if (!meshResource.isReady() || context.bufferUploads != 2 || context.deferredBuffers != 2) {
            std::cerr << "MeshResource upload counts are invalid\n";
            return false;
        }

        if (!meshResource.releaseDeferred(context)) {
            std::cerr << "MeshResource deferred release failed\n";
            return false;
        }

        if (meshResource.isReady() || meshResource.indexCount() != 0 || context.deferredBuffers != 4) {
            std::cerr << "MeshResource deferred release counts are invalid\n";
            return false;
        }

        return true;
    }

    bool validateLocalModelResourceDeferredReset() {
        const ark::Path texturePath = findTexturePath();
        if (texturePath.empty()) {
            std::cerr << "Failed to find texture asset\n";
            return false;
        }

        ark::asset::MaterialData material{};
        material.debugName = "LocalResetMaterial";
        material.baseColorTexturePath = texturePath;

        ark::asset::ModelData modelData{};
        modelData.debugName = "LocalResetModel";
        modelData.materials.push_back(material);
        modelData.meshes.push_back(makeTriangle("LocalResetTriangle", 0, 0.0f));

        FakeRenderDevice device{};
        ark::ModelResource modelResource{};
        if (!modelResource.create(device, modelData)) {
            std::cerr << "Local ModelResource create failed\n";
            return false;
        }

        FakeDeviceContext context{};
        if (!modelResource.upload(context)) {
            std::cerr << "Local ModelResource upload failed\n";
            return false;
        }

        if (context.bufferUploads != 2 || context.textureUploads != 1 || context.deferredBuffers != 3) {
            std::cerr << "Local ModelResource upload counts are invalid\n";
            return false;
        }

        if (!modelResource.resetDeferred(context)) {
            std::cerr << "Local ModelResource deferred reset failed\n";
            return false;
        }

        if (!modelResource.empty() || modelResource.meshCount() != 0 || modelResource.materialCount() != 0 ||
            modelResource.instanceCount() != 0 || context.deferredBuffers != 5 || context.deferredTextures != 1 ||
            context.deferredTextureViews != 1 || context.deferredSamplers != 1) {
            std::cerr << "Local ModelResource deferred reset counts are invalid\n";
            return false;
        }

        return true;
    }

    bool validateExternalModelResourceDeferredReset() {
        const ark::Path texturePath = findTexturePath();
        if (texturePath.empty()) {
            std::cerr << "Failed to find texture asset\n";
            return false;
        }

        ark::asset::MaterialData material{};
        material.debugName = "ExternalResetMaterial";
        material.baseColorTexturePath = texturePath;

        ark::asset::ModelData modelData{};
        modelData.debugName = "ExternalResetModel";
        modelData.materials.push_back(material);
        modelData.meshes.push_back(makeTriangle("ExternalResetTriangle", 0, 0.0f));

        FakeRenderDevice device{};
        ark::TextureCache textureCache{};
        ark::ModelResource modelResource{};
        if (!modelResource.create(device, textureCache, modelData)) {
            std::cerr << "External ModelResource create failed\n";
            return false;
        }

        if (textureCache.size() != 1) {
            std::cerr << "External texture cache was not populated\n";
            return false;
        }

        FakeDeviceContext context{};
        if (!modelResource.upload(context)) {
            std::cerr << "External ModelResource upload failed\n";
            return false;
        }

        if (!modelResource.resetDeferred(context)) {
            std::cerr << "External ModelResource deferred reset failed\n";
            return false;
        }

        if (!modelResource.empty() || textureCache.size() != 1 || context.deferredBuffers != 5 ||
            context.deferredTextures != 0 || context.deferredTextureViews != 0 || context.deferredSamplers != 0) {
            std::cerr << "External ModelResource reset touched external texture cache\n";
            return false;
        }

        if (!textureCache.clearDeferred(context)) {
            std::cerr << "External texture cache deferred clear failed\n";
            return false;
        }

        if (textureCache.size() != 0 || context.deferredTextures != 1 || context.deferredTextureViews != 1 ||
            context.deferredSamplers != 1) {
            std::cerr << "External texture cache deferred clear counts are invalid\n";
            return false;
        }

        return true;
    }

    bool validateModelResource() {
        const ark::Path texturePath = findTexturePath();
        if (texturePath.empty()) {
            std::cerr << "Failed to find texture asset\n";
            return false;
        }

        ark::asset::MaterialData material{};
        material.debugName = "SmokeMaterial";
        material.baseColorTexturePath = texturePath;
        material.baseColorFactor[0] = 0.2f;
        material.baseColorFactor[1] = 0.4f;
        material.baseColorFactor[2] = 0.6f;
        material.baseColorFactor[3] = 0.8f;
        material.metallicFactor = 0.3f;
        material.roughnessFactor = 0.7f;
        ark::asset::MaterialData duplicateMaterial = material;
        duplicateMaterial.debugName = "DuplicateSmokeMaterial";

        ark::asset::ModelData modelData{};
        modelData.debugName = "SmokeModel";
        modelData.materials.push_back(material);
        modelData.materials.push_back(duplicateMaterial);
        modelData.meshes.push_back(makeTriangle("TriangleA", 0, -1.0f));
        modelData.meshes.push_back(makeTriangle("TriangleB", 0, 1.0f));

        ark::asset::MeshPrimitiveInstanceData instanceA{};
        instanceA.meshIndex = 0;
        instanceA.debugName = "InstanceA";
        instanceA.localTransform.matrix[12] = -2.0f;
        modelData.instances.push_back(instanceA);

        ark::asset::MeshPrimitiveInstanceData instanceB{};
        instanceB.meshIndex = 0;
        instanceB.debugName = "InstanceB";
        instanceB.localTransform.matrix[12] = 2.0f;
        modelData.instances.push_back(instanceB);

        FakeRenderDevice device{};
        ark::TextureCache textureCache{};
        ark::ModelResource modelResource{};
        if (!modelResource.create(device, textureCache, modelData)) {
            std::cerr << "ModelResource create failed\n";
            return false;
        }

        if (modelResource.primitiveCount() != 2 || modelResource.instanceCount() != 2 ||
            modelResource.meshCount() != 2 || modelResource.materialCount() != 2) {
            std::cerr << "Unexpected ModelResource counts\n";
            return false;
        }

        if (textureCache.size() != 1 || device.textureCount != 1 || device.textureViewCount != 1 ||
            device.samplerCount != 1 || device.lastTextureFormat != ark::rhi::Format::RGBA8Srgb) {
            std::cerr << "TextureCache did not reuse sRGB base color texture\n";
            return false;
        }

        if (!modelResource.primitiveMesh(0) || !modelResource.primitiveMesh(1) ||
            !modelResource.primitiveMaterial(0) || !modelResource.primitiveMaterial(1)) {
            std::cerr << "ModelResource primitive resource lookup failed\n";
            return false;
        }

        const ark::MaterialFactors& materialFactors = modelResource.primitiveMaterial(0)->factors();
        if (!near(materialFactors.baseColorFactor[0], 0.2f) ||
            !near(materialFactors.baseColorFactor[1], 0.4f) ||
            !near(materialFactors.baseColorFactor[2], 0.6f) ||
            !near(materialFactors.baseColorFactor[3], 0.8f) ||
            !near(materialFactors.metallicFactor, 0.3f) ||
            !near(materialFactors.roughnessFactor, 0.7f)) {
            std::cerr << "ModelResource did not preserve material factors\n";
            return false;
        }

        FakeDeviceContext context{};
        if (!modelResource.upload(context)) {
            std::cerr << "ModelResource upload failed\n";
            return false;
        }

        if (context.bufferUploads != 4 || context.textureUploads != 1 || context.deferredBuffers != 5) {
            std::cerr << "Unexpected ModelResource upload counts\n";
            return false;
        }

        ark::RenderScene scene{};
        glm::mat4 transform{1.0f};
        transform[3][0] = 4.0f;
        scene.addModel(modelResource, transform, "SceneModel");

        ark::RenderQueue queue{};
        queue.build(scene);
        if (queue.size() != 2) {
            std::cerr << "RenderQueue did not expand model primitives\n";
            return false;
        }

        const std::span<const ark::DrawItem> drawItems = queue.drawItems();
        if (drawItems[0].mesh != modelResource.primitiveMesh(0) ||
            drawItems[1].mesh != modelResource.primitiveMesh(0) ||
            drawItems[0].material != modelResource.primitiveMaterial(0) ||
            drawItems[1].material != modelResource.primitiveMaterial(0)) {
            std::cerr << "RenderQueue model draw item references are invalid\n";
            return false;
        }

        if (drawItems[0].modelMatrix[3][0] != 2.0f || drawItems[1].modelMatrix[3][0] != 6.0f) {
            std::cerr << "RenderQueue did not combine scene and local transforms\n";
            return false;
        }

        return true;
    }

    bool validateTextureCacheFixtureModelResource() {
        const ark::Path modelPath = findModelPath(ark::Path{"assets/models/texture_cache_fixture.gltf"});
        if (modelPath.empty()) {
            std::cerr << "Failed to find texture cache fixture\n";
            return false;
        }

        const ark::asset::ModelData modelData = ark::asset::loadGltfModel(modelPath);
        if (modelData.empty() || modelData.meshes.size() != 2 || modelData.materials.size() != 2) {
            std::cerr << "Unexpected texture cache fixture model data\n";
            return false;
        }

        FakeRenderDevice device{};
        ark::TextureCache textureCache{};
        ark::ModelResource modelResource{};
        if (!modelResource.create(device, textureCache, modelData)) {
            std::cerr << "Texture cache fixture ModelResource create failed\n";
            return false;
        }

        if (modelResource.primitiveCount() != 2 || modelResource.materialCount() != 2 ||
            textureCache.size() != 1 || device.textureCount != 1 || device.textureViewCount != 1 ||
            device.samplerCount != 1 || device.lastTextureFormat != ark::rhi::Format::RGBA8Srgb) {
            std::cerr << "Texture cache fixture did not reuse one sRGB texture resource\n";
            return false;
        }

        FakeDeviceContext context{};
        if (!modelResource.upload(context)) {
            std::cerr << "Texture cache fixture upload failed\n";
            return false;
        }

        if (context.bufferUploads != 4 || context.textureUploads != 1 || context.deferredBuffers != 5) {
            std::cerr << "Unexpected texture cache fixture upload counts\n";
            return false;
        }

        ark::RenderScene scene{};
        scene.addModel(modelResource, glm::mat4{1.0f}, "TextureCacheModel");

        ark::RenderQueue queue{};
        queue.build(scene);
        if (queue.size() != 2 || queue.drawItems()[0].material != modelResource.primitiveMaterial(0) ||
            queue.drawItems()[1].material != modelResource.primitiveMaterial(1)) {
            std::cerr << "Texture cache fixture queue draw items are invalid\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateTextureResourceDeferredRelease() && validateTextureResourceMipDesc() &&
                   validateTextureCacheDeferredClear() && validateMeshResourceDeferredRelease() &&
                   validateLocalModelResourceDeferredReset() && validateExternalModelResourceDeferredReset() &&
                   validateModelResource() &&
                   validateTextureCacheFixtureModelResource()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
