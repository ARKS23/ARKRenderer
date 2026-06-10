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

    bool validTangent(const ark::asset::MeshVertex& vertex) {
        const float length = std::sqrt(vertex.tangent[0] * vertex.tangent[0] +
                                       vertex.tangent[1] * vertex.tangent[1] +
                                       vertex.tangent[2] * vertex.tangent[2]);
        return std::isfinite(length) && near(length, 1.0f) &&
               (near(vertex.tangent[3], 1.0f) || near(vertex.tangent[3], -1.0f));
    }

    bool isDefaultTangent(const ark::asset::MeshVertex& vertex) {
        return near(vertex.tangent[0], 1.0f) && near(vertex.tangent[1], 0.0f) &&
               near(vertex.tangent[2], 0.0f) && near(vertex.tangent[3], 1.0f);
    }

    bool isIdentityTransform(const ark::asset::TextureTransformData& transform) {
        return !transform.hasTransform &&
               near(transform.offset[0], 0.0f) && near(transform.offset[1], 0.0f) &&
               near(transform.scale[0], 1.0f) && near(transform.scale[1], 1.0f) &&
               near(transform.rotation, 0.0f);
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

        if (!near(firstVertex.uv1[0], firstVertex.uv0[0]) || !near(firstVertex.uv1[1], firstVertex.uv0[1])) {
            std::cerr << "Unexpected glTF TEXCOORD_1 fallback data\n";
            return false;
        }

        if (!validTangent(firstVertex) || !near(firstVertex.tangent[0], 1.0f) ||
            !near(firstVertex.tangent[1], 0.0f) || !near(firstVertex.tangent[2], 0.0f) ||
            !near(firstVertex.tangent[3], -1.0f)) {
            std::cerr << "Unexpected generated glTF tangent data\n";
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

        if (material.alphaMode != ark::asset::AlphaMode::Mask || !near(material.alphaCutoff, 0.42f) ||
            !material.doubleSided) {
            std::cerr << "Unexpected glTF material render state\n";
            return false;
        }

        if (!isIdentityTransform(material.baseColorTexture.transform) ||
            !isIdentityTransform(material.normalTexture.transform) ||
            !isIdentityTransform(material.metallicRoughnessTexture.transform) ||
            !isIdentityTransform(material.occlusionTexture.transform) ||
            !isIdentityTransform(material.emissiveTexture.transform)) {
            std::cerr << "Unexpected default glTF texture transform data\n";
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

    bool validateSamplerFixture() {
        const ark::Path path = findFixturePath(ark::Path{"assets/models/sampler_fixture.gltf"});
        if (path.empty()) {
            std::cerr << "Failed to find sampler glTF fixture\n";
            return false;
        }

        const ark::asset::ModelData model = ark::asset::loadGltfModel(path);
        if (model.empty() || model.meshes.size() != 2 || model.materials.size() != 2) {
            std::cerr << "Unexpected sampler glTF model shape\n";
            return false;
        }

        const ark::asset::MaterialTextureSlotData& defaultSlot = model.materials[0].baseColorTexture;
        if (!defaultSlot.hasTexture() || defaultSlot.hasSampler ||
            defaultSlot.path.filename() != "xiaowei.png" || defaultSlot.texCoord != 0 ||
            defaultSlot.sampler.minFilter != ark::asset::TextureFilter::Linear ||
            defaultSlot.sampler.magFilter != ark::asset::TextureFilter::Linear ||
            defaultSlot.sampler.mipFilter != ark::asset::TextureFilter::Linear ||
            defaultSlot.sampler.addressU != ark::asset::TextureAddressMode::Repeat ||
            defaultSlot.sampler.addressV != ark::asset::TextureAddressMode::Repeat) {
            std::cerr << "Unexpected default sampler texture slot\n";
            return false;
        }

        const ark::asset::MaterialTextureSlotData& explicitSlot = model.materials[1].baseColorTexture;
        if (!explicitSlot.hasTexture() || !explicitSlot.hasSampler ||
            explicitSlot.path.filename() != "xiaowei.png" ||
            explicitSlot.texCoord != 1 ||
            explicitSlot.sampler.minFilter != ark::asset::TextureFilter::Nearest ||
            explicitSlot.sampler.magFilter != ark::asset::TextureFilter::Nearest ||
            explicitSlot.sampler.mipFilter != ark::asset::TextureFilter::Linear ||
            explicitSlot.sampler.addressU != ark::asset::TextureAddressMode::ClampToEdge ||
            explicitSlot.sampler.addressV != ark::asset::TextureAddressMode::MirroredRepeat) {
            std::cerr << "Unexpected explicit sampler texture slot\n";
            return false;
        }

        if (model.materials[1].baseColorTexturePath != explicitSlot.path) {
            std::cerr << "Legacy texture path did not mirror texture slot path\n";
            return false;
        }

        return true;
    }

    bool validateAlphaModesFixture() {
        const ark::Path path = findFixturePath(ark::Path{"assets/models/alpha_modes_fixture.gltf"});
        if (path.empty()) {
            std::cerr << "Failed to find alpha modes glTF fixture\n";
            return false;
        }

        const ark::asset::ModelData model = ark::asset::loadGltfModel(path);
        if (model.empty() || model.meshes.size() != 3 || model.materials.size() != 3 ||
            model.instances.size() != 3) {
            std::cerr << "Unexpected alpha modes glTF model shape\n";
            return false;
        }

        if (model.meshes[0].materialIndex != 0 || model.meshes[1].materialIndex != 1 ||
            model.meshes[2].materialIndex != 2) {
            std::cerr << "Unexpected alpha modes material remap\n";
            return false;
        }

        const ark::asset::MaterialData& opaque = model.materials[0];
        const ark::asset::MaterialData& mask = model.materials[1];
        const ark::asset::MaterialData& blend = model.materials[2];
        if (opaque.alphaMode != ark::asset::AlphaMode::Opaque || !near(opaque.alphaCutoff, 0.5f) ||
            opaque.doubleSided || !near(opaque.baseColorFactor[3], 0.25f)) {
            std::cerr << "Unexpected opaque alpha material state\n";
            return false;
        }

        if (mask.alphaMode != ark::asset::AlphaMode::Mask || !near(mask.alphaCutoff, 0.35f) ||
            !mask.doubleSided || !near(mask.baseColorFactor[3], 0.5f)) {
            std::cerr << "Unexpected mask alpha material state\n";
            return false;
        }

        if (blend.alphaMode != ark::asset::AlphaMode::Blend || !near(blend.alphaCutoff, 0.5f) ||
            blend.doubleSided || !near(blend.baseColorFactor[3], 0.75f)) {
            std::cerr << "Unexpected blend alpha material state\n";
            return false;
        }

        return true;
    }

    bool validateTexcoord1Fixture() {
        const ark::Path path = findFixturePath(ark::Path{"assets/models/texcoord1_fixture.gltf"});
        if (path.empty()) {
            std::cerr << "Failed to find TEXCOORD_1 glTF fixture\n";
            return false;
        }

        const ark::asset::ModelData model = ark::asset::loadGltfModel(path);
        if (model.empty() || model.meshes.size() != 1 || model.materials.size() != 1) {
            std::cerr << "Unexpected TEXCOORD_1 glTF model shape\n";
            return false;
        }

        const ark::asset::MeshPrimitiveData& mesh = model.meshes.front();
        if (mesh.vertices.size() != 4 || mesh.indices.size() != 6) {
            std::cerr << "Unexpected TEXCOORD_1 primitive size\n";
            return false;
        }

        const ark::asset::MeshVertex& firstVertex = mesh.vertices.front();
        if (!near(firstVertex.uv0[0], 0.0f) || !near(firstVertex.uv0[1], 1.0f) ||
            !near(firstVertex.uv1[0], 0.25f) || !near(firstVertex.uv1[1], 0.75f)) {
            std::cerr << "Unexpected TEXCOORD_1 vertex data\n";
            return false;
        }

        const ark::asset::MaterialData& material = model.materials.front();
        if (material.baseColorTexture.texCoord != 0 ||
            material.normalTexture.texCoord != 1 ||
            material.metallicRoughnessTexture.texCoord != 1 ||
            material.occlusionTexture.texCoord != 1 ||
            material.emissiveTexture.texCoord != 1) {
            std::cerr << "Unexpected material texture coordinate slots\n";
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

        const ark::asset::MeshVertex& firstVertex = mesh.vertices.front();
        if (!validTangent(firstVertex)) {
            std::cerr << "DamagedHelmet generated invalid tangent\n";
            return false;
        }

        bool hasNonDefaultTangent = false;
        for (const ark::asset::MeshVertex& vertex : mesh.vertices) {
            if (!isDefaultTangent(vertex)) {
                hasNonDefaultTangent = true;
                break;
            }
        }

        if (!hasNonDefaultTangent) {
            std::cerr << "DamagedHelmet did not generate non-default tangent data\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateForwardFixture() && validateMultidrawFixture() && validateMultinodeFixture() &&
                   validateTextureCacheFixture() && validateSamplerFixture() && validateTangentFixture() &&
                   validateAlphaModesFixture() && validateTexcoord1Fixture() &&
                   validateOptionalDamagedHelmetFixture()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
