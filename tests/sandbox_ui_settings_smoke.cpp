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
    desc.view.visibility.enableFrustumCulling = true;

    ark::SandboxRuntimeSettings runtimeSettings = ark::makeSandboxRuntimeSettings(desc);
    if (!runtimeSettings.uiVisible ||
        runtimeSettings.view.toneMapping.operatorType != ark::ToneMappingOperator::ACES ||
        runtimeSettings.view.postProcessing.bloom.intensity != 0.12f ||
        runtimeSettings.view.shadows.filterMode != ark::ShadowFilterMode::Pcf5x5 ||
        !runtimeSettings.view.shadows.cascades.enabled ||
        runtimeSettings.view.shadows.cascades.cascadeCount != 4 ||
        runtimeSettings.view.shadows.cascades.splitLambda != 0.72f ||
        runtimeSettings.view.shadows.cascades.maxDistance != 96.0f ||
        !runtimeSettings.view.visibility.enableFrustumCulling) {
        return EXIT_FAILURE;
    }

    runtimeSettings.view.postProcessing.bloom.intensity = 2.0f;
    runtimeSettings.view.shadows.mapExtent = 8192;
    runtimeSettings.view.shadows.filterRadiusTexels = 99.0f;
    runtimeSettings.view.shadows.cascades.cascadeCount = 3;
    runtimeSettings.view.shadows.cascades.splitLambda = 9.0f;
    runtimeSettings.view.shadows.cascades.maxDistance = -1.0f;
    runtimeSettings.view.shadows.cascades.cascadeExtent = 99999;

    ark::RenderView view{};
    ark::applySandboxRuntimeSettings(view, runtimeSettings);
    if (view.postProcessingSettings().bloom.intensity <= 0.0f ||
        view.shadowSettings().mapExtent != 4096 ||
        view.shadowSettings().filterRadiusTexels != 8.0f ||
        view.shadowSettings().cascades.cascadeCount != 4 ||
        view.shadowSettings().cascades.splitLambda != 1.0f ||
        view.shadowSettings().cascades.maxDistance <= view.shadowSettings().nearPlane ||
        view.shadowSettings().cascades.cascadeExtent != 4096 ||
        !view.visibilitySettings().enableFrustumCulling) {
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
