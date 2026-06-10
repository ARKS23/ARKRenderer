#include "asset/MeshData.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
    bool near(float lhs, float rhs) {
        return std::fabs(lhs - rhs) < 0.0001f;
    }

    bool validateTangent(const ark::asset::MeshVertex& vertex) {
        const float length = std::sqrt(vertex.tangent[0] * vertex.tangent[0] +
                                       vertex.tangent[1] * vertex.tangent[1] +
                                       vertex.tangent[2] * vertex.tangent[2]);
        return near(length, 1.0f) && (near(vertex.tangent[3], 1.0f) || near(vertex.tangent[3], -1.0f));
    }

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
        v0.uv1[0] = 0.25f;
        v0.uv1[1] = 0.75f;

        if (v0.tangent[0] != 1.0f || v0.tangent[1] != 0.0f || v0.tangent[2] != 0.0f ||
            v0.tangent[3] != 1.0f) {
            std::cerr << "Unexpected default mesh tangent\n";
            return false;
        }

        if (!near(v0.uv1[0], 0.25f) || !near(v0.uv1[1], 0.75f)) {
            std::cerr << "Unexpected mesh uv1 data\n";
            return false;
        }

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

    bool validateTangentGeneration() {
        ark::asset::MeshVertex v0{};
        v0.position[0] = -1.0f;
        v0.normal[2] = 1.0f;

        ark::asset::MeshVertex v1 = v0;
        v1.position[0] = 1.0f;
        v1.uv0[0] = 1.0f;

        ark::asset::MeshVertex v2 = v0;
        v2.position[1] = 1.0f;
        v2.uv0[1] = 1.0f;

        ark::asset::MeshPrimitiveData primitive{};
        primitive.debugName = "GeneratedTangentTriangle";
        primitive.vertices = {v0, v1, v2};
        primitive.indices = {0, 1, 2};
        if (!ark::asset::generateTangents(primitive)) {
            std::cerr << "Tangent generation failed\n";
            return false;
        }

        const ark::asset::MeshVertex& firstVertex = primitive.vertices.front();
        if (!near(firstVertex.tangent[0], 1.0f) || !near(firstVertex.tangent[1], 0.0f) ||
            !near(firstVertex.tangent[2], 0.0f) || !near(firstVertex.tangent[3], 1.0f)) {
            std::cerr << "Unexpected generated tangent\n";
            return false;
        }

        return true;
    }

    bool validateDegenerateTangentFallback() {
        ark::asset::MeshVertex v0{};
        v0.position[0] = -1.0f;
        v0.normal[2] = 1.0f;

        ark::asset::MeshVertex v1 = v0;
        v1.position[0] = 1.0f;

        ark::asset::MeshVertex v2 = v0;
        v2.position[1] = 1.0f;

        ark::asset::MeshPrimitiveData primitive{};
        primitive.debugName = "DegenerateUvTriangle";
        primitive.vertices = {v0, v1, v2};
        primitive.indices = {0, 1, 2};
        if (!ark::asset::generateTangents(primitive)) {
            std::cerr << "Degenerate tangent generation failed\n";
            return false;
        }

        for (const ark::asset::MeshVertex& vertex : primitive.vertices) {
            if (!validateTangent(vertex)) {
                std::cerr << "Degenerate tangent fallback is invalid\n";
                return false;
            }
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
    return validateMeshPrimitive() && validateTangentGeneration() && validateDegenerateTangentFallback() &&
                   validateModelData()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
