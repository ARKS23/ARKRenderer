#pragma once

#include "app/Application.h"
#include "core/FileSystem.h"
#include "core/Types.h"
#include "renderer/RendererPreset.h"

#include <span>
#include <string_view>

namespace ark {
    struct SandboxLaunchOptions {
        RendererPresetDesc preset;
        ToneMappingSettings toneMapping{1.0f, 2.2f, ToneMappingOperator::ACES};
        PostProcessingSettings postProcessing{
            BloomSettings{true, 0.12f, 0.6f, 1.0f, 0.5f, 6},
        };
        ShadowSettings shadows{true, 0.9f, 0.0015f, 1024, 36.0f, 0.1f, 160.0f, 64.0f};
        bool useDebugOrientationEnvironment = false;
        Path modelPathOverride;
        Path environmentPathOverride;
        u32 ignoredExtraPositionalArgumentCount = 0;
        bool missingPresetValue = false;
        bool missingQualityValue = false;
        bool missingBloomIntensityValue = false;
        bool missingBloomScatterValue = false;
        bool missingBloomThresholdValue = false;
        bool missingBloomSoftKneeValue = false;
        bool missingBloomMipCountValue = false;
        bool missingToneMappingValue = false;
        bool missingShadowStrengthValue = false;
        bool missingShadowBiasValue = false;
        bool missingShadowExtentValue = false;
        bool missingShadowBoundsValue = false;
    };

    SandboxLaunchOptions parseSandboxLaunchOptions(std::span<const std::string_view> arguments);
    ApplicationDesc makeSandboxApplicationDesc(const SandboxLaunchOptions& options);
    ApplicationDesc makeSandboxApplicationDesc(std::span<const std::string_view> arguments);
} // namespace ark
