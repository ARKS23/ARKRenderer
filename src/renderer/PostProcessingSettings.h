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

    // Public settings contract: 描述一帧后处理参数，不暴露 Bloom/ToneMapping pass 的实现细节。
    struct PostProcessingSettings {
        BloomSettings bloom;
    };

    PostProcessingSettings sanitizePostProcessingSettings(const PostProcessingSettings& settings);
} // namespace ark
