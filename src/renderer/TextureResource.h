#pragma once

#include "core/FileSystem.h"
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
    enum class TextureColorSpace {
        Linear,
        Srgb,
    };

    struct TextureResourceDesc {
        Path path;
        TextureColorSpace colorSpace = TextureColorSpace::Linear;
        // Phase 0.14.1 先允许显式创建 mip chain；默认仍保持单 mip，等待 mip generation 接口落地。
        bool generateMips = false;
        std::string debugName;
    };

    // renderer 层 texture owner：把 CPU image 转成 RHI texture/view/sampler，并记录首次上传状态。
    class TextureResource final {
    public:
        TextureResource() = default;

        bool create(rhi::RenderDevice& device, const asset::ImageData& image, const TextureResourceDesc& desc);
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

        TextureColorSpace colorSpace() const {
            return m_ColorSpace;
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
        TextureColorSpace m_ColorSpace = TextureColorSpace::Linear;
        u32 m_RowPitch = 0;
        u32 m_BytesPerPixel = 0;
        u32 m_MipLevels = 1;
        bool m_Uploaded = false;
    };
} // namespace ark
