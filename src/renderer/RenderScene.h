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
        // RenderScene 只借用 renderer resource 指针；资源生命周期必须覆盖本帧渲染。
        ModelResource* model = nullptr;
        glm::mat4 transform{1.0f};
        std::string debugName;

        bool isDrawable() const {
            return model != nullptr;
        }
    };

    struct SceneObject {
        // 单个 mesh/material draw object，适合未来从引擎 scene extraction 阶段生成。
        MeshResource* mesh = nullptr;
        MaterialResource* material = nullptr;
        glm::mat4 transform{1.0f};
        std::string debugName;

        bool isDrawable() const {
            return mesh != nullptr && material != nullptr;
        }
    };

    // Public facade: RenderScene 是 renderer-facing scene submission 容器。
    // 它不表达完整引擎世界，不负责 ECS、层级 Transform、动画、物理或 gameplay 状态；
    // 未来引擎接入时，推荐由 adapter/extraction layer 把 engine scene 转换成 RenderScene。
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
