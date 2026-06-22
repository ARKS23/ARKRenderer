#include "renderer/resources/EnvironmentResource.h"

#include "asset/TextureLoader.h"
#include "core/Log.h"
#include "rhi/DeviceContext.h"
#include "rhi/RenderDevice.h"

#include <limits>

namespace ark {
    namespace {
        constexpr u32 Rgba32FloatBytesPerPixel = 16;

        bool validateEnvironmentImage(const asset::ImageData& image) {
            return !image.empty() && image.format == asset::ImageFormat::Rgba32Float &&
                   image.bytesPerPixel == Rgba32FloatBytesPerPixel;
        }

        rhi::SamplerDesc makeDefaultEnvironmentSamplerDesc(const std::string& debugName) {
            rhi::SamplerDesc samplerDesc{};
            samplerDesc.debugName = debugName + ".Sampler";
            samplerDesc.minFilter = rhi::FilterMode::Linear;
            samplerDesc.magFilter = rhi::FilterMode::Linear;
            samplerDesc.mipFilter = rhi::FilterMode::Nearest;
            samplerDesc.addressU = rhi::AddressMode::Repeat;
            samplerDesc.addressV = rhi::AddressMode::ClampToEdge;
            samplerDesc.addressW = rhi::AddressMode::ClampToEdge;
            return samplerDesc;
        }
    } // namespace

    bool EnvironmentResource::create(rhi::RenderDevice& device,
                                     const asset::ImageData& image,
                                     const EnvironmentResourceDesc& desc) {
        if (!validateEnvironmentImage(image)) {
            ARK_ERROR("EnvironmentResource requires RGBA32F image data: {}", image.debugName);
            return false;
        }

        if (image.width > std::numeric_limits<u32>::max() / image.bytesPerPixel) {
            ARK_ERROR("EnvironmentResource row pitch overflow: {}", image.debugName);
            return false;
        }

        m_Uploaded = false;
        m_Extent = rhi::Extent2D{image.width, image.height};
        m_Format = rhi::Format::RGBA32Float;
        m_BytesPerPixel = image.bytesPerPixel;
        m_RowPitch = image.width * image.bytesPerPixel;
        m_MipLevels = 1;

        const std::string debugName = desc.debugName.empty() ? "EnvironmentResource" : desc.debugName;

        rhi::BufferDesc stagingBufferDesc{};
        stagingBufferDesc.debugName = debugName + ".StagingBuffer";
        stagingBufferDesc.size = image.byteSize();
        stagingBufferDesc.usage = rhi::BufferUsage::TransferSrc;
        stagingBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
        stagingBufferDesc.initialData = image.pixels.data();
        m_StagingBuffer = device.createBuffer(stagingBufferDesc);
        if (!m_StagingBuffer) {
            ARK_ERROR("EnvironmentResource failed to create staging buffer: {}", debugName);
            return false;
        }

        rhi::TextureDesc textureDesc{};
        textureDesc.extent = m_Extent;
        textureDesc.format = m_Format;
        textureDesc.mipLevels = m_MipLevels;
        textureDesc.arrayLayers = 1;
        textureDesc.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::TransferDst;
        m_Texture = device.createTexture(textureDesc);
        if (!m_Texture) {
            ARK_ERROR("EnvironmentResource failed to create texture: {}", debugName);
            return false;
        }

        rhi::TextureViewDesc textureViewDesc{};
        textureViewDesc.format = textureDesc.format;
        textureViewDesc.mipLevelCount = m_MipLevels;
        m_TextureView = device.createTextureView(*m_Texture, textureViewDesc);
        if (!m_TextureView) {
            ARK_ERROR("EnvironmentResource failed to create texture view: {}", debugName);
            return false;
        }

        rhi::SamplerDesc samplerDesc =
            desc.hasSamplerOverride ? desc.sampler : makeDefaultEnvironmentSamplerDesc(debugName);
        if (samplerDesc.debugName.empty()) {
            samplerDesc.debugName = debugName + ".Sampler";
        }
        m_Sampler = device.createSampler(samplerDesc);
        if (!m_Sampler) {
            ARK_ERROR("EnvironmentResource failed to create sampler: {}", debugName);
            return false;
        }

        return true;
    }

    bool EnvironmentResource::upload(rhi::DeviceContext& context) {
        if (m_Uploaded) {
            return true;
        }

        if (!m_StagingBuffer || !m_Texture) {
            ARK_ERROR("EnvironmentResource requires upload resources");
            return false;
        }

        rhi::TextureUploadDesc uploadDesc{};
        uploadDesc.sourceBuffer = m_StagingBuffer.get();
        uploadDesc.texture = m_Texture.get();
        uploadDesc.extent = m_Extent;
        uploadDesc.rowPitch = m_RowPitch;
        uploadDesc.bytesPerPixel = m_BytesPerPixel;

        if (!context.uploadTextureData(uploadDesc)) {
            return false;
        }

        if (!context.deferReleaseBuffer(m_StagingBuffer)) {
            ARK_ERROR("EnvironmentResource failed to defer staging buffer");
            return false;
        }

        m_Uploaded = true;
        return true;
    }

    bool EnvironmentResource::releaseDeferred(rhi::DeviceContext& context) {
        if (m_TextureView && !context.deferReleaseTextureView(m_TextureView)) {
            ARK_ERROR("EnvironmentResource failed to defer texture view");
            return false;
        }

        if (m_Sampler && !context.deferReleaseSampler(m_Sampler)) {
            ARK_ERROR("EnvironmentResource failed to defer sampler");
            return false;
        }

        if (m_Texture && !context.deferReleaseTexture(m_Texture)) {
            ARK_ERROR("EnvironmentResource failed to defer texture");
            return false;
        }

        if (m_StagingBuffer && !context.deferReleaseBuffer(m_StagingBuffer)) {
            ARK_ERROR("EnvironmentResource failed to defer staging buffer");
            return false;
        }

        m_Extent = {};
        m_Format = rhi::Format::Unknown;
        m_RowPitch = 0;
        m_BytesPerPixel = 0;
        m_MipLevels = 1;
        m_Uploaded = false;
        return true;
    }

    void EnvironmentResource::resetImmediate() {
        m_StagingBuffer.reset();
        m_TextureView.reset();
        m_Sampler.reset();
        m_Texture.reset();
        m_Extent = {};
        m_Format = rhi::Format::Unknown;
        m_RowPitch = 0;
        m_BytesPerPixel = 0;
        m_MipLevels = 1;
        m_Uploaded = false;
    }
} // namespace ark
