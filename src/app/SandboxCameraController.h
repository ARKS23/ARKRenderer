#pragma once

#include "app/Input.h"
#include "rhi/RHICommon.h"

#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

namespace ark {
    class RenderView;

    enum class SandboxCameraMode {
        Orbit,
        FirstPerson,
    };

    struct SandboxCameraControllerDesc {
        SandboxCameraMode mode{SandboxCameraMode::Orbit};
        glm::vec3 target{0.0f, 0.0f, 0.0f};
        float distance = 4.0f;
        glm::vec3 position{0.0f, 0.0f, -4.0f};
        float yaw = 0.0f;
        float pitch = 0.0f;
        float moveSpeed = 4.0f;
        float fastMoveMultiplier = 4.0f;
        float mouseSensitivity = 0.005f;
        float verticalFovRadians = glm::radians(60.0f);
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
    };

    class SandboxCameraController final {
    public:
        explicit SandboxCameraController(const SandboxCameraControllerDesc& desc = {});

        void reset();
        void setViewportExtent(rhi::Extent2D extent);
        void setMode(SandboxCameraMode mode);
        void setMoveSpeed(float speed);
        void setFastMoveMultiplier(float multiplier);
        void setMouseSensitivity(float sensitivity);
        void update(const InputSnapshot& input);
        void update(const InputSnapshot& input, float deltaSeconds);
        void writeTo(RenderView& view) const;

        SandboxCameraMode mode() const {
            return m_Mode;
        }

        const glm::vec3& target() const {
            return m_Target;
        }

        const glm::vec3& position() const {
            return m_Position;
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

        float moveSpeed() const {
            return m_MoveSpeed;
        }

        float fastMoveMultiplier() const {
            return m_FastMoveMultiplier;
        }

        float mouseSensitivity() const {
            return m_MouseSensitivity;
        }

        rhi::Extent2D viewportExtent() const {
            return m_Extent;
        }

    private:
        glm::vec3 forwardDirection() const;
        glm::vec3 rightDirection() const;
        glm::vec3 firstPersonRightDirection() const;
        void updateOrbit(const InputSnapshot& input);
        void updateFirstPerson(const InputSnapshot& input, float deltaSeconds);

        SandboxCameraControllerDesc m_Desc;
        SandboxCameraMode m_Mode{SandboxCameraMode::Orbit};
        glm::vec3 m_Target{0.0f, 0.0f, 0.0f};
        glm::vec3 m_Position{0.0f, 0.0f, -4.0f};
        float m_Distance = 4.0f;
        float m_Yaw = 0.0f;
        float m_Pitch = 0.0f;
        float m_MoveSpeed = 4.0f;
        float m_FastMoveMultiplier = 4.0f;
        float m_MouseSensitivity = 0.005f;
        rhi::Extent2D m_Extent{1280, 720};
    };
} // namespace ark
