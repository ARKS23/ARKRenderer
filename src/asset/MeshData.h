#pragma once

#include "core/FileSystem.h"
#include "core/Types.h"

#include <string>
#include <vector>

namespace ark::asset {
    // Phase 0.8 固定 CPU 顶点格式；GPU vertex layout 由 renderer/RHI 映射。
    struct MeshVertex {
        float position[3]{};
        float normal[3]{};
        float uv0[2]{};
        float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    };

    // 单个可绘制 primitive 的 CPU 数据，保持 asset 层纯数据语义。
    struct MeshPrimitiveData {
        std::vector<MeshVertex> vertices;
        std::vector<u32> indices;
        u32 materialIndex = 0;
        std::string debugName;

        bool empty() const {
            return vertices.empty() || indices.empty();
        }

        u64 vertexByteSize() const {
            return static_cast<u64>(vertices.size()) * sizeof(MeshVertex);
        }

        u64 indexByteSize() const {
            return static_cast<u64>(indices.size()) * sizeof(u32);
        }
    };

    // CPU 材质数据只表达 glTF 语义；GPU texture / uniform 由 renderer 层创建。
    enum class TextureFilter {
        Nearest,
        Linear,
    };

    enum class TextureAddressMode {
        Repeat,
        ClampToEdge,
        MirroredRepeat,
    };

    enum class AlphaMode {
        Opaque,
        Mask,
        Blend,
    };

    struct TextureSamplerData {
        TextureFilter minFilter = TextureFilter::Linear;
        TextureFilter magFilter = TextureFilter::Linear;
        TextureFilter mipFilter = TextureFilter::Linear;
        TextureAddressMode addressU = TextureAddressMode::Repeat;
        TextureAddressMode addressV = TextureAddressMode::Repeat;
    };

    struct MaterialTextureSlotData {
        Path path;
        u32 texCoord = 0;
        TextureSamplerData sampler;
        bool hasSampler = false;

        bool hasTexture() const {
            return !path.empty();
        }
    };

    struct MaterialData {
        Path baseColorTexturePath;
        Path normalTexturePath;
        Path metallicRoughnessTexturePath;
        Path occlusionTexturePath;
        Path emissiveTexturePath;
        MaterialTextureSlotData baseColorTexture;
        MaterialTextureSlotData normalTexture;
        MaterialTextureSlotData metallicRoughnessTexture;
        MaterialTextureSlotData occlusionTexture;
        MaterialTextureSlotData emissiveTexture;
        float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // glTF pbrMetallicRoughness.baseColorFactor。
        float metallicFactor = 1.0f; // glTF metallicFactor，当前进入 material uniform。
        float roughnessFactor = 1.0f; // glTF roughnessFactor，当前进入 material uniform。
        float emissiveFactor[3] = {0.0f, 0.0f, 0.0f}; // glTF emissiveFactor，默认无自发光。
        float normalScale = 1.0f; // glTF normalTexture.scale，后续 normal mapping 使用。
        float occlusionStrength = 1.0f; // glTF occlusionTexture.strength，后续 AO 使用。
        AlphaMode alphaMode = AlphaMode::Opaque; // glTF alphaMode，控制 ForwardPass pipeline variant。
        float alphaCutoff = 0.5f; // glTF alphaCutoff，只对 MASK 生效。
        bool doubleSided = false; // glTF doubleSided，后续映射到 raster cull mode。
        std::string debugName;

        bool hasBaseColorTexture() const {
            return baseColorTexture.hasTexture() || !baseColorTexturePath.empty();
        }

        bool hasNormalTexture() const {
            return normalTexture.hasTexture() || !normalTexturePath.empty();
        }

        bool hasMetallicRoughnessTexture() const {
            return metallicRoughnessTexture.hasTexture() || !metallicRoughnessTexturePath.empty();
        }

        bool hasOcclusionTexture() const {
            return occlusionTexture.hasTexture() || !occlusionTexturePath.empty();
        }

        bool hasEmissiveTexture() const {
            return emissiveTexture.hasTexture() || !emissiveTexturePath.empty();
        }
    };

    // CPU 侧 transform 使用 glTF/GLM 兼容的 column-major 4x4 矩阵，避免 asset 层依赖 renderer 类型。
    struct TransformData {
        float matrix[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
    };

    // 表达一个 glTF node 对某个 primitive 的实例化；meshIndex 指向 ModelData::meshes。
    struct MeshPrimitiveInstanceData {
        u32 meshIndex = 0;
        TransformData localTransform;
        std::string debugName;
    };

    // glTF 2.0 最小子集加载结果：primitive resource、material 和 scene/node 生成的 instance。
    struct ModelData {
        std::vector<MeshPrimitiveData> meshes;
        std::vector<MaterialData> materials;
        std::vector<MeshPrimitiveInstanceData> instances;
        std::string debugName;

        bool empty() const {
            return meshes.empty();
        }
    };

    bool generateTangents(MeshPrimitiveData& mesh);
} // namespace ark::asset
