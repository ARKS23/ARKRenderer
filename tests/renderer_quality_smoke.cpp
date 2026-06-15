#include "renderer/Renderer.h"
#include "renderer/RendererQuality.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
    bool sameExtent(ark::rhi::Extent2D lhs, ark::rhi::Extent2D rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    bool near(float lhs, float rhs, float epsilon = 0.0001f) {
        return std::fabs(lhs - rhs) <= epsilon;
    }

    bool validateDefaultQuality() {
        const ark::RendererQualityDesc quality = ark::sanitizeRendererQualityDesc(ark::RendererQualityDesc{});
        const ark::EnvironmentBakeQualityDesc& bake = quality.environmentBake;

        if (!sameExtent(bake.environmentCubeFaceExtent, ark::DefaultEnvironmentBakeCubeFaceExtent) ||
            !sameExtent(bake.irradianceCubeFaceExtent, ark::DefaultEnvironmentBakeIrradianceFaceExtent) ||
            !sameExtent(bake.specularCubeFaceExtent, ark::DefaultEnvironmentBakeSpecularFaceExtent) ||
            !sameExtent(bake.brdfLutExtent, ark::DefaultEnvironmentBakeBrdfLutExtent) ||
            !near(bake.irradianceSampleDelta, ark::DefaultEnvironmentBakeIrradianceSampleDelta) ||
            bake.specularPrefilterSampleCount != ark::DefaultEnvironmentBakeSpecularSampleCount ||
            bake.brdfLutSampleCount != ark::DefaultEnvironmentBakeBrdfLutSampleCount ||
            !bake.enableEnvironmentCube ||
            !bake.enableIrradiance ||
            !bake.enableSpecularPrefilter ||
            !bake.enableBrdfLut) {
            std::cerr << "RendererQuality default values are invalid\n";
            return false;
        }

        ark::RendererDesc rendererDesc{};
        const ark::RendererQualityDesc rendererDefaultQuality =
            ark::sanitizeRendererQualityDesc(rendererDesc.quality);
        if (!sameExtent(rendererDefaultQuality.environmentBake.specularCubeFaceExtent,
                        ark::DefaultEnvironmentBakeSpecularFaceExtent)) {
            std::cerr << "RendererDesc default quality is invalid\n";
            return false;
        }

        return true;
    }

    bool validateCustomQuality() {
        ark::RendererQualityDesc quality{};
        quality.environmentBake.environmentCubeFaceExtent = ark::rhi::Extent2D{1024, 1024};
        quality.environmentBake.irradianceCubeFaceExtent = ark::rhi::Extent2D{64, 64};
        quality.environmentBake.specularCubeFaceExtent = ark::rhi::Extent2D{512, 512};
        quality.environmentBake.brdfLutExtent = ark::rhi::Extent2D{128, 128};
        quality.environmentBake.irradianceSampleDelta = 0.05f;
        quality.environmentBake.specularPrefilterSampleCount = 512;
        quality.environmentBake.brdfLutSampleCount = 2048;

        const ark::RendererQualityDesc sanitized = ark::sanitizeRendererQualityDesc(quality);
        const ark::EnvironmentBakeQualityDesc& bake = sanitized.environmentBake;

        if (!sameExtent(bake.environmentCubeFaceExtent, ark::rhi::Extent2D{1024, 1024}) ||
            !sameExtent(bake.irradianceCubeFaceExtent, ark::rhi::Extent2D{64, 64}) ||
            !sameExtent(bake.specularCubeFaceExtent, ark::rhi::Extent2D{512, 512}) ||
            !sameExtent(bake.brdfLutExtent, ark::rhi::Extent2D{128, 128}) ||
            !near(bake.irradianceSampleDelta, 0.05f) ||
            bake.specularPrefilterSampleCount != 512 ||
            bake.brdfLutSampleCount != 2048) {
            std::cerr << "RendererQuality custom values were not preserved\n";
            return false;
        }

        return true;
    }

    bool validateClampQuality() {
        ark::RendererQualityDesc quality{};
        quality.environmentBake.environmentCubeFaceExtent = ark::rhi::Extent2D{4096, 1024};
        quality.environmentBake.irradianceCubeFaceExtent = ark::rhi::Extent2D{1, 2};
        quality.environmentBake.specularCubeFaceExtent = ark::rhi::Extent2D{0, 128};
        quality.environmentBake.brdfLutExtent = ark::rhi::Extent2D{2048, 4096};
        quality.environmentBake.irradianceSampleDelta = 0.0f;
        quality.environmentBake.specularPrefilterSampleCount = 0;
        quality.environmentBake.brdfLutSampleCount = 100000;

        const ark::RendererQualityDesc sanitized = ark::sanitizeRendererQualityDesc(quality);
        const ark::EnvironmentBakeQualityDesc& bake = sanitized.environmentBake;

        if (!sameExtent(bake.environmentCubeFaceExtent, ark::rhi::Extent2D{1024, 1024}) ||
            !sameExtent(bake.irradianceCubeFaceExtent, ark::rhi::Extent2D{8, 8}) ||
            !sameExtent(bake.specularCubeFaceExtent, ark::DefaultEnvironmentBakeSpecularFaceExtent) ||
            !sameExtent(bake.brdfLutExtent, ark::rhi::Extent2D{1024, 1024}) ||
            !near(bake.irradianceSampleDelta, ark::DefaultEnvironmentBakeIrradianceSampleDelta) ||
            bake.specularPrefilterSampleCount != ark::MinEnvironmentBakeSampleCount ||
            bake.brdfLutSampleCount != ark::MaxEnvironmentBakeSampleCount) {
            std::cerr << "RendererQuality clamp behavior is invalid\n";
            return false;
        }

        quality.environmentBake.irradianceSampleDelta = 0.001f;
        const ark::RendererQualityDesc minDeltaQuality = ark::sanitizeRendererQualityDesc(quality);
        if (!near(minDeltaQuality.environmentBake.irradianceSampleDelta,
                  ark::MinEnvironmentBakeIrradianceSampleDelta)) {
            std::cerr << "RendererQuality minimum irradiance sample delta clamp is invalid\n";
            return false;
        }

        return true;
    }

    bool validateDisableDependencies() {
        ark::RendererQualityDesc quality{};
        quality.environmentBake.enableEnvironmentCube = false;
        quality.environmentBake.enableIrradiance = true;
        quality.environmentBake.enableSpecularPrefilter = true;
        quality.environmentBake.enableBrdfLut = true;

        ark::RendererQualityDesc sanitized = ark::sanitizeRendererQualityDesc(quality);
        if (sanitized.environmentBake.enableEnvironmentCube ||
            sanitized.environmentBake.enableIrradiance ||
            sanitized.environmentBake.enableSpecularPrefilter ||
            sanitized.environmentBake.enableBrdfLut) {
            std::cerr << "RendererQuality did not disable dependent environment bake targets\n";
            return false;
        }

        quality = ark::RendererQualityDesc{};
        quality.environmentBake.enableSpecularPrefilter = false;
        quality.environmentBake.enableBrdfLut = true;
        sanitized = ark::sanitizeRendererQualityDesc(quality);
        if (!sanitized.environmentBake.enableEnvironmentCube ||
            !sanitized.environmentBake.enableIrradiance ||
            sanitized.environmentBake.enableSpecularPrefilter ||
            sanitized.environmentBake.enableBrdfLut) {
            std::cerr << "RendererQuality did not disable BRDF LUT with specular prefilter\n";
            return false;
        }

        quality = ark::RendererQualityDesc{};
        quality.environmentBake.enableBrdfLut = false;
        sanitized = ark::sanitizeRendererQualityDesc(quality);
        if (!sanitized.environmentBake.enableEnvironmentCube ||
            !sanitized.environmentBake.enableIrradiance ||
            !sanitized.environmentBake.enableSpecularPrefilter ||
            sanitized.environmentBake.enableBrdfLut) {
            std::cerr << "RendererQuality explicit BRDF LUT disable is invalid\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateDefaultQuality() &&
                   validateCustomQuality() &&
                   validateClampQuality() &&
                   validateDisableDependencies()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
