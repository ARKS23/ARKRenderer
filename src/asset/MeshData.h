#pragma once

#include "core/FileSystem.h"
#include "core/Types.h"

#include <string>
#include <vector>

namespace ark::asset {
    // Phase 0.8 固定的 CPU 顶点格式；GPU vertex layout 由 renderer/RHI 后续映射。
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

    // glTF 2.0 最小子集加载结果：多个 primitive + 对应 CPU 材质描述。
    struct ModelData {
        std::vector<MeshPrimitiveData> meshes;
        std::vector<MaterialData> materials;
        std::string debugName;

        bool empty() const {
            return meshes.empty();
        }
    };
} // namespace ark::asset
