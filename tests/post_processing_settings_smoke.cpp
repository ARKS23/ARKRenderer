#include "app/Application.h"
#include "app/SandboxLaunchOptions.h"
#include "renderer/PostProcessingSettings.h"
#include "renderer/RenderView.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
    bool nearlyEqual(float lhs, float rhs, float epsilon = 0.0001f) {
        return std::abs(lhs - rhs) <= epsilon;
    }

    bool validateDefaultSettings() {
        const ark::PostProcessingSettings settings{};
        const ark::PostProcessingSettings sanitized = ark::sanitizePostProcessingSettings(settings);
        const ark::BloomSettings& bloom = sanitized.bloom;
        const ark::SsaoSettings& ssao = sanitized.ssao;
        if (bloom.enabled ||
            !nearlyEqual(bloom.intensity, 0.0f) ||
            !nearlyEqual(bloom.scatter, 0.6f) ||
            !nearlyEqual(bloom.threshold, 1.0f) ||
            !nearlyEqual(bloom.softKnee, 0.5f) ||
            bloom.maxMipCount != 6) {
            std::cerr << "Default post-processing settings should keep Bloom disabled\n";
            return false;
        }

        if (ssao.enabled ||
            !nearlyEqual(ssao.radius, 0.6f) ||
            !nearlyEqual(ssao.intensity, 1.0f) ||
            !nearlyEqual(ssao.bias, 0.025f) ||
            !nearlyEqual(ssao.power, 1.5f) ||
            ssao.sampleCount != 16 ||
            ssao.blurRadius != 2 ||
            !nearlyEqual(ssao.resolutionScale, 1.0f) ||
            ssao.debugMode != ark::SsaoDebugMode::None) {
            std::cerr << "Default post-processing settings should keep SSAO disabled\n";
            return false;
        }

        return true;
    }

    bool validateSanitizeSettings() {
        ark::PostProcessingSettings settings{};
        settings.bloom.enabled = true;
        settings.bloom.intensity = 0.25f;
        settings.bloom.scatter = 0.75f;
        settings.bloom.threshold = 1.35f;
        settings.bloom.softKnee = 0.25f;
        settings.bloom.maxMipCount = 7;
        settings.ssao.enabled = true;
        settings.ssao.radius = 1.25f;
        settings.ssao.intensity = 0.85f;
        settings.ssao.bias = 0.03f;
        settings.ssao.power = 2.0f;
        settings.ssao.sampleCount = 32;
        settings.ssao.blurRadius = 3;
        settings.ssao.resolutionScale = 0.5f;
        settings.ssao.debugMode = ark::SsaoDebugMode::Occlusion;

        const ark::PostProcessingSettings sanitized = ark::sanitizePostProcessingSettings(settings);
        if (!sanitized.bloom.enabled ||
            !nearlyEqual(sanitized.bloom.intensity, 0.25f) ||
            !nearlyEqual(sanitized.bloom.scatter, 0.75f) ||
            !nearlyEqual(sanitized.bloom.threshold, 1.35f) ||
            !nearlyEqual(sanitized.bloom.softKnee, 0.25f) ||
            sanitized.bloom.maxMipCount != 7) {
            std::cerr << "Valid Bloom settings were not preserved\n";
            return false;
        }
        if (!sanitized.ssao.enabled ||
            !nearlyEqual(sanitized.ssao.radius, 1.25f) ||
            !nearlyEqual(sanitized.ssao.intensity, 0.85f) ||
            !nearlyEqual(sanitized.ssao.bias, 0.03f) ||
            !nearlyEqual(sanitized.ssao.power, 2.0f) ||
            sanitized.ssao.sampleCount != 32 ||
            sanitized.ssao.blurRadius != 3 ||
            !nearlyEqual(sanitized.ssao.resolutionScale, 0.5f) ||
            sanitized.ssao.debugMode != ark::SsaoDebugMode::Occlusion) {
            std::cerr << "Valid SSAO settings were not preserved\n";
            return false;
        }

        settings.bloom.enabled = true;
        settings.bloom.intensity = -1.0f;
        settings.bloom.scatter = std::numeric_limits<float>::infinity();
        settings.bloom.threshold = std::numeric_limits<float>::quiet_NaN();
        settings.bloom.softKnee = 2.0f;
        settings.bloom.maxMipCount = 0;
        settings.ssao.enabled = true;
        settings.ssao.radius = std::numeric_limits<float>::infinity();
        settings.ssao.intensity = -1.0f;
        settings.ssao.bias = std::numeric_limits<float>::quiet_NaN();
        settings.ssao.power = 0.0f;
        settings.ssao.sampleCount = 0;
        settings.ssao.blurRadius = 999;
        settings.ssao.resolutionScale = -1.0f;
        settings.ssao.debugMode = static_cast<ark::SsaoDebugMode>(999);

        const ark::PostProcessingSettings clamped = ark::sanitizePostProcessingSettings(settings);
        if (clamped.bloom.enabled ||
            !nearlyEqual(clamped.bloom.intensity, 0.0f) ||
            !nearlyEqual(clamped.bloom.scatter, 0.6f) ||
            !nearlyEqual(clamped.bloom.threshold, 1.0f) ||
            !nearlyEqual(clamped.bloom.softKnee, 1.0f) ||
            clamped.bloom.maxMipCount != 1) {
            std::cerr << "Invalid Bloom settings were not clamped to the expected contract\n";
            return false;
        }
        if (clamped.ssao.enabled ||
            !nearlyEqual(clamped.ssao.radius, 0.6f) ||
            !nearlyEqual(clamped.ssao.intensity, 0.0f) ||
            !nearlyEqual(clamped.ssao.bias, 0.025f) ||
            !nearlyEqual(clamped.ssao.power, 0.25f) ||
            clamped.ssao.sampleCount != 4 ||
            clamped.ssao.blurRadius != 8 ||
            !nearlyEqual(clamped.ssao.resolutionScale, 0.25f) ||
            clamped.ssao.debugMode != ark::SsaoDebugMode::None) {
            std::cerr << "Invalid SSAO settings were not clamped to the expected contract\n";
            return false;
        }

        settings.bloom.enabled = true;
        settings.bloom.intensity = 100.0f;
        settings.bloom.scatter = -4.0f;
        settings.bloom.threshold = 128.0f;
        settings.bloom.softKnee = -2.0f;
        settings.bloom.maxMipCount = 999;
        settings.ssao.enabled = true;
        settings.ssao.radius = 128.0f;
        settings.ssao.intensity = 128.0f;
        settings.ssao.bias = 128.0f;
        settings.ssao.power = 128.0f;
        settings.ssao.sampleCount = 999;
        settings.ssao.blurRadius = 999;
        settings.ssao.resolutionScale = 128.0f;
        settings.ssao.debugMode = ark::SsaoDebugMode::NormalDepth;

        const ark::PostProcessingSettings highClamp = ark::sanitizePostProcessingSettings(settings);
        if (!highClamp.bloom.enabled ||
            !nearlyEqual(highClamp.bloom.intensity, 10.0f) ||
            !nearlyEqual(highClamp.bloom.scatter, 0.0f) ||
            !nearlyEqual(highClamp.bloom.threshold, 64.0f) ||
            !nearlyEqual(highClamp.bloom.softKnee, 0.0f) ||
            highClamp.bloom.maxMipCount != 12) {
            std::cerr << "Bloom settings upper/lower clamp contract is invalid\n";
            return false;
        }
        if (!highClamp.ssao.enabled ||
            !nearlyEqual(highClamp.ssao.radius, 8.0f) ||
            !nearlyEqual(highClamp.ssao.intensity, 4.0f) ||
            !nearlyEqual(highClamp.ssao.bias, 0.5f) ||
            !nearlyEqual(highClamp.ssao.power, 8.0f) ||
            highClamp.ssao.sampleCount != 64 ||
            highClamp.ssao.blurRadius != 8 ||
            !nearlyEqual(highClamp.ssao.resolutionScale, 1.0f) ||
            highClamp.ssao.debugMode != ark::SsaoDebugMode::NormalDepth) {
            std::cerr << "SSAO settings upper/lower clamp contract is invalid\n";
            return false;
        }

        return true;
    }

    bool validateRenderViewSettings() {
        ark::PostProcessingSettings settings{};
        settings.bloom.enabled = true;
        settings.bloom.intensity = 0.18f;
        settings.bloom.maxMipCount = 5;
        settings.ssao.enabled = true;
        settings.ssao.radius = 1.5f;
        settings.ssao.sampleCount = 24;

        ark::RenderView view{};
        view.setPostProcessingSettings(settings);
        if (!view.postProcessingSettings().bloom.enabled ||
            !nearlyEqual(view.postProcessingSettings().bloom.intensity, 0.18f) ||
            view.postProcessingSettings().bloom.maxMipCount != 5 ||
            !view.postProcessingSettings().ssao.enabled ||
            !nearlyEqual(view.postProcessingSettings().ssao.radius, 1.5f) ||
            view.postProcessingSettings().ssao.sampleCount != 24) {
            std::cerr << "RenderView did not preserve sanitized post-processing settings\n";
            return false;
        }

        settings.bloom.intensity = -2.0f;
        settings.ssao.intensity = -2.0f;
        view.setPostProcessingSettings(settings);
        if (view.postProcessingSettings().bloom.enabled ||
            view.postProcessingSettings().ssao.enabled) {
            std::cerr << "RenderView should sanitize disabled post effects when intensity is zero or below\n";
            return false;
        }

        return true;
    }

    bool validateSandboxParsing() {
        constexpr std::array<std::string_view, 13> arguments{
            "--preset",
            "material-ball",
            "--tone-mapping",
            "aces",
            "--bloom",
            "--bloom-intensity",
            "0.12",
            "--bloom-scatter=0.7",
            "--bloom-threshold",
            "1.4",
            "--bloom-soft-knee=0.35",
            "--bloom-mips",
            "5",
        };

        const ark::SandboxLaunchOptions options = ark::parseSandboxLaunchOptions(arguments);
        const ark::ApplicationDesc desc = ark::makeSandboxApplicationDesc(options);
        const ark::BloomSettings& bloom = desc.view.postProcessing.bloom;
        if (!bloom.enabled ||
            !nearlyEqual(bloom.intensity, 0.12f) ||
            !nearlyEqual(bloom.scatter, 0.7f) ||
            !nearlyEqual(bloom.threshold, 1.4f) ||
            !nearlyEqual(bloom.softKnee, 0.35f) ||
            bloom.maxMipCount != 5) {
            std::cerr << "Sandbox Bloom CLI options were not parsed into ApplicationDesc\n";
            return false;
        }

        if (desc.view.toneMapping.operatorType != ark::ToneMappingOperator::ACES) {
            std::cerr << "Sandbox tone mapping CLI option was not parsed into ApplicationDesc\n";
            return false;
        }

        constexpr std::array<std::string_view, 1> linearArguments{"--tone-mapping=linear"};
        const ark::ApplicationDesc linearDesc =
            ark::makeSandboxApplicationDesc(ark::parseSandboxLaunchOptions(linearArguments));
        if (linearDesc.view.toneMapping.operatorType != ark::ToneMappingOperator::Linear) {
            std::cerr << "Sandbox tone mapping equals option was not parsed\n";
            return false;
        }

        constexpr std::array<std::string_view, 1> filmicArguments{"--tone-mapping=filmic"};
        const ark::ApplicationDesc filmicDesc =
            ark::makeSandboxApplicationDesc(ark::parseSandboxLaunchOptions(filmicArguments));
        if (filmicDesc.view.toneMapping.operatorType != ark::ToneMappingOperator::ACES) {
            std::cerr << "Sandbox filmic tone mapping alias should map to ACES\n";
            return false;
        }

        constexpr std::array<std::string_view, 1> enableOnlyArguments{"--bloom"};
        const ark::ApplicationDesc enableOnlyDesc =
            ark::makeSandboxApplicationDesc(ark::parseSandboxLaunchOptions(enableOnlyArguments));
        if (!enableOnlyDesc.view.postProcessing.bloom.enabled ||
            !nearlyEqual(enableOnlyDesc.view.postProcessing.bloom.intensity, 0.12f)) {
            std::cerr << "--bloom should keep the default visible intensity\n";
            return false;
        }

        constexpr std::array<std::string_view, 6> missingValueArguments{
            "--bloom-intensity",
            "--bloom-scatter",
            "--bloom-threshold",
            "--bloom-soft-knee",
            "--bloom-mips",
            "--tone-mapping",
        };
        const ark::SandboxLaunchOptions missing =
            ark::parseSandboxLaunchOptions(missingValueArguments);
        if (!missing.missingBloomIntensityValue ||
            !missing.missingBloomScatterValue ||
            !missing.missingBloomThresholdValue ||
            !missing.missingBloomSoftKneeValue ||
            !missing.missingBloomMipCountValue ||
            !missing.missingToneMappingValue) {
            std::cerr << "Sandbox Bloom CLI should report missing option values\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateDefaultSettings() &&
                   validateSanitizeSettings() &&
                   validateRenderViewSettings() &&
                   validateSandboxParsing()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
