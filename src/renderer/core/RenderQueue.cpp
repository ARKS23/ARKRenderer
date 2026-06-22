#include "renderer/core/RenderQueue.h"

#include "renderer/material/MaterialResource.h"
#include "renderer/core/Frustum.h"
#include "renderer/resources/MeshResource.h"
#include "renderer/resources/ModelResource.h"
#include "renderer/RenderScene.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace ark {
    namespace {
        void appendBucket(std::vector<DrawItem>& drawItems, std::vector<DrawItem>& bucket) {
            drawItems.insert(drawItems.end(),
                             std::make_move_iterator(bucket.begin()),
                             std::make_move_iterator(bucket.end()));
        }

        float sortDistanceSq(const glm::mat4& modelMatrix, const glm::vec3& cameraPosition) {
            const glm::vec3 sortPosition{modelMatrix[3]};
            const glm::vec3 delta = sortPosition - cameraPosition;
            return glm::dot(delta, delta);
        }

        Bounds3 calculateWorldBounds(const MeshResource& mesh, const glm::mat4& modelMatrix) {
            return transformBounds(mesh.localBounds(), modelMatrix);
        }

        void recordItemStats(RenderQueueStats& stats, const DrawItem& item) {
            ++stats.totalItems;
            if (!item.worldBounds.isValid()) {
                ++stats.invalidBoundsItems;
            }
        }

        bool shouldCullDrawItem(const RenderQueueBuildDesc& desc, const DrawItem& item) {
            if (!desc.enableFrustumCulling || !desc.cameraFrustum) {
                return false;
            }

            // invalid bounds 保守视为可见，避免资源异常时误删 draw item。
            return !desc.cameraFrustum->intersects(item.worldBounds);
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

        void submitDrawItem(const RenderQueueBuildDesc& desc,
                            RenderQueueStats& stats,
                            std::vector<DrawItem>& opaqueItems,
                            std::vector<DrawItem>& maskItems,
                            std::vector<DrawItem>& blendItems,
                            DrawItem item) {
            recordItemStats(stats, item);
            if (shouldCullDrawItem(desc, item)) {
                ++stats.culledItems;
                return;
            }

            ++stats.visibleItems;
            pushDrawItem(opaqueItems, maskItems, blendItems, std::move(item));
        }

        void sortBlendBucket(std::vector<DrawItem>& blendItems) {
            std::stable_sort(blendItems.begin(), blendItems.end(), [](const DrawItem& lhs, const DrawItem& rhs) {
                return lhs.sortDistanceSq > rhs.sortDistanceSq;
            });
        }
    } // namespace

    void RenderQueue::build(const RenderScene& scene) {
        RenderQueueBuildDesc desc{};
        desc.scene = &scene;
        build(desc);
    }

    void RenderQueue::build(const RenderScene& scene, const glm::vec3& cameraPosition) {
        RenderQueueBuildDesc desc{};
        desc.scene = &scene;
        desc.cameraPosition = cameraPosition;
        build(desc);
    }

    void RenderQueue::build(const RenderQueueBuildDesc& desc) {
        clear();
        if (!desc.scene) {
            return;
        }

        const RenderScene& scene = *desc.scene;
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
                item.worldBounds = calculateWorldBounds(*mesh, item.modelMatrix);
                item.debugName = instance.debugName.empty() ? model.debugName : instance.debugName;
                item.sortDistanceSq = sortDistanceSq(item.modelMatrix, desc.cameraPosition);
                submitDrawItem(desc, m_Stats, opaqueItems, maskItems, blendItems, std::move(item));
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
            item.worldBounds = calculateWorldBounds(*object.mesh, item.modelMatrix);
            item.debugName = object.debugName;
            item.sortDistanceSq = sortDistanceSq(item.modelMatrix, desc.cameraPosition);
            submitDrawItem(desc, m_Stats, opaqueItems, maskItems, blendItems, std::move(item));
        }

        sortBlendBucket(blendItems);

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
        m_Stats = {};
    }
} // namespace ark
