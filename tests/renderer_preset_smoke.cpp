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

        if (resolved.scene.modelPath.filename() != "sponza.gltf" ||
            resolved.scene.additionalModels.size() != 2 ||
            resolved.scene.additionalModels[0].modelPath.filename() != "DamagedHelmet.gltf" ||
            resolved.scene.additionalModels[1].modelPath.filename() != "shadow_probe_spheres.gltf" ||
            !near(resolved.scene.modelTransform[0][0], 8.0f) ||
            !near(resolved.scene.additionalModels[0].transform[0][0], 2.4f) ||
            !near(resolved.scene.additionalModels[1].transform[0][0], 1.5f) ||
            !near(resolved.scene.additionalModels[1].transform[3][1], 6.85f) ||
            !resolved.scene.environmentPath.empty() ||
            resolved.scene.modelFallback != ark::SceneModelFallbackPolicy::DefaultSandboxModel ||
            resolved.scene.environmentFallback !=
                ark::SceneEnvironmentFallbackPolicy::DefaultHdrThenProcedural ||
            resolved.scene.sceneName != "DefaultSandboxScene" ||
            resolved.scene.modelName != "DefaultSandboxModel" ||
            resolved.scene.environmentName != "DefaultSandboxEnvironment" ||
            !near(resolved.scene.environmentIntensity, 0.55f) ||
            !resolved.scene.overrideLighting ||
            !near(resolved.scene.lighting.mainLight.direction.y, -0.45f) ||
            resolved.view.shadows.cascades.enabled ||
            resolved.view.shadows.cascades.cascadeCount != ark::MaxShadowCascadeCount ||
            !near(resolved.view.shadows.cascades.splitLambda, 0.65f) ||
            !near(resolved.view.shadows.cascades.maxDistance, 80.0f) ||
            resolved.view.shadows.cascades.cascadeExtent != 2048 ||
            !resolved.view.shadows.cascades.stabilize) {
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

        preset.scene = ark::RendererScenePreset::Sponza;
        resolved = ark::resolveRendererPreset(preset);
        if (resolved.scene.modelPath.filename() != "sponza.gltf" ||
            !resolved.scene.additionalModels.empty() ||
            !near(resolved.scene.modelTransform[0][0], 8.0f) ||
            !resolved.scene.overrideLighting ||
            resolved.scene.modelFallback != ark::SceneModelFallbackPolicy::DefaultSandboxModel ||
            resolved.scene.environmentFallback !=
                ark::SceneEnvironmentFallbackPolicy::DefaultHdrThenProcedural ||
            resolved.scene.sceneName != "SponzaScene") {
            std::cerr << "Sponza scene preset is invalid\n";
            return false;
        }

        preset.scene = ark::RendererScenePreset::ShadowValidation;
        resolved = ark::resolveRendererPreset(preset);
        if (resolved.scene.modelPath.filename() != "sponza.gltf" ||
            resolved.scene.additionalModels.size() != 1 ||
            resolved.scene.additionalModels.front().modelPath.filename() != "shadow_probe_spheres.gltf" ||
            resolved.scene.modelFallback != ark::SceneModelFallbackPolicy::DefaultSandboxModel ||
            resolved.scene.environmentFallback !=
                ark::SceneEnvironmentFallbackPolicy::DebugOrientation ||
            resolved.scene.sceneName != "ShadowValidationScene") {
            std::cerr << "Shadow validation scene preset is invalid\n";
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
            ark::parseRendererScenePreset("sponza-validation") !=
                ark::RendererScenePreset::Sponza ||
            ark::parseRendererScenePreset("shadows") !=
                ark::RendererScenePreset::ShadowValidation ||
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
            const ark::ApplicationDesc desc =
                ark::makeSandboxApplicationDesc(std::span<const std::string_view>{});
            if (desc.defaultScene.modelPath.filename() != "sponza.gltf" ||
                desc.defaultScene.additionalModels.size() != 2 ||
                desc.defaultScene.additionalModels[0].modelPath.filename() != "DamagedHelmet.gltf" ||
                desc.defaultScene.additionalModels[1].modelPath.filename() != "shadow_probe_spheres.gltf" ||
                !near(desc.defaultScene.modelTransform[0][0], 8.0f) ||
                !near(desc.defaultScene.additionalModels[0].transform[0][0], 2.4f) ||
                !near(desc.defaultScene.additionalModels[1].transform[0][0], 1.5f) ||
                !near(desc.defaultScene.additionalModels[1].transform[3][1], 6.85f) ||
                !desc.defaultScene.overrideLighting ||
                !near(desc.defaultScene.environmentIntensity, 0.55f) ||
                desc.camera.distance < 22.0f ||
                desc.camera.distance > 30.0f ||
                desc.camera.target.y < 2.5f ||
                desc.camera.target.y > 3.8f ||
                desc.camera.farPlane < 500.0f ||
                desc.camera.farPlane > 560.0f ||
                desc.view.toneMapping.operatorType != ark::ToneMappingOperator::ACES ||
                !desc.view.postProcessing.bloom.enabled ||
                !near(desc.view.postProcessing.bloom.intensity, 0.12f) ||
                !desc.view.postProcessing.ssao.enabled ||
                !near(desc.view.postProcessing.ssao.radius, 0.85f) ||
                desc.view.postProcessing.ssao.sampleCount != 24 ||
                !desc.view.shadows.enabled ||
                !near(desc.view.shadows.strength, 1.0f) ||
                desc.view.shadows.mapExtent != 2048 ||
                !near(desc.view.shadows.orthographicHalfExtent, 64.0f) ||
                !near(desc.view.shadows.farPlane, 256.0f) ||
                !near(desc.view.shadows.lightDistance, 96.0f) ||
                !desc.view.shadows.fitSceneBounds ||
                !desc.view.shadows.stabilizeProjection ||
                desc.view.shadows.filterMode != ark::ShadowFilterMode::Pcf3x3 ||
                !near(desc.view.shadows.filterRadiusTexels, 1.0f)) {
                std::cerr << "Sandbox default scene application desc is invalid\n";
                return false;
            }
        }

        {
            constexpr std::array<std::string_view, 6> args{
                "--preset",
                "material-ball",
                "--quality",
                "low",
                "--tone-mapping",
                "aces",
            };
            const ark::ApplicationDesc desc =
                ark::makeSandboxApplicationDesc(std::span<const std::string_view>{args});
            if (desc.defaultScene.modelPath.filename() != "material_ball_validation_fixture.gltf" ||
                desc.useDebugOrientationEnvironment ||
                desc.view.toneMapping.operatorType != ark::ToneMappingOperator::ACES ||
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
                desc.defaultScene.modelPath != ark::Path{"assets/models/custom_fixture.gltf"} ||
                !desc.defaultScene.additionalModels.empty() ||
                desc.defaultScene.environmentPath != ark::Path{"assets/HDR/custom.hdr"} ||
                desc.useDebugOrientationEnvironment ||
                !near(desc.camera.distance, 4.0f) ||
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
            if (desc.defaultScene.modelPath.filename() != "bloom_validation_fixture.gltf" ||
                !desc.useDebugOrientationEnvironment) {
                std::cerr << "Sandbox debug orientation override behavior is invalid\n";
                return false;
            }
        }

        {
            constexpr std::array<std::string_view, 10> args{
                "--preset",
                "shadow-validation",
                "--shadow-strength=0.45",
                "--shadow-bias",
                "0.004",
                "--shadow-extent",
                "2048",
                "--shadow-filter=pcf5x5",
                "--shadow-filter-radius",
                "2.0",
            };
            const ark::SandboxLaunchOptions options =
                ark::parseSandboxLaunchOptions(std::span<const std::string_view>{args});
            const ark::ApplicationDesc desc = ark::makeSandboxApplicationDesc(options);
            if (desc.defaultScene.modelPath.filename() != "sponza.gltf" ||
                !desc.useDebugOrientationEnvironment ||
                !desc.view.shadows.enabled ||
                !near(desc.view.shadows.strength, 0.45f) ||
                !near(desc.view.shadows.bias, 0.004f) ||
                desc.view.shadows.mapExtent != 2048 ||
                desc.view.shadows.orthographicHalfExtent < 64.0f ||
                desc.view.shadows.farPlane < 256.0f ||
                desc.view.shadows.lightDistance < 96.0f ||
                !desc.view.shadows.fitSceneBounds ||
                desc.view.shadows.filterMode != ark::ShadowFilterMode::Pcf5x5 ||
                !near(desc.view.shadows.filterRadiusTexels, 2.0f)) {
                std::cerr << "Sandbox shadow validation preset application desc is invalid\n";
                return false;
            }
        }

        {
            constexpr std::array<std::string_view, 3> args{
                "--preset=sponza",
                "--shadows",
                "--shadow-bounds=20.0",
            };
            const ark::ApplicationDesc desc =
                ark::makeSandboxApplicationDesc(std::span<const std::string_view>{args});
            if (desc.defaultScene.modelPath.filename() != "sponza.gltf" ||
                desc.useDebugOrientationEnvironment ||
                !desc.view.shadows.enabled ||
                !near(desc.view.shadows.orthographicHalfExtent, 20.0f) ||
                desc.view.shadows.fitSceneBounds) {
                std::cerr << "Sandbox Sponza shadow options are invalid\n";
                return false;
            }
        }

        {
            constexpr std::array<std::string_view, 4> args{
                "--preset",
                "--quality",
                "--tone-mapping",
                "--debug-orientation",
            };
            const ark::SandboxLaunchOptions options =
                ark::parseSandboxLaunchOptions(std::span<const std::string_view>{args});
            const ark::ApplicationDesc desc = ark::makeSandboxApplicationDesc(options);
            if (!options.missingPresetValue ||
                !options.missingQualityValue ||
                !options.missingToneMappingValue ||
                desc.defaultScene.modelPath.filename() != "sponza.gltf" ||
                desc.defaultScene.additionalModels.size() != 2 ||
                !desc.useDebugOrientationEnvironment) {
                std::cerr << "Sandbox missing option value fallback behavior is invalid\n";
                return false;
            }
        }

        {
            constexpr std::array<std::string_view, 6> args{
                "--shadow-strength",
                "--shadow-bias",
                "--shadow-extent",
                "--shadow-bounds",
                "--shadow-filter",
                "--shadow-filter-radius",
            };
            const ark::SandboxLaunchOptions options =
                ark::parseSandboxLaunchOptions(std::span<const std::string_view>{args});
            if (!options.missingShadowStrengthValue ||
                !options.missingShadowBiasValue ||
                !options.missingShadowExtentValue ||
                !options.missingShadowBoundsValue ||
                !options.missingShadowFilterValue ||
                !options.missingShadowFilterRadiusValue) {
                std::cerr << "Sandbox missing shadow option value fallback behavior is invalid\n";
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
