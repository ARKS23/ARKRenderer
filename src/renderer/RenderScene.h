#pragma once

#include "core/Types.h"
#include "renderer/Bounds.h"

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

#include <span>
#include <string>
#include <vector>

namespace ark {
    class EnvironmentResource;
    class MaterialResource;
    class MeshResource;
    class ModelResource;

    struct DirectionalLight {
        // 光线传播方向：从光源射向场景；shader 中会取反得到“着色点指向光源”的方向。
        glm::vec3 direction{-0.35f, -0.8f, -0.45f};
        glm::vec3 color{1.0f, 0.96f, 0.88f};
    };

    struct SceneLighting {
        DirectionalLight mainLight;
        glm::vec3 ambientColor{0.08f, 0.09f, 0.11f};
    };

    struct SceneEnvironment {
        EnvironmentResource* environment = nullptr;
        float intensity = 1.0f;

        bool isEnabled() const {
            return environment != nullptr && intensity > 0.0f;
        }
    };

    struct SceneModel {
        ModelResource* model = nullptr;
        glm::mat4 transform{1.0f};
        std::string debugName;

        bool isDrawable() const {
            return model != nullptr;
        }
    };

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

        void addModel(ModelResource& model,
                      const glm::mat4& transform = glm::mat4{1.0f},
                      std::string debugName = {});
        void addObject(MeshResource& mesh,
                       MaterialResource& material,
                       const glm::mat4& transform = glm::mat4{1.0f},
                       std::string debugName = {});
        std::span<const SceneModel> models() const;
        std::span<const SceneObject> objects() const;
        const SceneLighting& lighting() const;
        void setLighting(const SceneLighting& lighting);
        const SceneEnvironment& environment() const;
        void setEnvironment(const SceneEnvironment& environment);
        void clearEnvironment();
        void clear();

        const Bounds3& bounds() const {
            return m_Bounds;
        }

        bool hasBounds() const {
            return m_Bounds.isValid();
        }

        bool empty() const {
            return m_Models.empty() && m_Objects.empty();
        }

        usize size() const {
            return m_Models.size() + m_Objects.size();
        }

    private:
        std::vector<SceneModel> m_Models;
        std::vector<SceneObject> m_Objects;
        Bounds3 m_Bounds;
        SceneLighting m_Lighting;
        SceneEnvironment m_Environment;
    };
} // namespace ark
