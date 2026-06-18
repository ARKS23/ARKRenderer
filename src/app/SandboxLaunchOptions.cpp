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

        void applySandboxViewOverrides(const SandboxLaunchOptions& options,
                                       RenderViewProfileDesc& view) {
            const SandboxViewOverrideMask& mask = options.viewOverrides;
            const RenderViewProfileDesc& overrides = options.view;

            if (mask.toneMappingOperator) {
                view.toneMapping.operatorType = overrides.toneMapping.operatorType;
            }

            BloomSettings& bloom = view.postProcessing.bloom;
            const BloomSettings& bloomOverrides = overrides.postProcessing.bloom;
            if (mask.bloomEnabled) {
                bloom.enabled = bloomOverrides.enabled;
                if (bloom.enabled && bloom.intensity <= 0.0f && !mask.bloomIntensity) {
                    bloom.intensity = bloomOverrides.intensity > 0.0f ? bloomOverrides.intensity : 0.08f;
                }
            }
            if (mask.bloomIntensity) {
                bloom.enabled = true;
                bloom.intensity = bloomOverrides.intensity;
            }
            if (mask.bloomScatter) {
                bloom.enabled = true;
                bloom.scatter = bloomOverrides.scatter;
            }
            if (mask.bloomThreshold) {
                bloom.enabled = true;
                bloom.threshold = bloomOverrides.threshold;
            }
            if (mask.bloomSoftKnee) {
                bloom.enabled = true;
                bloom.softKnee = bloomOverrides.softKnee;
            }
            if (mask.bloomMipCount) {
                bloom.enabled = true;
                bloom.maxMipCount = bloomOverrides.maxMipCount;
            }

            ShadowSettings& shadows = view.shadows;
            const ShadowSettings& shadowOverrides = overrides.shadows;
            if (mask.shadowsEnabled) {
                shadows.enabled = shadowOverrides.enabled;
            }
            if (mask.shadowStrength) {
                shadows.enabled = true;
                shadows.strength = shadowOverrides.strength;
            }
            if (mask.shadowBias) {
                shadows.enabled = true;
                shadows.bias = shadowOverrides.bias;
            }
            if (mask.shadowExtent) {
                shadows.enabled = true;
                shadows.mapExtent = shadowOverrides.mapExtent;
            }
            if (mask.shadowBounds) {
                shadows.enabled = true;
                shadows.fitSceneBounds = shadowOverrides.fitSceneBounds;
                shadows.orthographicHalfExtent = shadowOverrides.orthographicHalfExtent;
            }
            if (mask.shadowFilterMode) {
                shadows.enabled = true;
                shadows.filterMode = shadowOverrides.filterMode;
            }
            if (mask.shadowFilterRadius) {
                shadows.enabled = true;
                shadows.filterRadiusTexels = shadowOverrides.filterRadiusTexels;
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
                options.viewOverrides.bloomEnabled = true;
                options.view.postProcessing.bloom.enabled = true;
                if (options.view.postProcessing.bloom.intensity <= 0.0f) {
                    options.view.postProcessing.bloom.intensity = 0.08f;
                }
                continue;
            }

            if (argument == "--shadows" || argument == "--shadow") {
                options.viewOverrides.shadowsEnabled = true;
                options.view.shadows.enabled = true;
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
                    options.viewOverrides.toneMappingOperator = true;
                    options.view.toneMapping.operatorType =
                        parseToneMappingOperator(value, options.view.toneMapping.operatorType);
                } else {
                    options.missingToneMappingValue = true;
                }
                continue;
            }

            constexpr std::string_view toneMappingPrefix = "--tone-mapping=";
            if (hasPrefix(argument, toneMappingPrefix)) {
                options.viewOverrides.toneMappingOperator = true;
                options.view.toneMapping.operatorType =
                    parseToneMappingOperator(argument.substr(toneMappingPrefix.size()),
                                             options.view.toneMapping.operatorType);
                continue;
            }

            if (argument == "--bloom-intensity") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.viewOverrides.bloomEnabled = true;
                    options.viewOverrides.bloomIntensity = true;
                    options.view.postProcessing.bloom.enabled = true;
                    applyFloatOption(value, options.view.postProcessing.bloom.intensity);
                } else {
                    options.missingBloomIntensityValue = true;
                }
                continue;
            }

            constexpr std::string_view bloomIntensityPrefix = "--bloom-intensity=";
            if (hasPrefix(argument, bloomIntensityPrefix)) {
                options.viewOverrides.bloomEnabled = true;
                options.viewOverrides.bloomIntensity = true;
                options.view.postProcessing.bloom.enabled = true;
                applyFloatOption(argument.substr(bloomIntensityPrefix.size()),
                                 options.view.postProcessing.bloom.intensity);
                continue;
            }

            if (argument == "--bloom-scatter") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.viewOverrides.bloomEnabled = true;
                    options.viewOverrides.bloomScatter = true;
                    options.view.postProcessing.bloom.enabled = true;
                    applyFloatOption(value, options.view.postProcessing.bloom.scatter);
                } else {
                    options.missingBloomScatterValue = true;
                }
                continue;
            }

            constexpr std::string_view bloomScatterPrefix = "--bloom-scatter=";
            if (hasPrefix(argument, bloomScatterPrefix)) {
                options.viewOverrides.bloomEnabled = true;
                options.viewOverrides.bloomScatter = true;
                options.view.postProcessing.bloom.enabled = true;
                applyFloatOption(argument.substr(bloomScatterPrefix.size()),
                                 options.view.postProcessing.bloom.scatter);
                continue;
            }

            if (argument == "--bloom-threshold") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.viewOverrides.bloomEnabled = true;
                    options.viewOverrides.bloomThreshold = true;
                    options.view.postProcessing.bloom.enabled = true;
                    applyFloatOption(value, options.view.postProcessing.bloom.threshold);
                } else {
                    options.missingBloomThresholdValue = true;
                }
                continue;
            }

            constexpr std::string_view bloomThresholdPrefix = "--bloom-threshold=";
            if (hasPrefix(argument, bloomThresholdPrefix)) {
                options.viewOverrides.bloomEnabled = true;
                options.viewOverrides.bloomThreshold = true;
                options.view.postProcessing.bloom.enabled = true;
                applyFloatOption(argument.substr(bloomThresholdPrefix.size()),
                                 options.view.postProcessing.bloom.threshold);
                continue;
            }

            if (argument == "--bloom-soft-knee") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.viewOverrides.bloomEnabled = true;
                    options.viewOverrides.bloomSoftKnee = true;
                    options.view.postProcessing.bloom.enabled = true;
                    applyFloatOption(value, options.view.postProcessing.bloom.softKnee);
                } else {
                    options.missingBloomSoftKneeValue = true;
                }
                continue;
            }

            constexpr std::string_view bloomSoftKneePrefix = "--bloom-soft-knee=";
            if (hasPrefix(argument, bloomSoftKneePrefix)) {
                options.viewOverrides.bloomEnabled = true;
                options.viewOverrides.bloomSoftKnee = true;
                options.view.postProcessing.bloom.enabled = true;
                applyFloatOption(argument.substr(bloomSoftKneePrefix.size()),
                                 options.view.postProcessing.bloom.softKnee);
                continue;
            }

            if (argument == "--bloom-mips") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.viewOverrides.bloomEnabled = true;
                    options.viewOverrides.bloomMipCount = true;
                    options.view.postProcessing.bloom.enabled = true;
                    applyU32Option(value, options.view.postProcessing.bloom.maxMipCount);
                } else {
                    options.missingBloomMipCountValue = true;
                }
                continue;
            }

            if (argument == "--shadow-strength") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.viewOverrides.shadowsEnabled = true;
                    options.viewOverrides.shadowStrength = true;
                    options.view.shadows.enabled = true;
                    applyFloatOption(value, options.view.shadows.strength);
                } else {
                    options.missingShadowStrengthValue = true;
                }
                continue;
            }

            constexpr std::string_view shadowStrengthPrefix = "--shadow-strength=";
            if (hasPrefix(argument, shadowStrengthPrefix)) {
                options.viewOverrides.shadowsEnabled = true;
                options.viewOverrides.shadowStrength = true;
                options.view.shadows.enabled = true;
                applyFloatOption(argument.substr(shadowStrengthPrefix.size()), options.view.shadows.strength);
                continue;
            }

            if (argument == "--shadow-bias") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.viewOverrides.shadowsEnabled = true;
                    options.viewOverrides.shadowBias = true;
                    options.view.shadows.enabled = true;
                    applyFloatOption(value, options.view.shadows.bias);
                } else {
                    options.missingShadowBiasValue = true;
                }
                continue;
            }

            constexpr std::string_view shadowBiasPrefix = "--shadow-bias=";
            if (hasPrefix(argument, shadowBiasPrefix)) {
                options.viewOverrides.shadowsEnabled = true;
                options.viewOverrides.shadowBias = true;
                options.view.shadows.enabled = true;
                applyFloatOption(argument.substr(shadowBiasPrefix.size()), options.view.shadows.bias);
                continue;
            }

            if (argument == "--shadow-extent") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.viewOverrides.shadowsEnabled = true;
                    options.viewOverrides.shadowExtent = true;
                    options.view.shadows.enabled = true;
                    applyU32Option(value, options.view.shadows.mapExtent);
                } else {
                    options.missingShadowExtentValue = true;
                }
                continue;
            }

            constexpr std::string_view shadowExtentPrefix = "--shadow-extent=";
            if (hasPrefix(argument, shadowExtentPrefix)) {
                options.viewOverrides.shadowsEnabled = true;
                options.viewOverrides.shadowExtent = true;
                options.view.shadows.enabled = true;
                applyU32Option(argument.substr(shadowExtentPrefix.size()), options.view.shadows.mapExtent);
                continue;
            }

            if (argument == "--shadow-bounds") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.viewOverrides.shadowsEnabled = true;
                    options.viewOverrides.shadowBounds = true;
                    options.view.shadows.enabled = true;
                    options.view.shadows.fitSceneBounds = false;
                    applyFloatOption(value, options.view.shadows.orthographicHalfExtent);
                } else {
                    options.missingShadowBoundsValue = true;
                }
                continue;
            }

            constexpr std::string_view shadowBoundsPrefix = "--shadow-bounds=";
            if (hasPrefix(argument, shadowBoundsPrefix)) {
                options.viewOverrides.shadowsEnabled = true;
                options.viewOverrides.shadowBounds = true;
                options.view.shadows.enabled = true;
                options.view.shadows.fitSceneBounds = false;
                applyFloatOption(argument.substr(shadowBoundsPrefix.size()),
                                 options.view.shadows.orthographicHalfExtent);
                continue;
            }

            if (argument == "--shadow-filter") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.viewOverrides.shadowsEnabled = true;
                    options.viewOverrides.shadowFilterMode = true;
                    options.view.shadows.enabled = true;
                    options.view.shadows.filterMode =
                        parseShadowFilterMode(value, options.view.shadows.filterMode);
                } else {
                    options.missingShadowFilterValue = true;
                }
                continue;
            }

            constexpr std::string_view shadowFilterPrefix = "--shadow-filter=";
            if (hasPrefix(argument, shadowFilterPrefix)) {
                options.viewOverrides.shadowsEnabled = true;
                options.viewOverrides.shadowFilterMode = true;
                options.view.shadows.enabled = true;
                options.view.shadows.filterMode =
                    parseShadowFilterMode(argument.substr(shadowFilterPrefix.size()),
                                          options.view.shadows.filterMode);
                continue;
            }

            if (argument == "--shadow-filter-radius") {
                std::string_view value;
                if (takeValue(arguments, argumentIndex, value)) {
                    options.viewOverrides.shadowsEnabled = true;
                    options.viewOverrides.shadowFilterRadius = true;
                    options.view.shadows.enabled = true;
                    applyFloatOption(value, options.view.shadows.filterRadiusTexels);
                } else {
                    options.missingShadowFilterRadiusValue = true;
                }
                continue;
            }

            constexpr std::string_view shadowFilterRadiusPrefix = "--shadow-filter-radius=";
            if (hasPrefix(argument, shadowFilterRadiusPrefix)) {
                options.viewOverrides.shadowsEnabled = true;
                options.viewOverrides.shadowFilterRadius = true;
                options.view.shadows.enabled = true;
                applyFloatOption(argument.substr(shadowFilterRadiusPrefix.size()),
                                 options.view.shadows.filterRadiusTexels);
                continue;
            }

            constexpr std::string_view bloomMipsPrefix = "--bloom-mips=";
            if (hasPrefix(argument, bloomMipsPrefix)) {
                options.viewOverrides.bloomEnabled = true;
                options.viewOverrides.bloomMipCount = true;
                options.view.postProcessing.bloom.enabled = true;
                applyU32Option(argument.substr(bloomMipsPrefix.size()),
                               options.view.postProcessing.bloom.maxMipCount);
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
        const bool hasModelPathOverride = !options.modelPathOverride.empty();

        if (options.useDebugOrientationEnvironment) {
            resolved.scene.environmentFallback = SceneEnvironmentFallbackPolicy::DebugOrientation;
        }

        if (hasModelPathOverride) {
            resolved.scene.modelPath = options.modelPathOverride;
            resolved.scene.additionalModels.clear();
        }

        if (!options.environmentPathOverride.empty()) {
            resolved.scene.environmentPath = options.environmentPathOverride;
        }

        ApplicationDesc desc{};
        desc.defaultScene = resolved.scene;
        desc.rendererQuality = resolved.quality;
        desc.view = resolved.view;
        applySandboxViewOverrides(options, desc.view);
        desc.camera = hasModelPathOverride && options.preset.scene == RendererScenePreset::Default
                          ? OrbitCameraProfileDesc{}
                          : resolved.camera;
        if (options.preset.scene == RendererScenePreset::ShadowValidation) {
            desc.view.shadows.enabled = true;
            desc.view.shadows.strength = desc.view.shadows.strength <= 0.0f ? 1.0f : desc.view.shadows.strength;
            if (desc.view.shadows.orthographicHalfExtent < 64.0f) {
                desc.view.shadows.orthographicHalfExtent = 64.0f;
            }
            if (desc.view.shadows.farPlane < 256.0f) {
                desc.view.shadows.farPlane = 256.0f;
            }
            if (desc.view.shadows.lightDistance < 96.0f) {
                desc.view.shadows.lightDistance = 96.0f;
            }
        }
        desc.useDebugOrientationEnvironment =
            desc.defaultScene.environmentFallback == SceneEnvironmentFallbackPolicy::DebugOrientation;
        return desc;
    }

    ApplicationDesc makeSandboxApplicationDesc(std::span<const std::string_view> arguments) {
        return makeSandboxApplicationDesc(parseSandboxLaunchOptions(arguments));
    }
} // namespace ark
