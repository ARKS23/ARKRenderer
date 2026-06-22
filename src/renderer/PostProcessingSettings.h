#pragma once

#include "core/Types.h"

namespace ark {
    enum class SsaoDebugMode : u32 {
        None = 0,
        Occlusion = 1,
        NormalDepth = 2,
    };

    struct BloomSettings {
        bool enabled = false;
        float intensity = 0.0f;
        float scatter = 0.6f;
        float threshold = 1.0f;
        float softKnee = 0.5f;
        u32 maxMipCount = 6;
    };

    struct SsaoSettings {
        bool enabled = false;
        float radius = 0.6f;
        float intensity = 1.0f;
        float bias = 0.025f;
        float power = 1.5f;
        u32 sampleCount = 16;
        u32 blurRadius = 2;
        float resolutionScale = 1.0f;
        SsaoDebugMode debugMode = SsaoDebugMode::None;
    };

    // Public settings contract: 描述一帧后处理参数，不暴露具体 pass 的内部资源和管线细节。
    struct PostProcessingSettings {
        BloomSettings bloom;
        SsaoSettings ssao;
    };

    bool isValidSsaoDebugMode(SsaoDebugMode mode);
    PostProcessingSettings sanitizePostProcessingSettings(const PostProcessingSettings& settings);
} // namespace ark
