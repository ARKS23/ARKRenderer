#include "renderer/TextureResource.h"

#include "asset/TextureLoader.h"
#include "core/Log.h"
#include "rhi/DeviceContext.h"
#include "rhi/RenderDevice.h"

#include <limits>

namespace ark {
    namespace {
        constexpr u32 Rgba8BytesPerPixel = 4;

        bool validateTextureImage(const asset::ImageData& image) {
            return !image.empty() && image.format == asset::ImageFormat::Rgba8Unorm &&
                   image.bytesPerPixel == Rgba8BytesPerPixel;
        }

        rhi::Format toTextureFormat(TextureColorSpace colorSpace) {
            return colorSpace == TextureColorSpace::Srgb ? rhi::Format::RGBA8Srgb : rhi::Format::RGBA8Unorm;
        }
    } // namespace

    bool TextureResource::create(rhi::RenderDevice& device,
                                 const asset::ImageData& image,
                                 const TextureResourceDesc& desc) {
        if (!validateTextureImage(image)) {
            ARK_ERROR("TextureResource requires RGBA8 image data: {}", image.debugName);
            return false;
        }

        if (image.width > std::numeric_limits<u32>::max() / image.bytesPerPixel) {
            ARK_ERROR("TextureResource row pitch overflow: {}", image.debugName);
            return false;
        }

        m_Uploaded = false;
        m_Extent = rhi::Extent2D{image.width, image.height};
        m_ColorSpace = desc.colorSpace;
        m_Format = toTextureFormat(desc.colorSpace);
        m_BytesPerPixel = image.bytesPerPixel;
        m_RowPitch = image.width * image.bytesPerPixel;

        const std::string debugName = desc.debugName.empty() ? "TextureResource" : desc.debugName;

        // CPU 像素数据只在 staging buffer 中短暂保留；真实 texture 生命周期由 RHI 对象持有。
        rhi::BufferDesc stagingBufferDesc{};
        stagingBufferDesc.debugName = debugName + ".StagingBuffer";
        stagingBufferDesc.size = image.byteSize();
        stagingBufferDesc.usage = rhi::BufferUsage::TransferSrc;
        stagingBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
        stagingBufferDesc.initialData = image.pixels.data();
        m_StagingBuffer = device.createBuffer(stagingBufferDesc);
        if (!m_StagingBuffer) {
            ARK_ERROR("TextureResource failed to create staging buffer: {}", debugName);
            return false;
        }

        rhi::TextureDesc textureDesc{};
        textureDesc.extent = m_Extent;
        textureDesc.format = m_Format;
        textureDesc.mipLevels = 1;
        textureDesc.arrayLayers = 1;
        textureDesc.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::TransferDst;
        m_Texture = device.createTexture(textureDesc);
        if (!m_Texture) {
            ARK_ERROR("TextureResource failed to create texture: {}", debugName);
            return false;
        }

        rhi::TextureViewDesc textureViewDesc{};
        textureViewDesc.format = textureDesc.format;
        m_TextureView = device.createTextureView(*m_Texture, textureViewDesc);
        if (!m_TextureView) {
            ARK_ERROR("TextureResource failed to create texture view: {}", debugName);
            return false;
        }

        rhi::SamplerDesc samplerDesc{};
        samplerDesc.debugName = debugName + ".Sampler";
        samplerDesc.minFilter = rhi::FilterMode::Linear;
        samplerDesc.magFilter = rhi::FilterMode::Linear;
        samplerDesc.mipFilter = rhi::FilterMode::Nearest;
        samplerDesc.addressU = rhi::AddressMode::Repeat;
        samplerDesc.addressV = rhi::AddressMode::Repeat;
        samplerDesc.addressW = rhi::AddressMode::Repeat;
        m_Sampler = device.createSampler(samplerDesc);
        if (!m_Sampler) {
            ARK_ERROR("TextureResource failed to create sampler: {}", debugName);
            return false;
        }

        return true;
    }

    bool TextureResource::upload(rhi::DeviceContext& context) {
        if (m_Uploaded) {
            return true;
        }

        if (!m_StagingBuffer || !m_Texture) {
            ARK_ERROR("TextureResource requires upload resources");
            return false;
        }

        // texture upload 必须在 dynamic rendering scope 外记录，由 ForwardPass::prepare() 间接触发。
        rhi::TextureUploadDesc uploadDesc{};
        uploadDesc.sourceBuffer = m_StagingBuffer.get();
        uploadDesc.texture = m_Texture.get();
        uploadDesc.extent = m_Extent;
        uploadDesc.rowPitch = m_RowPitch;
        uploadDesc.bytesPerPixel = m_BytesPerPixel;

        if (!context.uploadTextureData(uploadDesc)) {
            return false;
        }

        // copy 命令已进入当前 frame command buffer，staging 延迟到 frame fence 完成后释放。
        if (!context.deferReleaseBuffer(m_StagingBuffer)) {
            ARK_ERROR("TextureResource failed to defer staging buffer");
            return false;
        }

        m_Uploaded = true;
        return true;
    }
} // namespace ark
