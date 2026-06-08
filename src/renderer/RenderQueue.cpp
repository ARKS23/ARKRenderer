#include "renderer/RenderQueue.h"

#include "renderer/RenderScene.h"

#include <utility>

namespace ark {
    void RenderQueue::build(const RenderScene& scene) {
        clear();
        m_DrawItems.reserve(scene.size());

        for (const SceneObject& object : scene.objects()) {
            if (!object.isDrawable()) {
                continue;
            }

            DrawItem item{};
            item.mesh = object.mesh;
            item.material = object.material;
            item.modelMatrix = object.transform;
            item.debugName = object.debugName;
            m_DrawItems.push_back(std::move(item));
        }
    }

    std::span<const DrawItem> RenderQueue::drawItems() const {
        return m_DrawItems;
    }

    void RenderQueue::clear() {
        m_DrawItems.clear();
    }
} // namespace ark
