#pragma once

#include "app/Application.h"
#include "core/FileSystem.h"
#include "core/Types.h"
#include "renderer/RendererPreset.h"

#include <span>
#include <string_view>

namespace ark {
    struct SandboxViewOverrideMask {
        bool toneMappingOperator = false;
        bool bloomEnabled = false;
        bool bloomIntensity = false;
        bool bloomScatter = false;
        bool bloomThreshold = false;
        bool bloomSoftKnee = false;
        bool bloomMipCount = false;
        bool shadowsEnabled = false;
        bool shadowStrength = false;
        bool shadowBias = false;
        bool shadowExtent = false;
        bool shadowBounds = false;
        bool shadowFilterMode = false;
        bool shadowFilterRadius = false;
    };

    struct SandboxLaunchOptions {
        RendererPresetDesc preset;
        RenderViewProfileDesc view;
        SandboxViewOverrideMask viewOverrides;
        bool useDebugOrientationEnvironment = false;
        bool debugUiEnabled = true;
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
        bool missingShadowFilterValue = false;
        bool missingShadowFilterRadiusValue = false;
    };

    SandboxLaunchOptions parseSandboxLaunchOptions(std::span<const std::string_view> arguments);
    ApplicationDesc makeSandboxApplicationDesc(const SandboxLaunchOptions& options);
    ApplicationDesc makeSandboxApplicationDesc(std::span<const std::string_view> arguments);
} // namespace ark
