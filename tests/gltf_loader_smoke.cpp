#include "asset/GltfLoader.h"
#include "core/FileSystem.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {
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

        const ark::asset::MaterialData& material = model.materials.front();
        if (!material.hasBaseColorTexture() || material.baseColorTexturePath.filename() != "xiaowei.png") {
            std::cerr << "Unexpected glTF material texture path\n";
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
} // namespace

int main() {
    return validateForwardFixture() && validateMultidrawFixture() ? EXIT_SUCCESS : EXIT_FAILURE;
}
