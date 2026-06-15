#include "app/SandboxLaunchOptions.h"

#include <string>

namespace ark {
    namespace {
        bool isFlag(std::string_view argument) {
            return argument.size() >= 2 && argument[0] == '-' && argument[1] == '-';
        }

        bool hasPrefix(std::string_view argument, std::string_view prefix) {
            return argument.size() > prefix.size() && argument.substr(0, prefix.size()) == prefix;
        }

        Path makePath(std::string_view argument) {
            return Path{std::string{argument}};
        }
    } // namespace

    SandboxLaunchOptions parseSandboxLaunchOptions(std::span<const std::string_view> arguments) {
        SandboxLaunchOptions options{};
        u32 positionalArgument = 0;

        for (usize argumentIndex = 0; argumentIndex < arguments.size(); ++argumentIndex) {
            const std::string_view argument = arguments[argumentIndex];
            if (argument.empty()) {
                continue;
            }

            if (argument == "--debug-orientation") {
                options.useDebugOrientationEnvironment = true;
                continue;
            }

            if (argument == "--preset") {
                if (argumentIndex + 1 < arguments.size() && !isFlag(arguments[argumentIndex + 1])) {
                    ++argumentIndex;
                    options.preset.scene =
                        parseRendererScenePreset(arguments[argumentIndex], options.preset.scene);
                } else {
                    options.missingPresetValue = true;
                }
                continue;
            }

            constexpr std::string_view presetPrefix = "--preset=";
            if (hasPrefix(argument, presetPrefix)) {
                options.preset.scene =
                    parseRendererScenePreset(argument.substr(presetPrefix.size()), options.preset.scene);
                continue;
            }

            if (argument == "--quality") {
                if (argumentIndex + 1 < arguments.size() && !isFlag(arguments[argumentIndex + 1])) {
                    ++argumentIndex;
                    options.preset.quality =
                        parseRendererQualityPreset(arguments[argumentIndex], options.preset.quality);
                } else {
                    options.missingQualityValue = true;
                }
                continue;
            }

            constexpr std::string_view qualityPrefix = "--quality=";
            if (hasPrefix(argument, qualityPrefix)) {
                options.preset.quality =
                    parseRendererQualityPreset(argument.substr(qualityPrefix.size()),
                                               options.preset.quality);
                continue;
            }

            if (isFlag(argument)) {
                continue;
            }

            if (positionalArgument == 0) {
                options.modelPathOverride = makePath(argument);
            } else if (positionalArgument == 1) {
                options.environmentPathOverride = makePath(argument);
            } else {
                ++options.ignoredExtraPositionalArgumentCount;
            }
            ++positionalArgument;
        }

        return options;
    }

    ApplicationDesc makeSandboxApplicationDesc(const SandboxLaunchOptions& options) {
        ResolvedRendererPreset resolved = resolveRendererPreset(options.preset);

        if (options.useDebugOrientationEnvironment) {
            resolved.scene.environmentFallback = SceneEnvironmentFallbackPolicy::DebugOrientation;
        }

        if (!options.modelPathOverride.empty()) {
            resolved.scene.modelPath = options.modelPathOverride;
        }

        if (!options.environmentPathOverride.empty()) {
            resolved.scene.environmentPath = options.environmentPathOverride;
        }

        ApplicationDesc desc{};
        desc.defaultModelPath = resolved.scene.modelPath;
        desc.defaultEnvironmentPath = resolved.scene.environmentPath;
        desc.rendererQuality = resolved.quality;
        desc.useDebugOrientationEnvironment =
            resolved.scene.environmentFallback == SceneEnvironmentFallbackPolicy::DebugOrientation;
        return desc;
    }

    ApplicationDesc makeSandboxApplicationDesc(std::span<const std::string_view> arguments) {
        return makeSandboxApplicationDesc(parseSandboxLaunchOptions(arguments));
    }
} // namespace ark
