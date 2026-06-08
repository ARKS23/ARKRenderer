#pragma once

#include "core/Types.h"

#include <glm/mat4x4.hpp>

#include <span>
#include <string>
#include <vector>

namespace ark {
    class MaterialResource;
    class MeshResource;

    struct SceneObject {
        MeshResource* mesh = nullptr;
        MaterialResource* material = nullptr;
        glm::mat4 transform{1.0f};
        std::string debugName;

        bool isDrawable() const {
            return mesh != nullptr && material != nullptr;
        }
    };

    class RenderScene {
    public:
        virtual ~RenderScene() = default;

        void addObject(MeshResource& mesh,
                       MaterialResource& material,
                       const glm::mat4& transform = glm::mat4{1.0f},
                       std::string debugName = {});
        std::span<const SceneObject> objects() const;
        void clear();

        bool empty() const {
            return m_Objects.empty();
        }

        usize size() const {
            return m_Objects.size();
        }

    private:
        std::vector<SceneObject> m_Objects;
    };
} // namespace ark
