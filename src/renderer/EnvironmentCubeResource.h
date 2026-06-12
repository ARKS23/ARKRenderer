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
    struct EnvironmentCubeResourceDesc {
        rhi::Extent2D faceExtent{};
        rhi::Format format = rhi::Format::RGBA16Float;
        u32 mipLevels = 1;
        rhi::SamplerDesc sampler;
        bool hasSamplerOverride = false;
        std::string debugName;
    };

    class EnvironmentCubeResource final {
    public:
        EnvironmentCubeResource() = default;

        bool create(rhi::RenderDevice& device, const EnvironmentCubeResourceDesc& desc);
        bool releaseDeferred(rhi::DeviceContext& context);
        void resetImmediate();

        bool isValid() const {
            return m_Texture && m_TextureView && m_Sampler;
        }

        rhi::Texture* texture() const {
            return m_Texture.get();
        }

        rhi::TextureView* textureView() const {
            return m_TextureView.get();
        }

        rhi::Sampler* sampler() const {
            return m_Sampler.get();
        }

        rhi::Extent2D faceExtent() const {
            return m_FaceExtent;
        }

        rhi::Format format() const {
            return m_Format;
        }

        u32 mipLevels() const {
            return m_MipLevels;
        }

    private:
        Scope<rhi::Texture> m_Texture;
        Scope<rhi::TextureView> m_TextureView;
        Scope<rhi::Sampler> m_Sampler;
        rhi::Extent2D m_FaceExtent{};
        rhi::Format m_Format = rhi::Format::Unknown;
        u32 m_MipLevels = 1;
    };
} // namespace ark
