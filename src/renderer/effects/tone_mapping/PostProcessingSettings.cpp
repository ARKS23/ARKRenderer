#include "renderer/effects/tone_mapping/PostProcessingSettings.h"

#include <algorithm>
#include <cmath>

namespace ark {
    namespace {
        constexpr float DefaultBloomScatter = 0.6f;
        constexpr float DefaultBloomThreshold = 1.0f;
        constexpr float DefaultBloomSoftKnee = 0.5f;
        constexpr u32 MinBloomMipCount = 1;
        constexpr u32 MaxBloomMipCount = 12;

        float finiteOr(float value, float fallback) {
            return std::isfinite(value) ? value : fallback;
        }
    } // namespace

    PostProcessingSettings sanitizePostProcessingSettings(const PostProcessingSettings& settings) {
        PostProcessingSettings sanitized = settings;
        BloomSettings& bloom = sanitized.bloom;

        bloom.intensity = std::clamp(finiteOr(bloom.intensity, 0.0f), 0.0f, 10.0f);
        bloom.scatter = std::clamp(finiteOr(bloom.scatter, DefaultBloomScatter), 0.0f, 1.0f);
        bloom.threshold = std::clamp(finiteOr(bloom.threshold, DefaultBloomThreshold), 0.0f, 64.0f);
        bloom.softKnee = std::clamp(finiteOr(bloom.softKnee, DefaultBloomSoftKnee), 0.0f, 1.0f);
        bloom.maxMipCount = std::clamp(bloom.maxMipCount, MinBloomMipCount, MaxBloomMipCount);

        if (bloom.intensity <= 0.0f) {
            bloom.enabled = false;
        }

        return sanitized;
    }
} // namespace ark
