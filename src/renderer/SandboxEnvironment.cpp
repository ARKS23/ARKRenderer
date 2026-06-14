#include "renderer/SandboxEnvironment.h"

#include "renderer/CubemapOrientation.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace ark {
    namespace {
        constexpr u32 EnvironmentBytesPerPixel = 16;
        constexpr float Pi = 3.14159265359f;

        void writePixel(std::vector<float>& pixels, u32 width, u32 x, u32 y, LinearColor color) {
            const usize pixelOffset = (static_cast<usize>(y) * width + x) * 4;
            pixels[pixelOffset + 0] = color.r;
            pixels[pixelOffset + 1] = color.g;
            pixels[pixelOffset + 2] = color.b;
            pixels[pixelOffset + 3] = color.a;
        }

        asset::ImageData makeRgba32FloatImage(u32 width,
                                              u32 height,
                                              const std::vector<float>& pixels,
                                              const char* debugName) {
            asset::ImageData image{};
            image.width = width;
            image.height = height;
            image.format = asset::ImageFormat::Rgba32Float;
            image.bytesPerPixel = EnvironmentBytesPerPixel;
            image.pixels.resize(pixels.size() * sizeof(float));
            std::memcpy(image.pixels.data(), pixels.data(), image.pixels.size());
            image.debugName = debugName;
            return image;
        }

        CubemapDirection equirectangularDirectionFromUv(float u, float v) {
            const float phi = (u - 0.5f) * 2.0f * Pi;
            const float theta = v * Pi;
            const float sinTheta = std::sin(theta);
            return CubemapDirection{
                std::cos(phi) * sinTheta,
                std::cos(theta),
                std::sin(phi) * sinTheta,
            };
        }
    } // namespace

    asset::ImageData makeProceduralSandboxEnvironmentImage() {
        constexpr u32 Width = 64;
        constexpr u32 Height = 32;

        std::vector<float> pixels(Width * Height * 4);
        for (u32 y = 0; y < Height; ++y) {
            const float v = Height > 1 ? static_cast<float>(y) / static_cast<float>(Height - 1) : 0.0f;
            const float sky = 1.0f - v;
            const float horizon = 1.0f - (v > 0.5f ? (v - 0.5f) * 2.0f : (0.5f - v) * 2.0f);
            for (u32 x = 0; x < Width; ++x) {
                const float u = Width > 1 ? static_cast<float>(x) / static_cast<float>(Width - 1) : 0.0f;
                LinearColor color{};
                color.r = 0.04f + sky * 0.12f + horizon * 0.35f;
                color.g = 0.08f + sky * 0.20f + horizon * 0.30f;
                color.b = 0.18f + sky * 0.55f + horizon * 0.18f;

                const float sunU = u - 0.12f;
                const float sunV = v - 0.42f;
                const float sunDistanceSquared = sunU * sunU + sunV * sunV;
                if (sunDistanceSquared < 0.0025f) {
                    color.r += 6.0f;
                    color.g += 4.0f;
                    color.b += 1.4f;
                }

                writePixel(pixels, Width, x, y, color);
            }
        }

        return makeRgba32FloatImage(Width, Height, pixels, "ProceduralSandboxEnvironment");
    }

    asset::ImageData makeDebugOrientationEnvironmentImage() {
        constexpr u32 Width = 64;
        constexpr u32 Height = 32;

        std::vector<float> pixels(Width * Height * 4);
        for (u32 y = 0; y < Height; ++y) {
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(Height);
            for (u32 x = 0; x < Width; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(Width);
                const CubemapDirection direction = equirectangularDirectionFromUv(u, v);
                writePixel(pixels, Width, x, y, debugOrientationColorForDirection(direction));
            }
        }

        return makeRgba32FloatImage(Width, Height, pixels, "DebugOrientationEnvironment");
    }
} // namespace ark
