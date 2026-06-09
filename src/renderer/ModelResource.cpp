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
            TextureResourceDesc textureDesc{};
            textureDesc.path = materialData.baseColorTexturePath;
            textureDesc.colorSpace = TextureColorSpace::Srgb;
            textureDesc.debugName = materialData.debugName.empty()
                                        ? "ModelMaterial." + std::to_string(materialIndex) + ".BaseColor"
                                        : materialData.debugName + ".BaseColor";

            // glTF baseColor texture 按 sRGB 创建；CPU ImageData 仍只是 RGBA8 字节布局。
            TextureResource* baseColorTexture = textureCache.getOrCreate(device, textureDesc);
            if (!baseColorTexture) {
                ARK_ERROR("ModelResource failed to acquire base color texture {}", materialIndex);
                return false;
            }

            if (!m_Materials[materialIndex].create(materialData, *baseColorTexture)) {
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

    void ModelResource::reset() {
        m_Instances.clear();
        m_Primitives.clear();
        m_Materials.clear();
        m_Meshes.clear();
        m_LocalTextureCache.clear();
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
