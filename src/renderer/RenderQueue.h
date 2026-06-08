#pragma once

#include "core/Types.h"

#include <glm/mat4x4.hpp>

#include <span>
#include <string>
#include <vector>

namespace ark {
    class MaterialResource;
    class MeshResource;
    class RenderScene;

    struct DrawItem {
        MeshResource* mesh = nullptr;
        MaterialResource* material = nullptr;
        glm::mat4 modelMatrix{1.0f};
        std::string debugName;

        bool isDrawable() const {
            return mesh != nullptr && material != nullptr;
        }
    };

    class RenderQueue {
    public:
        virtual ~RenderQueue() = default;

        void build(const RenderScene& scene);
        std::span<const DrawItem> drawItems() const;
        void clear();

        bool empty() const {
            return m_DrawItems.empty();
        }

        usize size() const {
            return m_DrawItems.size();
        }

    private:
        std::vector<DrawItem> m_DrawItems;
    };
} // namespace ark
