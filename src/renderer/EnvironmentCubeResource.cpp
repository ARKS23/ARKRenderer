#include "renderer/EnvironmentCubeResource.h"

#include "core/Log.h"
#include "rhi/DeviceContext.h"
#include "rhi/RenderDevice.h"

namespace ark {
    namespace {
        bool isSupportedEnvironmentCubeFormat(rhi::Format format) {
            return format == rhi::Format::RGBA16Float || format == rhi::Format::RGBA32Float;
        }

        bool validateEnvironmentCubeDesc(const EnvironmentCubeResourceDesc& desc) {
            if (!rhi::isValidExtent(desc.faceExtent)) {
                ARK_ERROR("EnvironmentCubeResource requires a valid face extent");
                return false;
            }

            if (desc.faceExtent.width != desc.faceExtent.height) {
                ARK_ERROR("EnvironmentCubeResource requires square faces");
                return false;
            }

            if (desc.mipLevels == 0) {
                ARK_ERROR("EnvironmentCubeResource requires at least one mip level");
                return false;
            }

            if (!isSupportedEnvironmentCubeFormat(desc.format)) {
                ARK_ERROR("EnvironmentCubeResource requires RGBA16Float or RGBA32Float format");
                return false;
            }

            const u32 maxMipLevels = rhi::calculateMipLevelCount(desc.faceExtent);
            if (desc.mipLevels > maxMipLevels) {
                ARK_ERROR("EnvironmentCubeResource mip levels exceed face extent");
                return false;
            }

            return true;
        }

        rhi::SamplerDesc makeDefaultEnvironmentCubeSamplerDesc(const std::string& debugName, u32 mipLevels) {
            rhi::SamplerDesc samplerDesc{};
            samplerDesc.debugName = debugName + ".Sampler";
            samplerDesc.minFilter = rhi::FilterMode::Linear;
            samplerDesc.magFilter = rhi::FilterMode::Linear;
            samplerDesc.mipFilter = mipLevels > 1 ? rhi::FilterMode::Linear : rhi::FilterMode::Nearest;
            samplerDesc.addressU = rhi::AddressMode::ClampToEdge;
            samplerDesc.addressV = rhi::AddressMode::ClampToEdge;
            samplerDesc.addressW = rhi::AddressMode::ClampToEdge;
            return samplerDesc;
        }
    } // namespace

    bool EnvironmentCubeResource::create(rhi::RenderDevice& device, const EnvironmentCubeResourceDesc& desc) {
        if (!validateEnvironmentCubeDesc(desc)) {
            return false;
        }

        m_FaceExtent = desc.faceExtent;
        m_Format = desc.format;
        m_MipLevels = desc.mipLevels;

        const std::string debugName = desc.debugName.empty() ? "EnvironmentCubeResource" : desc.debugName;

        rhi::TextureDesc textureDesc{};
        textureDesc.extent = m_FaceExtent;
        textureDesc.format = m_Format;
        textureDesc.mipLevels = m_MipLevels;
        textureDesc.arrayLayers = FaceCount;
        textureDesc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
        if (desc.allowReadback) {
            textureDesc.usage = textureDesc.usage | rhi::TextureUsage::TransferSrc;
        }
        textureDesc.type = rhi::TextureType::Cube;
        m_Texture = device.createTexture(textureDesc);
        if (!m_Texture) {
            ARK_ERROR("EnvironmentCubeResource failed to create texture: {}", debugName);
            resetImmediate();
            return false;
        }

        rhi::TextureViewDesc textureViewDesc{};
        textureViewDesc.format = textureDesc.format;
        textureViewDesc.mipLevelCount = m_MipLevels;
        textureViewDesc.arrayLayerCount = FaceCount;
        textureViewDesc.type = rhi::TextureViewType::Cube;
        m_TextureView = device.createTextureView(*m_Texture, textureViewDesc);
        if (!m_TextureView) {
            ARK_ERROR("EnvironmentCubeResource failed to create texture view: {}", debugName);
            resetImmediate();
            return false;
        }

        for (u32 faceIndex = 0; faceIndex < FaceCount; ++faceIndex) {
            rhi::TextureViewDesc faceViewDesc{};
            faceViewDesc.format = textureDesc.format;
            faceViewDesc.baseMipLevel = 0;
            faceViewDesc.mipLevelCount = 1;
            faceViewDesc.baseArrayLayer = faceIndex;
            faceViewDesc.arrayLayerCount = 1;
            faceViewDesc.type = rhi::TextureViewType::Texture2D;
            m_FaceViews[faceIndex] = device.createTextureView(*m_Texture, faceViewDesc);
            if (!m_FaceViews[faceIndex]) {
                ARK_ERROR("EnvironmentCubeResource failed to create face texture view {}: {}", faceIndex, debugName);
                resetImmediate();
                return false;
            }
        }

        rhi::SamplerDesc samplerDesc =
            desc.hasSamplerOverride ? desc.sampler : makeDefaultEnvironmentCubeSamplerDesc(debugName, m_MipLevels);
        if (samplerDesc.debugName.empty()) {
            samplerDesc.debugName = debugName + ".Sampler";
        }
        m_Sampler = device.createSampler(samplerDesc);
        if (!m_Sampler) {
            ARK_ERROR("EnvironmentCubeResource failed to create sampler: {}", debugName);
            resetImmediate();
            return false;
        }

        return true;
    }

    bool EnvironmentCubeResource::releaseDeferred(rhi::DeviceContext& context) {
        for (Scope<rhi::TextureView>& faceView : m_FaceViews) {
            if (faceView && !context.deferReleaseTextureView(faceView)) {
                ARK_ERROR("EnvironmentCubeResource failed to defer face texture view");
                return false;
            }
        }

        if (m_TextureView && !context.deferReleaseTextureView(m_TextureView)) {
            ARK_ERROR("EnvironmentCubeResource failed to defer texture view");
            return false;
        }

        if (m_Sampler && !context.deferReleaseSampler(m_Sampler)) {
            ARK_ERROR("EnvironmentCubeResource failed to defer sampler");
            return false;
        }

        if (m_Texture && !context.deferReleaseTexture(m_Texture)) {
            ARK_ERROR("EnvironmentCubeResource failed to defer texture");
            return false;
        }

        m_FaceExtent = {};
        m_Format = rhi::Format::Unknown;
        m_MipLevels = 1;
        return true;
    }

    void EnvironmentCubeResource::resetImmediate() {
        for (Scope<rhi::TextureView>& faceView : m_FaceViews) {
            faceView.reset();
        }
        m_TextureView.reset();
        m_Sampler.reset();
        m_Texture.reset();
        m_FaceExtent = {};
        m_Format = rhi::Format::Unknown;
        m_MipLevels = 1;
    }
} // namespace ark
