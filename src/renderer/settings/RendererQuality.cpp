#include "renderer/settings/RendererQuality.h"

#include <algorithm>
#include <cmath>

namespace ark {
    namespace {
        u32 clampU32(u32 value, u32 minValue, u32 maxValue) {
            return std::clamp(value, minValue, maxValue);
        }

        float clampFloat(float value, float minValue, float maxValue) {
            return std::clamp(value, minValue, maxValue);
        }

        float sanitizeIrradianceSampleDelta(float value) {
            if (!std::isfinite(value) || value <= 0.0f) {
                return DefaultEnvironmentBakeIrradianceSampleDelta;
            }

            return clampFloat(value,
                              MinEnvironmentBakeIrradianceSampleDelta,
                              MaxEnvironmentBakeIrradianceSampleDelta);
        }

        rhi::Extent2D sanitizeSquareExtent(rhi::Extent2D extent,
                                           rhi::Extent2D defaultExtent,
                                           u32 minSize,
                                           u32 maxSize) {
            if (!rhi::isValidExtent(extent)) {
                return defaultExtent;
            }

            const u32 squareSize = clampU32(std::min(extent.width, extent.height), minSize, maxSize);
            return rhi::Extent2D{squareSize, squareSize};
        }
    } // namespace

    RendererQualityDesc sanitizeRendererQualityDesc(const RendererQualityDesc& desc) {
        RendererQualityDesc result = desc;
        EnvironmentBakeQualityDesc& bake = result.environmentBake;

        bake.environmentCubeFaceExtent =
            sanitizeSquareExtent(bake.environmentCubeFaceExtent,
                                 DefaultEnvironmentBakeCubeFaceExtent,
                                 MinEnvironmentBakeCubeFaceSize,
                                 MaxEnvironmentBakeCubeFaceSize);
        bake.irradianceCubeFaceExtent =
            sanitizeSquareExtent(bake.irradianceCubeFaceExtent,
                                 DefaultEnvironmentBakeIrradianceFaceExtent,
                                 MinEnvironmentBakeIrradianceFaceSize,
                                 MaxEnvironmentBakeIrradianceFaceSize);
        bake.specularCubeFaceExtent =
            sanitizeSquareExtent(bake.specularCubeFaceExtent,
                                 DefaultEnvironmentBakeSpecularFaceExtent,
                                 MinEnvironmentBakeSpecularFaceSize,
                                 MaxEnvironmentBakeSpecularFaceSize);
        bake.brdfLutExtent = sanitizeSquareExtent(bake.brdfLutExtent,
                                                  DefaultEnvironmentBakeBrdfLutExtent,
                                                  MinEnvironmentBakeBrdfLutSize,
                                                  MaxEnvironmentBakeBrdfLutSize);

        bake.irradianceSampleDelta = sanitizeIrradianceSampleDelta(bake.irradianceSampleDelta);
        bake.specularPrefilterSampleCount = clampU32(bake.specularPrefilterSampleCount,
                                                     MinEnvironmentBakeSampleCount,
                                                     MaxEnvironmentBakeSampleCount);
        bake.brdfLutSampleCount = clampU32(bake.brdfLutSampleCount,
                                           MinEnvironmentBakeSampleCount,
                                           MaxEnvironmentBakeSampleCount);

        if (!bake.enableEnvironmentCube) {
            bake.enableIrradiance = false;
            bake.enableSpecularPrefilter = false;
            bake.enableBrdfLut = false;
        }

        if (!bake.enableSpecularPrefilter) {
            bake.enableBrdfLut = false;
        }

        return result;
    }
} // namespace ark
