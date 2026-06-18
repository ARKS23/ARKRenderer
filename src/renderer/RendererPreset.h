#pragma once

#include "renderer/RenderView.h"
#include "renderer/RendererQuality.h"
#include "renderer/SceneResource.h"

#include <glm/trigonometric.hpp>

#include <string_view>

namespace ark {
    enum class RendererScenePreset {
        Default,
        MaterialBall,
        SpecularValidation,
        BloomValidation,
        Sponza,
        ShadowValidation,
        DebugOrientation,
    };

    enum class RendererQualityPreset {
        Low,
        Default,
        High,
    };

    struct RendererPresetDesc {
        RendererScenePreset scene = RendererScenePreset::Default;
        RendererQualityPreset quality = RendererQualityPreset::Default;
    };

    struct RenderViewProfileDesc {
        ToneMappingSettings toneMapping{1.0f, 2.2f, ToneMappingOperator::ACES};
        PostProcessingSettings postProcessing{
            BloomSettings{true, 0.12f, 0.6f, 1.0f, 0.5f, 6},
        };
        ShadowSettings shadows{
            true,
            1.0f,
            0.0015f,
            2048,
            64.0f,
            0.1f,
            256.0f,
            96.0f,
            true,
            true,
            ShadowFilterMode::Pcf3x3,
            1.0f,
        };
    };

    struct OrbitCameraProfileDesc {
        glm::vec3 target{0.0f, 0.0f, 0.0f};
        float distance = 4.0f;
        float yawRadians = 0.0f;
        float pitchRadians = 0.0f;
        float verticalFovRadians = glm::radians(60.0f);
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
    };

    struct ResolvedRendererPreset {
        SceneResourceLoadDesc scene;
        RendererQualityDesc quality;
        RenderViewProfileDesc view;
        OrbitCameraProfileDesc camera;
        OrbitCameraProfileDesc captureCamera;
    };

    RendererScenePreset parseRendererScenePreset(
        std::string_view name,
        RendererScenePreset fallback = RendererScenePreset::Default);
    RendererQualityPreset parseRendererQualityPreset(
        std::string_view name,
        RendererQualityPreset fallback = RendererQualityPreset::Default);
    ResolvedRendererPreset resolveRendererPreset(const RendererPresetDesc& desc);
    void applyOrbitCameraProfile(RenderView& view,
                                 const OrbitCameraProfileDesc& camera,
                                 rhi::Extent2D extent);
} // namespace ark
