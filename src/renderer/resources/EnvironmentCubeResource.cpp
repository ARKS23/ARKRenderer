#include "renderer/resources/EnvironmentCubeResource.h"

#include "core/Log.h"
#include "rhi/DeviceContext.h"
#include "rhi/RenderDevice.h"

#include <algorithm>

namespace ark {
    namespace {
        usize faceMipViewIndex(u32 faceIndex, u32 mipLevel) {
            return static_cast<usize>(mipLevel) * EnvironmentCubeResource::FaceCount + faceIndex;
        }

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

    bool EnvironmentCubeResource::isValid() const {
        if (!m_Texture || !m_TextureView || !m_Sampler) {
            return false;
        }

        const usize expectedFaceMipViewCount = static_cast<usize>(FaceCount) * m_MipLevels;
        if (m_FaceMipViews.size() != expectedFaceMipViewCount) {
            return false;
        }

        for (const Scope<rhi::TextureView>& faceMipView : m_FaceMipViews) {
            if (!faceMipView) {
                return false;
            }
        }

        return true;
    }

    rhi::TextureView* EnvironmentCubeResource::faceMipRenderTargetView(u32 faceIndex, u32 mipLevel) const {
        if (faceIndex >= FaceCount || mipLevel >= m_MipLevels) {
            return nullptr;
        }

        const usize viewIndex = faceMipViewIndex(faceIndex, mipLevel);
        return viewIndex < m_FaceMipViews.size() ? m_FaceMipViews[viewIndex].get() : nullptr;
    }

    rhi::Extent2D EnvironmentCubeResource::mipExtent(u32 mipLevel) const {
        if (mipLevel >= m_MipLevels || !rhi::isValidExtent(m_FaceExtent)) {
            return {};
        }

        return rhi::Extent2D{
            std::max(1u, m_FaceExtent.width >> mipLevel),
            std::max(1u, m_FaceExtent.height >> mipLevel),
        };
    }

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

        m_FaceMipViews.clear();
        m_FaceMipViews.resize(static_cast<usize>(FaceCount) * m_MipLevels);
        for (u32 mipLevel = 0; mipLevel < m_MipLevels; ++mipLevel) {
            for (u32 faceIndex = 0; faceIndex < FaceCount; ++faceIndex) {
                rhi::TextureViewDesc faceMipViewDesc{};
                faceMipViewDesc.format = textureDesc.format;
                faceMipViewDesc.baseMipLevel = mipLevel;
                faceMipViewDesc.mipLevelCount = 1;
                faceMipViewDesc.baseArrayLayer = faceIndex;
                faceMipViewDesc.arrayLayerCount = 1;
                faceMipViewDesc.type = rhi::TextureViewType::Texture2D;
                m_FaceMipViews[faceMipViewIndex(faceIndex, mipLevel)] =
                    device.createTextureView(*m_Texture, faceMipViewDesc);
                if (!m_FaceMipViews[faceMipViewIndex(faceIndex, mipLevel)]) {
                    ARK_ERROR("EnvironmentCubeResource failed to create face mip texture view face={} mip={}: {}",
                              faceIndex,
                              mipLevel,
                              debugName);
                    resetImmediate();
                    return false;
                }
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
        for (Scope<rhi::TextureView>& faceMipView : m_FaceMipViews) {
            if (faceMipView && !context.deferReleaseTextureView(faceMipView)) {
                ARK_ERROR("EnvironmentCubeResource failed to defer face mip texture view");
                return false;
            }
        }
        m_FaceMipViews.clear();

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
        m_FaceMipViews.clear();
        m_TextureView.reset();
        m_Sampler.reset();
        m_Texture.reset();
        m_FaceExtent = {};
        m_Format = rhi::Format::Unknown;
        m_MipLevels = 1;
    }
} // namespace ark
