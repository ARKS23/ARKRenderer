#include "renderer/RenderQueue.h"
#include "renderer/RenderScene.h"
#include "renderer/EnvironmentResource.h"
#include "renderer/Frustum.h"
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

    glm::mat4 translated(float x, float y, float z) {
        glm::mat4 transform{1.0f};
        transform[3][0] = x;
        transform[3][1] = y;
        transform[3][2] = z;
        return transform;
    }

    bool validateSceneQueueBuild() {
        ark::MeshResource meshA{};
        ark::MeshResource meshB{};
        ark::MaterialResource materialA{};
        ark::MaterialResource materialB{};

        ark::RenderScene scene{};
        if (!scene.empty() || scene.size() != 0 || scene.hasBounds()) {
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

        ark::EnvironmentResource environmentResource{};
        ark::SceneEnvironment environment{};
        environment.environment = &environmentResource;
        environment.intensity = 1.25f;
        scene.setEnvironment(environment);
        if (scene.environment().environment != &environmentResource ||
            !near(scene.environment().intensity, 1.25f) ||
            !scene.environment().isEnabled()) {
            std::cerr << "RenderScene did not preserve custom environment\n";
            return false;
        }

        environment.intensity = -1.0f;
        scene.setEnvironment(environment);
        if (!near(scene.environment().intensity, 0.0f) || scene.environment().isEnabled()) {
            std::cerr << "RenderScene did not clamp negative environment intensity\n";
            return false;
        }

        environment.intensity = 1.25f;
        scene.setEnvironment(environment);

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
        if (scene.hasBounds()) {
            std::cerr << "RenderScene should ignore invalid resource bounds\n";
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

        if (drawItems[0].worldBounds.isValid() || drawItems[1].worldBounds.isValid() ||
            queue.stats().totalItems != 2 || queue.stats().visibleItems != 2 ||
            queue.stats().culledItems != 0 || queue.stats().invalidBoundsItems != 2) {
            std::cerr << "RenderQueue did not track invalid draw item bounds\n";
            return false;
        }

        scene.clear();
        if (scene.hasBounds()) {
            std::cerr << "RenderScene clear did not reset bounds\n";
            return false;
        }
        if (scene.environment().environment != &environmentResource || !scene.environment().isEnabled()) {
            std::cerr << "RenderScene clear should preserve scene environment policy\n";
            return false;
        }

        scene.clearEnvironment();
        if (scene.environment().environment != nullptr || scene.environment().isEnabled()) {
            std::cerr << "RenderScene did not clear environment\n";
            return false;
        }

        queue.build(scene);
        if (!queue.empty()) {
            std::cerr << "RenderQueue did not clear stale draw items\n";
            return false;
        }
        if (queue.stats().totalItems != 0 || queue.stats().visibleItems != 0 ||
            queue.stats().culledItems != 0 || queue.stats().invalidBoundsItems != 0) {
            std::cerr << "RenderQueue did not reset stats for empty builds\n";
            return false;
        }

        return true;
    }

    bool validateBuildDescInvalidBoundsRemainVisible() {
        ark::TextureResource texture{};
        ark::MeshResource meshA{};
        ark::MeshResource meshB{};
        ark::MaterialResource materialA{};
        ark::MaterialResource materialB{};
        if (!createMaterial(materialA, texture, ark::asset::AlphaMode::Opaque, "InvalidA") ||
            !createMaterial(materialB, texture, ark::asset::AlphaMode::Mask, "InvalidB")) {
            std::cerr << "Failed to create invalid bounds culling materials\n";
            return false;
        }

        ark::RenderScene scene{};
        scene.addObject(meshA, materialA, translated(20.0f, 0.0f, 0.0f), "InvalidA");
        scene.addObject(meshB, materialB, translated(-20.0f, 0.0f, 0.0f), "InvalidB");

        const ark::Frustum frustum = ark::Frustum::fromViewProjection(glm::mat4{1.0f});
        ark::RenderQueueBuildDesc desc{};
        desc.scene = &scene;
        desc.cameraFrustum = &frustum;
        desc.enableFrustumCulling = true;

        ark::RenderQueue queue{};
        queue.build(desc);
        if (queue.size() != 2 || queue.stats().totalItems != 2 ||
            queue.stats().visibleItems != 2 || queue.stats().culledItems != 0 ||
            queue.stats().invalidBoundsItems != 2) {
            std::cerr << "Frustum culling should keep invalid bounds visible\n";
            return false;
        }

        ark::RenderQueue emptyQueue{};
        emptyQueue.build(ark::RenderQueueBuildDesc{});
        if (!emptyQueue.empty() || emptyQueue.stats().totalItems != 0 ||
            emptyQueue.stats().visibleItems != 0 || emptyQueue.stats().culledItems != 0 ||
            emptyQueue.stats().invalidBoundsItems != 0) {
            std::cerr << "RenderQueue build desc without scene should produce an empty queue\n";
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

    bool validateBlendBucketBackToFrontSort() {
        ark::TextureResource texture{};
        ark::MeshResource opaqueMesh{};
        ark::MeshResource maskMesh{};
        ark::MeshResource nearBlendMesh{};
        ark::MeshResource midBlendMesh{};
        ark::MeshResource farBlendMesh{};
        ark::MeshResource stableBlendMeshA{};
        ark::MeshResource stableBlendMeshB{};
        ark::MaterialResource opaqueMaterial{};
        ark::MaterialResource maskMaterial{};
        ark::MaterialResource nearBlendMaterial{};
        ark::MaterialResource midBlendMaterial{};
        ark::MaterialResource farBlendMaterial{};
        ark::MaterialResource stableBlendMaterialA{};
        ark::MaterialResource stableBlendMaterialB{};

        if (!createMaterial(opaqueMaterial, texture, ark::asset::AlphaMode::Opaque, "Opaque") ||
            !createMaterial(maskMaterial, texture, ark::asset::AlphaMode::Mask, "Mask") ||
            !createMaterial(nearBlendMaterial, texture, ark::asset::AlphaMode::Blend, "NearBlend") ||
            !createMaterial(midBlendMaterial, texture, ark::asset::AlphaMode::Blend, "MidBlend") ||
            !createMaterial(farBlendMaterial, texture, ark::asset::AlphaMode::Blend, "FarBlend") ||
            !createMaterial(stableBlendMaterialA, texture, ark::asset::AlphaMode::Blend, "StableBlendA") ||
            !createMaterial(stableBlendMaterialB, texture, ark::asset::AlphaMode::Blend, "StableBlendB")) {
            std::cerr << "Failed to create blend sorting test materials\n";
            return false;
        }

        ark::RenderScene scene{};
        scene.addObject(nearBlendMesh, nearBlendMaterial, translated(0.0f, 0.0f, 1.0f), "NearBlend");
        scene.addObject(opaqueMesh, opaqueMaterial, translated(0.0f, 0.0f, 0.0f), "Opaque");
        scene.addObject(midBlendMesh, midBlendMaterial, translated(0.0f, 0.0f, 3.0f), "MidBlend");
        scene.addObject(maskMesh, maskMaterial, translated(0.0f, 0.0f, 0.0f), "Mask");
        scene.addObject(farBlendMesh, farBlendMaterial, translated(0.0f, 0.0f, 6.0f), "FarBlend");
        scene.addObject(stableBlendMeshA, stableBlendMaterialA, translated(2.0f, 0.0f, 0.0f), "StableBlendA");
        scene.addObject(stableBlendMeshB, stableBlendMaterialB, translated(-2.0f, 0.0f, 0.0f), "StableBlendB");

        ark::RenderQueue queue{};
        queue.build(scene, glm::vec3{0.0f});
        const std::span<const ark::DrawItem> drawItems = queue.drawItems();
        if (drawItems.size() != 7) {
            std::cerr << "Blend sorting test produced unexpected item count\n";
            return false;
        }

        if (drawItems[0].debugName != "Opaque" || drawItems[1].debugName != "Mask") {
            std::cerr << "Blend sorting test did not preserve bucket order\n";
            return false;
        }

        if (drawItems[2].debugName != "FarBlend" ||
            drawItems[3].debugName != "MidBlend" ||
            drawItems[4].debugName != "StableBlendA" ||
            drawItems[5].debugName != "StableBlendB" ||
            drawItems[6].debugName != "NearBlend") {
            std::cerr << "Blend sorting test did not order Blend items back-to-front\n";
            return false;
        }

        if (!near(drawItems[2].sortDistanceSq, 36.0f) ||
            !near(drawItems[3].sortDistanceSq, 9.0f) ||
            !near(drawItems[4].sortDistanceSq, 4.0f) ||
            !near(drawItems[5].sortDistanceSq, 4.0f) ||
            !near(drawItems[6].sortDistanceSq, 1.0f)) {
            std::cerr << "Blend sorting test produced invalid sort keys\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateSceneQueueBuild() &&
                   validateBuildDescInvalidBoundsRemainVisible() &&
                   validateAlphaBucketOrder() &&
                   validateBlendBucketBackToFrontSort()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
