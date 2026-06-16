#include "renderer/RendererPreset.h"

#include <algorithm>
#include <cctype>
#include <glm/ext/matrix_transform.hpp>
#include <string>

namespace ark {
    namespace {
        constexpr const char* MaterialBallFixturePath =
            "assets/models/material_ball_validation_fixture.gltf";
        constexpr const char* SpecularValidationFixturePath =
            "assets/models/specular_ibl_validation_fixture.gltf";
        constexpr const char* BloomValidationFixturePath =
            "assets/models/bloom_validation_fixture.gltf";
        constexpr const char* SponzaFixturePath =
            "assets/models/sponza/sponza.gltf";
        constexpr const char* DamagedHelmetFixturePath =
            "assets/models/DamagedHelmet/DamagedHelmet.gltf";
        constexpr float DefaultSponzaScale = 5.0f;

        std::string normalizePresetName(std::string_view name) {
            std::string normalized{name};
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
                if (ch == '_') {
                    return '-';
                }

                return static_cast<char>(std::tolower(ch));
            });
            return normalized;
        }

        SceneResourceLoadDesc makeSceneDesc(RendererScenePreset preset) {
            SceneResourceLoadDesc scene{};
            scene.modelFallback = SceneModelFallbackPolicy::DefaultSandboxModel;
            scene.environmentFallback = SceneEnvironmentFallbackPolicy::DefaultHdrThenProcedural;
            scene.sceneName = "DefaultSandboxScene";
            scene.modelName = "DefaultSandboxModel";
            scene.environmentName = "DefaultSandboxEnvironment";
            scene.environmentIntensity = 1.0f;

            switch (preset) {
            case RendererScenePreset::Default:
                scene.modelPath = SponzaFixturePath;
                scene.modelTransform = glm::scale(glm::mat4{1.0f}, glm::vec3{DefaultSponzaScale});
                scene.environmentIntensity = 0.65f;
                scene.overrideLighting = true;
                scene.lighting.mainLight.direction = glm::vec3{-0.75f, -0.45f, -0.35f};
                scene.lighting.mainLight.color = glm::vec3{1.35f, 1.22f, 1.02f};
                scene.lighting.ambientColor = glm::vec3{0.03f, 0.035f, 0.045f};
                scene.additionalModels.push_back(SceneAdditionalModelDesc{
                    DamagedHelmetFixturePath,
                    glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 2.9f, 0.6f}) *
                        glm::scale(glm::mat4{1.0f}, glm::vec3{2.4f}),
                    "DefaultSandboxDamagedHelmet",
                });
                break;
            case RendererScenePreset::MaterialBall:
                scene.modelPath = MaterialBallFixturePath;
                scene.sceneName = "MaterialBallScene";
                scene.modelName = "MaterialBallModel";
                scene.environmentName = "MaterialBallEnvironment";
                break;
            case RendererScenePreset::SpecularValidation:
                scene.modelPath = SpecularValidationFixturePath;
                scene.sceneName = "SpecularValidationScene";
                scene.modelName = "SpecularValidationModel";
                scene.environmentName = "SpecularValidationEnvironment";
                break;
            case RendererScenePreset::BloomValidation:
                scene.modelPath = BloomValidationFixturePath;
                scene.sceneName = "BloomValidationScene";
                scene.modelName = "BloomValidationModel";
                scene.environmentName = "BloomValidationEnvironment";
                break;
            case RendererScenePreset::Sponza:
                scene.modelPath = SponzaFixturePath;
                scene.modelTransform = glm::scale(glm::mat4{1.0f}, glm::vec3{DefaultSponzaScale});
                scene.environmentIntensity = 0.65f;
                scene.overrideLighting = true;
                scene.lighting.mainLight.direction = glm::vec3{-0.75f, -0.45f, -0.35f};
                scene.lighting.mainLight.color = glm::vec3{1.35f, 1.22f, 1.02f};
                scene.lighting.ambientColor = glm::vec3{0.03f, 0.035f, 0.045f};
                scene.sceneName = "SponzaScene";
                scene.modelName = "SponzaModel";
                scene.environmentName = "SponzaEnvironment";
                break;
            case RendererScenePreset::ShadowValidation:
                scene.modelPath = SponzaFixturePath;
                scene.modelTransform = glm::scale(glm::mat4{1.0f}, glm::vec3{DefaultSponzaScale});
                scene.environmentIntensity = 0.45f;
                scene.overrideLighting = true;
                scene.lighting.mainLight.direction = glm::vec3{-0.75f, -0.45f, -0.35f};
                scene.lighting.mainLight.color = glm::vec3{1.45f, 1.30f, 1.08f};
                scene.lighting.ambientColor = glm::vec3{0.02f, 0.025f, 0.03f};
                scene.sceneName = "ShadowValidationScene";
                scene.modelName = "ShadowValidationModel";
                scene.environmentName = "ShadowValidationEnvironment";
                scene.environmentFallback = SceneEnvironmentFallbackPolicy::DebugOrientation;
                break;
            case RendererScenePreset::DebugOrientation:
                scene.environmentFallback = SceneEnvironmentFallbackPolicy::DebugOrientation;
                scene.sceneName = "DebugOrientationScene";
                scene.modelName = "DebugOrientationModel";
                scene.environmentName = "DebugOrientationEnvironment";
                break;
            }

            return scene;
        }

        RendererQualityDesc makeQualityDesc(RendererQualityPreset preset) {
            RendererQualityDesc quality{};
            EnvironmentBakeQualityDesc& bake = quality.environmentBake;

            switch (preset) {
            case RendererQualityPreset::Low:
                bake.environmentCubeFaceExtent = rhi::Extent2D{256, 256};
                bake.irradianceCubeFaceExtent = rhi::Extent2D{16, 16};
                bake.specularCubeFaceExtent = rhi::Extent2D{128, 128};
                bake.brdfLutExtent = rhi::Extent2D{128, 128};
                bake.irradianceSampleDelta = 0.2f;
                bake.specularPrefilterSampleCount = 64;
                bake.brdfLutSampleCount = 512;
                break;
            case RendererQualityPreset::Default:
                break;
            case RendererQualityPreset::High:
                bake.environmentCubeFaceExtent = rhi::Extent2D{1024, 1024};
                bake.irradianceCubeFaceExtent = rhi::Extent2D{64, 64};
                bake.specularCubeFaceExtent = rhi::Extent2D{512, 512};
                bake.brdfLutExtent = rhi::Extent2D{512, 512};
                bake.irradianceSampleDelta = 0.05f;
                bake.specularPrefilterSampleCount = 256;
                bake.brdfLutSampleCount = 2048;
                break;
            }

            return sanitizeRendererQualityDesc(quality);
        }
    } // namespace

    RendererScenePreset parseRendererScenePreset(std::string_view name, RendererScenePreset fallback) {
        const std::string normalized = normalizePresetName(name);
        if (normalized == "default") {
            return RendererScenePreset::Default;
        }

        if (normalized == "material-ball" || normalized == "materialball") {
            return RendererScenePreset::MaterialBall;
        }

        if (normalized == "specular-validation" ||
            normalized == "specular-ibl-validation" ||
            normalized == "specular") {
            return RendererScenePreset::SpecularValidation;
        }

        if (normalized == "bloom-validation" ||
            normalized == "bloomvalidation" ||
            normalized == "bloom" ||
            normalized == "emissive-bloom" ||
            normalized == "emissivebloom") {
            return RendererScenePreset::BloomValidation;
        }

        if (normalized == "sponza" || normalized == "sponza-validation") {
            return RendererScenePreset::Sponza;
        }

        if (normalized == "shadow-validation" ||
            normalized == "shadow" ||
            normalized == "shadows") {
            return RendererScenePreset::ShadowValidation;
        }

        if (normalized == "debug-orientation" || normalized == "debug") {
            return RendererScenePreset::DebugOrientation;
        }

        return fallback;
    }

    RendererQualityPreset parseRendererQualityPreset(std::string_view name,
                                                     RendererQualityPreset fallback) {
        const std::string normalized = normalizePresetName(name);
        if (normalized == "low") {
            return RendererQualityPreset::Low;
        }

        if (normalized == "default") {
            return RendererQualityPreset::Default;
        }

        if (normalized == "high") {
            return RendererQualityPreset::High;
        }

        return fallback;
    }

    ResolvedRendererPreset resolveRendererPreset(const RendererPresetDesc& desc) {
        ResolvedRendererPreset resolved{};
        resolved.scene = makeSceneDesc(desc.scene);
        resolved.quality = makeQualityDesc(desc.quality);
        return resolved;
    }
} // namespace ark
