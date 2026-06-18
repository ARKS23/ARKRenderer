#pragma once

#include "core/Types.h"
#include "renderer/Bounds.h"

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

#include <span>
#include <string>
#include <vector>

namespace ark {
    class Frustum;
    class MaterialResource;
    class MeshResource;
    class RenderScene;

    struct DrawItem {
        MeshResource* mesh = nullptr;
        MaterialResource* material = nullptr;
        glm::mat4 modelMatrix{1.0f};
        // DrawItem 的 world bounds 用于 CPU visibility / debug，不代表精确碰撞体。
        Bounds3 worldBounds;
        std::string debugName;
        float sortDistanceSq = 0.0f;

        bool isDrawable() const {
            return mesh != nullptr && material != nullptr;
        }
    };

    struct RenderQueueStats {
        usize totalItems = 0;
        usize visibleItems = 0;
        usize culledItems = 0;
        usize invalidBoundsItems = 0;
    };

    struct RenderQueueBuildDesc {
        const RenderScene* scene = nullptr;
        glm::vec3 cameraPosition{0.0f};
        const Frustum* cameraFrustum = nullptr;
        bool enableFrustumCulling = false;
    };

    class RenderQueue {
    public:
        virtual ~RenderQueue() = default;

        void build(const RenderScene& scene);
        void build(const RenderScene& scene, const glm::vec3& cameraPosition);
        void build(const RenderQueueBuildDesc& desc);
        std::span<const DrawItem> drawItems() const;
        const RenderQueueStats& stats() const {
            return m_Stats;
        }

        void clear();

        bool empty() const {
            return m_DrawItems.empty();
        }

        usize size() const {
            return m_DrawItems.size();
        }

    private:
        std::vector<DrawItem> m_DrawItems;
        RenderQueueStats m_Stats{};
    };
} // namespace ark
