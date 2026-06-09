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

    // Phase 0.8 最小材质数据只表达 base color 贴图路径，不创建 GPU texture。
    struct MaterialData {
        Path baseColorTexturePath;
        std::string debugName;

        bool hasBaseColorTexture() const {
            return !baseColorTexturePath.empty();
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
} // namespace ark::asset
