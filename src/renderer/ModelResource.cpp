#include "renderer/ModelResource.h"

#include "core/Log.h"
#include "rhi/DeviceContext.h"
#include "rhi/RenderDevice.h"

#include <limits>
#include <utility>

namespace ark {
    namespace {
        glm::mat4 toMat4(const asset::TransformData& transform) {
            glm::mat4 matrix{1.0f};
            for (usize column = 0; column < 4; ++column) {
                for (usize row = 0; row < 4; ++row) {
                    matrix[static_cast<glm::length_t>(column)][static_cast<glm::length_t>(row)] =
                        transform.matrix[column * 4 + row];
                }
            }
            return matrix;
        }

        rhi::FilterMode toRhiFilter(asset::TextureFilter filter) {
            switch (filter) {
            case asset::TextureFilter::Nearest:
                return rhi::FilterMode::Nearest;
            case asset::TextureFilter::Linear:
                return rhi::FilterMode::Linear;
            }

            return rhi::FilterMode::Linear;
        }

        rhi::AddressMode toRhiAddressMode(asset::TextureAddressMode addressMode) {
            switch (addressMode) {
            case asset::TextureAddressMode::Repeat:
                return rhi::AddressMode::Repeat;
            case asset::TextureAddressMode::ClampToEdge:
                return rhi::AddressMode::ClampToEdge;
            case asset::TextureAddressMode::MirroredRepeat:
                return rhi::AddressMode::MirroredRepeat;
            }

            return rhi::AddressMode::Repeat;
        }

        rhi::SamplerDesc toRhiSamplerDesc(const asset::TextureSamplerData& sampler, const std::string& debugName) {
            rhi::SamplerDesc samplerDesc{};
            samplerDesc.debugName = debugName + ".Sampler";
            samplerDesc.minFilter = toRhiFilter(sampler.minFilter);
            samplerDesc.magFilter = toRhiFilter(sampler.magFilter);
            samplerDesc.mipFilter = toRhiFilter(sampler.mipFilter);
            samplerDesc.addressU = toRhiAddressMode(sampler.addressU);
            samplerDesc.addressV = toRhiAddressMode(sampler.addressV);
            samplerDesc.addressW = rhi::AddressMode::Repeat;
            return samplerDesc;
        }

        FallbackTextureKind failedLoadFallbackKind(FallbackTextureKind fallbackKind) {
            switch (fallbackKind) {
            case FallbackTextureKind::White:
                return FallbackTextureKind::MissingBaseColor;
            case FallbackTextureKind::MetallicRoughnessDefault:
                return FallbackTextureKind::MissingMetallicRoughness;
            case FallbackTextureKind::MissingBaseColor:
            case FallbackTextureKind::MissingMetallicRoughness:
            case FallbackTextureKind::FlatNormal:
            case FallbackTextureKind::OcclusionDefault:
            case FallbackTextureKind::Black:
                return fallbackKind;
            }

            return fallbackKind;
        }

        TextureResource* acquireTexture(rhi::RenderDevice& device,
                                        TextureCache& textureCache,
                                        const asset::MaterialTextureSlotData& slot,
                                        const Path& legacyPath,
                                        TextureColorSpace colorSpace,
                                        FallbackTextureKind fallbackKind,
                                        const std::string& debugName) {
            const bool useSlotPath = slot.hasTexture();
            const Path& path = useSlotPath ? slot.path : legacyPath;
            if (path.empty()) {
                return textureCache.getOrCreateFallback(device, fallbackKind);
            }

            TextureResourceDesc textureDesc{};
            textureDesc.path = path;
            textureDesc.colorSpace = colorSpace;
            textureDesc.debugName = debugName;
            if (useSlotPath && slot.hasSampler) {
                textureDesc.sampler = toRhiSamplerDesc(slot.sampler, debugName);
                textureDesc.hasSamplerOverride = true;
            }

            TextureResource* texture = textureCache.getOrCreate(device, textureDesc);
            if (texture) {
                return texture;
            }

            ARK_WARN("ModelResource texture load failed, using slot fallback: path={}, slot={}",
                     path.string(),
                     debugName);
            return textureCache.getOrCreateFallback(device, failedLoadFallbackKind(fallbackKind));
        }

        std::string makeMaterialTextureDebugName(const asset::MaterialData& material,
                                                 usize materialIndex,
                                                 const char* slotName) {
            const std::string materialName = material.debugName.empty()
                                                 ? "ModelMaterial." + std::to_string(materialIndex)
                                                 : material.debugName;
            return materialName + "." + slotName;
        }
    } // namespace

    bool ModelResource::create(rhi::RenderDevice& device, const asset::ModelData& model) {
        return create(device, m_LocalTextureCache, model);
    }

    bool ModelResource::create(rhi::RenderDevice& device, TextureCache& textureCache, const asset::ModelData& model) {
        if (model.empty()) {
            ARK_ERROR("ModelResource requires non-empty model data");
            return false;
        }

        if (model.materials.empty()) {
            ARK_ERROR("ModelResource requires at least one material");
            return false;
        }

        if (model.meshes.size() > std::numeric_limits<u32>::max() ||
            model.materials.size() > std::numeric_limits<u32>::max()) {
            ARK_ERROR("ModelResource mesh/material count exceeds u32 range");
            return false;
        }

        m_UsesExternalTextureCache = &textureCache != &m_LocalTextureCache;
        m_Meshes.clear();
        m_Materials.clear();
        m_Primitives.clear();
        m_Instances.clear();
        m_LocalTextureCache.clear();
        m_Meshes.resize(model.meshes.size());
        m_Materials.resize(model.materials.size());
        m_Primitives.reserve(model.meshes.size());
        m_Instances.reserve(model.instances.empty() ? model.meshes.size() : model.instances.size());

        for (usize materialIndex = 0; materialIndex < model.materials.size(); ++materialIndex) {
            const asset::MaterialData& materialData = model.materials[materialIndex];
            MaterialTextureSet textures{};

            if (!materialData.hasBaseColorTexture()) {
                ARK_ERROR("ModelResource material requires a base color texture {}", materialIndex);
                return false;
            }

            // glTF baseColor / emissive 是颜色纹理；normal / MR / AO 是数据纹理，不能走 sRGB decode。
            textures.baseColor = acquireTexture(device, textureCache, materialData.baseColorTexture,
                                                materialData.baseColorTexturePath,
                                                TextureColorSpace::Srgb, FallbackTextureKind::White,
                                                makeMaterialTextureDebugName(materialData, materialIndex, "BaseColor"));
            if (!textures.baseColor) {
                ARK_ERROR("ModelResource failed to acquire base color texture {}", materialIndex);
                return false;
            }

            textures.normal = acquireTexture(device, textureCache, materialData.normalTexture,
                                             materialData.normalTexturePath,
                                             TextureColorSpace::Linear, FallbackTextureKind::FlatNormal,
                                             makeMaterialTextureDebugName(materialData, materialIndex, "Normal"));
            textures.metallicRoughness =
                acquireTexture(device, textureCache, materialData.metallicRoughnessTexture,
                               materialData.metallicRoughnessTexturePath,
                               TextureColorSpace::Linear, FallbackTextureKind::MetallicRoughnessDefault,
                               makeMaterialTextureDebugName(materialData, materialIndex, "MetallicRoughness"));
            textures.occlusion = acquireTexture(device, textureCache, materialData.occlusionTexture,
                                                materialData.occlusionTexturePath,
                                                TextureColorSpace::Linear, FallbackTextureKind::OcclusionDefault,
                                                makeMaterialTextureDebugName(materialData, materialIndex, "Occlusion"));
            textures.emissive = acquireTexture(device, textureCache, materialData.emissiveTexture,
                                               materialData.emissiveTexturePath,
                                               TextureColorSpace::Srgb, FallbackTextureKind::Black,
                                               makeMaterialTextureDebugName(materialData, materialIndex, "Emissive"));
            if (!textures.normal || !textures.metallicRoughness || !textures.occlusion || !textures.emissive) {
                ARK_ERROR("ModelResource failed to acquire optional texture slots {}", materialIndex);
                return false;
            }

            if (!m_Materials[materialIndex].create(materialData, textures)) {
                ARK_ERROR("ModelResource failed to create material {}", materialIndex);
                return false;
            }
        }

        for (usize meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
            const asset::MeshPrimitiveData& meshData = model.meshes[meshIndex];
            if (meshData.materialIndex >= model.materials.size()) {
                ARK_ERROR("ModelResource mesh material index is out of range");
                return false;
            }

            if (!m_Meshes[meshIndex].create(device, meshData)) {
                ARK_ERROR("ModelResource failed to create mesh {}", meshIndex);
                return false;
            }

            ModelPrimitiveResource primitive{};
            primitive.meshIndex = static_cast<u32>(meshIndex);
            primitive.materialIndex = meshData.materialIndex;
            primitive.debugName = meshData.debugName;
            m_Primitives.push_back(std::move(primitive));
        }

        if (model.instances.empty()) {
            for (usize primitiveIndex = 0; primitiveIndex < m_Primitives.size(); ++primitiveIndex) {
                ModelPrimitiveInstance instance{};
                instance.primitiveIndex = static_cast<u32>(primitiveIndex);
                instance.localTransform = glm::mat4{1.0f};
                instance.debugName = m_Primitives[primitiveIndex].debugName;
                m_Instances.push_back(std::move(instance));
            }
        } else {
            for (const asset::MeshPrimitiveInstanceData& instanceData : model.instances) {
                if (instanceData.meshIndex >= m_Primitives.size()) {
                    ARK_ERROR("ModelResource primitive instance index is out of range");
                    return false;
                }

                ModelPrimitiveInstance instance{};
                instance.primitiveIndex = instanceData.meshIndex;
                instance.localTransform = toMat4(instanceData.localTransform);
                instance.debugName = instanceData.debugName;
                m_Instances.push_back(std::move(instance));
            }
        }

        return !m_Primitives.empty() && !m_Instances.empty();
    }

    bool ModelResource::upload(rhi::DeviceContext& context) {
        for (MeshResource& meshResource : m_Meshes) {
            if (!meshResource.upload(context)) {
                return false;
            }
        }

        for (MaterialResource& materialResource : m_Materials) {
            if (!materialResource.upload(context)) {
                return false;
            }
        }

        return true;
    }

    bool ModelResource::resetDeferred(rhi::DeviceContext& context) {
        // 先移除 draw 可见元数据和 material 的 texture 引用，再释放底层 GPU resource。
        m_Instances.clear();
        m_Primitives.clear();
        m_Materials.clear();

        for (MeshResource& meshResource : m_Meshes) {
            if (!meshResource.releaseDeferred(context)) {
                return false;
            }
        }

        if (!m_UsesExternalTextureCache && !m_LocalTextureCache.clearDeferred(context)) {
            return false;
        }

        m_Meshes.clear();
        m_UsesExternalTextureCache = false;
        return true;
    }

    void ModelResource::reset() {
        m_Instances.clear();
        m_Primitives.clear();
        m_Materials.clear();
        for (MeshResource& meshResource : m_Meshes) {
            meshResource.resetImmediate();
        }
        m_Meshes.clear();
        m_LocalTextureCache.clear();
        m_UsesExternalTextureCache = false;
    }

    std::span<const ModelPrimitiveResource> ModelResource::primitives() const {
        return m_Primitives;
    }

    std::span<const ModelPrimitiveInstance> ModelResource::instances() const {
        return m_Instances;
    }

    MeshResource* ModelResource::mesh(usize index) {
        return index < m_Meshes.size() ? &m_Meshes[index] : nullptr;
    }

    MaterialResource* ModelResource::material(usize index) {
        return index < m_Materials.size() ? &m_Materials[index] : nullptr;
    }

    MeshResource* ModelResource::primitiveMesh(usize primitiveIndex) {
        if (primitiveIndex >= m_Primitives.size()) {
            return nullptr;
        }

        return mesh(m_Primitives[primitiveIndex].meshIndex);
    }

    MaterialResource* ModelResource::primitiveMaterial(usize primitiveIndex) {
        if (primitiveIndex >= m_Primitives.size()) {
            return nullptr;
        }

        return material(m_Primitives[primitiveIndex].materialIndex);
    }
} // namespace ark
