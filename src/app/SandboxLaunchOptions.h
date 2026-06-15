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
        bool useDebugOrientationEnvironment = false;
        Path modelPathOverride;
        Path environmentPathOverride;
        u32 ignoredExtraPositionalArgumentCount = 0;
        bool missingPresetValue = false;
        bool missingQualityValue = false;
    };

    SandboxLaunchOptions parseSandboxLaunchOptions(std::span<const std::string_view> arguments);
    ApplicationDesc makeSandboxApplicationDesc(const SandboxLaunchOptions& options);
    ApplicationDesc makeSandboxApplicationDesc(std::span<const std::string_view> arguments);
} // namespace ark
