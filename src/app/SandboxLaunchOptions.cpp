#include "app/SandboxLaunchOptions.h"

#include <charconv>
#include <string>
#include <system_error>

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

        bool parseFloat(std::string_view argument, float& value) {
            const char* begin = argument.data();
            const char* end = argument.data() + argument.size();
            float parsed = 0.0f;
            const std::from_chars_result result = std::from_chars(begin, end, parsed);
            if (result.ec != std::errc{} || result.ptr != end) {
                return false;
            }

            value = parsed;
            return true;
        }

        bool parseU32(std::string_view argument, u32& value) {
            const char* begin = argument.data();
            const char* end = argument.data() + argument.size();
            u32 parsed = 0;
            const std::from_chars_result result = std::from_chars(begin, end, parsed);
            if (result.ec != std::errc{} || result.ptr != end) {
                return false;
            }

            value = parsed;
            return true;
        }

        bool takeValue(std::span<const std::string_view> arguments,
                       usize& argumentIndex,
                       std::string_view& value) {
            if (argumentIndex + 1 >= arguments.size() || isFlag(arguments[argumentIndex + 1])) {
                return false;
            }

            ++argumentIndex;
            value = arguments[argumentIndex];
            return true;
        }

        void applyFloatOption(std::string_view value, float& target) {
            float parsed = 0.0f;
            if (parseFloat(value, parsed)) {
                target = parsed;
            }
        }

        void applyU32Option(std::string_view value, u32& target) {
            u32 parsed = 0;
            if (parseU32(value, parsed)) {
                target = parsed;
            }
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

            if (argument == "--bloom") {
                options.postProcessing.bloom.enabled = true;
                if (options.postProcessing.bloom.intensity <= 0.0f) {
                    options.postProcessing.bloom.intensity = 0.08f;
                }
                continue;
            }

            if (argument == "--shadows" || argument == "--shadow") {
                options.shadows.enabled = true;
                continue;
            }

            if (argument == "--preset") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.preset.scene =
                        parseRendererScenePreset(value, options.preset.scene);
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
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.preset.quality =
                        parseRendererQualityPreset(value, options.preset.quality);
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

            if (argument == "--tone-mapping") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.toneMapping.operatorType =
                        parseToneMappingOperator(value, options.toneMapping.operatorType);
                } else {
                    options.missingToneMappingValue = true;
                }
                continue;
            }

            constexpr std::string_view toneMappingPrefix = "--tone-mapping=";
            if (hasPrefix(argument, toneMappingPrefix)) {
                options.toneMapping.operatorType =
                    parseToneMappingOperator(argument.substr(toneMappingPrefix.size()),
                                             options.toneMapping.operatorType);
                continue;
            }

            if (argument == "--bloom-intensity") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.postProcessing.bloom.enabled = true;
                    applyFloatOption(value, options.postProcessing.bloom.intensity);
                } else {
                    options.missingBloomIntensityValue = true;
                }
                continue;
            }

            constexpr std::string_view bloomIntensityPrefix = "--bloom-intensity=";
            if (hasPrefix(argument, bloomIntensityPrefix)) {
                options.postProcessing.bloom.enabled = true;
                applyFloatOption(argument.substr(bloomIntensityPrefix.size()),
                                 options.postProcessing.bloom.intensity);
                continue;
            }

            if (argument == "--bloom-scatter") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.postProcessing.bloom.enabled = true;
                    applyFloatOption(value, options.postProcessing.bloom.scatter);
                } else {
                    options.missingBloomScatterValue = true;
                }
                continue;
            }

            constexpr std::string_view bloomScatterPrefix = "--bloom-scatter=";
            if (hasPrefix(argument, bloomScatterPrefix)) {
                options.postProcessing.bloom.enabled = true;
                applyFloatOption(argument.substr(bloomScatterPrefix.size()),
                                 options.postProcessing.bloom.scatter);
                continue;
            }

            if (argument == "--bloom-threshold") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.postProcessing.bloom.enabled = true;
                    applyFloatOption(value, options.postProcessing.bloom.threshold);
                } else {
                    options.missingBloomThresholdValue = true;
                }
                continue;
            }

            constexpr std::string_view bloomThresholdPrefix = "--bloom-threshold=";
            if (hasPrefix(argument, bloomThresholdPrefix)) {
                options.postProcessing.bloom.enabled = true;
                applyFloatOption(argument.substr(bloomThresholdPrefix.size()),
                                 options.postProcessing.bloom.threshold);
                continue;
            }

            if (argument == "--bloom-soft-knee") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.postProcessing.bloom.enabled = true;
                    applyFloatOption(value, options.postProcessing.bloom.softKnee);
                } else {
                    options.missingBloomSoftKneeValue = true;
                }
                continue;
            }

            constexpr std::string_view bloomSoftKneePrefix = "--bloom-soft-knee=";
            if (hasPrefix(argument, bloomSoftKneePrefix)) {
                options.postProcessing.bloom.enabled = true;
                applyFloatOption(argument.substr(bloomSoftKneePrefix.size()),
                                 options.postProcessing.bloom.softKnee);
                continue;
            }

            if (argument == "--bloom-mips") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.postProcessing.bloom.enabled = true;
                    applyU32Option(value, options.postProcessing.bloom.maxMipCount);
                } else {
                    options.missingBloomMipCountValue = true;
                }
                continue;
            }

            if (argument == "--shadow-strength") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.shadows.enabled = true;
                    applyFloatOption(value, options.shadows.strength);
                } else {
                    options.missingShadowStrengthValue = true;
                }
                continue;
            }

            constexpr std::string_view shadowStrengthPrefix = "--shadow-strength=";
            if (hasPrefix(argument, shadowStrengthPrefix)) {
                options.shadows.enabled = true;
                applyFloatOption(argument.substr(shadowStrengthPrefix.size()), options.shadows.strength);
                continue;
            }

            if (argument == "--shadow-bias") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.shadows.enabled = true;
                    applyFloatOption(value, options.shadows.bias);
                } else {
                    options.missingShadowBiasValue = true;
                }
                continue;
            }

            constexpr std::string_view shadowBiasPrefix = "--shadow-bias=";
            if (hasPrefix(argument, shadowBiasPrefix)) {
                options.shadows.enabled = true;
                applyFloatOption(argument.substr(shadowBiasPrefix.size()), options.shadows.bias);
                continue;
            }

            if (argument == "--shadow-extent") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.shadows.enabled = true;
                    applyU32Option(value, options.shadows.mapExtent);
                } else {
                    options.missingShadowExtentValue = true;
                }
                continue;
            }

            constexpr std::string_view shadowExtentPrefix = "--shadow-extent=";
            if (hasPrefix(argument, shadowExtentPrefix)) {
                options.shadows.enabled = true;
                applyU32Option(argument.substr(shadowExtentPrefix.size()), options.shadows.mapExtent);
                continue;
            }

            if (argument == "--shadow-bounds") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.shadows.enabled = true;
                    applyFloatOption(value, options.shadows.orthographicHalfExtent);
                } else {
                    options.missingShadowBoundsValue = true;
                }
                continue;
            }

            constexpr std::string_view shadowBoundsPrefix = "--shadow-bounds=";
            if (hasPrefix(argument, shadowBoundsPrefix)) {
                options.shadows.enabled = true;
                applyFloatOption(argument.substr(shadowBoundsPrefix.size()),
                                 options.shadows.orthographicHalfExtent);
                continue;
            }

            constexpr std::string_view bloomMipsPrefix = "--bloom-mips=";
            if (hasPrefix(argument, bloomMipsPrefix)) {
                options.postProcessing.bloom.enabled = true;
                applyU32Option(argument.substr(bloomMipsPrefix.size()),
                               options.postProcessing.bloom.maxMipCount);
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
        desc.toneMapping = options.toneMapping;
        desc.postProcessing = sanitizePostProcessingSettings(options.postProcessing);
        desc.shadows = options.shadows;
        if (options.preset.scene == RendererScenePreset::ShadowValidation) {
            desc.shadows.enabled = true;
            desc.shadows.strength = desc.shadows.strength <= 0.0f ? 0.7f : desc.shadows.strength;
            if (desc.shadows.orthographicHalfExtent < 12.0f) {
                desc.shadows.orthographicHalfExtent = 12.0f;
            }
            if (desc.shadows.lightDistance < 24.0f) {
                desc.shadows.lightDistance = 24.0f;
            }
        }
        desc.useDebugOrientationEnvironment =
            resolved.scene.environmentFallback == SceneEnvironmentFallbackPolicy::DebugOrientation;
        return desc;
    }

    ApplicationDesc makeSandboxApplicationDesc(std::span<const std::string_view> arguments) {
        return makeSandboxApplicationDesc(parseSandboxLaunchOptions(arguments));
    }
} // namespace ark
