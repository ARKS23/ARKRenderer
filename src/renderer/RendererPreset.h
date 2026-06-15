#pragma once

#include "renderer/RendererQuality.h"
#include "renderer/SceneResource.h"

#include <string_view>

namespace ark {
    enum class RendererScenePreset {
        Default,
        MaterialBall,
        SpecularValidation,
        BloomValidation,
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

    struct ResolvedRendererPreset {
        SceneResourceLoadDesc scene;
        RendererQualityDesc quality;
    };

    RendererScenePreset parseRendererScenePreset(
        std::string_view name,
        RendererScenePreset fallback = RendererScenePreset::Default);
    RendererQualityPreset parseRendererQualityPreset(
        std::string_view name,
        RendererQualityPreset fallback = RendererQualityPreset::Default);
    ResolvedRendererPreset resolveRendererPreset(const RendererPresetDesc& desc);
} // namespace ark
