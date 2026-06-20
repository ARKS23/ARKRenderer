#include "renderer/RendererPreset.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
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
        constexpr const char* ShadowProbeSpheresFixturePath =
            "assets/models/shadow_probe_spheres.gltf";
        constexpr float DefaultSponzaScale = 8.0f;

        glm::mat4 makeDefaultDamagedHelmetTransform() {
            return glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 2.9f, 0.6f}) *
                   glm::scale(glm::mat4{1.0f}, glm::vec3{2.4f});
        }

        glm::mat4 makeShadowProbeSpheresTransform() {
            // 高细分球体组用于观察接触阴影、受影和后续 cascade 覆盖，独立于历史 material ball golden fixture。
            return glm::translate(glm::mat4{1.0f}, glm::vec3{-2.0f, 6.85f, 0.0f}) *
                   glm::scale(glm::mat4{1.0f}, glm::vec3{1.5f});
        }

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
                scene.environmentIntensity = 0.55f;
                scene.overrideLighting = true;
                scene.lighting.mainLight.direction = glm::vec3{-0.75f, -0.45f, -0.35f};
                scene.lighting.mainLight.color = glm::vec3{1.50f, 1.34f, 1.10f};
                scene.lighting.ambientColor = glm::vec3{0.02f, 0.024f, 0.030f};
                scene.additionalModels.push_back(SceneAdditionalModelDesc{
                    DamagedHelmetFixturePath,
                    makeDefaultDamagedHelmetTransform(),
                    "DefaultSandboxDamagedHelmet",
                });
                scene.additionalModels.push_back(SceneAdditionalModelDesc{
                    ShadowProbeSpheresFixturePath,
                    makeShadowProbeSpheresTransform(),
                    "DefaultSandboxShadowProbeSpheres",
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
                scene.environmentIntensity = 0.55f;
                scene.overrideLighting = true;
                scene.lighting.mainLight.direction = glm::vec3{-0.75f, -0.45f, -0.35f};
                scene.lighting.mainLight.color = glm::vec3{1.50f, 1.34f, 1.10f};
                scene.lighting.ambientColor = glm::vec3{0.02f, 0.024f, 0.030f};
                scene.sceneName = "SponzaScene";
                scene.modelName = "SponzaModel";
                scene.environmentName = "SponzaEnvironment";
                break;
            case RendererScenePreset::ShadowValidation:
                scene.modelPath = SponzaFixturePath;
                scene.modelTransform = glm::scale(glm::mat4{1.0f}, glm::vec3{DefaultSponzaScale});
                scene.environmentIntensity = 0.35f;
                scene.overrideLighting = true;
                scene.lighting.mainLight.direction = glm::vec3{-0.75f, -0.45f, -0.35f};
                scene.lighting.mainLight.color = glm::vec3{1.70f, 1.52f, 1.24f};
                scene.lighting.ambientColor = glm::vec3{0.012f, 0.014f, 0.018f};
                scene.sceneName = "ShadowValidationScene";
                scene.modelName = "ShadowValidationModel";
                scene.environmentName = "ShadowValidationEnvironment";
                scene.environmentFallback = SceneEnvironmentFallbackPolicy::DebugOrientation;
                scene.additionalModels.push_back(SceneAdditionalModelDesc{
                    ShadowProbeSpheresFixturePath,
                    makeShadowProbeSpheresTransform(),
                    "ShadowValidationProbeSpheres",
                });
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

        RenderViewProfileDesc makeViewDesc(RendererScenePreset preset) {
            RenderViewProfileDesc view{};

            switch (preset) {
            case RendererScenePreset::Default:
            case RendererScenePreset::MaterialBall:
            case RendererScenePreset::SpecularValidation:
            case RendererScenePreset::BloomValidation:
            case RendererScenePreset::Sponza:
            case RendererScenePreset::ShadowValidation:
            case RendererScenePreset::DebugOrientation:
                break;
            }

            return view;
        }

        OrbitCameraProfileDesc makeInteractiveCameraDesc(RendererScenePreset preset) {
            OrbitCameraProfileDesc camera{};

            switch (preset) {
            case RendererScenePreset::Default:
            case RendererScenePreset::Sponza:
            case RendererScenePreset::ShadowValidation:
                camera.target = glm::vec3{0.0f, 3.2f, 0.6f};
                camera.distance = 26.0f;
                camera.yawRadians = glm::radians(18.0f);
                camera.pitchRadians = glm::radians(-12.0f);
                camera.nearPlane = 0.05f;
                camera.farPlane = 512.0f;
                break;
            case RendererScenePreset::MaterialBall:
            case RendererScenePreset::SpecularValidation:
            case RendererScenePreset::BloomValidation:
            case RendererScenePreset::DebugOrientation:
                break;
            }

            return camera;
        }

        OrbitCameraProfileDesc makeCaptureCameraDesc(RendererScenePreset preset) {
            OrbitCameraProfileDesc camera = makeInteractiveCameraDesc(preset);

            switch (preset) {
            case RendererScenePreset::Default:
                camera.target = glm::vec3{0.0f, 3.2f, 0.6f};
                camera.distance = 16.0f;
                camera.yawRadians = glm::radians(90.0f);
                camera.pitchRadians = glm::radians(-8.0f);
                camera.nearPlane = 0.05f;
                camera.farPlane = 512.0f;
                break;
            case RendererScenePreset::MaterialBall:
            case RendererScenePreset::SpecularValidation:
            case RendererScenePreset::BloomValidation:
            case RendererScenePreset::Sponza:
            case RendererScenePreset::ShadowValidation:
            case RendererScenePreset::DebugOrientation:
                break;
            }

            return camera;
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
        resolved.view = makeViewDesc(desc.scene);
        resolved.camera = makeInteractiveCameraDesc(desc.scene);
        resolved.captureCamera = makeCaptureCameraDesc(desc.scene);
        return resolved;
    }

    void applyOrbitCameraProfile(RenderView& view,
                                 const OrbitCameraProfileDesc& camera,
                                 rhi::Extent2D extent) {
        const float aspect =
            extent.height == 0 ? 1.0f : static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const float cosPitch = std::cos(camera.pitchRadians);
        const glm::vec3 forward = glm::normalize(glm::vec3{
            cosPitch * std::sin(camera.yawRadians),
            std::sin(camera.pitchRadians),
            cosPitch * std::cos(camera.yawRadians),
        });
        const glm::vec3 cameraPosition = camera.target - forward * camera.distance;
        glm::mat4 projection = glm::perspectiveRH_ZO(camera.verticalFovRadians,
                                                     aspect,
                                                     camera.nearPlane,
                                                     camera.farPlane);
        projection[1][1] *= -1.0f;
        view.setMatrices(glm::lookAt(cameraPosition, camera.target, glm::vec3{0.0f, 1.0f, 0.0f}),
                         projection,
                         cameraPosition);
    }
} // namespace ark
