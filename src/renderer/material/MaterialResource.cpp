#include "renderer/material/MaterialResource.h"

#include "asset/TextureLoader.h"
#include "core/Log.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DeviceContext.h"
#include "rhi/RenderDevice.h"

#include <limits>

namespace ark {
    namespace {
        constexpr u32 Rgba8BytesPerPixel = 4;

        bool validateMaterialImage(const asset::ImageData& image) {
            return !image.empty() && image.format == asset::ImageFormat::Rgba8Unorm &&
                   image.bytesPerPixel == Rgba8BytesPerPixel;
        }
    } // namespace

    bool MaterialResource::create(rhi::RenderDevice& device, const asset::MaterialData& material) {
        if (!material.hasBaseColorTexture()) {
            ARK_ERROR("MaterialResource requires a base color texture path");
            return false;
        }

        asset::ImageData image = asset::loadImageRgba8(material.baseColorTexturePath);
        if (!validateMaterialImage(image)) {
            ARK_ERROR("MaterialResource requires RGBA8 base color texture: {}",
                      material.baseColorTexturePath.string());
            return false;
        }

        if (image.width > std::numeric_limits<u32>::max() / image.bytesPerPixel) {
            ARK_ERROR("MaterialResource texture row pitch overflow: {}", image.debugName);
            return false;
        }

        m_Uploaded = false;
        m_TextureExtent = rhi::Extent2D{image.width, image.height};
        m_TextureBytesPerPixel = image.bytesPerPixel;
        m_TextureRowPitch = image.width * image.bytesPerPixel;

        const std::string debugName = material.debugName.empty() ? "Material" : material.debugName;

        // renderer 层只把 CPU 图片数据转换为 RHI 资源；真实 VkImage/VkBuffer 创建仍由后端实现。
        rhi::BufferDesc stagingBufferDesc{};
        stagingBufferDesc.debugName = debugName + ".BaseColorStagingBuffer";
        stagingBufferDesc.size = image.byteSize();
        stagingBufferDesc.usage = rhi::BufferUsage::TransferSrc;
        stagingBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
        stagingBufferDesc.initialData = image.pixels.data();
        m_TextureStagingBuffer = device.createBuffer(stagingBufferDesc);

        rhi::TextureDesc textureDesc{};
        textureDesc.extent = m_TextureExtent;
        textureDesc.format = rhi::Format::RGBA8Unorm;
        textureDesc.mipLevels = 1;
        textureDesc.arrayLayers = 1;
        textureDesc.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::TransferDst;
        m_Texture = device.createTexture(textureDesc);

        // Phase 0.8 最小材质只暴露整张 2D base color texture 的默认 view。
        rhi::TextureViewDesc textureViewDesc{};
        textureViewDesc.format = textureDesc.format;
        m_TextureView = device.createTextureView(*m_Texture, textureViewDesc);

        rhi::SamplerDesc samplerDesc{};
        samplerDesc.debugName = debugName + ".BaseColorSampler";
        samplerDesc.minFilter = rhi::FilterMode::Linear;
        samplerDesc.magFilter = rhi::FilterMode::Linear;
        samplerDesc.mipFilter = rhi::FilterMode::Nearest;
        samplerDesc.addressU = rhi::AddressMode::Repeat;
        samplerDesc.addressV = rhi::AddressMode::Repeat;
        samplerDesc.addressW = rhi::AddressMode::Repeat;
        m_Sampler = device.createSampler(samplerDesc);

        if (!m_TextureStagingBuffer || !m_Texture || !m_TextureView || !m_Sampler) {
            ARK_ERROR("MaterialResource failed to create base color resources");
            return false;
        }

        return true;
    }

    bool MaterialResource::upload(rhi::DeviceContext& context) {
        if (m_Uploaded) {
            return true;
        }

        if (!m_TextureStagingBuffer || !m_Texture) {
            ARK_ERROR("MaterialResource requires texture upload resources");
            return false;
        }

        // texture upload 必须在 dynamic rendering scope 外记录，由 ForwardPass::prepare() 触发。
        rhi::TextureUploadDesc uploadDesc{};
        uploadDesc.sourceBuffer = m_TextureStagingBuffer.get();
        uploadDesc.texture = m_Texture.get();
        uploadDesc.extent = m_TextureExtent;
        uploadDesc.rowPitch = m_TextureRowPitch;
        uploadDesc.bytesPerPixel = m_TextureBytesPerPixel;

        if (!context.uploadTextureData(uploadDesc)) {
            return false;
        }

        // copy 命令已经进入当前 frame command buffer，staging 需延迟到 frame fence 完成后释放。
        if (!context.deferReleaseBuffer(m_TextureStagingBuffer)) {
            ARK_ERROR("MaterialResource failed to defer texture staging buffer");
            return false;
        }

        m_Uploaded = true;
        return true;
    }

    void MaterialResource::updateDescriptorSet(rhi::DescriptorSet& descriptorSet,
                                               u32 imageBinding,
                                               u32 samplerBinding) const {
        if (!m_TextureView || !m_Sampler) {
            ARK_ERROR("MaterialResource requires texture view and sampler before descriptor update");
            return;
        }

        // binding 1/2 继续使用 separate sampled image / sampler，和 mesh.frag.hlsl 保持一致。
        rhi::SampledImageDescriptor imageDescriptor{};
        imageDescriptor.view = m_TextureView.get();
        descriptorSet.updateSampledImage(imageBinding, imageDescriptor);

        rhi::SamplerDescriptor samplerDescriptor{};
        samplerDescriptor.sampler = m_Sampler.get();
        descriptorSet.updateSampler(samplerBinding, samplerDescriptor);
    }
} // namespace ark
