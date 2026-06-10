#pragma once

#include "asset/MeshData.h"
#include "core/Types.h"

namespace ark::rhi {
    class DescriptorSet;
    class DeviceContext;
} // namespace ark::rhi

namespace ark {
    class TextureResource;

    struct MaterialFactors {
        float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float emissiveFactor[3] = {0.0f, 0.0f, 0.0f};
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        float normalScale = 1.0f;
        float occlusionStrength = 1.0f;
    };

    struct MaterialRenderState {
        asset::AlphaMode alphaMode = asset::AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
    };

    struct MaterialTextureSet {
        TextureResource* baseColor = nullptr;
        TextureResource* normal = nullptr;
        TextureResource* metallicRoughness = nullptr;
        TextureResource* occlusion = nullptr;
        TextureResource* emissive = nullptr;
    };

    struct MaterialTextureBindingSet {
        u32 baseColorImage = 1;
        u32 baseColorSampler = 2;
        u32 normalImage = 5;
        u32 normalSampler = 6;
        u32 metallicRoughnessImage = 7;
        u32 metallicRoughnessSampler = 8;
        u32 occlusionImage = 9;
        u32 occlusionSampler = 10;
        u32 emissiveImage = 11;
        u32 emissiveSampler = 12;
    };

    // 材质资源只保存材质语义和 texture 引用；真实 GPU texture 生命周期由 TextureResource 管理。
    class MaterialResource final {
    public:
        MaterialResource() = default;

        bool create(const asset::MaterialData& material, const MaterialTextureSet& textures);
        bool upload(rhi::DeviceContext& context);
        bool updateDescriptorSet(rhi::DescriptorSet& descriptorSet, const MaterialTextureBindingSet& bindings) const;

        const MaterialFactors& factors() const {
            return m_Factors;
        }

        const MaterialTextureSet& textures() const {
            return m_Textures;
        }

        const MaterialRenderState& renderState() const {
            return m_RenderState;
        }

        bool isReady() const;

    private:
        MaterialFactors m_Factors;
        MaterialRenderState m_RenderState;
        MaterialTextureSet m_Textures;
    };
} // namespace ark
