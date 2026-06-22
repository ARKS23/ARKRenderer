#pragma once

#include "core/Types.h"
#include "rhi/RHICommon.h"

namespace ark {
    inline constexpr rhi::Extent2D DefaultEnvironmentBakeCubeFaceExtent{512, 512};
    inline constexpr rhi::Extent2D DefaultEnvironmentBakeIrradianceFaceExtent{32, 32};
    inline constexpr rhi::Extent2D DefaultEnvironmentBakeSpecularFaceExtent{256, 256};
    inline constexpr rhi::Extent2D DefaultEnvironmentBakeBrdfLutExtent{256, 256};
    inline constexpr float DefaultEnvironmentBakeIrradianceSampleDelta = 0.1f;
    inline constexpr u32 DefaultEnvironmentBakeSpecularSampleCount = 128;
    inline constexpr u32 DefaultEnvironmentBakeBrdfLutSampleCount = 1024;

    inline constexpr u32 MinEnvironmentBakeCubeFaceSize = 16;
    inline constexpr u32 MaxEnvironmentBakeCubeFaceSize = 2048;
    inline constexpr u32 MinEnvironmentBakeIrradianceFaceSize = 8;
    inline constexpr u32 MaxEnvironmentBakeIrradianceFaceSize = 256;
    inline constexpr u32 MinEnvironmentBakeSpecularFaceSize = 16;
    inline constexpr u32 MaxEnvironmentBakeSpecularFaceSize = 2048;
    inline constexpr u32 MinEnvironmentBakeBrdfLutSize = 16;
    inline constexpr u32 MaxEnvironmentBakeBrdfLutSize = 1024;
    inline constexpr u32 MinEnvironmentBakeSampleCount = 1;
    inline constexpr u32 MaxEnvironmentBakeSampleCount = 4096;
    inline constexpr float MinEnvironmentBakeIrradianceSampleDelta = 0.005f;
    inline constexpr float MaxEnvironmentBakeIrradianceSampleDelta = 1.0f;

    // Public-ish quality contract: 描述环境贴图 bake 的资源尺寸和采样质量。
    // 这些值可以来自 preset/UI/engine settings，进入 renderer 前统一经过 sanitize。
    struct EnvironmentBakeQualityDesc {
        rhi::Extent2D environmentCubeFaceExtent = DefaultEnvironmentBakeCubeFaceExtent;
        rhi::Extent2D irradianceCubeFaceExtent = DefaultEnvironmentBakeIrradianceFaceExtent;
        rhi::Extent2D specularCubeFaceExtent = DefaultEnvironmentBakeSpecularFaceExtent;
        rhi::Extent2D brdfLutExtent = DefaultEnvironmentBakeBrdfLutExtent;

        float irradianceSampleDelta = DefaultEnvironmentBakeIrradianceSampleDelta;
        u32 specularPrefilterSampleCount = DefaultEnvironmentBakeSpecularSampleCount;
        u32 brdfLutSampleCount = DefaultEnvironmentBakeBrdfLutSampleCount;

        bool enableEnvironmentCube = true;
        bool enableIrradiance = true;
        bool enableSpecularPrefilter = true;
        bool enableBrdfLut = true;
    };

    // RendererQualityDesc 只保存可公开调节的质量参数，不暴露具体 pass 或 generator 实现。
    struct RendererQualityDesc {
        EnvironmentBakeQualityDesc environmentBake;
    };

    RendererQualityDesc sanitizeRendererQualityDesc(const RendererQualityDesc& desc);
} // namespace ark
