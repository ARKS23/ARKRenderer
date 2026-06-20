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

    bool textureTransformNear(const ark::asset::TextureTransformData& transform,
                              float offsetX,
                              float offsetY,
                              float scaleX,
                              float scaleY,
                              float rotation) {
        return transform.hasTransform &&
               near(transform.offset[0], offsetX) && near(transform.offset[1], offsetY) &&
               near(transform.scale[0], scaleX) && near(transform.scale[1], scaleY) &&
               near(transform.rotation, rotation);
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

    bool validateSpecularIblValidationFixture() {
        constexpr std::size_t RowCount = 3;
        constexpr std::size_t ColumnCount = 5;
        constexpr std::size_t MaterialCount = RowCount * ColumnCount;
        const std::array<float, RowCount> metallicValues{0.0f, 0.5f, 1.0f};
        const std::array<float, ColumnCount> roughnessValues{0.05f, 0.25f, 0.5f, 0.75f, 1.0f};
        const std::array<const char*, MaterialCount> materialNames{
            "M00_R005",
            "M00_R025",
            "M00_R050",
            "M00_R075",
            "M00_R100",
            "M50_R005",
            "M50_R025",
            "M50_R050",
            "M50_R075",
            "M50_R100",
            "M100_R005",
            "M100_R025",
            "M100_R050",
            "M100_R075",
            "M100_R100",
        };

        const ark::Path path = findFixturePath(ark::Path{"assets/models/specular_ibl_validation_fixture.gltf"});
        if (path.empty()) {
            std::cerr << "Failed to find specular IBL validation fixture\n";
            return false;
        }

        const ark::asset::ModelData model = ark::asset::loadGltfModel(path);
        if (model.empty() || model.meshes.size() != MaterialCount ||
            model.materials.size() != MaterialCount || model.instances.size() != MaterialCount) {
            std::cerr << "Unexpected specular IBL validation fixture shape\n";
            return false;
        }

        if (model.cameras.size() != 1 || model.sceneCameras.size() != 1) {
            std::cerr << "Specular IBL validation fixture camera was not loaded\n";
            return false;
        }

        const ark::asset::CameraData& camera = model.cameras.front();
        const ark::asset::SceneCameraData& sceneCamera = model.sceneCameras.front();
        if (camera.debugName != "ValidationCamera" ||
            camera.type != ark::asset::CameraProjectionType::Perspective ||
            !near(camera.perspective.yfov, 0.785398f) ||
            !near(camera.perspective.aspectRatio, 1.777778f) ||
            !near(camera.perspective.znear, 0.1f) ||
            !near(camera.perspective.zfar, 100.0f) ||
            !camera.perspective.hasZfar ||
            sceneCamera.debugName != "ValidationCameraNode" ||
            sceneCamera.cameraIndex != 0 ||
            !near(sceneCamera.worldTransform.matrix[12], 0.0f) ||
            !near(sceneCamera.worldTransform.matrix[13], 0.0f) ||
            !near(sceneCamera.worldTransform.matrix[14], 4.0f)) {
            std::cerr << "Specular IBL validation fixture camera data is invalid\n";
            return false;
        }

        for (std::size_t index = 0; index < MaterialCount; ++index) {
            const std::size_t row = index / ColumnCount;
            const std::size_t column = index % ColumnCount;
            const ark::asset::MaterialData& material = model.materials[index];
            const ark::asset::MeshPrimitiveData& mesh = model.meshes[index];
            const ark::asset::MeshPrimitiveInstanceData& instance = model.instances[index];

            if (material.debugName != materialNames[index] ||
                mesh.materialIndex != index || instance.meshIndex != index ||
                material.alphaMode != ark::asset::AlphaMode::Opaque ||
                material.doubleSided ||
                !material.hasBaseColorTexture() ||
                material.baseColorTexturePath.filename() != "xiaowei.png" ||
                material.hasNormalTexture() ||
                material.hasMetallicRoughnessTexture() ||
                material.hasOcclusionTexture() ||
                material.hasEmissiveTexture()) {
                std::cerr << "Unexpected specular IBL validation fixture material or primitive metadata\n";
                return false;
            }

            if (!near(material.baseColorFactor[0], 1.0f) ||
                !near(material.baseColorFactor[1], 1.0f) ||
                !near(material.baseColorFactor[2], 1.0f) ||
                !near(material.baseColorFactor[3], 1.0f) ||
                !near(material.metallicFactor, metallicValues[row]) ||
                !near(material.roughnessFactor, roughnessValues[column])) {
                std::cerr << "Unexpected specular IBL validation fixture material factors\n";
                return false;
            }

            if (mesh.vertices.size() != 4 || mesh.indices.size() != 6) {
                std::cerr << "Unexpected specular IBL validation fixture mesh data\n";
                return false;
            }
        }

        if (!near(model.instances.front().localTransform.matrix[12], -1.2f) ||
            !near(model.instances.front().localTransform.matrix[13], 0.6f) ||
            !near(model.instances.front().localTransform.matrix[0], 0.25f) ||
            !near(model.instances.back().localTransform.matrix[12], 1.2f) ||
            !near(model.instances.back().localTransform.matrix[13], -0.6f) ||
            !near(model.instances.back().localTransform.matrix[5], 0.25f)) {
            std::cerr << "Unexpected specular IBL validation fixture transforms\n";
            return false;
        }

        return true;
    }

    bool validateMaterialBallValidationFixture() {
        constexpr std::size_t RowCount = 3;
        constexpr std::size_t ColumnCount = 5;
        constexpr std::size_t MaterialCount = RowCount * ColumnCount;
        constexpr std::size_t SphereVertexCount = 91;
        constexpr std::size_t SphereIndexCount = 432;
        const std::array<float, RowCount> metallicValues{0.0f, 0.5f, 1.0f};
        const std::array<float, ColumnCount> roughnessValues{0.05f, 0.25f, 0.5f, 0.75f, 1.0f};
        const std::array<std::array<float, 4>, RowCount> baseColorValues{{
            {0.8f, 0.76f, 0.68f, 1.0f},
            {0.82f, 0.82f, 0.82f, 1.0f},
            {0.95f, 0.93f, 0.86f, 1.0f},
        }};
        const std::array<const char*, MaterialCount> materialNames{
            "MB_M00_R005",
            "MB_M00_R025",
            "MB_M00_R050",
            "MB_M00_R075",
            "MB_M00_R100",
            "MB_M50_R005",
            "MB_M50_R025",
            "MB_M50_R050",
            "MB_M50_R075",
            "MB_M50_R100",
            "MB_M100_R005",
            "MB_M100_R025",
            "MB_M100_R050",
            "MB_M100_R075",
            "MB_M100_R100",
        };

        const ark::Path path = findFixturePath(ark::Path{"assets/models/material_ball_validation_fixture.gltf"});
        if (path.empty()) {
            std::cerr << "Failed to find material ball validation fixture\n";
            return false;
        }

        const ark::asset::ModelData model = ark::asset::loadGltfModel(path);
        if (model.empty() || model.meshes.size() != MaterialCount ||
            model.materials.size() != MaterialCount || model.instances.size() != MaterialCount) {
            std::cerr << "Unexpected material ball validation fixture shape\n";
            return false;
        }

        if (model.cameras.size() != 1 || model.sceneCameras.size() != 1) {
            std::cerr << "Material ball validation fixture camera was not loaded\n";
            return false;
        }

        const ark::asset::CameraData& camera = model.cameras.front();
        const ark::asset::SceneCameraData& sceneCamera = model.sceneCameras.front();
        if (camera.debugName != "MaterialBallCamera" ||
            camera.type != ark::asset::CameraProjectionType::Perspective ||
            !near(camera.perspective.yfov, 0.785398f) ||
            !near(camera.perspective.aspectRatio, 1.777778f) ||
            !near(camera.perspective.znear, 0.1f) ||
            !near(camera.perspective.zfar, 100.0f) ||
            !camera.perspective.hasZfar ||
            sceneCamera.debugName != "MaterialBallCameraNode" ||
            sceneCamera.cameraIndex != 0 ||
            !near(sceneCamera.worldTransform.matrix[12], 0.0f) ||
            !near(sceneCamera.worldTransform.matrix[13], 0.0f) ||
            !near(sceneCamera.worldTransform.matrix[14], 6.0f)) {
            std::cerr << "Material ball validation fixture camera data is invalid\n";
            return false;
        }

        for (std::size_t index = 0; index < MaterialCount; ++index) {
            const std::size_t row = index / ColumnCount;
            const std::size_t column = index % ColumnCount;
            const ark::asset::MaterialData& material = model.materials[index];
            const ark::asset::MeshPrimitiveData& mesh = model.meshes[index];
            const ark::asset::MeshPrimitiveInstanceData& instance = model.instances[index];

            if (material.debugName != materialNames[index] ||
                mesh.materialIndex != index || instance.meshIndex != index ||
                material.alphaMode != ark::asset::AlphaMode::Opaque ||
                material.doubleSided ||
                !material.hasBaseColorTexture() ||
                material.baseColorTexturePath.filename() != "xiaowei.png" ||
                material.hasNormalTexture() ||
                material.hasMetallicRoughnessTexture() ||
                material.hasOcclusionTexture() ||
                material.hasEmissiveTexture()) {
                std::cerr << "Unexpected material ball validation fixture material or primitive metadata\n";
                return false;
            }

            const std::array<float, 4>& baseColor = baseColorValues[row];
            if (!near(material.baseColorFactor[0], baseColor[0]) ||
                !near(material.baseColorFactor[1], baseColor[1]) ||
                !near(material.baseColorFactor[2], baseColor[2]) ||
                !near(material.baseColorFactor[3], baseColor[3]) ||
                !near(material.metallicFactor, metallicValues[row]) ||
                !near(material.roughnessFactor, roughnessValues[column])) {
                std::cerr << "Unexpected material ball validation fixture material factors\n";
                return false;
            }

            if (mesh.vertices.size() != SphereVertexCount || mesh.indices.size() != SphereIndexCount) {
                std::cerr << "Unexpected material ball validation fixture mesh data\n";
                return false;
            }

            const ark::asset::MeshVertex& sampleVertex = mesh.vertices[20];
            const float normalLength =
                std::sqrt(sampleVertex.normal[0] * sampleVertex.normal[0] +
                          sampleVertex.normal[1] * sampleVertex.normal[1] +
                          sampleVertex.normal[2] * sampleVertex.normal[2]);
            if (!near(normalLength, 1.0f) || !validTangent(sampleVertex)) {
                std::cerr << "Material ball validation fixture generated invalid sphere basis data\n";
                return false;
            }
        }

        if (!near(model.instances.front().localTransform.matrix[12], -2.4f) ||
            !near(model.instances.front().localTransform.matrix[13], 1.1f) ||
            !near(model.instances.front().localTransform.matrix[0], 0.45f) ||
            !near(model.instances.back().localTransform.matrix[12], 2.4f) ||
            !near(model.instances.back().localTransform.matrix[13], -1.1f) ||
            !near(model.instances.back().localTransform.matrix[10], 0.45f)) {
            std::cerr << "Unexpected material ball validation fixture transforms\n";
            return false;
        }

        return true;
    }

    bool validateShadowProbeSpheresFixture() {
        constexpr std::size_t SphereCount = 5;
        constexpr std::size_t SphereVertexCount = 1225;
        constexpr std::size_t SphereIndexCount = 6912;

        const ark::Path path = findFixturePath(ark::Path{"assets/models/shadow_probe_spheres.gltf"});
        if (path.empty()) {
            std::cerr << "Failed to find shadow probe spheres fixture\n";
            return false;
        }

        const ark::asset::ModelData model = ark::asset::loadGltfModel(path);
        if (model.empty() ||
            model.meshes.size() != SphereCount ||
            model.materials.size() != SphereCount ||
            model.instances.size() != SphereCount) {
            std::cerr << "Unexpected shadow probe spheres fixture shape\n";
            return false;
        }

        for (std::size_t index = 0; index < SphereCount; ++index) {
            const ark::asset::MeshPrimitiveData& mesh = model.meshes[index];
            const ark::asset::MaterialData& material = model.materials[index];
            const ark::asset::MeshPrimitiveInstanceData& instance = model.instances[index];

            if (mesh.vertices.size() != SphereVertexCount ||
                mesh.indices.size() != SphereIndexCount ||
                mesh.materialIndex != index ||
                instance.meshIndex != index ||
                !material.hasBaseColorTexture() ||
                material.baseColorTexturePath.filename() != "xiaowei.png" ||
                material.alphaMode != ark::asset::AlphaMode::Opaque ||
                material.doubleSided) {
                std::cerr << "Unexpected shadow probe sphere mesh or material metadata\n";
                return false;
            }

            const ark::asset::MeshVertex& sampleVertex = mesh.vertices[128];
            const float normalLength =
                std::sqrt(sampleVertex.normal[0] * sampleVertex.normal[0] +
                          sampleVertex.normal[1] * sampleVertex.normal[1] +
                          sampleVertex.normal[2] * sampleVertex.normal[2]);
            if (!near(normalLength, 1.0f) || !validTangent(sampleVertex)) {
                std::cerr << "Shadow probe sphere generated invalid basis data\n";
                return false;
            }
        }

        if (!near(model.instances.front().localTransform.matrix[12], -4.8f) ||
            !near(model.instances.front().localTransform.matrix[0], 0.72f) ||
            !near(model.instances.back().localTransform.matrix[12], 4.8f) ||
            !near(model.instances.back().localTransform.matrix[14], 2.0f) ||
            !near(model.instances.back().localTransform.matrix[10], 0.72f)) {
            std::cerr << "Unexpected shadow probe sphere transforms\n";
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

    bool validateTextureTransformFixture() {
        const ark::Path path = findFixturePath(ark::Path{"assets/models/texture_transform_fixture.gltf"});
        if (path.empty()) {
            std::cerr << "Failed to find texture transform glTF fixture\n";
            return false;
        }

        const ark::asset::ModelData model = ark::asset::loadGltfModel(path);
        if (model.empty() || model.meshes.size() != 1 || model.materials.size() != 1) {
            std::cerr << "Unexpected texture transform glTF model shape\n";
            return false;
        }

        const ark::asset::MeshPrimitiveData& mesh = model.meshes.front();
        if (mesh.vertices.size() != 4 || mesh.indices.size() != 6) {
            std::cerr << "Unexpected texture transform primitive size\n";
            return false;
        }

        const ark::asset::MeshVertex& firstVertex = mesh.vertices.front();
        if (!near(firstVertex.uv0[0], 0.0f) || !near(firstVertex.uv0[1], 1.0f) ||
            !near(firstVertex.uv1[0], 0.25f) || !near(firstVertex.uv1[1], 0.75f)) {
            std::cerr << "Unexpected texture transform vertex UV data\n";
            return false;
        }

        const ark::asset::MaterialData& material = model.materials.front();
        if (!material.hasBaseColorTexture() || !material.hasNormalTexture() ||
            !material.hasMetallicRoughnessTexture() || !material.hasOcclusionTexture() ||
            !material.hasEmissiveTexture()) {
            std::cerr << "Texture transform fixture did not expose all texture slots\n";
            return false;
        }

        if (material.baseColorTexture.texCoord != 1 ||
            material.normalTexture.texCoord != 0 ||
            material.metallicRoughnessTexture.texCoord != 1 ||
            material.occlusionTexture.texCoord != 0 ||
            material.emissiveTexture.texCoord != 1) {
            std::cerr << "Unexpected texture transform texCoord slots\n";
            return false;
        }

        if (!textureTransformNear(material.baseColorTexture.transform, 0.125f, 0.25f, 2.0f, 0.5f, 0.5f) ||
            !textureTransformNear(material.normalTexture.transform, -0.25f, 0.0f, 0.75f, 0.5f, 1.0f) ||
            !textureTransformNear(material.metallicRoughnessTexture.transform, 0.0f, 0.4f, 1.5f, 1.0f, 0.0f) ||
            !textureTransformNear(material.occlusionTexture.transform, 0.2f, -0.1f, 1.0f, 2.0f, -0.25f) ||
            !textureTransformNear(material.emissiveTexture.transform, 0.0f, 0.0f, 1.0f, 1.0f, 0.25f)) {
            std::cerr << "Unexpected texture transform data\n";
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
                   validateSpecularIblValidationFixture() &&
                   validateMaterialBallValidationFixture() &&
                   validateShadowProbeSpheresFixture() &&
                   validateTextureCacheFixture() && validateSamplerFixture() && validateTangentFixture() &&
                   validateAlphaModesFixture() && validateTexcoord1Fixture() &&
                   validateTextureTransformFixture() && validateOptionalDamagedHelmetFixture()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
