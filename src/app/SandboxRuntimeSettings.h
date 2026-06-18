#pragma once

#include "app/Application.h"
#include "app/Input.h"
#include "renderer/RenderView.h"
#include "renderer/RendererPreset.h"

namespace ark {
    // SandboxRuntimeSettings 是启动配置的可变运行时副本，UI 只改它，再统一写回 RenderView。
    struct SandboxRuntimeSettings {
        RenderViewProfileDesc view;
        OrbitCameraProfileDesc camera;
        bool uiVisible = true;
    };

    SandboxRuntimeSettings makeSandboxRuntimeSettings(const ApplicationDesc& desc);
    void applySandboxRuntimeSettings(RenderView& view, const SandboxRuntimeSettings& settings);
    InputSnapshot filterSandboxInputForUiCapture(const InputSnapshot& input,
                                                 bool captureMouse,
                                                 bool captureKeyboard);
} // namespace ark
