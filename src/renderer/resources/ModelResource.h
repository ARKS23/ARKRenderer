#pragma once

#include "asset/MeshData.h"
#include "core/Types.h"
#include "renderer/core/Bounds.h"
#include "renderer/resources/MeshResource.h"
#include "renderer/resources/TextureCache.h"
#include "renderer/material/MaterialResource.h"

#include <glm/mat4x4.hpp>

#include <span>
#include <string>
#include <vector>

namespace ark::rhi {
    class DeviceContext;
    class RenderDevice;
} // namespace ark::rhi

namespace ark {
    struct ModelPrimitiveResource {
        u32 meshIndex = 0;
        u32 materialIndex = 0;
        std::string debugName;
    };

    struct ModelPrimitiveInstance {
        u32 primitiveIndex = 0;
        glm::mat4 localTransform{1.0f};
        std::string debugName;
    };

    // renderer 层 model resource：把 asset::ModelData 拆成 GPU resource 和可绘制 primitive instance。
    class ModelResource final {
    public:
        ModelResource() = default;

        bool create(rhi::RenderDevice& device, const asset::ModelData& model);
        bool create(rhi::RenderDevice& device, TextureCache& textureCache, const asset::ModelData& model);
        bool upload(rhi::DeviceContext& context);
        bool resetDeferred(rhi::DeviceContext& context);
        void reset();

        const Bounds3& localBounds() const {
            return m_LocalBounds;
        }

        std::span<const ModelPrimitiveResource> primitives() const;
        std::span<const ModelPrimitiveInstance> instances() const;
        MeshResource* mesh(usize index);
        MaterialResource* material(usize index);
        MeshResource* primitiveMesh(usize primitiveIndex);
        MaterialResource* primitiveMaterial(usize primitiveIndex);

        bool empty() const {
            return m_Primitives.empty();
        }

        usize primitiveCount() const {
            return m_Primitives.size();
        }

        usize instanceCount() const {
            return m_Instances.size();
        }

        usize meshCount() const {
            return m_Meshes.size();
        }

        usize materialCount() const {
            return m_Materials.size();
        }

    private:
        TextureCache m_LocalTextureCache;
        std::vector<MeshResource> m_Meshes;
        std::vector<MaterialResource> m_Materials;
        std::vector<ModelPrimitiveResource> m_Primitives;
        std::vector<ModelPrimitiveInstance> m_Instances;
        Bounds3 m_LocalBounds;
        bool m_UsesExternalTextureCache = false;
    };
} // namespace ark
