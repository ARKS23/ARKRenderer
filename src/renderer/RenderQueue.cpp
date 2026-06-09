#include "renderer/RenderQueue.h"

#include "renderer/ModelResource.h"
#include "renderer/RenderScene.h"

#include <utility>

namespace ark {
    void RenderQueue::build(const RenderScene& scene) {
        clear();
        m_DrawItems.reserve(scene.size());

        for (const SceneModel& model : scene.models()) {
            if (!model.isDrawable()) {
                continue;
            }

            for (const ModelPrimitiveInstance& instance : model.model->instances()) {
                MeshResource* mesh = model.model->primitiveMesh(instance.primitiveIndex);
                MaterialResource* material = model.model->primitiveMaterial(instance.primitiveIndex);
                if (!mesh || !material) {
                    continue;
                }

                DrawItem item{};
                item.mesh = mesh;
                item.material = material;
                item.modelMatrix = model.transform * instance.localTransform;
                item.debugName = instance.debugName.empty() ? model.debugName : instance.debugName;
                m_DrawItems.push_back(std::move(item));
            }
        }

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
