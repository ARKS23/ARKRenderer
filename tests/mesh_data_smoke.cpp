#include "asset/MeshData.h"

#include <cstdlib>
#include <iostream>

namespace {
    bool validateMeshPrimitive() {
        ark::asset::MeshPrimitiveData primitive{};
        primitive.debugName = "SmokePrimitive";

        if (!primitive.empty()) {
            std::cerr << "Empty primitive reported as non-empty\n";
            return false;
        }

        ark::asset::MeshVertex v0{};
        v0.position[0] = -1.0f;
        v0.position[1] = 0.0f;
        v0.position[2] = 0.0f;
        v0.normal[2] = 1.0f;
        v0.uv0[0] = 0.0f;
        v0.uv0[1] = 0.0f;

        ark::asset::MeshVertex v1 = v0;
        v1.position[0] = 1.0f;
        v1.uv0[0] = 1.0f;

        ark::asset::MeshVertex v2 = v0;
        v2.position[1] = 1.0f;
        v2.uv0[1] = 1.0f;

        primitive.vertices = {v0, v1, v2};
        primitive.indices = {0, 1, 2};
        primitive.materialIndex = 1;

        if (primitive.empty()) {
            std::cerr << "Valid primitive reported as empty\n";
            return false;
        }

        if (primitive.vertexByteSize() != primitive.vertices.size() * sizeof(ark::asset::MeshVertex)) {
            std::cerr << "Unexpected vertex byte size\n";
            return false;
        }

        if (primitive.indexByteSize() != primitive.indices.size() * sizeof(ark::u32)) {
            std::cerr << "Unexpected index byte size\n";
            return false;
        }

        return true;
    }

    bool validateModelData() {
        ark::asset::MaterialData material{};
        if (material.hasBaseColorTexture()) {
            std::cerr << "Empty material unexpectedly has a base color texture\n";
            return false;
        }

        material.debugName = "SmokeMaterial";
        material.baseColorTexturePath = "assets/textures/xiaowei.png";
        if (!material.hasBaseColorTexture()) {
            std::cerr << "Material texture path was not detected\n";
            return false;
        }

        ark::asset::MeshPrimitiveData primitive{};
        primitive.vertices.resize(3);
        primitive.indices = {0, 1, 2};

        ark::asset::ModelData model{};
        if (!model.empty()) {
            std::cerr << "Empty model reported as non-empty\n";
            return false;
        }

        model.debugName = "SmokeModel";
        model.meshes.push_back(primitive);
        model.materials.push_back(material);

        if (model.empty() || model.meshes.size() != 1 || model.materials.size() != 1) {
            std::cerr << "ModelData did not preserve mesh/material data\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateMeshPrimitive() && validateModelData() ? EXIT_SUCCESS : EXIT_FAILURE;
}
