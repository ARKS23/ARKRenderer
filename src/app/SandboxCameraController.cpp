#include "app/SandboxCameraController.h"

#include "renderer/RenderView.h"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <limits>

namespace ark {
    namespace {
        constexpr float OrbitSensitivity = 0.005f;
        constexpr float PanSensitivity = 0.0015f;
        constexpr float ZoomSensitivity = 0.15f;
        constexpr float MinPitch = glm::radians(-89.0f);
        constexpr float MaxPitch = glm::radians(89.0f);
        constexpr float MinDistance = 0.25f;
        constexpr float MaxDistance = 10000.0f;
        constexpr float MinMoveSpeed = 0.01f;
        constexpr float MaxMoveSpeed = 1000.0f;
        constexpr float MinFastMoveMultiplier = 1.0f;
        constexpr float MaxFastMoveMultiplier = 32.0f;
        constexpr float MinMouseSensitivity = 0.0001f;
        constexpr float MaxMouseSensitivity = 0.05f;
        constexpr float MaxDeltaSeconds = 0.25f;

        float aspectFromExtent(rhi::Extent2D extent) {
            if (extent.width == 0 || extent.height == 0) {
                return 1.0f;
            }

            return static_cast<float>(extent.width) / static_cast<float>(extent.height);
        }
    } // namespace

    SandboxCameraController::SandboxCameraController(const SandboxCameraControllerDesc& desc)
        : m_Desc(desc), m_Mode(desc.mode) {
        reset();
    }

    void SandboxCameraController::reset() {
        m_Target = m_Desc.target;
        m_Position = m_Desc.position;
        m_Distance = std::clamp(m_Desc.distance, MinDistance, MaxDistance);
        m_Yaw = m_Desc.yaw;
        m_Pitch = std::clamp(m_Desc.pitch, MinPitch, MaxPitch);
        setMoveSpeed(m_Desc.moveSpeed);
        setFastMoveMultiplier(m_Desc.fastMoveMultiplier);
        setMouseSensitivity(m_Desc.mouseSensitivity);
    }

    void SandboxCameraController::setViewportExtent(rhi::Extent2D extent) {
        m_Extent = extent;
    }

    void SandboxCameraController::setMode(SandboxCameraMode mode) {
        if (m_Mode == mode) {
            return;
        }

        if (mode == SandboxCameraMode::FirstPerson) {
            m_Position = m_Target - forwardDirection() * m_Distance;
        } else {
            m_Target = m_Position + forwardDirection() * m_Distance;
        }

        m_Mode = mode;
    }

    void SandboxCameraController::setMoveSpeed(float speed) {
        if (!std::isfinite(speed)) {
            return;
        }

        m_MoveSpeed = std::clamp(speed, MinMoveSpeed, MaxMoveSpeed);
    }

    void SandboxCameraController::setFastMoveMultiplier(float multiplier) {
        if (!std::isfinite(multiplier)) {
            return;
        }

        m_FastMoveMultiplier =
            std::clamp(multiplier, MinFastMoveMultiplier, MaxFastMoveMultiplier);
    }

    void SandboxCameraController::setMouseSensitivity(float sensitivity) {
        if (!std::isfinite(sensitivity)) {
            return;
        }

        m_MouseSensitivity = std::clamp(sensitivity, MinMouseSensitivity, MaxMouseSensitivity);
    }

    void SandboxCameraController::update(const InputSnapshot& input) {
        update(input, 1.0f / 60.0f);
    }

    void SandboxCameraController::update(const InputSnapshot& input, float deltaSeconds) {
        if (input.resetPressed) {
            reset();
            return;
        }

        if (m_Mode == SandboxCameraMode::FirstPerson) {
            updateFirstPerson(input, deltaSeconds);
            return;
        }

        updateOrbit(input);
    }

    void SandboxCameraController::updateOrbit(const InputSnapshot& input) {
        const bool panActive = input.middleMouseDown || (input.rightMouseDown && input.shiftDown);
        if (panActive) {
            const glm::vec3 forward = forwardDirection();
            const glm::vec3 right = rightDirection();
            const glm::vec3 up = glm::normalize(glm::cross(forward, right));
            const float panScale = m_Distance * PanSensitivity;
            m_Target += (-right * input.cursorDelta.x + up * input.cursorDelta.y) * panScale;
        } else if (input.rightMouseDown) {
            m_Yaw -= input.cursorDelta.x * OrbitSensitivity;
            m_Pitch = std::clamp(m_Pitch - input.cursorDelta.y * OrbitSensitivity,
                                 MinPitch,
                                 MaxPitch);
        }

        if (input.scrollDelta.y != 0.0f) {
            m_Distance *= std::exp(-input.scrollDelta.y * ZoomSensitivity);
            m_Distance = std::clamp(m_Distance, MinDistance, MaxDistance);
        }
    }

    void SandboxCameraController::updateFirstPerson(const InputSnapshot& input, float deltaSeconds) {
        if (input.rightMouseDown) {
            // 世界坐标采用右手系；这里的 yaw/pitch 表示相机朝向，forward 会继续交给 glm::lookAt 的 RH view 语义。
            m_Yaw -= input.cursorDelta.x * m_MouseSensitivity;
            m_Pitch = std::clamp(m_Pitch - input.cursorDelta.y * m_MouseSensitivity,
                                 MinPitch,
                                 MaxPitch);
        }

        const float clampedDeltaSeconds =
            std::clamp(std::isfinite(deltaSeconds) ? deltaSeconds : 0.0f, 0.0f, MaxDeltaSeconds);
        if (clampedDeltaSeconds <= std::numeric_limits<float>::epsilon()) {
            return;
        }

        glm::vec3 move{0.0f};
        const glm::vec3 forward = forwardDirection();
        const glm::vec3 right = rightDirection();
        constexpr glm::vec3 WorldUp{0.0f, 1.0f, 0.0f};
        if (input.moveForward) {
            move += forward;
        }
        if (input.moveBackward) {
            move -= forward;
        }
        if (input.moveRight) {
            move += right;
        }
        if (input.moveLeft) {
            move -= right;
        }
        if (input.moveUp) {
            move += WorldUp;
        }
        if (input.moveDown) {
            move -= WorldUp;
        }

        const float moveLength = glm::length(move);
        if (moveLength <= std::numeric_limits<float>::epsilon()) {
            return;
        }

        const float speed =
            m_MoveSpeed * (input.fastMove ? m_FastMoveMultiplier : 1.0f);
        m_Position += glm::normalize(move) * speed * clampedDeltaSeconds;
    }

    void SandboxCameraController::writeTo(RenderView& view) const {
        const glm::vec3 forward = forwardDirection();
        const glm::vec3 cameraPosition =
            m_Mode == SandboxCameraMode::FirstPerson ? m_Position : m_Target - forward * m_Distance;
        const glm::vec3 lookTarget =
            m_Mode == SandboxCameraMode::FirstPerson ? cameraPosition + forward : m_Target;
        const glm::mat4 viewMatrix =
            glm::lookAt(cameraPosition, lookTarget, glm::vec3{0.0f, 1.0f, 0.0f});
        glm::mat4 projection =
            glm::perspectiveRH_ZO(m_Desc.verticalFovRadians,
                                  aspectFromExtent(m_Extent),
                                  m_Desc.nearPlane,
                                  m_Desc.farPlane);
        projection[1][1] *= -1.0f;
        view.setMatrices(viewMatrix, projection, cameraPosition);
        view.setClipRange(m_Desc.nearPlane, m_Desc.farPlane);
    }

    glm::vec3 SandboxCameraController::forwardDirection() const {
        const float cosPitch = std::cos(m_Pitch);
        return glm::normalize(glm::vec3{
            cosPitch * std::sin(m_Yaw),
            std::sin(m_Pitch),
            cosPitch * std::cos(m_Yaw),
        });
    }

    glm::vec3 SandboxCameraController::rightDirection() const {
        return glm::normalize(glm::cross(glm::vec3{0.0f, 1.0f, 0.0f}, forwardDirection()));
    }
} // namespace ark
