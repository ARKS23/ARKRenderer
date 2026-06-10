#include "renderer/RenderQueue.h"

#include "renderer/material/MaterialResource.h"
#include "renderer/ModelResource.h"
#include "renderer/RenderScene.h"

#include <iterator>
#include <utility>

namespace ark {
    namespace {
        void appendBucket(std::vector<DrawItem>& drawItems, std::vector<DrawItem>& bucket) {
            drawItems.insert(drawItems.end(),
                             std::make_move_iterator(bucket.begin()),
                             std::make_move_iterator(bucket.end()));
        }

        void pushDrawItem(std::vector<DrawItem>& opaqueItems,
                          std::vector<DrawItem>& maskItems,
                          std::vector<DrawItem>& blendItems,
                          DrawItem item) {
            switch (item.material->renderState().alphaMode) {
            case asset::AlphaMode::Opaque:
                opaqueItems.push_back(std::move(item));
                break;
            case asset::AlphaMode::Mask:
                maskItems.push_back(std::move(item));
                break;
            case asset::AlphaMode::Blend:
                blendItems.push_back(std::move(item));
                break;
            }
        }
    } // namespace

    void RenderQueue::build(const RenderScene& scene) {
        clear();
        std::vector<DrawItem> opaqueItems;
        std::vector<DrawItem> maskItems;
        std::vector<DrawItem> blendItems;
        opaqueItems.reserve(scene.size());
        maskItems.reserve(scene.size());
        blendItems.reserve(scene.size());

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
                pushDrawItem(opaqueItems, maskItems, blendItems, std::move(item));
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
            pushDrawItem(opaqueItems, maskItems, blendItems, std::move(item));
        }

        m_DrawItems.reserve(opaqueItems.size() + maskItems.size() + blendItems.size());
        appendBucket(m_DrawItems, opaqueItems);
        appendBucket(m_DrawItems, maskItems);
        appendBucket(m_DrawItems, blendItems);
    }

    std::span<const DrawItem> RenderQueue::drawItems() const {
        return m_DrawItems;
    }

    void RenderQueue::clear() {
        m_DrawItems.clear();
    }
} // namespace ark
