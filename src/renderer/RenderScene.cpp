#include "renderer/RenderScene.h"

#include "renderer/MeshResource.h"
#include "renderer/ModelResource.h"

#include <utility>

namespace ark {
    void RenderScene::addModel(ModelResource& model, const glm::mat4& transform, std::string debugName) {
        SceneModel sceneModel{};
        sceneModel.model = &model;
        sceneModel.transform = transform;
        sceneModel.debugName = std::move(debugName);
        m_Models.push_back(std::move(sceneModel));

        // RenderScene 只维护 world-space AABB；model 内部仍保留自己的 local bounds。
        mergeBounds(m_Bounds, transformBounds(model.localBounds(), transform));
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

        // 单独提交的 mesh object 使用 mesh local bounds 与 object transform 进入 scene bounds。
        mergeBounds(m_Bounds, transformBounds(mesh.localBounds(), transform));
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

    const SceneEnvironment& RenderScene::environment() const {
        return m_Environment;
    }

    void RenderScene::setEnvironment(const SceneEnvironment& environment) {
        m_Environment = environment;
        if (m_Environment.intensity < 0.0f) {
            m_Environment.intensity = 0.0f;
        }
    }

    void RenderScene::clearEnvironment() {
        m_Environment = {};
    }

    void RenderScene::clear() {
        m_Models.clear();
        m_Objects.clear();
        m_Bounds = makeInvalidBounds();
    }
} // namespace ark
