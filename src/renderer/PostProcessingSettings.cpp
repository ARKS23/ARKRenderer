#include "renderer/PostProcessingSettings.h"

#include <algorithm>
#include <cmath>

namespace ark {
    namespace {
        constexpr float DefaultBloomScatter = 0.6f;
        constexpr float DefaultBloomThreshold = 1.0f;
        constexpr float DefaultBloomSoftKnee = 0.5f;
        constexpr u32 MinBloomMipCount = 1;
        constexpr u32 MaxBloomMipCount = 12;
        constexpr float DefaultSsaoRadius = 0.6f;
        constexpr float DefaultSsaoIntensity = 1.0f;
        constexpr float DefaultSsaoBias = 0.025f;
        constexpr float DefaultSsaoPower = 1.5f;
        constexpr float DefaultSsaoResolutionScale = 1.0f;
        constexpr u32 MinSsaoSampleCount = 4;
        constexpr u32 MaxSsaoSampleCount = 64;
        constexpr u32 MaxSsaoBlurRadius = 8;

        float finiteOr(float value, float fallback) {
            return std::isfinite(value) ? value : fallback;
        }
    } // namespace

    bool isValidSsaoDebugMode(SsaoDebugMode mode) {
        switch (mode) {
            case SsaoDebugMode::None:
            case SsaoDebugMode::Occlusion:
            case SsaoDebugMode::NormalDepth:
                return true;
        }
        return false;
    }

    PostProcessingSettings sanitizePostProcessingSettings(const PostProcessingSettings& settings) {
        PostProcessingSettings sanitized = settings;
        BloomSettings& bloom = sanitized.bloom;
        SsaoSettings& ssao = sanitized.ssao;

        bloom.intensity = std::clamp(finiteOr(bloom.intensity, 0.0f), 0.0f, 10.0f);
        bloom.scatter = std::clamp(finiteOr(bloom.scatter, DefaultBloomScatter), 0.0f, 1.0f);
        bloom.threshold = std::clamp(finiteOr(bloom.threshold, DefaultBloomThreshold), 0.0f, 64.0f);
        bloom.softKnee = std::clamp(finiteOr(bloom.softKnee, DefaultBloomSoftKnee), 0.0f, 1.0f);
        bloom.maxMipCount = std::clamp(bloom.maxMipCount, MinBloomMipCount, MaxBloomMipCount);

        if (bloom.intensity <= 0.0f) {
            bloom.enabled = false;
        }

        // SSAO 的参数会直接影响采样循环和临时贴图尺寸，进入 pass 前必须先收敛到稳定范围。
        ssao.radius = std::clamp(finiteOr(ssao.radius, DefaultSsaoRadius), 0.01f, 8.0f);
        ssao.intensity = std::clamp(finiteOr(ssao.intensity, DefaultSsaoIntensity), 0.0f, 4.0f);
        ssao.bias = std::clamp(finiteOr(ssao.bias, DefaultSsaoBias), 0.0f, 0.5f);
        ssao.power = std::clamp(finiteOr(ssao.power, DefaultSsaoPower), 0.25f, 8.0f);
        ssao.sampleCount = std::clamp(ssao.sampleCount, MinSsaoSampleCount, MaxSsaoSampleCount);
        ssao.blurRadius = std::clamp(ssao.blurRadius, 0u, MaxSsaoBlurRadius);
        ssao.resolutionScale =
            std::clamp(finiteOr(ssao.resolutionScale, DefaultSsaoResolutionScale), 0.25f, 1.0f);
        if (!isValidSsaoDebugMode(ssao.debugMode)) {
            ssao.debugMode = SsaoDebugMode::None;
        }
        if (ssao.intensity <= 0.0f) {
            ssao.enabled = false;
        }

        return sanitized;
    }
} // namespace ark
