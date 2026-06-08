#pragma once

#include "asset/MeshData.h"
#include "core/FileSystem.h"

namespace ark::asset {
    class GltfLoader {
    public:
        GltfLoader() = default;

        static ModelData loadModel(const Path& path);
    };

    // Phase 0.8 只加载 glTF 2.0 core profile 的单 mesh / 单材质最小子集。
    ModelData loadGltfModel(const Path& path);
} // namespace ark::asset
