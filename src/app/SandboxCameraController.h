#pragma once

#include "app/Input.h"
#include "rhi/RHICommon.h"

#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

namespace ark {
    class RenderView;

    struct SandboxCameraControllerDesc {
        glm::vec3 target{0.0f, 0.0f, 0.0f};
        float distance = 4.0f;
        float yaw = 0.0f;
        float pitch = 0.0f;
        float verticalFovRadians = glm::radians(60.0f);
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
    };

    class SandboxCameraController final {
    public:
        explicit SandboxCameraController(const SandboxCameraControllerDesc& desc = {});

        void reset();
        void setViewportExtent(rhi::Extent2D extent);
        void update(const InputSnapshot& input);
        void writeTo(RenderView& view) const;

        const glm::vec3& target() const {
            return m_Target;
        }

        float distance() const {
            return m_Distance;
        }

        float yaw() const {
            return m_Yaw;
        }

        float pitch() const {
            return m_Pitch;
        }

        rhi::Extent2D viewportExtent() const {
            return m_Extent;
        }

    private:
        glm::vec3 forwardDirection() const;

        SandboxCameraControllerDesc m_Desc;
        glm::vec3 m_Target{0.0f, 0.0f, 0.0f};
        float m_Distance = 4.0f;
        float m_Yaw = 0.0f;
        float m_Pitch = 0.0f;
        rhi::Extent2D m_Extent{1280, 720};
    };
} // namespace ark
