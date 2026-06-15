#include "app/SandboxLaunchOptions.h"
#include "renderer/RendererPreset.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
    bool sameExtent(ark::rhi::Extent2D lhs, ark::rhi::Extent2D rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    bool near(float lhs, float rhs, float epsilon = 0.0001f) {
        return std::fabs(lhs - rhs) <= epsilon;
    }

    bool validateDefaultPreset() {
        const ark::ResolvedRendererPreset resolved =
            ark::resolveRendererPreset(ark::RendererPresetDesc{});

        if (!resolved.scene.modelPath.empty() ||
            !resolved.scene.environmentPath.empty() ||
            resolved.scene.modelFallback != ark::SceneModelFallbackPolicy::DefaultSandboxModel ||
            resolved.scene.environmentFallback !=
                ark::SceneEnvironmentFallbackPolicy::DefaultHdrThenProcedural ||
            resolved.scene.sceneName != "DefaultSandboxScene" ||
            resolved.scene.modelName != "DefaultSandboxModel" ||
            resolved.scene.environmentName != "DefaultSandboxEnvironment" ||
            !near(resolved.scene.environmentIntensity, 1.0f)) {
            std::cerr << "Default scene preset is invalid\n";
            return false;
        }

        const ark::EnvironmentBakeQualityDesc& bake = resolved.quality.environmentBake;
        if (!sameExtent(bake.environmentCubeFaceExtent, ark::DefaultEnvironmentBakeCubeFaceExtent) ||
            !sameExtent(bake.irradianceCubeFaceExtent,
                        ark::DefaultEnvironmentBakeIrradianceFaceExtent) ||
            !sameExtent(bake.specularCubeFaceExtent,
                        ark::DefaultEnvironmentBakeSpecularFaceExtent) ||
            !sameExtent(bake.brdfLutExtent, ark::DefaultEnvironmentBakeBrdfLutExtent) ||
            !near(bake.irradianceSampleDelta,
                  ark::DefaultEnvironmentBakeIrradianceSampleDelta) ||
            bake.specularPrefilterSampleCount != ark::DefaultEnvironmentBakeSpecularSampleCount ||
            bake.brdfLutSampleCount != ark::DefaultEnvironmentBakeBrdfLutSampleCount ||
            !bake.enableEnvironmentCube ||
            !bake.enableIrradiance ||
            !bake.enableSpecularPrefilter ||
            !bake.enableBrdfLut) {
            std::cerr << "Default quality preset is invalid\n";
            return false;
        }

        return true;
    }

    bool validateScenePresets() {
        ark::RendererPresetDesc preset{};
        preset.scene = ark::RendererScenePreset::MaterialBall;
        ark::ResolvedRendererPreset resolved = ark::resolveRendererPreset(preset);
        if (resolved.scene.modelPath.filename() != "material_ball_validation_fixture.gltf" ||
            resolved.scene.modelFallback != ark::SceneModelFallbackPolicy::DefaultSandboxModel ||
            resolved.scene.environmentFallback !=
                ark::SceneEnvironmentFallbackPolicy::DefaultHdrThenProcedural ||
            resolved.scene.sceneName != "MaterialBallScene") {
            std::cerr << "Material ball scene preset is invalid\n";
            return false;
        }

        preset.scene = ark::RendererScenePreset::SpecularValidation;
        resolved = ark::resolveRendererPreset(preset);
        if (resolved.scene.modelPath.filename() != "specular_ibl_validation_fixture.gltf" ||
            resolved.scene.modelFallback != ark::SceneModelFallbackPolicy::DefaultSandboxModel ||
            resolved.scene.environmentFallback !=
                ark::SceneEnvironmentFallbackPolicy::DefaultHdrThenProcedural ||
            resolved.scene.sceneName != "SpecularValidationScene") {
            std::cerr << "Specular validation scene preset is invalid\n";
            return false;
        }

        preset.scene = ark::RendererScenePreset::BloomValidation;
        resolved = ark::resolveRendererPreset(preset);
        if (resolved.scene.modelPath.filename() != "bloom_validation_fixture.gltf" ||
            resolved.scene.modelFallback != ark::SceneModelFallbackPolicy::DefaultSandboxModel ||
            resolved.scene.environmentFallback !=
                ark::SceneEnvironmentFallbackPolicy::DefaultHdrThenProcedural ||
            resolved.scene.sceneName != "BloomValidationScene") {
            std::cerr << "Bloom validation scene preset is invalid\n";
            return false;
        }

        preset.scene = ark::RendererScenePreset::DebugOrientation;
        resolved = ark::resolveRendererPreset(preset);
        if (!resolved.scene.modelPath.empty() ||
            resolved.scene.modelFallback != ark::SceneModelFallbackPolicy::DefaultSandboxModel ||
            resolved.scene.environmentFallback !=
                ark::SceneEnvironmentFallbackPolicy::DebugOrientation ||
            resolved.scene.sceneName != "DebugOrientationScene") {
            std::cerr << "Debug orientation scene preset is invalid\n";
            return false;
        }

        return true;
    }

    bool validateQualityPresets() {
        ark::RendererPresetDesc preset{};
        preset.quality = ark::RendererQualityPreset::Low;
        ark::ResolvedRendererPreset resolved = ark::resolveRendererPreset(preset);
        const ark::EnvironmentBakeQualityDesc& low = resolved.quality.environmentBake;
        if (!sameExtent(low.environmentCubeFaceExtent, ark::rhi::Extent2D{256, 256}) ||
            !sameExtent(low.irradianceCubeFaceExtent, ark::rhi::Extent2D{16, 16}) ||
            !sameExtent(low.specularCubeFaceExtent, ark::rhi::Extent2D{128, 128}) ||
            !sameExtent(low.brdfLutExtent, ark::rhi::Extent2D{128, 128}) ||
            !near(low.irradianceSampleDelta, 0.2f) ||
            low.specularPrefilterSampleCount != 64 ||
            low.brdfLutSampleCount != 512 ||
            !low.enableEnvironmentCube ||
            !low.enableIrradiance ||
            !low.enableSpecularPrefilter ||
            !low.enableBrdfLut) {
            std::cerr << "Low quality preset is invalid\n";
            return false;
        }

        preset.quality = ark::RendererQualityPreset::High;
        resolved = ark::resolveRendererPreset(preset);
        const ark::EnvironmentBakeQualityDesc& high = resolved.quality.environmentBake;
        if (!sameExtent(high.environmentCubeFaceExtent, ark::rhi::Extent2D{1024, 1024}) ||
            !sameExtent(high.irradianceCubeFaceExtent, ark::rhi::Extent2D{64, 64}) ||
            !sameExtent(high.specularCubeFaceExtent, ark::rhi::Extent2D{512, 512}) ||
            !sameExtent(high.brdfLutExtent, ark::rhi::Extent2D{512, 512}) ||
            !near(high.irradianceSampleDelta, 0.05f) ||
            high.specularPrefilterSampleCount != 256 ||
            high.brdfLutSampleCount != 2048 ||
            !high.enableEnvironmentCube ||
            !high.enableIrradiance ||
            !high.enableSpecularPrefilter ||
            !high.enableBrdfLut) {
            std::cerr << "High quality preset is invalid\n";
            return false;
        }

        return true;
    }

    bool validateParseFallbacks() {
        if (ark::parseRendererScenePreset("material_ball") !=
                ark::RendererScenePreset::MaterialBall ||
            ark::parseRendererScenePreset("SPECULAR") !=
                ark::RendererScenePreset::SpecularValidation ||
            ark::parseRendererScenePreset("bloom-validation") !=
                ark::RendererScenePreset::BloomValidation ||
            ark::parseRendererScenePreset("EMISSIVE_BLOOM") !=
                ark::RendererScenePreset::BloomValidation ||
            ark::parseRendererScenePreset("unknown",
                                          ark::RendererScenePreset::DebugOrientation) !=
                ark::RendererScenePreset::DebugOrientation ||
            ark::parseRendererQualityPreset("HIGH") != ark::RendererQualityPreset::High ||
            ark::parseRendererQualityPreset("unknown",
                                            ark::RendererQualityPreset::Low) !=
                ark::RendererQualityPreset::Low) {
            std::cerr << "Preset parse fallback behavior is invalid\n";
            return false;
        }

        return true;
    }

    bool validateSandboxLaunchOptions() {
        {
            constexpr std::array<std::string_view, 4> args{
                "--preset",
                "material-ball",
                "--quality",
                "low",
            };
            const ark::ApplicationDesc desc =
                ark::makeSandboxApplicationDesc(std::span<const std::string_view>{args});
            if (desc.defaultModelPath.filename() != "material_ball_validation_fixture.gltf" ||
                desc.useDebugOrientationEnvironment ||
                !sameExtent(desc.rendererQuality.environmentBake.brdfLutExtent,
                            ark::rhi::Extent2D{128, 128})) {
                std::cerr << "Sandbox material-ball low preset application desc is invalid\n";
                return false;
            }
        }

        {
            constexpr std::array<std::string_view, 5> args{
                "--preset=specular-validation",
                "--quality=high",
                "assets/models/custom_fixture.gltf",
                "assets/HDR/custom.hdr",
                "ignored_extra.gltf",
            };
            const ark::SandboxLaunchOptions options =
                ark::parseSandboxLaunchOptions(std::span<const std::string_view>{args});
            const ark::ApplicationDesc desc = ark::makeSandboxApplicationDesc(options);
            if (options.ignoredExtraPositionalArgumentCount != 1 ||
                desc.defaultModelPath != ark::Path{"assets/models/custom_fixture.gltf"} ||
                desc.defaultEnvironmentPath != ark::Path{"assets/HDR/custom.hdr"} ||
                desc.useDebugOrientationEnvironment ||
                desc.rendererQuality.environmentBake.specularPrefilterSampleCount != 256) {
                std::cerr << "Sandbox explicit path override behavior is invalid\n";
                return false;
            }
        }

        {
            constexpr std::array<std::string_view, 3> args{
                "--preset",
                "bloom-validation",
                "--debug-orientation",
            };
            const ark::ApplicationDesc desc =
                ark::makeSandboxApplicationDesc(std::span<const std::string_view>{args});
            if (desc.defaultModelPath.filename() != "bloom_validation_fixture.gltf" ||
                !desc.useDebugOrientationEnvironment) {
                std::cerr << "Sandbox debug orientation override behavior is invalid\n";
                return false;
            }
        }

        {
            constexpr std::array<std::string_view, 3> args{
                "--preset",
                "--quality",
                "--debug-orientation",
            };
            const ark::SandboxLaunchOptions options =
                ark::parseSandboxLaunchOptions(std::span<const std::string_view>{args});
            const ark::ApplicationDesc desc = ark::makeSandboxApplicationDesc(options);
            if (!options.missingPresetValue ||
                !options.missingQualityValue ||
                !desc.defaultModelPath.empty() ||
                !desc.useDebugOrientationEnvironment) {
                std::cerr << "Sandbox missing option value fallback behavior is invalid\n";
                return false;
            }
        }

        return true;
    }
} // namespace

int main() {
    return validateDefaultPreset() &&
                   validateScenePresets() &&
                   validateQualityPresets() &&
                   validateParseFallbacks() &&
                   validateSandboxLaunchOptions()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
