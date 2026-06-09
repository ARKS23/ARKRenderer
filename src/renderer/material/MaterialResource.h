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
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
    };

    // 材质资源只保存材质语义和 texture 引用；真实 GPU texture 生命周期由 TextureResource 管理。
    class MaterialResource final {
    public:
        MaterialResource() = default;

        bool create(const asset::MaterialData& material, TextureResource& baseColorTexture);
        bool upload(rhi::DeviceContext& context);
        void updateDescriptorSet(rhi::DescriptorSet& descriptorSet, u32 imageBinding, u32 samplerBinding) const;

        const MaterialFactors& factors() const {
            return m_Factors;
        }

        bool isReady() const;

    private:
        MaterialFactors m_Factors;
        TextureResource* m_BaseColorTexture = nullptr;
    };
} // namespace ark
