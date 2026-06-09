#include "renderer/material/MaterialResource.h"

#include "core/Log.h"
#include "renderer/TextureResource.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DeviceContext.h"

namespace ark {
    bool MaterialResource::create(const asset::MaterialData& material, TextureResource& baseColorTexture) {
        if (!material.hasBaseColorTexture()) {
            ARK_ERROR("MaterialResource requires a base color texture path");
            return false;
        }

        m_BaseColorTexture = &baseColorTexture;
        return true;
    }

    bool MaterialResource::upload(rhi::DeviceContext& context) {
        if (!m_BaseColorTexture) {
            ARK_ERROR("MaterialResource requires a base color texture resource");
            return false;
        }

        return m_BaseColorTexture->upload(context);
    }

    bool MaterialResource::isReady() const {
        return m_BaseColorTexture && m_BaseColorTexture->isReady();
    }

    void MaterialResource::updateDescriptorSet(rhi::DescriptorSet& descriptorSet,
                                               u32 imageBinding,
                                               u32 samplerBinding) const {
        if (!m_BaseColorTexture || !m_BaseColorTexture->textureView() || !m_BaseColorTexture->sampler()) {
            ARK_ERROR("MaterialResource requires texture view and sampler before descriptor update");
            return;
        }

        // binding 1/2 继续使用 separate sampled image / sampler，和 mesh.frag.hlsl 保持一致。
        rhi::SampledImageDescriptor imageDescriptor{};
        imageDescriptor.view = m_BaseColorTexture->textureView();
        descriptorSet.updateSampledImage(imageBinding, imageDescriptor);

        rhi::SamplerDescriptor samplerDescriptor{};
        samplerDescriptor.sampler = m_BaseColorTexture->sampler();
        descriptorSet.updateSampler(samplerBinding, samplerDescriptor);
    }
} // namespace ark
