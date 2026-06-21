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
        desc.target = glm::vec3{0.0f, 3.2f, 0.6f};
        desc.distance = 26.0f;
        desc.yaw = glm::radians(18.0f);
        desc.pitch = glm::radians(-12.0f);
        desc.nearPlane = 0.05f;
        desc.farPlane = 512.0f;

        ark::SandboxCameraController controller{desc};
        controller.setViewportExtent(ark::rhi::Extent2D{1280, 720});

        ark::RenderView view{};
        controller.writeTo(view);
        if (!near(controller.distance(), 26.0f) ||
            !near(controller.target().y, 3.2f) ||
            !near(controller.target().z, 0.6f) ||
            view.cameraPosition().y <= desc.target.y) {
            std::cerr << "Sandbox large scene camera state is invalid\n";
            return false;
        }

        return true;
    }

    bool validateFirstPersonMovement() {
        ark::SandboxCameraControllerDesc desc{};
        desc.mode = ark::SandboxCameraMode::FirstPerson;
        desc.position = glm::vec3{1.0f, 2.0f, -3.0f};
        desc.yaw = 0.0f;
        desc.pitch = 0.0f;
        desc.moveSpeed = 2.0f;
        desc.fastMoveMultiplier = 3.0f;

        ark::SandboxCameraController controller{desc};
        if (controller.mode() != ark::SandboxCameraMode::FirstPerson ||
            !nearVec3(controller.position(), desc.position)) {
            std::cerr << "Sandbox first-person camera default state is invalid\n";
            return false;
        }

        ark::InputSnapshot forwardInput{};
        forwardInput.moveForward = true;
        controller.update(forwardInput, 0.25f);
        if (!nearVec3(controller.position(), glm::vec3{1.0f, 2.0f, -2.5f})) {
            std::cerr << "Sandbox first-person forward movement failed\n";
            return false;
        }

        ark::InputSnapshot strafeInput{};
        strafeInput.moveRight = true;
        strafeInput.fastMove = true;
        controller.update(strafeInput, 0.25f);
        if (!nearVec3(controller.position(), glm::vec3{-0.5f, 2.0f, -2.5f})) {
            std::cerr << "Sandbox first-person fast strafe movement failed\n";
            return false;
        }

        ark::InputSnapshot leftInput{};
        leftInput.moveLeft = true;
        leftInput.fastMove = true;
        controller.update(leftInput, 0.25f);
        if (!nearVec3(controller.position(), glm::vec3{1.0f, 2.0f, -2.5f})) {
            std::cerr << "Sandbox first-person left strafe movement failed\n";
            return false;
        }

        ark::RenderView view{};
        controller.writeTo(view);
        if (!nearVec3(view.cameraPosition(), controller.position())) {
            std::cerr << "Sandbox first-person RenderView camera position mismatch\n";
            return false;
        }

        return true;
    }

    bool validateFirstPersonLookClampAndReset() {
        ark::SandboxCameraControllerDesc desc{};
        desc.mode = ark::SandboxCameraMode::FirstPerson;
        desc.position = glm::vec3{0.0f, 1.0f, -5.0f};
        desc.mouseSensitivity = 0.01f;

        ark::SandboxCameraController controller{desc};
        ark::InputSnapshot lookInput{};
        lookInput.rightMouseDown = true;
        lookInput.cursorDelta = glm::vec2{10.0f, -10000.0f};
        controller.update(lookInput, 1.0f / 60.0f);
        if (!near(controller.yaw(), -0.1f) ||
            controller.pitch() > glm::radians(89.0f) + 0.0001f) {
            std::cerr << "Sandbox first-person look or pitch clamp failed\n";
            return false;
        }

        ark::InputSnapshot resetInput{};
        resetInput.moveForward = true;
        resetInput.resetPressed = true;
        controller.update(resetInput, 1.0f);
        if (controller.mode() != ark::SandboxCameraMode::FirstPerson ||
            !nearVec3(controller.position(), desc.position) ||
            !near(controller.yaw(), 0.0f) ||
            !near(controller.pitch(), 0.0f)) {
            std::cerr << "Sandbox first-person reset failed\n";
            return false;
        }

        return true;
    }

    bool validateModeSwitching() {
        ark::SandboxCameraController controller{};
        ark::RenderView orbitView{};
        controller.writeTo(orbitView);

        controller.setMode(ark::SandboxCameraMode::FirstPerson);
        if (controller.mode() != ark::SandboxCameraMode::FirstPerson ||
            !nearVec3(controller.position(), orbitView.cameraPosition())) {
            std::cerr << "Sandbox camera mode switch did not preserve orbit camera position\n";
            return false;
        }

        ark::InputSnapshot moveInput{};
        moveInput.moveForward = true;
        controller.update(moveInput, 1.0f);
        const glm::vec3 firstPersonPosition = controller.position();

        controller.setMode(ark::SandboxCameraMode::Orbit);
        if (controller.mode() != ark::SandboxCameraMode::Orbit ||
            nearVec3(controller.target(), glm::vec3{0.0f}) ||
            !nearVec3(firstPersonPosition, controller.position())) {
            std::cerr << "Sandbox camera mode switch back to orbit failed\n";
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
                   validateLargeSceneCamera() &&
                   validateFirstPersonMovement() &&
                   validateFirstPersonLookClampAndReset() &&
                   validateModeSwitching()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
