#pragma once

#include "asset/MeshData.h"
#include "core/Memory.h"
#include "core/Types.h"
#include "rhi/Buffer.h"
#include "rhi/RHICommon.h"
#include "rhi/Sampler.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

namespace ark::rhi {
    class DescriptorSet;
    class DeviceContext;
    class RenderDevice;
} // namespace ark::rhi

namespace ark {
    // Phase 0.8 最小材质资源：只管理 base color RGBA8 texture/view/sampler 和首次上传。
    class MaterialResource final {
    public:
        MaterialResource() = default;

        bool create(rhi::RenderDevice& device, const asset::MaterialData& material);
        bool upload(rhi::DeviceContext& context);
        void updateDescriptorSet(rhi::DescriptorSet& descriptorSet, u32 imageBinding, u32 samplerBinding) const;

        bool isReady() const {
            return m_Uploaded && m_Texture && m_TextureView && m_Sampler;
        }

    private:
        Scope<rhi::Buffer> m_TextureStagingBuffer;
        Scope<rhi::Texture> m_Texture;
        Scope<rhi::TextureView> m_TextureView;
        Scope<rhi::Sampler> m_Sampler;
        rhi::Extent2D m_TextureExtent{};
        u32 m_TextureRowPitch = 0;
        u32 m_TextureBytesPerPixel = 0;
        bool m_Uploaded = false;
    };
} // namespace ark
