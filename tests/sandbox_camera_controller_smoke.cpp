#include "app/Input.h"
#include "app/SandboxCameraController.h"
#include "renderer/RenderView.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
    bool near(float a, float b, float epsilon = 0.0001f) {
        return std::fabs(a - b) <= epsilon;
    }

    bool nearVec3(const glm::vec3& a, const glm::vec3& b, float epsilon = 0.0001f) {
        return near(a.x, b.x, epsilon) && near(a.y, b.y, epsilon) && near(a.z, b.z, epsilon);
    }

    bool validateDefaultCamera() {
        ark::SandboxCameraController controller{};
        controller.setViewportExtent(ark::rhi::Extent2D{1280, 720});

        ark::RenderView view{};
        controller.writeTo(view);

        if (!nearVec3(view.cameraPosition(), glm::vec3{0.0f, 0.0f, -4.0f}) ||
            !near(controller.distance(), 4.0f) ||
            !near(controller.yaw(), 0.0f) ||
            !near(controller.pitch(), 0.0f)) {
            std::cerr << "Sandbox camera default state is invalid\n";
            return false;
        }

        return true;
    }

    bool validateOrbitZoomAndPan() {
        ark::SandboxCameraController controller{};
        controller.setViewportExtent(ark::rhi::Extent2D{1280, 720});

        ark::InputSnapshot orbitInput{};
        orbitInput.rightMouseDown = true;
        orbitInput.cursorDelta = glm::vec2{100.0f, -40.0f};
        controller.update(orbitInput);
        if (!near(controller.yaw(), -0.5f) || !near(controller.pitch(), 0.2f)) {
            std::cerr << "Sandbox camera orbit input did not update yaw/pitch\n";
            return false;
        }

        const float distanceBeforeZoom = controller.distance();
        ark::InputSnapshot zoomInput{};
        zoomInput.scrollDelta = glm::vec2{0.0f, 2.0f};
        controller.update(zoomInput);
        if (!(controller.distance() < distanceBeforeZoom)) {
            std::cerr << "Sandbox camera zoom input did not reduce distance\n";
            return false;
        }

        const glm::vec3 targetBeforePan = controller.target();
        ark::InputSnapshot panInput{};
        panInput.middleMouseDown = true;
        panInput.cursorDelta = glm::vec2{20.0f, 10.0f};
        controller.update(panInput);
        if (nearVec3(controller.target(), targetBeforePan)) {
            std::cerr << "Sandbox camera pan input did not move target\n";
            return false;
        }

        return true;
    }

    bool validateClampsAndReset() {
        ark::SandboxCameraController controller{};

        ark::InputSnapshot pitchInput{};
        pitchInput.rightMouseDown = true;
        pitchInput.cursorDelta = glm::vec2{0.0f, -10000.0f};
        controller.update(pitchInput);
        if (controller.pitch() > glm::radians(89.0f) + 0.0001f) {
            std::cerr << "Sandbox camera pitch clamp failed\n";
            return false;
        }

        ark::InputSnapshot zoomInput{};
        zoomInput.scrollDelta = glm::vec2{0.0f, 1000.0f};
        controller.update(zoomInput);
        if (controller.distance() < 0.25f - 0.0001f) {
            std::cerr << "Sandbox camera minimum distance clamp failed\n";
            return false;
        }

        ark::InputSnapshot resetInput{};
        resetInput.resetPressed = true;
        controller.update(resetInput);
        if (!nearVec3(controller.target(), glm::vec3{0.0f}) ||
            !near(controller.distance(), 4.0f) ||
            !near(controller.yaw(), 0.0f) ||
            !near(controller.pitch(), 0.0f)) {
            std::cerr << "Sandbox camera reset failed\n";
            return false;
        }

        return true;
    }

    bool validateResizeDoesNotResetCamera() {
        ark::SandboxCameraController controller{};
        controller.setViewportExtent(ark::rhi::Extent2D{1280, 720});

        ark::InputSnapshot orbitInput{};
        orbitInput.rightMouseDown = true;
        orbitInput.cursorDelta = glm::vec2{80.0f, 20.0f};
        controller.update(orbitInput);

        const float yawBeforeResize = controller.yaw();
        const float pitchBeforeResize = controller.pitch();
        const float distanceBeforeResize = controller.distance();
        const glm::vec3 targetBeforeResize = controller.target();

        ark::RenderView viewBefore{};
        controller.writeTo(viewBefore);
        const float projectionXBefore = viewBefore.projectionMatrix()[0][0];

        controller.setViewportExtent(ark::rhi::Extent2D{800, 800});
        ark::RenderView viewAfter{};
        controller.writeTo(viewAfter);

        if (!near(controller.yaw(), yawBeforeResize) ||
            !near(controller.pitch(), pitchBeforeResize) ||
            !near(controller.distance(), distanceBeforeResize) ||
            !nearVec3(controller.target(), targetBeforeResize)) {
            std::cerr << "Sandbox camera resize reset camera state\n";
            return false;
        }

        if (near(viewAfter.projectionMatrix()[0][0], projectionXBefore)) {
            std::cerr << "Sandbox camera resize did not update projection aspect\n";
            return false;
        }

        return true;
    }

    bool validateLargeSceneCamera() {
        ark::SandboxCameraControllerDesc desc{};
        desc.target = glm::vec3{0.0f, 2.0f, 0.3f};
        desc.distance = 22.0f;
        desc.yaw = glm::radians(18.0f);
        desc.pitch = glm::radians(-10.0f);
        desc.nearPlane = 0.05f;
        desc.farPlane = 200.0f;

        ark::SandboxCameraController controller{desc};
        controller.setViewportExtent(ark::rhi::Extent2D{1280, 720});

        ark::RenderView view{};
        controller.writeTo(view);
        if (!near(controller.distance(), 22.0f) ||
            !near(controller.target().y, 2.0f) ||
            !near(controller.target().z, 0.3f) ||
            view.cameraPosition().y <= desc.target.y) {
            std::cerr << "Sandbox large scene camera state is invalid\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateDefaultCamera() &&
                   validateOrbitZoomAndPan() &&
                   validateClampsAndReset() &&
                   validateResizeDoesNotResetCamera() &&
                   validateLargeSceneCamera()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
