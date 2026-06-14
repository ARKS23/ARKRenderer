#pragma once

#include "core/Types.h"

#include <array>
#include <cmath>

namespace ark {
    struct CubemapDirection {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct LinearColor {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
    };

    enum class CubemapFace : u32 {
        PositiveX = 0,
        NegativeX = 1,
        PositiveY = 2,
        NegativeY = 3,
        PositiveZ = 4,
        NegativeZ = 5,
    };

    struct CubemapFaceContract {
        CubemapFace face = CubemapFace::PositiveX;
        const char* name = "+X";
        CubemapDirection axis{};
        LinearColor debugColor{};
    };

    inline constexpr u32 CubemapFaceCount = 6;
    inline constexpr const char* CubemapFaceOrderDescription =
        "Face order: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z.";

    inline constexpr std::array<CubemapFaceContract, CubemapFaceCount> CubemapFaceContracts{{
        {CubemapFace::PositiveX, "+X", {1.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f, 1.0f}},
        {CubemapFace::NegativeX, "-X", {-1.0f, 0.0f, 0.0f}, {0.0f, 2.5f, 2.5f, 1.0f}},
        {CubemapFace::PositiveY, "+Y", {0.0f, 1.0f, 0.0f}, {3.0f, 3.0f, 3.0f, 1.0f}},
        {CubemapFace::NegativeY, "-Y", {0.0f, -1.0f, 0.0f}, {0.05f, 0.05f, 0.05f, 1.0f}},
        {CubemapFace::PositiveZ, "+Z", {0.0f, 0.0f, 1.0f}, {0.0f, 0.2f, 3.0f, 1.0f}},
        {CubemapFace::NegativeZ, "-Z", {0.0f, 0.0f, -1.0f}, {3.0f, 2.5f, 0.0f, 1.0f}},
    }};

    inline const CubemapFaceContract& cubemapFaceContract(CubemapFace face) {
        const u32 faceIndex = static_cast<u32>(face);
        return CubemapFaceContracts[faceIndex < CubemapFaceCount ? faceIndex : 0];
    }

    inline CubemapFace dominantCubemapFace(CubemapDirection direction) {
        const float ax = std::fabs(direction.x);
        const float ay = std::fabs(direction.y);
        const float az = std::fabs(direction.z);

        if (ax >= ay && ax >= az) {
            return direction.x >= 0.0f ? CubemapFace::PositiveX : CubemapFace::NegativeX;
        }
        if (ay >= ax && ay >= az) {
            return direction.y >= 0.0f ? CubemapFace::PositiveY : CubemapFace::NegativeY;
        }
        return direction.z >= 0.0f ? CubemapFace::PositiveZ : CubemapFace::NegativeZ;
    }

    inline LinearColor debugOrientationColorForDirection(CubemapDirection direction) {
        return cubemapFaceContract(dominantCubemapFace(direction)).debugColor;
    }
} // namespace ark
