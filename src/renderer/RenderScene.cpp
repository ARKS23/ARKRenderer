#include "renderer/RenderScene.h"

#include <utility>

namespace ark {
    void RenderScene::addModel(ModelResource& model, const glm::mat4& transform, std::string debugName) {
        SceneModel sceneModel{};
        sceneModel.model = &model;
        sceneModel.transform = transform;
        sceneModel.debugName = std::move(debugName);
        m_Models.push_back(std::move(sceneModel));
    }

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

    std::span<const SceneModel> RenderScene::models() const {
        return m_Models;
    }

    std::span<const SceneObject> RenderScene::objects() const {
        return m_Objects;
    }

    const SceneLighting& RenderScene::lighting() const {
        return m_Lighting;
    }

    void RenderScene::setLighting(const SceneLighting& lighting) {
        m_Lighting = lighting;
    }

    void RenderScene::clear() {
        m_Models.clear();
        m_Objects.clear();
    }
} // namespace ark
