#pragma once

#include "core/Types.h"

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

#include <span>
#include <string>
#include <vector>

namespace ark {
    class MaterialResource;
    class MeshResource;
    class ModelResource;

    struct DirectionalLight {
        glm::vec3 direction{-0.35f, -0.8f, -0.45f};
        glm::vec3 color{1.0f, 0.96f, 0.88f};
    };

    struct SceneLighting {
        DirectionalLight mainLight;
        glm::vec3 ambientColor{0.08f, 0.09f, 0.11f};
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
        void clear();

        bool empty() const {
            return m_Models.empty() && m_Objects.empty();
        }

        usize size() const {
            return m_Models.size() + m_Objects.size();
        }

    private:
        std::vector<SceneModel> m_Models;
        std::vector<SceneObject> m_Objects;
        SceneLighting m_Lighting;
    };
} // namespace ark
