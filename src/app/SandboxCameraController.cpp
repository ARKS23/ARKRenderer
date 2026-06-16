#include "app/SandboxCameraController.h"

#include "renderer/RenderView.h"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace ark {
    namespace {
        constexpr float OrbitSensitivity = 0.005f;
        constexpr float PanSensitivity = 0.0015f;
        constexpr float ZoomSensitivity = 0.15f;
        constexpr float MinPitch = glm::radians(-89.0f);
        constexpr float MaxPitch = glm::radians(89.0f);
        constexpr float MinDistance = 0.25f;
        constexpr float MaxDistance = 10000.0f;

        float aspectFromExtent(rhi::Extent2D extent) {
            if (extent.width == 0 || extent.height == 0) {
                return 1.0f;
            }

            return static_cast<float>(extent.width) / static_cast<float>(extent.height);
        }
    } // namespace

    SandboxCameraController::SandboxCameraController(const SandboxCameraControllerDesc& desc) : m_Desc(desc) {
        reset();
    }

    void SandboxCameraController::reset() {
        m_Target = m_Desc.target;
        m_Distance = std::clamp(m_Desc.distance, MinDistance, MaxDistance);
        m_Yaw = m_Desc.yaw;
        m_Pitch = std::clamp(m_Desc.pitch, MinPitch, MaxPitch);
    }

    void SandboxCameraController::setViewportExtent(rhi::Extent2D extent) {
        m_Extent = extent;
    }

    void SandboxCameraController::update(const InputSnapshot& input) {
        if (input.resetPressed) {
            reset();
            return;
        }

        const bool panActive = input.middleMouseDown || (input.rightMouseDown && input.shiftDown);
        if (panActive) {
            const glm::vec3 forward = forwardDirection();
            const glm::vec3 right = glm::normalize(glm::cross(glm::vec3{0.0f, 1.0f, 0.0f}, forward));
            const glm::vec3 up = glm::normalize(glm::cross(forward, right));
            const float panScale = m_Distance * PanSensitivity;
            m_Target += (-right * input.cursorDelta.x + up * input.cursorDelta.y) * panScale;
        } else if (input.rightMouseDown) {
            m_Yaw -= input.cursorDelta.x * OrbitSensitivity;
            m_Pitch = std::clamp(m_Pitch - input.cursorDelta.y * OrbitSensitivity, MinPitch, MaxPitch);
        }

        if (input.scrollDelta.y != 0.0f) {
            m_Distance *= std::exp(-input.scrollDelta.y * ZoomSensitivity);
            m_Distance = std::clamp(m_Distance, MinDistance, MaxDistance);
        }
    }

    void SandboxCameraController::writeTo(RenderView& view) const {
        const glm::vec3 forward = forwardDirection();
        const glm::vec3 cameraPosition = m_Target - forward * m_Distance;
        const glm::mat4 viewMatrix =
            glm::lookAt(cameraPosition, m_Target, glm::vec3{0.0f, 1.0f, 0.0f});
        glm::mat4 projection =
            glm::perspectiveRH_ZO(m_Desc.verticalFovRadians,
                                  aspectFromExtent(m_Extent),
                                  m_Desc.nearPlane,
                                  m_Desc.farPlane);
        projection[1][1] *= -1.0f;
        view.setMatrices(viewMatrix, projection, cameraPosition);
    }

    glm::vec3 SandboxCameraController::forwardDirection() const {
        const float cosPitch = std::cos(m_Pitch);
        return glm::normalize(glm::vec3{
            cosPitch * std::sin(m_Yaw),
            std::sin(m_Pitch),
            cosPitch * std::cos(m_Yaw),
        });
    }
} // namespace ark
