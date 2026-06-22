#pragma once

#include "core/Memory.h"
#include "core/Types.h"
#include "rhi/RHICommon.h"
#include "rhi/Sampler.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <string>
#include <vector>

namespace ark::rhi {
    class DeviceContext;
    class RenderDevice;
} // namespace ark::rhi

namespace ark {
    struct EnvironmentCubeResourceDesc {
        rhi::Extent2D faceExtent{};
        rhi::Format format = rhi::Format::RGBA16Float;
        u32 mipLevels = 1;
        rhi::SamplerDesc sampler;
        bool hasSamplerOverride = false;
        bool allowReadback = false;
        std::string debugName;
    };

    class EnvironmentCubeResource final {
    public:
        static constexpr u32 FaceCount = 6;

        EnvironmentCubeResource() = default;

        bool create(rhi::RenderDevice& device, const EnvironmentCubeResourceDesc& desc);
        bool releaseDeferred(rhi::DeviceContext& context);
        void resetImmediate();

        bool isValid() const;

        rhi::Texture* texture() const {
            return m_Texture.get();
        }

        rhi::TextureView* textureView() const {
            return m_TextureView.get();
        }

        rhi::TextureView* faceRenderTargetView(u32 faceIndex) const {
            return faceMipRenderTargetView(faceIndex, 0);
        }

        rhi::TextureView* faceMipRenderTargetView(u32 faceIndex, u32 mipLevel) const;

        rhi::Sampler* sampler() const {
            return m_Sampler.get();
        }

        rhi::Extent2D faceExtent() const {
            return m_FaceExtent;
        }

        rhi::Extent2D mipExtent(u32 mipLevel) const;

        rhi::Format format() const {
            return m_Format;
        }

        u32 mipLevels() const {
            return m_MipLevels;
        }

    private:
        Scope<rhi::Texture> m_Texture;
        Scope<rhi::TextureView> m_TextureView;
        std::vector<Scope<rhi::TextureView>> m_FaceMipViews;
        Scope<rhi::Sampler> m_Sampler;
        rhi::Extent2D m_FaceExtent{};
        rhi::Format m_Format = rhi::Format::Unknown;
        u32 m_MipLevels = 1;
    };
} // namespace ark
