#pragma once

#include "core/Types.h"
#include "renderer/settings/ShadowConstants.h"

#include <algorithm>

namespace ark {
    enum class ShadowDebugMode : u32 {
        None = 0,
        CascadeColor = 1,
        ShadowFactor = 2,
        LightDepth = 3,
    };

    struct ShadowDebugSettings {
        bool enabled = false;
        ShadowDebugMode mode = ShadowDebugMode::None;
        bool showPreview = false;
        u32 previewCascadeIndex = 0;
    };

    inline constexpr float ShadowDebugOverlayAlpha = 0.35f;

    inline bool isValidShadowDebugMode(ShadowDebugMode mode) {
        switch (mode) {
        case ShadowDebugMode::None:
        case ShadowDebugMode::CascadeColor:
        case ShadowDebugMode::ShadowFactor:
        case ShadowDebugMode::LightDepth:
            return true;
        default:
            return false;
        }
    }

    inline ShadowDebugSettings sanitizeShadowDebugSettings(ShadowDebugSettings settings) {
        settings.previewCascadeIndex =
            std::min(settings.previewCascadeIndex, MaxShadowCascadeCount - 1u);

        if (!settings.enabled) {
            settings.mode = ShadowDebugMode::None;
            settings.showPreview = false;
            return settings;
        }

        if (!isValidShadowDebugMode(settings.mode)) {
            settings.mode = ShadowDebugMode::None;
        }
        return settings;
    }
} // namespace ark
