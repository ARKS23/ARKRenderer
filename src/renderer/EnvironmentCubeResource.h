#pragma once

#include "core/Memory.h"
#include "core/Types.h"
#include "rhi/RHICommon.h"
#include "rhi/Sampler.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <array>
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

        bool isValid() const {
            if (!m_Texture || !m_TextureView || !m_Sampler) {
                return false;
            }

            for (const Scope<rhi::TextureView>& faceView : m_FaceViews) {
                if (!faceView) {
                    return false;
                }
            }

            return true;
        }

        rhi::Texture* texture() const {
            return m_Texture.get();
        }

        rhi::TextureView* textureView() const {
            return m_TextureView.get();
        }

        rhi::TextureView* faceRenderTargetView(u32 faceIndex) const {
            return faceIndex < FaceCount ? m_FaceViews[faceIndex].get() : nullptr;
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
        std::array<Scope<rhi::TextureView>, FaceCount> m_FaceViews;
        Scope<rhi::Sampler> m_Sampler;
        rhi::Extent2D m_FaceExtent{};
        rhi::Format m_Format = rhi::Format::Unknown;
        u32 m_MipLevels = 1;
    };
} // namespace ark
