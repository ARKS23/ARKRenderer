#include "app/Application.h"
#include "app/Input.h"
#include "app/SandboxLaunchOptions.h"
#include "app/SandboxRuntimeSettings.h"
#include "renderer/RenderView.h"

#include <array>
#include <cstdlib>
#include <string_view>

int main() {
    ark::ApplicationDesc desc{};
    desc.debugUiEnabled = true;
    desc.view.toneMapping.operatorType = ark::ToneMappingOperator::ACES;
    desc.view.toneMapping.exposure = 1.5f;
    desc.view.postProcessing.bloom.enabled = true;
    desc.view.postProcessing.bloom.intensity = 0.12f;
    desc.view.postProcessing.bloom.maxMipCount = 7;
    desc.view.postProcessing.ssao.enabled = true;
    desc.view.postProcessing.ssao.radius = 1.25f;
    desc.view.postProcessing.ssao.intensity = 0.9f;
    desc.view.postProcessing.ssao.sampleCount = 32;
    desc.view.postProcessing.ssao.debugMode = ark::SsaoDebugMode::Occlusion;
    desc.view.shadows.enabled = true;
    desc.view.shadows.strength = 0.8f;
    desc.view.shadows.bias = 0.003f;
    desc.view.shadows.mapExtent = 2048;
    desc.view.shadows.filterMode = ark::ShadowFilterMode::Pcf5x5;
    desc.view.shadows.filterRadiusTexels = 2.5f;
    desc.view.shadows.cascades.enabled = true;
    desc.view.shadows.cascades.cascadeCount = 4;
    desc.view.shadows.cascades.splitLambda = 0.72f;
    desc.view.shadows.cascades.maxDistance = 96.0f;
    desc.view.shadows.cascades.cascadeExtent = 2048;
    desc.view.shadows.cascades.stabilize = true;
    desc.view.shadowDebug.enabled = true;
    desc.view.shadowDebug.mode = ark::ShadowDebugMode::CascadeColor;
    desc.view.shadowDebug.showPreview = true;
    desc.view.shadowDebug.previewCascadeIndex = 2;
    desc.view.visibility.enableFrustumCulling = true;

    ark::SandboxRuntimeSettings runtimeSettings = ark::makeSandboxRuntimeSettings(desc);
    runtimeSettings.cameraMode = ark::SandboxCameraMode::FirstPerson;
    runtimeSettings.cameraMoveSpeed = 12.0f;
    runtimeSettings.cameraFastMoveMultiplier = 6.0f;
    runtimeSettings.cameraMouseSensitivity = 0.01f;
    if (!runtimeSettings.uiVisible ||
        runtimeSettings.view.toneMapping.operatorType != ark::ToneMappingOperator::ACES ||
        runtimeSettings.view.postProcessing.bloom.intensity != 0.12f ||
        !runtimeSettings.view.postProcessing.ssao.enabled ||
        runtimeSettings.view.postProcessing.ssao.radius != 1.25f ||
        runtimeSettings.view.postProcessing.ssao.intensity != 0.9f ||
        runtimeSettings.view.postProcessing.ssao.sampleCount != 32 ||
        runtimeSettings.view.postProcessing.ssao.debugMode != ark::SsaoDebugMode::Occlusion ||
        runtimeSettings.view.shadows.filterMode != ark::ShadowFilterMode::Pcf5x5 ||
        !runtimeSettings.view.shadows.cascades.enabled ||
        runtimeSettings.view.shadows.cascades.cascadeCount != 4 ||
        runtimeSettings.view.shadows.cascades.splitLambda != 0.72f ||
        runtimeSettings.view.shadows.cascades.maxDistance != 96.0f ||
        !runtimeSettings.view.shadowDebug.enabled ||
        runtimeSettings.view.shadowDebug.mode != ark::ShadowDebugMode::CascadeColor ||
        !runtimeSettings.view.shadowDebug.showPreview ||
        runtimeSettings.view.shadowDebug.previewCascadeIndex != 2 ||
        !runtimeSettings.view.visibility.enableFrustumCulling ||
        runtimeSettings.cameraMode != ark::SandboxCameraMode::FirstPerson ||
        runtimeSettings.cameraMoveSpeed != 12.0f ||
        runtimeSettings.cameraFastMoveMultiplier != 6.0f ||
        runtimeSettings.cameraMouseSensitivity != 0.01f) {
        return EXIT_FAILURE;
    }

    runtimeSettings.view.postProcessing.bloom.intensity = 2.0f;
    runtimeSettings.view.postProcessing.ssao.radius = 128.0f;
    runtimeSettings.view.postProcessing.ssao.intensity = -1.0f;
    runtimeSettings.view.postProcessing.ssao.sampleCount = 999;
    runtimeSettings.view.postProcessing.ssao.blurRadius = 999;
    runtimeSettings.view.postProcessing.ssao.resolutionScale = -1.0f;
    runtimeSettings.view.shadows.mapExtent = 8192;
    runtimeSettings.view.shadows.filterRadiusTexels = 99.0f;
    runtimeSettings.view.shadows.cascades.cascadeCount = 3;
    runtimeSettings.view.shadows.cascades.splitLambda = 9.0f;
    runtimeSettings.view.shadows.cascades.maxDistance = -1.0f;
    runtimeSettings.view.shadows.cascades.cascadeExtent = 99999;
    runtimeSettings.view.shadows.cascades.stabilize = false;
    runtimeSettings.view.shadowDebug.previewCascadeIndex = 99;

    ark::RenderView view{};
    ark::applySandboxRuntimeSettings(view, runtimeSettings);
    if (view.postProcessingSettings().bloom.intensity <= 0.0f ||
        view.postProcessingSettings().ssao.enabled ||
        view.postProcessingSettings().ssao.radius != 8.0f ||
        view.postProcessingSettings().ssao.intensity != 0.0f ||
        view.postProcessingSettings().ssao.sampleCount != 64 ||
        view.postProcessingSettings().ssao.blurRadius != 8 ||
        view.postProcessingSettings().ssao.resolutionScale != 0.25f ||
        view.shadowSettings().mapExtent != 4096 ||
        view.shadowSettings().filterRadiusTexels != 8.0f ||
        view.shadowSettings().cascades.cascadeCount != 4 ||
        view.shadowSettings().cascades.splitLambda != 1.0f ||
        view.shadowSettings().cascades.maxDistance <= view.shadowSettings().nearPlane ||
        view.shadowSettings().cascades.cascadeExtent != 4096 ||
        view.shadowSettings().cascades.stabilize ||
        !view.shadowDebugSettings().enabled ||
        view.shadowDebugSettings().mode != ark::ShadowDebugMode::CascadeColor ||
        !view.shadowDebugSettings().showPreview ||
        view.shadowDebugSettings().previewCascadeIndex != ark::MaxShadowCascadeCount - 1u ||
        !view.visibilitySettings().enableFrustumCulling) {
        return EXIT_FAILURE;
    }

    runtimeSettings.view.shadowDebug.enabled = false;
    runtimeSettings.view.shadowDebug.mode = ark::ShadowDebugMode::LightDepth;
    runtimeSettings.view.shadowDebug.showPreview = true;
    ark::applySandboxRuntimeSettings(view, runtimeSettings);
    if (view.shadowDebugSettings().enabled ||
        view.shadowDebugSettings().mode != ark::ShadowDebugMode::None ||
        view.shadowDebugSettings().showPreview) {
        return EXIT_FAILURE;
    }

    runtimeSettings.view.shadows.cascades.cascadeCount = 1;
    runtimeSettings.view.shadows.cascades.splitLambda = 0.35f;
    runtimeSettings.view.shadows.cascades.maxDistance = 48.0f;
    runtimeSettings.view.shadows.cascades.cascadeExtent = 128;
    runtimeSettings.view.shadows.cascades.stabilize = true;
    ark::applySandboxRuntimeSettings(view, runtimeSettings);
    if (view.shadowSettings().cascades.cascadeCount != 1 ||
        view.shadowSettings().cascades.splitLambda != 0.35f ||
        view.shadowSettings().cascades.maxDistance != 48.0f ||
        view.shadowSettings().cascades.cascadeExtent != 128 ||
        !view.shadowSettings().cascades.stabilize) {
        return EXIT_FAILURE;
    }

    ark::InputSnapshot input{};
    input.cursorPosition = {10.0f, 20.0f};
    input.cursorDelta = {3.0f, -2.0f};
    input.scrollDelta = {0.0f, 1.0f};
    input.leftMouseDown = true;
    input.rightMouseDown = true;
    input.middleMouseDown = true;
    input.shiftDown = true;
    input.moveForward = true;
    input.moveBackward = true;
    input.moveLeft = true;
    input.moveRight = true;
    input.moveUp = true;
    input.moveDown = true;
    input.fastMove = true;
    input.resetPressed = true;
    input.debugUiTogglePressed = true;

    const ark::InputSnapshot mouseCaptured =
        ark::filterSandboxInputForUiCapture(input, true, false);
    if (mouseCaptured.cursorPosition != input.cursorPosition ||
        mouseCaptured.cursorDelta != glm::vec2{0.0f} ||
        mouseCaptured.scrollDelta != glm::vec2{0.0f} ||
        mouseCaptured.leftMouseDown ||
        mouseCaptured.rightMouseDown ||
        mouseCaptured.middleMouseDown ||
        !mouseCaptured.moveForward ||
        !mouseCaptured.moveBackward ||
        !mouseCaptured.moveLeft ||
        !mouseCaptured.moveRight ||
        !mouseCaptured.moveUp ||
        !mouseCaptured.moveDown ||
        !mouseCaptured.fastMove ||
        !mouseCaptured.shiftDown ||
        !mouseCaptured.resetPressed ||
        !mouseCaptured.debugUiTogglePressed) {
        return EXIT_FAILURE;
    }

    const ark::InputSnapshot keyboardCaptured =
        ark::filterSandboxInputForUiCapture(input, false, true);
    if (keyboardCaptured.cursorDelta != input.cursorDelta ||
        keyboardCaptured.scrollDelta != input.scrollDelta ||
        !keyboardCaptured.leftMouseDown ||
        keyboardCaptured.moveForward ||
        keyboardCaptured.moveBackward ||
        keyboardCaptured.moveLeft ||
        keyboardCaptured.moveRight ||
        keyboardCaptured.moveUp ||
        keyboardCaptured.moveDown ||
        keyboardCaptured.fastMove ||
        keyboardCaptured.resetPressed ||
        keyboardCaptured.debugUiTogglePressed) {
        return EXIT_FAILURE;
    }

    constexpr std::array<std::string_view, 1> noUiArgs{"--no-ui"};
    const ark::ApplicationDesc noUiDesc = ark::makeSandboxApplicationDesc(noUiArgs);
    if (noUiDesc.debugUiEnabled) {
        return EXIT_FAILURE;
    }

    constexpr std::array<std::string_view, 1> uiArgs{"--ui"};
    const ark::ApplicationDesc uiDesc = ark::makeSandboxApplicationDesc(uiArgs);
    if (!uiDesc.debugUiEnabled) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
