#pragma once

#include "core/Types.h"

namespace ark {
    struct BloomSettings {
        bool enabled = false;
        float intensity = 0.0f;
        float scatter = 0.6f;
        float threshold = 1.0f;
        float softKnee = 0.5f;
        u32 maxMipCount = 6;
    };

    struct PostProcessingSettings {
        BloomSettings bloom;
    };

    PostProcessingSettings sanitizePostProcessingSettings(const PostProcessingSettings& settings);
} // namespace ark
