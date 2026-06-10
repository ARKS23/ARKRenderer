#include "renderer/material/MaterialResource.h"

#include "core/Log.h"
#include "renderer/TextureResource.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DeviceContext.h"

namespace ark {
    namespace {
        bool hasAllTextureSlots(const MaterialTextureSet& textures) {
            return textures.baseColor && textures.normal && textures.metallicRoughness && textures.occlusion &&
                   textures.emissive;
        }

        bool uploadTextureSlot(rhi::DeviceContext& context, TextureResource* texture, const char* slotName) {
            if (!texture) {
                ARK_ERROR("MaterialResource requires texture slot: {}", slotName);
                return false;
            }

            return texture->upload(context);
        }

        bool isTextureSlotReady(TextureResource* texture) {
            return texture && texture->isReady();
        }

        u32 clampTextureCoordinate(u32 texCoord, const char* slotName) {
            if (texCoord <= 1) {
                return texCoord;
            }

            ARK_WARN("Material texture slot uses unsupported texCoord: slot={}, texCoord={}; fallback to 0",
                     slotName,
                     texCoord);
            return 0;
        }

        MaterialTextureTransform toMaterialTextureTransform(const asset::TextureTransformData& transform) {
            MaterialTextureTransform result{};
            result.offset[0] = transform.offset[0];
            result.offset[1] = transform.offset[1];
            result.scale[0] = transform.scale[0];
            result.scale[1] = transform.scale[1];
            result.rotation = transform.rotation;
            return result;
        }

        bool updateTextureSlot(rhi::DescriptorSet& descriptorSet,
                               TextureResource* texture,
                               u32 imageBinding,
                               u32 samplerBinding,
                               const char* slotName) {
            if (!texture || !texture->textureView() || !texture->sampler()) {
                ARK_ERROR("MaterialResource requires ready texture slot before descriptor update: {}", slotName);
                return false;
            }

            rhi::SampledImageDescriptor imageDescriptor{};
            imageDescriptor.view = texture->textureView();
            descriptorSet.updateSampledImage(imageBinding, imageDescriptor);

            rhi::SamplerDescriptor samplerDescriptor{};
            samplerDescriptor.sampler = texture->sampler();
            descriptorSet.updateSampler(samplerBinding, samplerDescriptor);
            return true;
        }
    } // namespace

    bool MaterialResource::create(const asset::MaterialData& material, const MaterialTextureSet& textures) {
        if (!material.hasBaseColorTexture()) {
            ARK_ERROR("MaterialResource requires a base color texture path");
            return false;
        }

        if (!hasAllTextureSlots(textures)) {
            ARK_ERROR("MaterialResource requires complete texture slots");
            return false;
        }

        // factors 是 draw 级 uniform 的来源；texture resource 仍由 TextureResource 独立管理。
        for (usize i = 0; i < 4; ++i) {
            m_Factors.baseColorFactor[i] = material.baseColorFactor[i];
        }
        for (usize i = 0; i < 3; ++i) {
            m_Factors.emissiveFactor[i] = material.emissiveFactor[i];
        }
        m_Factors.metallicFactor = material.metallicFactor;
        m_Factors.roughnessFactor = material.roughnessFactor;
        m_Factors.normalScale = material.normalScale;
        m_Factors.occlusionStrength = material.occlusionStrength;
        m_RenderState.alphaMode = material.alphaMode;
        m_RenderState.alphaCutoff = material.alphaCutoff;
        m_RenderState.doubleSided = material.doubleSided;
        m_TextureCoordinates.baseColor = clampTextureCoordinate(material.baseColorTexture.texCoord, "baseColor");
        m_TextureCoordinates.normal = clampTextureCoordinate(material.normalTexture.texCoord, "normal");
        m_TextureCoordinates.metallicRoughness =
            clampTextureCoordinate(material.metallicRoughnessTexture.texCoord, "metallicRoughness");
        m_TextureCoordinates.occlusion = clampTextureCoordinate(material.occlusionTexture.texCoord, "occlusion");
        m_TextureCoordinates.emissive = clampTextureCoordinate(material.emissiveTexture.texCoord, "emissive");
        m_TextureTransforms.baseColor = toMaterialTextureTransform(material.baseColorTexture.transform);
        m_TextureTransforms.normal = toMaterialTextureTransform(material.normalTexture.transform);
        m_TextureTransforms.metallicRoughness =
            toMaterialTextureTransform(material.metallicRoughnessTexture.transform);
        m_TextureTransforms.occlusion = toMaterialTextureTransform(material.occlusionTexture.transform);
        m_TextureTransforms.emissive = toMaterialTextureTransform(material.emissiveTexture.transform);
        m_Textures = textures;
        return true;
    }

    bool MaterialResource::upload(rhi::DeviceContext& context) {
        return uploadTextureSlot(context, m_Textures.baseColor, "baseColor") &&
               uploadTextureSlot(context, m_Textures.normal, "normal") &&
               uploadTextureSlot(context, m_Textures.metallicRoughness, "metallicRoughness") &&
               uploadTextureSlot(context, m_Textures.occlusion, "occlusion") &&
               uploadTextureSlot(context, m_Textures.emissive, "emissive");
    }

    bool MaterialResource::isReady() const {
        return isTextureSlotReady(m_Textures.baseColor) && isTextureSlotReady(m_Textures.normal) &&
               isTextureSlotReady(m_Textures.metallicRoughness) && isTextureSlotReady(m_Textures.occlusion) &&
               isTextureSlotReady(m_Textures.emissive);
    }

    bool MaterialResource::updateDescriptorSet(rhi::DescriptorSet& descriptorSet,
                                               const MaterialTextureBindingSet& bindings) const {
        // separate sampled image / sampler 继续保持 RHI 现有 descriptor 语义。
        return updateTextureSlot(descriptorSet, m_Textures.baseColor, bindings.baseColorImage,
                                 bindings.baseColorSampler, "baseColor") &&
               updateTextureSlot(descriptorSet, m_Textures.normal, bindings.normalImage, bindings.normalSampler,
                                 "normal") &&
               updateTextureSlot(descriptorSet, m_Textures.metallicRoughness, bindings.metallicRoughnessImage,
                                 bindings.metallicRoughnessSampler, "metallicRoughness") &&
               updateTextureSlot(descriptorSet, m_Textures.occlusion, bindings.occlusionImage,
                                 bindings.occlusionSampler, "occlusion") &&
               updateTextureSlot(descriptorSet, m_Textures.emissive, bindings.emissiveImage,
                                 bindings.emissiveSampler, "emissive");
    }
} // namespace ark
