#include "renderer/RenderQueue.h"
#include "renderer/RenderScene.h"
#include "renderer/MeshResource.h"
#include "renderer/material/MaterialResource.h"

#include <cstdlib>
#include <iostream>

namespace {
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
} // namespace

int main() {
    return validateSceneQueueBuild() ? EXIT_SUCCESS : EXIT_FAILURE;
}
