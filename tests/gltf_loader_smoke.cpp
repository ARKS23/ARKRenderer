#include "asset/GltfLoader.h"
#include "core/FileSystem.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
    bool near(float lhs, float rhs) {
        return std::fabs(lhs - rhs) < 0.0001f;
    }

    ark::Path findFixturePath(const ark::Path& relative) {
        const std::array<ark::Path, 3> candidates{
            relative,
            ark::Path{"../"} / relative,
            ark::Path{"../../"} / relative,
        };

        return ark::findFirstExistingPath(candidates);
    }

    bool validateForwardFixture() {
        const ark::Path path = findFixturePath(ark::Path{"assets/models/forward_fixture.gltf"});
        if (path.empty()) {
            std::cerr << "Failed to find glTF fixture\n";
            return false;
        }

        const ark::asset::ModelData model = ark::asset::loadGltfModel(path);
        if (model.empty() || model.meshes.size() != 1 || model.materials.size() != 1) {
            std::cerr << "Unexpected glTF model shape\n";
            return false;
        }

        if (model.instances.size() != 1 || model.instances.front().meshIndex != 0) {
            std::cerr << "Unexpected glTF instance data\n";
            return false;
        }

        const ark::asset::MeshPrimitiveData& mesh = model.meshes.front();
        if (mesh.vertices.size() != 4 || mesh.indices.size() != 6) {
            std::cerr << "Unexpected glTF primitive size\n";
            return false;
        }

        if (mesh.indices[0] != 0 || mesh.indices[1] != 1 || mesh.indices[2] != 2 || mesh.indices[5] != 3) {
            std::cerr << "Unexpected glTF indices\n";
            return false;
        }

        const ark::asset::MeshVertex& firstVertex = mesh.vertices.front();
        if (firstVertex.position[0] != -1.0f || firstVertex.position[1] != -1.0f ||
            firstVertex.normal[2] != 1.0f || firstVertex.uv0[1] != 1.0f) {
            std::cerr << "Unexpected glTF vertex data\n";
            return false;
        }

        if (!near(firstVertex.tangent[0], 1.0f) || !near(firstVertex.tangent[1], 0.0f) ||
            !near(firstVertex.tangent[2], 0.0f) || !near(firstVertex.tangent[3], 1.0f)) {
            std::cerr << "Unexpected fallback glTF tangent data\n";
            return false;
        }

        const ark::asset::MaterialData& material = model.materials.front();
        if (!material.hasBaseColorTexture() || material.baseColorTexturePath.filename() != "xiaowei.png") {
            std::cerr << "Unexpected glTF material texture path\n";
            return false;
        }

        if (!material.hasNormalTexture() || !material.hasMetallicRoughnessTexture() ||
            !material.hasOcclusionTexture() || !material.hasEmissiveTexture() ||
            material.normalTexturePath.filename() != "xiaowei.png" ||
            material.metallicRoughnessTexturePath.filename() != "xiaowei.png" ||
            material.occlusionTexturePath.filename() != "xiaowei.png" ||
            material.emissiveTexturePath.filename() != "xiaowei.png") {
            std::cerr << "Unexpected glTF optional material texture paths\n";
            return false;
        }

        if (!near(material.baseColorFactor[0], 0.25f) || !near(material.baseColorFactor[1], 0.5f) ||
            !near(material.baseColorFactor[2], 0.75f) || !near(material.baseColorFactor[3], 0.8f) ||
            !near(material.metallicFactor, 0.35f) || !near(material.roughnessFactor, 0.65f) ||
            !near(material.emissiveFactor[0], 0.1f) || !near(material.emissiveFactor[1], 0.2f) ||
            !near(material.emissiveFactor[2], 0.3f) || !near(material.normalScale, 0.75f) ||
            !near(material.occlusionStrength, 0.5f)) {
            std::cerr << "Unexpected glTF material factors\n";
            return false;
        }

        return true;
    }

    bool validateTangentFixture() {
        const ark::Path path = findFixturePath(ark::Path{"assets/models/forward_tangent_fixture.gltf"});
        if (path.empty()) {
            std::cerr << "Failed to find tangent glTF fixture\n";
            return false;
        }

        const ark::asset::ModelData model = ark::asset::loadGltfModel(path);
        if (model.empty() || model.meshes.size() != 1 || model.materials.size() != 1) {
            std::cerr << "Unexpected tangent glTF model shape\n";
            return false;
        }

        const ark::asset::MeshPrimitiveData& mesh = model.meshes.front();
        if (mesh.vertices.size() != 4 || mesh.indices.size() != 6) {
            std::cerr << "Unexpected tangent glTF primitive size\n";
            return false;
        }

        const ark::asset::MeshVertex& firstVertex = mesh.vertices.front();
        if (!near(firstVertex.tangent[0], 0.0f) || !near(firstVertex.tangent[1], 1.0f) ||
            !near(firstVertex.tangent[2], 0.0f) || !near(firstVertex.tangent[3], -1.0f)) {
            std::cerr << "Unexpected explicit glTF tangent data\n";
            return false;
        }

        return true;
    }

    bool validateMultidrawFixture() {
        const ark::Path path = findFixturePath(ark::Path{"assets/models/forward_multidraw_fixture.gltf"});
        if (path.empty()) {
            std::cerr << "Failed to find multidraw glTF fixture\n";
            return false;
        }

        const ark::asset::ModelData model = ark::asset::loadGltfModel(path);
        if (model.empty() || model.meshes.size() != 2 || model.materials.size() != 2) {
            std::cerr << "Unexpected multidraw glTF model shape\n";
            return false;
        }

        if (model.instances.size() != 2 || model.instances[0].meshIndex != 0 || model.instances[1].meshIndex != 1) {
            std::cerr << "Unexpected multidraw instance data\n";
            return false;
        }

        const ark::asset::MeshPrimitiveData& firstMesh = model.meshes[0];
        const ark::asset::MeshPrimitiveData& secondMesh = model.meshes[1];
        if (firstMesh.vertices.size() != 4 || firstMesh.indices.size() != 3 ||
            secondMesh.vertices.size() != 4 || secondMesh.indices.size() != 3) {
            std::cerr << "Unexpected multidraw primitive sizes\n";
            return false;
        }

        if (firstMesh.materialIndex != 0 || secondMesh.materialIndex != 1) {
            std::cerr << "Unexpected multidraw material remap\n";
            return false;
        }

        if (firstMesh.indices[0] != 0 || firstMesh.indices[1] != 1 || firstMesh.indices[2] != 2 ||
            secondMesh.indices[0] != 0 || secondMesh.indices[1] != 2 || secondMesh.indices[2] != 3) {
            std::cerr << "Unexpected multidraw indices\n";
            return false;
        }

        if (model.materials[0].baseColorTexturePath.filename() != "xiaowei.png" ||
            model.materials[1].baseColorTexturePath.filename() != "xiaowei.png") {
            std::cerr << "Unexpected multidraw material texture paths\n";
            return false;
        }

        return true;
    }

    bool validateMultinodeFixture() {
        const ark::Path path = findFixturePath(ark::Path{"assets/models/forward_multinode_fixture.gltf"});
        if (path.empty()) {
            std::cerr << "Failed to find multinode glTF fixture\n";
            return false;
        }

        const ark::asset::ModelData model = ark::asset::loadGltfModel(path);
        if (model.empty() || model.meshes.size() != 1 || model.materials.size() != 1 || model.instances.size() != 2) {
            std::cerr << "Unexpected multinode glTF model shape\n";
            return false;
        }

        if (model.instances[0].meshIndex != 0 || model.instances[1].meshIndex != 0) {
            std::cerr << "Unexpected multinode primitive instance indices\n";
            return false;
        }

        const float leftTranslation = model.instances[0].localTransform.matrix[12];
        const float rightTranslation = model.instances[1].localTransform.matrix[12];
        const float rightScaleX = model.instances[1].localTransform.matrix[0];
        if (leftTranslation != -1.25f || rightTranslation != 1.25f || rightScaleX != 0.5f) {
            std::cerr << "Unexpected multinode transforms\n";
            return false;
        }

        const ark::asset::MaterialData& material = model.materials.front();
        if (!near(material.baseColorFactor[0], 1.0f) || !near(material.baseColorFactor[1], 1.0f) ||
            !near(material.baseColorFactor[2], 1.0f) || !near(material.baseColorFactor[3], 1.0f) ||
            !near(material.metallicFactor, 1.0f) || !near(material.roughnessFactor, 1.0f) ||
            !near(material.emissiveFactor[0], 0.0f) || !near(material.emissiveFactor[1], 0.0f) ||
            !near(material.emissiveFactor[2], 0.0f) || !near(material.normalScale, 1.0f) ||
            !near(material.occlusionStrength, 1.0f)) {
            std::cerr << "Unexpected default glTF material factors\n";
            return false;
        }

        if (material.hasNormalTexture() || material.hasMetallicRoughnessTexture() ||
            material.hasOcclusionTexture() || material.hasEmissiveTexture()) {
            std::cerr << "Unexpected optional glTF material texture defaults\n";
            return false;
        }

        return true;
    }

    bool validateTextureCacheFixture() {
        const ark::Path path = findFixturePath(ark::Path{"assets/models/texture_cache_fixture.gltf"});
        if (path.empty()) {
            std::cerr << "Failed to find texture cache glTF fixture\n";
            return false;
        }

        const ark::asset::ModelData model = ark::asset::loadGltfModel(path);
        if (model.empty() || model.meshes.size() != 2 || model.materials.size() != 2 ||
            model.instances.size() != 2) {
            std::cerr << "Unexpected texture cache glTF model shape\n";
            return false;
        }

        if (model.meshes[0].materialIndex != 0 || model.meshes[1].materialIndex != 1) {
            std::cerr << "Unexpected texture cache material remap\n";
            return false;
        }

        if (model.materials[0].baseColorTexturePath.empty() ||
            model.materials[0].baseColorTexturePath != model.materials[1].baseColorTexturePath ||
            model.materials[0].baseColorTexturePath.filename() != "xiaowei.png") {
            std::cerr << "Texture cache fixture did not resolve shared texture path\n";
            return false;
        }

        return true;
    }

    bool validateOptionalDamagedHelmetFixture() {
        const ark::Path path = findFixturePath(ark::Path{"assets/models/DamagedHelmet/DamagedHelmet.gltf"});
        if (path.empty()) {
            return true;
        }

        const ark::asset::ModelData model = ark::asset::loadGltfModel(path);
        if (model.empty() || model.meshes.size() != 1 || model.materials.size() != 1 ||
            model.instances.size() != 1) {
            std::cerr << "Unexpected DamagedHelmet glTF model shape\n";
            return false;
        }

        const ark::asset::MeshPrimitiveData& mesh = model.meshes.front();
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            std::cerr << "DamagedHelmet mesh data is empty\n";
            return false;
        }

        const ark::asset::MaterialData& material = model.materials.front();
        if (!material.hasBaseColorTexture() || !material.hasNormalTexture() ||
            !material.hasMetallicRoughnessTexture() || !material.hasOcclusionTexture() ||
            !material.hasEmissiveTexture()) {
            std::cerr << "DamagedHelmet material did not expose expected texture slots\n";
            return false;
        }

        // DamagedHelmet fixture has no TANGENT in the checked-in local copy; Phase 0.17 should use fallback.
        const ark::asset::MeshVertex& firstVertex = mesh.vertices.front();
        if (!near(firstVertex.tangent[0], 1.0f) || !near(firstVertex.tangent[1], 0.0f) ||
            !near(firstVertex.tangent[2], 0.0f) || !near(firstVertex.tangent[3], 1.0f)) {
            std::cerr << "Unexpected DamagedHelmet fallback tangent\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateForwardFixture() && validateMultidrawFixture() && validateMultinodeFixture() &&
                   validateTextureCacheFixture() && validateTangentFixture() && validateOptionalDamagedHelmetFixture()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
