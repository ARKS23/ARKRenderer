#pragma once

#include "core/Memory.h"
#include "core/Types.h"
#include "rhi/RHICommon.h"
#include "rhi/Sampler.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <string>

namespace ark::rhi {
    class DeviceContext;
    class RenderDevice;
} // namespace ark::rhi

namespace ark {
    struct EnvironmentBrdfLutResourceDesc {
        rhi::Extent2D extent{256, 256};
        rhi::Format format = rhi::Format::RGBA16Float;
        rhi::SamplerDesc sampler;
        bool hasSamplerOverride = false;
        std::string debugName;
    };

    class EnvironmentBrdfLutResource final {
    public:
        EnvironmentBrdfLutResource() = default;
        ~EnvironmentBrdfLutResource();

        bool create(rhi::RenderDevice& device, const EnvironmentBrdfLutResourceDesc& desc);
        bool releaseDeferred(rhi::DeviceContext& context);
        void resetImmediate();

        bool isValid() const;

        rhi::Texture* texture() const {
            return m_Texture.get();
        }

        rhi::TextureView* textureView() const {
            return m_TextureView.get();
        }

        rhi::TextureView* renderTargetView() const {
            return m_RenderTargetView.get();
        }

        rhi::Sampler* sampler() const {
            return m_Sampler.get();
        }

        rhi::Extent2D extent() const {
            return m_Extent;
        }

        rhi::Format format() const {
            return m_Format;
        }

    private:
        Scope<rhi::Texture> m_Texture;
        Scope<rhi::TextureView> m_TextureView;
        Scope<rhi::TextureView> m_RenderTargetView;
        Scope<rhi::Sampler> m_Sampler;
        rhi::Extent2D m_Extent{};
        rhi::Format m_Format = rhi::Format::Unknown;
    };
} // namespace ark
