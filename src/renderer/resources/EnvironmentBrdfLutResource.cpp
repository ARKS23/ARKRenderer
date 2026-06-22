#include "renderer/resources/EnvironmentBrdfLutResource.h"

#include "core/Log.h"
#include "rhi/DeviceContext.h"
#include "rhi/RenderDevice.h"

namespace ark {
    namespace {
        bool isSupportedBrdfLutFormat(rhi::Format format) {
            return format == rhi::Format::RGBA16Float || format == rhi::Format::RGBA32Float;
        }

        bool validateBrdfLutDesc(const EnvironmentBrdfLutResourceDesc& desc) {
            if (!rhi::isValidExtent(desc.extent)) {
                ARK_ERROR("EnvironmentBrdfLutResource requires a valid extent");
                return false;
            }

            if (!isSupportedBrdfLutFormat(desc.format)) {
                ARK_ERROR("EnvironmentBrdfLutResource requires RGBA16Float or RGBA32Float format");
                return false;
            }

            return true;
        }

        rhi::SamplerDesc makeDefaultBrdfLutSamplerDesc(const std::string& debugName) {
            rhi::SamplerDesc samplerDesc{};
            samplerDesc.debugName = debugName + ".Sampler";
            samplerDesc.minFilter = rhi::FilterMode::Linear;
            samplerDesc.magFilter = rhi::FilterMode::Linear;
            samplerDesc.mipFilter = rhi::FilterMode::Nearest;
            samplerDesc.addressU = rhi::AddressMode::ClampToEdge;
            samplerDesc.addressV = rhi::AddressMode::ClampToEdge;
            samplerDesc.addressW = rhi::AddressMode::ClampToEdge;
            return samplerDesc;
        }
    } // namespace

    EnvironmentBrdfLutResource::~EnvironmentBrdfLutResource() = default;

    bool EnvironmentBrdfLutResource::isValid() const {
        return m_Texture && m_TextureView && m_RenderTargetView && m_Sampler &&
               rhi::isValidExtent(m_Extent) && m_Format != rhi::Format::Unknown;
    }

    bool EnvironmentBrdfLutResource::create(rhi::RenderDevice& device, const EnvironmentBrdfLutResourceDesc& desc) {
        if (!validateBrdfLutDesc(desc)) {
            return false;
        }

        m_Extent = desc.extent;
        m_Format = desc.format;

        const std::string debugName = desc.debugName.empty() ? "EnvironmentBrdfLutResource" : desc.debugName;

        rhi::TextureDesc textureDesc{};
        textureDesc.extent = m_Extent;
        textureDesc.format = m_Format;
        textureDesc.mipLevels = 1;
        textureDesc.arrayLayers = 1;
        textureDesc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
        textureDesc.type = rhi::TextureType::Texture2D;
        m_Texture = device.createTexture(textureDesc);
        if (!m_Texture) {
            ARK_ERROR("EnvironmentBrdfLutResource failed to create texture: {}", debugName);
            resetImmediate();
            return false;
        }

        rhi::TextureViewDesc textureViewDesc{};
        textureViewDesc.format = textureDesc.format;
        textureViewDesc.mipLevelCount = 1;
        textureViewDesc.arrayLayerCount = 1;
        textureViewDesc.type = rhi::TextureViewType::Texture2D;
        m_TextureView = device.createTextureView(*m_Texture, textureViewDesc);
        if (!m_TextureView) {
            ARK_ERROR("EnvironmentBrdfLutResource failed to create sampled texture view: {}", debugName);
            resetImmediate();
            return false;
        }

        rhi::TextureViewDesc renderTargetViewDesc{};
        renderTargetViewDesc.format = textureDesc.format;
        renderTargetViewDesc.mipLevelCount = 1;
        renderTargetViewDesc.arrayLayerCount = 1;
        renderTargetViewDesc.type = rhi::TextureViewType::Texture2D;
        m_RenderTargetView = device.createTextureView(*m_Texture, renderTargetViewDesc);
        if (!m_RenderTargetView) {
            ARK_ERROR("EnvironmentBrdfLutResource failed to create render target texture view: {}", debugName);
            resetImmediate();
            return false;
        }

        rhi::SamplerDesc samplerDesc =
            desc.hasSamplerOverride ? desc.sampler : makeDefaultBrdfLutSamplerDesc(debugName);
        if (samplerDesc.debugName.empty()) {
            samplerDesc.debugName = debugName + ".Sampler";
        }
        m_Sampler = device.createSampler(samplerDesc);
        if (!m_Sampler) {
            ARK_ERROR("EnvironmentBrdfLutResource failed to create sampler: {}", debugName);
            resetImmediate();
            return false;
        }

        return true;
    }

    bool EnvironmentBrdfLutResource::releaseDeferred(rhi::DeviceContext& context) {
        if (m_RenderTargetView && !context.deferReleaseTextureView(m_RenderTargetView)) {
            ARK_ERROR("EnvironmentBrdfLutResource failed to defer render target texture view");
            return false;
        }

        if (m_TextureView && !context.deferReleaseTextureView(m_TextureView)) {
            ARK_ERROR("EnvironmentBrdfLutResource failed to defer sampled texture view");
            return false;
        }

        if (m_Sampler && !context.deferReleaseSampler(m_Sampler)) {
            ARK_ERROR("EnvironmentBrdfLutResource failed to defer sampler");
            return false;
        }

        if (m_Texture && !context.deferReleaseTexture(m_Texture)) {
            ARK_ERROR("EnvironmentBrdfLutResource failed to defer texture");
            return false;
        }

        m_Extent = {};
        m_Format = rhi::Format::Unknown;
        return true;
    }

    void EnvironmentBrdfLutResource::resetImmediate() {
        m_RenderTargetView.reset();
        m_TextureView.reset();
        m_Sampler.reset();
        m_Texture.reset();
        m_Extent = {};
        m_Format = rhi::Format::Unknown;
    }
} // namespace ark
