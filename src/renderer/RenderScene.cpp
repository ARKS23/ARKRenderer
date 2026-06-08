#include "renderer/RenderScene.h"

#include <utility>

namespace ark {
    void RenderScene::addObject(MeshResource& mesh,
                                MaterialResource& material,
                                const glm::mat4& transform,
                                std::string debugName) {
        SceneObject object{};
        object.mesh = &mesh;
        object.material = &material;
        object.transform = transform;
        object.debugName = std::move(debugName);
        m_Objects.push_back(std::move(object));
    }

    std::span<const SceneObject> RenderScene::objects() const {
        return m_Objects;
    }

    void RenderScene::clear() {
        m_Objects.clear();
    }
} // namespace ark
