#pragma once

#include "core/Memory.h"
#include "core/Types.h"
#include "rhi/Buffer.h"
#include "rhi/RHICommon.h"
#include "rhi/Sampler.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <string>

namespace ark::asset {
    struct ImageData;
} // namespace ark::asset

namespace ark::rhi {
    class DeviceContext;
    class RenderDevice;
} // namespace ark::rhi

namespace ark {
    struct EnvironmentResourceDesc {
        rhi::SamplerDesc sampler;
        bool hasSamplerOverride = false;
        std::string debugName;
    };

    class EnvironmentResource final {
    public:
        EnvironmentResource() = default;

        bool create(rhi::RenderDevice& device, const asset::ImageData& image, const EnvironmentResourceDesc& desc);
        bool upload(rhi::DeviceContext& context);
        bool releaseDeferred(rhi::DeviceContext& context);
        void resetImmediate();

        bool isReady() const {
            return m_Uploaded && m_Texture && m_TextureView && m_Sampler;
        }

        rhi::TextureView* textureView() const {
            return m_TextureView.get();
        }

        rhi::Sampler* sampler() const {
            return m_Sampler.get();
        }

        rhi::Format format() const {
            return m_Format;
        }

        u32 mipLevels() const {
            return m_MipLevels;
        }

    private:
        Scope<rhi::Buffer> m_StagingBuffer;
        Scope<rhi::Texture> m_Texture;
        Scope<rhi::TextureView> m_TextureView;
        Scope<rhi::Sampler> m_Sampler;
        rhi::Extent2D m_Extent{};
        rhi::Format m_Format = rhi::Format::Unknown;
        u32 m_RowPitch = 0;
        u32 m_BytesPerPixel = 0;
        u32 m_MipLevels = 1;
        bool m_Uploaded = false;
    };
} // namespace ark
