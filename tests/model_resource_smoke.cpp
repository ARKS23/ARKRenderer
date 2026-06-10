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
#include <vector>

namespace {
    bool near(float lhs, float rhs) {
        return std::fabs(lhs - rhs) < 0.0001f;
    }

    bool textureTransformNear(const ark::MaterialTextureTransform& transform,
                              float offsetX,
                              float offsetY,
                              float scaleX,
                              float scaleY,
                              float rotation) {
        return near(transform.offset[0], offsetX) && near(transform.offset[1], offsetY) &&
               near(transform.scale[0], scaleX) && near(transform.scale[1], scaleY) &&
               near(transform.rotation, rotation);
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
        void updateUniformBuffer(ark::u32 binding, const ark::rhi::BufferDescriptor&) override {
            ++uniformUpdates;
            if (binding < uniformBindings.size()) {
                uniformBindings[binding] = true;
            }
        }

        void updateSampledImage(ark::u32 binding, const ark::rhi::SampledImageDescriptor&) override {
            ++sampledImageUpdates;
            if (binding < sampledImageBindings.size()) {
                sampledImageBindings[binding] = true;
            }
        }

        void updateSampler(ark::u32 binding, const ark::rhi::SamplerDescriptor&) override {
            ++samplerUpdates;
            if (binding < samplerBindings.size()) {
                samplerBindings[binding] = true;
            }
        }

        int uniformUpdates = 0;
        int sampledImageUpdates = 0;
        int samplerUpdates = 0;
        std::array<bool, 14> uniformBindings{};
        std::array<bool, 14> sampledImageBindings{};
        std::array<bool, 14> samplerBindings{};
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

        int bufferCount = 0;
        int textureCount = 0;
        int textureViewCount = 0;
        int samplerCount = 0;
        ark::rhi::TextureDesc lastTextureDesc{};
        ark::rhi::TextureViewDesc lastTextureViewDesc{};
        ark::rhi::SamplerDesc lastSamplerDesc{};
        std::vector<ark::rhi::SamplerDesc> samplerDescs;
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

    bool hasSamplerDesc(const FakeRenderDevice& device,
                        ark::rhi::FilterMode minFilter,
                        ark::rhi::FilterMode magFilter,
                        ark::rhi::FilterMode mipFilter,
                        ark::rhi::AddressMode addressU,
                        ark::rhi::AddressMode addressV) {
        for (const ark::rhi::SamplerDesc& desc : device.samplerDescs) {
            if (desc.minFilter == minFilter && desc.magFilter == magFilter &&
                desc.mipFilter == mipFilter && desc.addressU == addressU && desc.addressV == addressV) {
                return true;
            }
        }

        return false;
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

    bool validateTextureCacheSamplerKey() {
        const ark::Path texturePath = findTexturePath();
        if (texturePath.empty()) {
            std::cerr << "Failed to find texture asset\n";
            return false;
        }

        FakeRenderDevice device{};
        ark::TextureCache textureCache{};
        ark::TextureResourceDesc defaultDesc{};
        defaultDesc.path = texturePath;
        defaultDesc.colorSpace = ark::TextureColorSpace::Srgb;
        defaultDesc.debugName = "SamplerKeyDefault";

        ark::TextureResource* defaultTexture = textureCache.getOrCreate(device, defaultDesc);
        ark::TextureResource* defaultTextureAgain = textureCache.getOrCreate(device, defaultDesc);

        ark::TextureResourceDesc explicitDesc = defaultDesc;
        explicitDesc.debugName = "SamplerKeyExplicit";
        explicitDesc.hasSamplerOverride = true;
        explicitDesc.sampler.debugName = "SamplerKeyExplicit.Sampler";
        explicitDesc.sampler.minFilter = ark::rhi::FilterMode::Nearest;
        explicitDesc.sampler.magFilter = ark::rhi::FilterMode::Nearest;
        explicitDesc.sampler.mipFilter = ark::rhi::FilterMode::Linear;
        explicitDesc.sampler.addressU = ark::rhi::AddressMode::ClampToEdge;
        explicitDesc.sampler.addressV = ark::rhi::AddressMode::MirroredRepeat;
        explicitDesc.sampler.addressW = ark::rhi::AddressMode::Repeat;

        ark::TextureResource* explicitTexture = textureCache.getOrCreate(device, explicitDesc);
        if (!defaultTexture || !explicitTexture || defaultTexture != defaultTextureAgain ||
            defaultTexture == explicitTexture || textureCache.size() != 2 || device.textureCount != 2 ||
            device.textureViewCount != 2 || device.samplerCount != 2) {
            std::cerr << "TextureCache sampler key did not separate sampler variants\n";
            return false;
        }

        if (!hasSamplerDesc(device,
                            ark::rhi::FilterMode::Nearest,
                            ark::rhi::FilterMode::Nearest,
                            ark::rhi::FilterMode::Linear,
                            ark::rhi::AddressMode::ClampToEdge,
                            ark::rhi::AddressMode::MirroredRepeat)) {
            std::cerr << "TextureCache explicit sampler desc was not created\n";
            return false;
        }

        return true;
    }

    bool validateTextureCacheFallbacks() {
        FakeRenderDevice device{};
        ark::TextureCache textureCache{};

        ark::TextureResource* white = textureCache.getOrCreateFallback(device, ark::FallbackTextureKind::White);
        ark::TextureResource* whiteAgain = textureCache.getOrCreateFallback(device, ark::FallbackTextureKind::White);
        ark::TextureResource* normal =
            textureCache.getOrCreateFallback(device, ark::FallbackTextureKind::FlatNormal);
        ark::TextureResource* metallicRoughness =
            textureCache.getOrCreateFallback(device, ark::FallbackTextureKind::MetallicRoughnessDefault);
        ark::TextureResource* occlusion =
            textureCache.getOrCreateFallback(device, ark::FallbackTextureKind::OcclusionDefault);
        ark::TextureResource* emissive = textureCache.getOrCreateFallback(device, ark::FallbackTextureKind::Black);

        if (!white || !normal || !metallicRoughness || !occlusion || !emissive || white != whiteAgain) {
            std::cerr << "TextureCache fallback texture reuse failed\n";
            return false;
        }

        if (textureCache.size() != 5 || device.bufferCount != 5 || device.textureCount != 5 ||
            device.textureViewCount != 5 || device.samplerCount != 5) {
            std::cerr << "TextureCache fallback resource counts are invalid\n";
            return false;
        }

        if (white->colorSpace() != ark::TextureColorSpace::Srgb ||
            white->format() != ark::rhi::Format::RGBA8Srgb ||
            emissive->colorSpace() != ark::TextureColorSpace::Srgb ||
            emissive->format() != ark::rhi::Format::RGBA8Srgb) {
            std::cerr << "TextureCache fallback color texture format is invalid\n";
            return false;
        }

        if (normal->colorSpace() != ark::TextureColorSpace::Linear ||
            normal->format() != ark::rhi::Format::RGBA8Unorm ||
            metallicRoughness->colorSpace() != ark::TextureColorSpace::Linear ||
            metallicRoughness->format() != ark::rhi::Format::RGBA8Unorm ||
            occlusion->colorSpace() != ark::TextureColorSpace::Linear ||
            occlusion->format() != ark::rhi::Format::RGBA8Unorm) {
            std::cerr << "TextureCache fallback data texture format is invalid\n";
            return false;
        }

        if (white->mipLevels() != 1 || normal->mipLevels() != 1 || metallicRoughness->mipLevels() != 1 ||
            occlusion->mipLevels() != 1 || emissive->mipLevels() != 1) {
            std::cerr << "TextureCache fallback mip level count is invalid\n";
            return false;
        }

        return true;
    }

    bool validateMaterialResourceTextureSlots() {
        FakeRenderDevice device{};
        ark::TextureCache textureCache{};

        ark::MaterialTextureSet textures{};
        textures.baseColor = textureCache.getOrCreateFallback(device, ark::FallbackTextureKind::White);
        textures.normal = textureCache.getOrCreateFallback(device, ark::FallbackTextureKind::FlatNormal);
        textures.metallicRoughness =
            textureCache.getOrCreateFallback(device, ark::FallbackTextureKind::MetallicRoughnessDefault);
        textures.occlusion = textureCache.getOrCreateFallback(device, ark::FallbackTextureKind::OcclusionDefault);
        textures.emissive = textureCache.getOrCreateFallback(device, ark::FallbackTextureKind::Black);

        ark::asset::MaterialData material{};
        material.debugName = "TextureSlotMaterial";
        material.baseColorTexturePath = "assets/textures/xiaowei.png";
        material.baseColorFactor[0] = 0.25f;
        material.baseColorFactor[1] = 0.5f;
        material.baseColorFactor[2] = 0.75f;
        material.baseColorFactor[3] = 0.8f;
        material.emissiveFactor[0] = 0.1f;
        material.emissiveFactor[1] = 0.2f;
        material.emissiveFactor[2] = 0.3f;
        material.metallicFactor = 0.35f;
        material.roughnessFactor = 0.65f;
        material.normalScale = 0.75f;
        material.occlusionStrength = 0.5f;
        material.baseColorTexture.transform.offset[0] = 0.1f;
        material.baseColorTexture.transform.offset[1] = 0.2f;
        material.baseColorTexture.transform.scale[0] = 2.0f;
        material.baseColorTexture.transform.scale[1] = 3.0f;
        material.baseColorTexture.transform.rotation = 0.5f;
        material.baseColorTexture.transform.hasTransform = true;
        material.normalTexture.transform.offset[0] = -0.25f;
        material.normalTexture.transform.scale[0] = 0.75f;
        material.normalTexture.transform.scale[1] = 0.5f;
        material.normalTexture.transform.rotation = 1.0f;
        material.normalTexture.transform.hasTransform = true;
        material.metallicRoughnessTexture.transform.offset[1] = 0.4f;
        material.metallicRoughnessTexture.transform.scale[0] = 1.5f;
        material.metallicRoughnessTexture.transform.hasTransform = true;

        ark::MaterialResource materialResource{};
        if (!materialResource.create(material, textures)) {
            std::cerr << "MaterialResource texture slot create failed\n";
            return false;
        }

        const ark::MaterialFactors& factors = materialResource.factors();
        if (!near(factors.baseColorFactor[0], 0.25f) || !near(factors.baseColorFactor[1], 0.5f) ||
            !near(factors.baseColorFactor[2], 0.75f) || !near(factors.baseColorFactor[3], 0.8f) ||
            !near(factors.emissiveFactor[0], 0.1f) || !near(factors.emissiveFactor[1], 0.2f) ||
            !near(factors.emissiveFactor[2], 0.3f) || !near(factors.metallicFactor, 0.35f) ||
            !near(factors.roughnessFactor, 0.65f) || !near(factors.normalScale, 0.75f) ||
            !near(factors.occlusionStrength, 0.5f)) {
            std::cerr << "MaterialResource did not preserve multi-slot factors\n";
            return false;
        }

        const ark::MaterialTextureTransformSet& transforms = materialResource.textureTransforms();
        if (!textureTransformNear(transforms.baseColor, 0.1f, 0.2f, 2.0f, 3.0f, 0.5f) ||
            !textureTransformNear(transforms.normal, -0.25f, 0.0f, 0.75f, 0.5f, 1.0f) ||
            !textureTransformNear(transforms.metallicRoughness, 0.0f, 0.4f, 1.5f, 1.0f, 0.0f) ||
            !textureTransformNear(transforms.occlusion, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f) ||
            !textureTransformNear(transforms.emissive, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f)) {
            std::cerr << "MaterialResource did not preserve texture transforms\n";
            return false;
        }

        FakeDeviceContext context{};
        if (!materialResource.upload(context) || !materialResource.isReady()) {
            std::cerr << "MaterialResource multi-slot upload failed\n";
            return false;
        }

        if (context.textureUploads != 5 || context.deferredBuffers != 5) {
            std::cerr << "MaterialResource multi-slot upload counts are invalid\n";
            return false;
        }

        FakeDescriptorSet descriptorSet{};
        ark::MaterialTextureBindingSet bindings{};
        if (!materialResource.updateDescriptorSet(descriptorSet, bindings)) {
            std::cerr << "MaterialResource multi-slot descriptor update failed\n";
            return false;
        }

        if (descriptorSet.sampledImageUpdates != 5 || descriptorSet.samplerUpdates != 5 ||
            !descriptorSet.sampledImageBindings[1] || !descriptorSet.samplerBindings[2] ||
            !descriptorSet.sampledImageBindings[5] || !descriptorSet.samplerBindings[6] ||
            !descriptorSet.sampledImageBindings[7] || !descriptorSet.samplerBindings[8] ||
            !descriptorSet.sampledImageBindings[9] || !descriptorSet.samplerBindings[10] ||
            !descriptorSet.sampledImageBindings[11] || !descriptorSet.samplerBindings[12]) {
            std::cerr << "MaterialResource did not write all texture slot descriptors\n";
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

        if (context.bufferUploads != 2 || context.textureUploads != 5 || context.deferredBuffers != 7) {
            std::cerr << "Local ModelResource upload counts are invalid\n";
            return false;
        }

        if (!modelResource.resetDeferred(context)) {
            std::cerr << "Local ModelResource deferred reset failed\n";
            return false;
        }

        if (!modelResource.empty() || modelResource.meshCount() != 0 || modelResource.materialCount() != 0 ||
            modelResource.instanceCount() != 0 || context.deferredBuffers != 9 || context.deferredTextures != 5 ||
            context.deferredTextureViews != 5 || context.deferredSamplers != 5) {
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

        if (textureCache.size() != 5) {
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

        if (!modelResource.empty() || textureCache.size() != 5 || context.deferredBuffers != 9 ||
            context.deferredTextures != 0 || context.deferredTextureViews != 0 || context.deferredSamplers != 0) {
            std::cerr << "External ModelResource reset touched external texture cache\n";
            return false;
        }

        if (!textureCache.clearDeferred(context)) {
            std::cerr << "External texture cache deferred clear failed\n";
            return false;
        }

        if (textureCache.size() != 0 || context.deferredTextures != 5 || context.deferredTextureViews != 5 ||
            context.deferredSamplers != 5) {
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
        material.normalTexturePath = texturePath;
        material.baseColorFactor[0] = 0.2f;
        material.baseColorFactor[1] = 0.4f;
        material.baseColorFactor[2] = 0.6f;
        material.baseColorFactor[3] = 0.8f;
        material.emissiveFactor[0] = 0.1f;
        material.emissiveFactor[1] = 0.2f;
        material.emissiveFactor[2] = 0.3f;
        material.metallicFactor = 0.3f;
        material.roughnessFactor = 0.7f;
        material.normalScale = 0.6f;
        material.occlusionStrength = 0.9f;
        material.alphaMode = ark::asset::AlphaMode::Mask;
        material.alphaCutoff = 0.35f;
        material.doubleSided = true;
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

        if (textureCache.size() != 5 || device.textureCount != 5 || device.textureViewCount != 5 ||
            device.samplerCount != 5) {
            std::cerr << "TextureCache did not reuse multi-slot material textures\n";
            return false;
        }

        const ark::MaterialTextureSet& materialTextures = modelResource.primitiveMaterial(0)->textures();
        const ark::MaterialTextureSet& duplicateTextures = modelResource.primitiveMaterial(1)->textures();
        if (!materialTextures.baseColor || !materialTextures.normal || !materialTextures.metallicRoughness ||
            !materialTextures.occlusion || !materialTextures.emissive ||
            materialTextures.baseColor != duplicateTextures.baseColor ||
            materialTextures.normal != duplicateTextures.normal ||
            materialTextures.metallicRoughness != duplicateTextures.metallicRoughness ||
            materialTextures.occlusion != duplicateTextures.occlusion ||
            materialTextures.emissive != duplicateTextures.emissive) {
            std::cerr << "ModelResource did not reuse shared material texture slots\n";
            return false;
        }

        if (materialTextures.baseColor == materialTextures.normal ||
            materialTextures.baseColor->format() != ark::rhi::Format::RGBA8Srgb ||
            materialTextures.normal->format() != ark::rhi::Format::RGBA8Unorm) {
            std::cerr << "TextureCache did not separate sRGB and linear texture cache keys\n";
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
            !near(materialFactors.emissiveFactor[0], 0.1f) ||
            !near(materialFactors.emissiveFactor[1], 0.2f) ||
            !near(materialFactors.emissiveFactor[2], 0.3f) ||
            !near(materialFactors.metallicFactor, 0.3f) ||
            !near(materialFactors.roughnessFactor, 0.7f) ||
            !near(materialFactors.normalScale, 0.6f) ||
            !near(materialFactors.occlusionStrength, 0.9f)) {
            std::cerr << "ModelResource did not preserve material factors\n";
            return false;
        }

        const ark::MaterialRenderState& renderState = modelResource.primitiveMaterial(0)->renderState();
        if (renderState.alphaMode != ark::asset::AlphaMode::Mask || !near(renderState.alphaCutoff, 0.35f) ||
            !renderState.doubleSided) {
            std::cerr << "ModelResource did not preserve material render state\n";
            return false;
        }

        FakeDeviceContext context{};
        if (!modelResource.upload(context)) {
            std::cerr << "ModelResource upload failed\n";
            return false;
        }

        if (context.bufferUploads != 4 || context.textureUploads != 5 || context.deferredBuffers != 9) {
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
            textureCache.size() != 5 || device.textureCount != 5 || device.textureViewCount != 5 ||
            device.samplerCount != 5) {
            std::cerr << "Texture cache fixture did not reuse shared texture resources\n";
            return false;
        }

        const ark::MaterialTextureSet& firstTextures = modelResource.primitiveMaterial(0)->textures();
        const ark::MaterialTextureSet& secondTextures = modelResource.primitiveMaterial(1)->textures();
        if (!firstTextures.baseColor || firstTextures.baseColor != secondTextures.baseColor ||
            firstTextures.baseColor->format() != ark::rhi::Format::RGBA8Srgb ||
            !firstTextures.normal || firstTextures.normal != secondTextures.normal ||
            firstTextures.normal->format() != ark::rhi::Format::RGBA8Unorm ||
            !firstTextures.emissive || firstTextures.emissive != secondTextures.emissive ||
            firstTextures.emissive->format() != ark::rhi::Format::RGBA8Srgb) {
            std::cerr << "Texture cache fixture material texture slots are invalid\n";
            return false;
        }

        FakeDeviceContext context{};
        if (!modelResource.upload(context)) {
            std::cerr << "Texture cache fixture upload failed\n";
            return false;
        }

        if (context.bufferUploads != 4 || context.textureUploads != 5 || context.deferredBuffers != 9) {
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

    bool validateAlphaModesModelResource() {
        const ark::Path modelPath = findModelPath(ark::Path{"assets/models/alpha_modes_fixture.gltf"});
        if (modelPath.empty()) {
            std::cerr << "Failed to find alpha modes fixture\n";
            return false;
        }

        const ark::asset::ModelData modelData = ark::asset::loadGltfModel(modelPath);
        if (modelData.empty() || modelData.meshes.size() != 3 || modelData.materials.size() != 3) {
            std::cerr << "Unexpected alpha modes fixture model data\n";
            return false;
        }

        FakeRenderDevice device{};
        ark::TextureCache textureCache{};
        ark::ModelResource modelResource{};
        if (!modelResource.create(device, textureCache, modelData)) {
            std::cerr << "Alpha modes ModelResource create failed\n";
            return false;
        }

        if (modelResource.primitiveCount() != 3 || modelResource.materialCount() != 3 ||
            textureCache.size() != 5 || device.textureCount != 5 || device.textureViewCount != 5 ||
            device.samplerCount != 5) {
            std::cerr << "Alpha modes fixture resource counts are invalid\n";
            return false;
        }

        const ark::MaterialRenderState& opaque = modelResource.primitiveMaterial(0)->renderState();
        const ark::MaterialRenderState& mask = modelResource.primitiveMaterial(1)->renderState();
        const ark::MaterialRenderState& blend = modelResource.primitiveMaterial(2)->renderState();
        if (opaque.alphaMode != ark::asset::AlphaMode::Opaque || !near(opaque.alphaCutoff, 0.5f) ||
            opaque.doubleSided ||
            mask.alphaMode != ark::asset::AlphaMode::Mask || !near(mask.alphaCutoff, 0.35f) ||
            !mask.doubleSided ||
            blend.alphaMode != ark::asset::AlphaMode::Blend || !near(blend.alphaCutoff, 0.5f) ||
            blend.doubleSided) {
            std::cerr << "Alpha modes MaterialResource render state is invalid\n";
            return false;
        }

        return true;
    }

    bool validateRenderQueueAlphaBucketsForModelResource() {
        const ark::Path texturePath = findTexturePath();
        if (texturePath.empty()) {
            std::cerr << "Failed to find texture asset for alpha bucket model\n";
            return false;
        }

        auto makeMaterial = [&texturePath](const char* name, ark::asset::AlphaMode alphaMode) {
            ark::asset::MaterialData material{};
            material.debugName = name;
            material.baseColorTexturePath = texturePath;
            material.alphaMode = alphaMode;
            return material;
        };

        ark::asset::ModelData modelData{};
        modelData.debugName = "AlphaBucketModel";
        modelData.materials.push_back(makeMaterial("BlendA", ark::asset::AlphaMode::Blend));
        modelData.materials.push_back(makeMaterial("OpaqueA", ark::asset::AlphaMode::Opaque));
        modelData.materials.push_back(makeMaterial("Mask", ark::asset::AlphaMode::Mask));
        modelData.materials.push_back(makeMaterial("BlendB", ark::asset::AlphaMode::Blend));
        modelData.materials.push_back(makeMaterial("OpaqueB", ark::asset::AlphaMode::Opaque));
        modelData.meshes.push_back(makeTriangle("BlendPrimitiveA", 0, -2.0f));
        modelData.meshes.push_back(makeTriangle("OpaquePrimitiveA", 1, -1.0f));
        modelData.meshes.push_back(makeTriangle("MaskPrimitive", 2, 0.0f));
        modelData.meshes.push_back(makeTriangle("BlendPrimitiveB", 3, 1.0f));
        modelData.meshes.push_back(makeTriangle("OpaquePrimitiveB", 4, 2.0f));

        FakeRenderDevice device{};
        ark::TextureCache textureCache{};
        ark::ModelResource modelResource{};
        if (!modelResource.create(device, textureCache, modelData)) {
            std::cerr << "Alpha bucket ModelResource create failed\n";
            return false;
        }

        ark::RenderScene scene{};
        scene.addModel(modelResource, glm::mat4{1.0f}, "AlphaBucketModel");

        ark::RenderQueue queue{};
        queue.build(scene);
        const std::span<const ark::DrawItem> drawItems = queue.drawItems();
        if (drawItems.size() != 5) {
            std::cerr << "Alpha bucket model queue item count is invalid\n";
            return false;
        }

        if (drawItems[0].material != modelResource.primitiveMaterial(1) ||
            drawItems[1].material != modelResource.primitiveMaterial(4) ||
            drawItems[2].material != modelResource.primitiveMaterial(2) ||
            drawItems[3].material != modelResource.primitiveMaterial(0) ||
            drawItems[4].material != modelResource.primitiveMaterial(3)) {
            std::cerr << "RenderQueue did not bucket model primitive materials correctly\n";
            return false;
        }

        if (drawItems[0].debugName != "OpaquePrimitiveA" ||
            drawItems[1].debugName != "OpaquePrimitiveB" ||
            drawItems[2].debugName != "MaskPrimitive" ||
            drawItems[3].debugName != "BlendPrimitiveA" ||
            drawItems[4].debugName != "BlendPrimitiveB") {
            std::cerr << "RenderQueue did not preserve model primitive order within alpha buckets\n";
            return false;
        }

        return true;
    }

    bool validateTexcoord1ModelResource() {
        const ark::Path modelPath = findModelPath(ark::Path{"assets/models/texcoord1_fixture.gltf"});
        if (modelPath.empty()) {
            std::cerr << "Failed to find TEXCOORD_1 fixture\n";
            return false;
        }

        const ark::asset::ModelData modelData = ark::asset::loadGltfModel(modelPath);
        if (modelData.empty() || modelData.meshes.size() != 1 || modelData.materials.size() != 1) {
            std::cerr << "Unexpected TEXCOORD_1 fixture model data\n";
            return false;
        }

        FakeRenderDevice device{};
        ark::TextureCache textureCache{};
        ark::ModelResource modelResource{};
        if (!modelResource.create(device, textureCache, modelData)) {
            std::cerr << "TEXCOORD_1 ModelResource create failed\n";
            return false;
        }

        if (!modelResource.primitiveMaterial(0)) {
            std::cerr << "TEXCOORD_1 primitive material lookup failed\n";
            return false;
        }

        const ark::MaterialTextureCoordinateSet& coordinates =
            modelResource.primitiveMaterial(0)->textureCoordinates();
        if (coordinates.baseColor != 0 || coordinates.normal != 1 ||
            coordinates.metallicRoughness != 1 || coordinates.occlusion != 1 ||
            coordinates.emissive != 1) {
            std::cerr << "TEXCOORD_1 MaterialResource coordinate set is invalid\n";
            return false;
        }

        return true;
    }

    bool validateTextureTransformModelResource() {
        const ark::Path modelPath = findModelPath(ark::Path{"assets/models/texture_transform_fixture.gltf"});
        if (modelPath.empty()) {
            std::cerr << "Failed to find texture transform fixture\n";
            return false;
        }

        const ark::asset::ModelData modelData = ark::asset::loadGltfModel(modelPath);
        if (modelData.empty() || modelData.meshes.size() != 1 || modelData.materials.size() != 1) {
            std::cerr << "Unexpected texture transform fixture model data\n";
            return false;
        }

        FakeRenderDevice device{};
        ark::TextureCache textureCache{};
        ark::ModelResource modelResource{};
        if (!modelResource.create(device, textureCache, modelData)) {
            std::cerr << "Texture transform ModelResource create failed\n";
            return false;
        }

        if (modelResource.primitiveCount() != 1 || modelResource.materialCount() != 1 ||
            textureCache.size() != 2 || device.textureCount != 2 ||
            device.textureViewCount != 2 || device.samplerCount != 2) {
            std::cerr << "Texture transform fixture resource counts are invalid\n";
            return false;
        }

        const ark::MaterialResource* material = modelResource.primitiveMaterial(0);
        if (!material) {
            std::cerr << "Texture transform primitive material lookup failed\n";
            return false;
        }

        const ark::MaterialTextureCoordinateSet& coordinates = material->textureCoordinates();
        if (coordinates.baseColor != 1 || coordinates.normal != 0 ||
            coordinates.metallicRoughness != 1 || coordinates.occlusion != 0 ||
            coordinates.emissive != 1) {
            std::cerr << "Texture transform MaterialResource coordinate set is invalid\n";
            return false;
        }

        const ark::MaterialTextureTransformSet& transforms = material->textureTransforms();
        if (!textureTransformNear(transforms.baseColor, 0.125f, 0.25f, 2.0f, 0.5f, 0.5f) ||
            !textureTransformNear(transforms.normal, -0.25f, 0.0f, 0.75f, 0.5f, 1.0f) ||
            !textureTransformNear(transforms.metallicRoughness, 0.0f, 0.4f, 1.5f, 1.0f, 0.0f) ||
            !textureTransformNear(transforms.occlusion, 0.2f, -0.1f, 1.0f, 2.0f, -0.25f) ||
            !textureTransformNear(transforms.emissive, 0.0f, 0.0f, 1.0f, 1.0f, 0.25f)) {
            std::cerr << "Texture transform MaterialResource transform set is invalid\n";
            return false;
        }

        const ark::MaterialTextureSet& textures = material->textures();
        if (!textures.baseColor || !textures.normal || !textures.metallicRoughness ||
            !textures.occlusion || !textures.emissive ||
            textures.baseColor != textures.emissive ||
            textures.normal != textures.metallicRoughness ||
            textures.normal != textures.occlusion ||
            textures.baseColor == textures.normal) {
            std::cerr << "Texture transform resources were not reused by color space\n";
            return false;
        }

        return true;
    }

    bool validateModelResourceSamplerOverride() {
        const ark::Path texturePath = findTexturePath();
        if (texturePath.empty()) {
            std::cerr << "Failed to find texture asset\n";
            return false;
        }

        ark::asset::MaterialData material{};
        material.debugName = "SamplerOverrideMaterial";
        material.baseColorTexture.path = texturePath;
        material.baseColorTexture.hasSampler = true;
        material.baseColorTexture.sampler.minFilter = ark::asset::TextureFilter::Nearest;
        material.baseColorTexture.sampler.magFilter = ark::asset::TextureFilter::Nearest;
        material.baseColorTexture.sampler.mipFilter = ark::asset::TextureFilter::Linear;
        material.baseColorTexture.sampler.addressU = ark::asset::TextureAddressMode::ClampToEdge;
        material.baseColorTexture.sampler.addressV = ark::asset::TextureAddressMode::MirroredRepeat;
        material.baseColorTexturePath = material.baseColorTexture.path;

        ark::asset::ModelData modelData{};
        modelData.debugName = "SamplerOverrideModel";
        modelData.materials.push_back(material);
        modelData.meshes.push_back(makeTriangle("SamplerOverrideTriangle", 0, 0.0f));

        FakeRenderDevice device{};
        ark::TextureCache textureCache{};
        ark::ModelResource modelResource{};
        if (!modelResource.create(device, textureCache, modelData)) {
            std::cerr << "Sampler override ModelResource create failed\n";
            return false;
        }

        if (textureCache.size() != 5 || device.textureCount != 5 || device.textureViewCount != 5 ||
            device.samplerCount != 5) {
            std::cerr << "Sampler override ModelResource resource counts are invalid\n";
            return false;
        }

        if (!hasSamplerDesc(device,
                            ark::rhi::FilterMode::Nearest,
                            ark::rhi::FilterMode::Nearest,
                            ark::rhi::FilterMode::Linear,
                            ark::rhi::AddressMode::ClampToEdge,
                            ark::rhi::AddressMode::MirroredRepeat)) {
            std::cerr << "ModelResource did not pass asset sampler to RHI sampler desc\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateTextureResourceDeferredRelease() && validateTextureResourceMipDesc() &&
                   validateTextureCacheDeferredClear() && validateTextureCacheSamplerKey() &&
                   validateTextureCacheFallbacks() &&
                   validateMaterialResourceTextureSlots() && validateMeshResourceDeferredRelease() &&
                   validateLocalModelResourceDeferredReset() && validateExternalModelResourceDeferredReset() &&
                   validateModelResource() &&
                   validateTextureCacheFixtureModelResource() && validateAlphaModesModelResource() &&
                   validateRenderQueueAlphaBucketsForModelResource() &&
                   validateTexcoord1ModelResource() &&
                   validateTextureTransformModelResource() &&
                   validateModelResourceSamplerOverride()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
