#pragma once

#include "app/Application.h"
#include "app/Input.h"
#include "app/SandboxCameraController.h"
#include "renderer/RenderView.h"
#include "renderer/RendererPreset.h"

namespace ark {
    // SandboxRuntimeSettings 是启动配置的运行时副本；UI 只修改这里，再统一同步到相机和 RenderView。
    struct SandboxRuntimeSettings {
        RenderViewProfileDesc view;
        OrbitCameraProfileDesc camera;
        SandboxCameraMode cameraMode{SandboxCameraMode::Orbit};
        float cameraMoveSpeed = 4.0f;
        float cameraFastMoveMultiplier = 4.0f;
        float cameraMouseSensitivity = 0.005f;
        bool uiVisible = true;
    };

    SandboxRuntimeSettings makeSandboxRuntimeSettings(const ApplicationDesc& desc);
    void applySandboxRuntimeSettings(RenderView& view, const SandboxRuntimeSettings& settings);
    InputSnapshot filterSandboxInputForUiCapture(const InputSnapshot& input,
                                                 bool captureMouse,
                                                 bool captureKeyboard);
} // namespace ark
