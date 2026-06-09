#include "renderer/ModelResource.h"

#include "core/Log.h"
#include "rhi/DeviceContext.h"
#include "rhi/RenderDevice.h"

#include <limits>
#include <utility>

namespace ark {
    bool ModelResource::create(rhi::RenderDevice& device, const asset::ModelData& model) {
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
        m_Meshes.resize(model.meshes.size());
        m_Materials.resize(model.materials.size());
        m_Primitives.reserve(model.meshes.size());

        for (usize materialIndex = 0; materialIndex < model.materials.size(); ++materialIndex) {
            if (!m_Materials[materialIndex].create(device, model.materials[materialIndex])) {
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

        return !m_Primitives.empty();
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
        m_Primitives.clear();
        m_Materials.clear();
        m_Meshes.clear();
    }

    std::span<const ModelPrimitiveResource> ModelResource::primitives() const {
        return m_Primitives;
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
