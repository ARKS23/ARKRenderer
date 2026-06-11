#include "renderer/RenderQueue.h"
#include "renderer/RenderScene.h"
#include "renderer/MeshResource.h"
#include "renderer/TextureResource.h"
#include "renderer/material/MaterialResource.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
    bool near(float a, float b, float epsilon = 0.0001f) {
        return std::fabs(a - b) <= epsilon;
    }

    bool nearVec3(const glm::vec3& a, const glm::vec3& b, float epsilon = 0.0001f) {
        return near(a.x, b.x, epsilon) && near(a.y, b.y, epsilon) && near(a.z, b.z, epsilon);
    }

    bool createMaterial(ark::MaterialResource& resource,
                        ark::TextureResource& texture,
                        ark::asset::AlphaMode alphaMode,
                        const char* debugName) {
        ark::asset::MaterialData material{};
        material.baseColorTexture.path = ark::Path{"queue_dummy_base_color.png"};
        material.alphaMode = alphaMode;
        material.debugName = debugName;

        ark::MaterialTextureSet textures{};
        textures.baseColor = &texture;
        textures.normal = &texture;
        textures.metallicRoughness = &texture;
        textures.occlusion = &texture;
        textures.emissive = &texture;
        return resource.create(material, textures);
    }

    bool validateSceneQueueBuild() {
        ark::MeshResource meshA{};
        ark::MeshResource meshB{};
        ark::MaterialResource materialA{};
        ark::MaterialResource materialB{};

        ark::RenderScene scene{};
        if (!scene.empty() || scene.size() != 0) {
            std::cerr << "New RenderScene is not empty\n";
            return false;
        }

        if (!nearVec3(scene.lighting().mainLight.direction, glm::vec3{-0.35f, -0.8f, -0.45f}) ||
            !nearVec3(scene.lighting().mainLight.color, glm::vec3{1.0f, 0.96f, 0.88f}) ||
            !nearVec3(scene.lighting().ambientColor, glm::vec3{0.08f, 0.09f, 0.11f})) {
            std::cerr << "RenderScene default lighting is invalid\n";
            return false;
        }

        ark::SceneLighting lighting{};
        lighting.mainLight.direction = glm::vec3{0.0f, -1.0f, 0.0f};
        lighting.mainLight.color = glm::vec3{0.2f, 0.4f, 0.8f};
        lighting.ambientColor = glm::vec3{0.01f, 0.02f, 0.03f};
        scene.setLighting(lighting);
        if (!nearVec3(scene.lighting().mainLight.direction, lighting.mainLight.direction) ||
            !nearVec3(scene.lighting().mainLight.color, lighting.mainLight.color) ||
            !nearVec3(scene.lighting().ambientColor, lighting.ambientColor)) {
            std::cerr << "RenderScene did not preserve custom lighting\n";
            return false;
        }

        glm::mat4 transformA{1.0f};
        transformA[3][0] = 2.0f;
        glm::mat4 transformB{1.0f};
        transformB[3][1] = -3.0f;

        scene.addObject(meshA, materialA, transformA, "DrawA");
        scene.addObject(meshB, materialB, transformB, "DrawB");
        if (scene.size() != 2 || scene.objects()[0].debugName != "DrawA") {
            std::cerr << "RenderScene did not preserve objects\n";
            return false;
        }

        ark::RenderQueue queue{};
        queue.build(scene);
        if (queue.size() != 2 || queue.empty()) {
            std::cerr << "RenderQueue did not build expected draw items\n";
            return false;
        }

        const std::span<const ark::DrawItem> drawItems = queue.drawItems();
        if (drawItems[0].mesh != &meshA || drawItems[0].material != &materialA ||
            drawItems[1].mesh != &meshB || drawItems[1].material != &materialB) {
            std::cerr << "RenderQueue draw item resource references are invalid\n";
            return false;
        }

        if (drawItems[0].modelMatrix[3][0] != 2.0f || drawItems[1].modelMatrix[3][1] != -3.0f) {
            std::cerr << "RenderQueue did not preserve model matrices\n";
            return false;
        }

        scene.clear();
        queue.build(scene);
        if (!queue.empty()) {
            std::cerr << "RenderQueue did not clear stale draw items\n";
            return false;
        }

        return true;
    }

    bool validateAlphaBucketOrder() {
        ark::TextureResource texture{};
        ark::MeshResource opaqueMeshA{};
        ark::MeshResource opaqueMeshB{};
        ark::MeshResource maskMesh{};
        ark::MeshResource blendMeshA{};
        ark::MeshResource blendMeshB{};
        ark::MaterialResource opaqueMaterialA{};
        ark::MaterialResource opaqueMaterialB{};
        ark::MaterialResource maskMaterial{};
        ark::MaterialResource blendMaterialA{};
        ark::MaterialResource blendMaterialB{};

        if (!createMaterial(opaqueMaterialA, texture, ark::asset::AlphaMode::Opaque, "OpaqueA") ||
            !createMaterial(opaqueMaterialB, texture, ark::asset::AlphaMode::Opaque, "OpaqueB") ||
            !createMaterial(maskMaterial, texture, ark::asset::AlphaMode::Mask, "Mask") ||
            !createMaterial(blendMaterialA, texture, ark::asset::AlphaMode::Blend, "BlendA") ||
            !createMaterial(blendMaterialB, texture, ark::asset::AlphaMode::Blend, "BlendB")) {
            std::cerr << "Failed to create alpha bucket test materials\n";
            return false;
        }

        ark::RenderScene scene{};
        scene.addObject(blendMeshA, blendMaterialA, glm::mat4{1.0f}, "BlendA");
        scene.addObject(opaqueMeshA, opaqueMaterialA, glm::mat4{1.0f}, "OpaqueA");
        scene.addObject(maskMesh, maskMaterial, glm::mat4{1.0f}, "Mask");
        scene.addObject(blendMeshB, blendMaterialB, glm::mat4{1.0f}, "BlendB");
        scene.addObject(opaqueMeshB, opaqueMaterialB, glm::mat4{1.0f}, "OpaqueB");

        ark::RenderQueue queue{};
        queue.build(scene);
        const std::span<const ark::DrawItem> drawItems = queue.drawItems();
        if (drawItems.size() != 5) {
            std::cerr << "RenderQueue alpha bucket test produced unexpected item count\n";
            return false;
        }

        if (drawItems[0].material != &opaqueMaterialA || drawItems[1].material != &opaqueMaterialB ||
            drawItems[2].material != &maskMaterial || drawItems[3].material != &blendMaterialA ||
            drawItems[4].material != &blendMaterialB) {
            std::cerr << "RenderQueue did not order alpha buckets as expected\n";
            return false;
        }

        if (drawItems[0].debugName != "OpaqueA" || drawItems[1].debugName != "OpaqueB" ||
            drawItems[2].debugName != "Mask" || drawItems[3].debugName != "BlendA" ||
            drawItems[4].debugName != "BlendB") {
            std::cerr << "RenderQueue did not preserve stable order within alpha buckets\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateSceneQueueBuild() && validateAlphaBucketOrder() ? EXIT_SUCCESS : EXIT_FAILURE;
}
