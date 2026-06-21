#include "app/SandboxRuntimeSettings.h"

namespace ark {
    SandboxRuntimeSettings makeSandboxRuntimeSettings(const ApplicationDesc& desc) {
        SandboxRuntimeSettings settings{};
        settings.view = desc.view;
        settings.camera = desc.camera;
        settings.cameraMode = SandboxCameraMode::Orbit;
        settings.cameraMoveSpeed = 4.0f;
        settings.cameraFastMoveMultiplier = 4.0f;
        settings.cameraMouseSensitivity = 0.005f;
        settings.uiVisible = desc.debugUiEnabled;
        return settings;
    }

    void applySandboxRuntimeSettings(RenderView& view, const SandboxRuntimeSettings& settings) {
        view.setToneMappingSettings(settings.view.toneMapping);
        view.setPostProcessingSettings(settings.view.postProcessing);
        view.setShadowSettings(settings.view.shadows);
        view.setVisibilitySettings(settings.view.visibility);
    }

    InputSnapshot filterSandboxInputForUiCapture(const InputSnapshot& input,
                                                 bool captureMouse,
                                                 bool captureKeyboard) {
        InputSnapshot filtered = input;
        if (captureMouse) {
            filtered.cursorDelta = glm::vec2{0.0f};
            filtered.scrollDelta = glm::vec2{0.0f};
            filtered.leftMouseDown = false;
            filtered.rightMouseDown = false;
            filtered.middleMouseDown = false;
        }

        if (captureKeyboard) {
            filtered.moveForward = false;
            filtered.moveBackward = false;
            filtered.moveLeft = false;
            filtered.moveRight = false;
            filtered.moveUp = false;
            filtered.moveDown = false;
            filtered.fastMove = false;
            filtered.resetPressed = false;
            filtered.debugUiTogglePressed = false;
        }

        return filtered;
    }
} // namespace ark
