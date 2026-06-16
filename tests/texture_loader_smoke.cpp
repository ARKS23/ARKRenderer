#include "asset/TextureLoader.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {
    bool writeBinaryFile(const std::filesystem::path& path, const unsigned char* data, std::size_t size) {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to create test image: " << path.string() << '\n';
            return false;
        }

        file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        return file.good();
    }

    template <std::size_t Size>
    bool writeBinaryFile(const std::filesystem::path& path, const std::array<unsigned char, Size>& data) {
        return writeBinaryFile(path, data.data(), data.size());
    }

    bool validateLdrRgba8(const std::filesystem::path& path) {
        const ark::asset::ImageData image = ark::asset::loadImageRgba8(path);
        if (image.empty()) {
            std::cerr << "Failed to load LDR test image\n";
            return false;
        }

        if (image.width != 2 || image.height != 2) {
            std::cerr << "Unexpected image dimensions\n";
            return false;
        }

        if (image.format != ark::asset::ImageFormat::Rgba8Unorm || image.bytesPerPixel != 4) {
            std::cerr << "Unexpected image format\n";
            return false;
        }

        if (image.pixels.size() != 16) {
            std::cerr << "Unexpected RGBA8 byte size\n";
            return false;
        }

        const bool firstPixelIsRed =
            image.pixels[0] == 255 && image.pixels[1] == 0 && image.pixels[2] == 0 && image.pixels[3] == 255;
        if (!firstPixelIsRed) {
            std::cerr << "Unexpected first pixel value\n";
            return false;
        }

        const ark::asset::ImageData imageFromClass = ark::asset::TextureLoader::loadRgba8(path);
        if (imageFromClass.empty() || imageFromClass.byteSize() != image.byteSize()) {
            std::cerr << "TextureLoader class wrapper failed\n";
            return false;
        }

        return true;
    }

    bool validateHdrRejected(const std::filesystem::path& path) {
        const ark::asset::ImageData image = ark::asset::loadImageRgba8(path);
        if (!image.empty()) {
            std::cerr << "HDR image was unexpectedly loaded as RGBA8\n";
            return false;
        }

        return true;
    }

    bool near(float a, float b, float epsilon = 0.0001f) {
        return std::fabs(a - b) <= epsilon;
    }

    bool validateHdrRgba32F(const std::filesystem::path& path) {
        const ark::asset::ImageData image = ark::asset::loadImageHdrRgba32F(path);
        if (image.empty()) {
            std::cerr << "Failed to load HDR test image\n";
            return false;
        }

        if (image.width != 1 || image.height != 1) {
            std::cerr << "Unexpected HDR image dimensions\n";
            return false;
        }

        if (image.format != ark::asset::ImageFormat::Rgba32Float || image.bytesPerPixel != 16 ||
            image.pixels.size() != 16) {
            std::cerr << "Unexpected HDR image format\n";
            return false;
        }

        float rgba[4]{};
        std::memcpy(rgba, image.pixels.data(), sizeof(rgba));
        if (!near(rgba[0], 1.0f) || !near(rgba[1], 0.0f) || !near(rgba[2], 0.0f) || !near(rgba[3], 1.0f)) {
            std::cerr << "Unexpected HDR pixel value\n";
            return false;
        }

        const ark::asset::ImageData imageFromClass = ark::asset::TextureLoader::loadHdrRgba32F(path);
        if (imageFromClass.empty() || imageFromClass.byteSize() != image.byteSize()) {
            std::cerr << "TextureLoader HDR class wrapper failed\n";
            return false;
        }

        return true;
    }

    std::filesystem::path findAssetPath(const std::filesystem::path& relative) {
        const std::array<std::filesystem::path, 3> candidates{
            relative,
            std::filesystem::path{"../"} / relative,
            std::filesystem::path{"../../"} / relative,
        };

        return ark::findFirstExistingPath(candidates);
    }

    bool validateKtxRgba8(const std::filesystem::path& path, ark::u32 expectedWidth, ark::u32 expectedHeight) {
        const ark::asset::ImageData image = ark::asset::loadImageKtx(path);
        if (image.empty()) {
            std::cerr << "Failed to load KTX test image\n";
            return false;
        }

        if (image.width != expectedWidth || image.height != expectedHeight) {
            std::cerr << "Unexpected KTX image dimensions\n";
            return false;
        }

        if (image.format != ark::asset::ImageFormat::Rgba8Unorm || image.bytesPerPixel != 4) {
            std::cerr << "Unexpected KTX image format\n";
            return false;
        }

        if (image.pixels.size() != static_cast<std::size_t>(expectedWidth) * expectedHeight * 4) {
            std::cerr << "Unexpected KTX byte size\n";
            return false;
        }

        const ark::asset::ImageData imageFromClass = ark::asset::TextureLoader::loadKtx(path);
        if (imageFromClass.empty() || imageFromClass.byteSize() != image.byteSize()) {
            std::cerr << "TextureLoader KTX class wrapper failed\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path();
    const std::filesystem::path ldrPath = tempDir / "ark_texture_loader_smoke.ppm";
    const std::filesystem::path hdrPath = tempDir / "ark_texture_loader_smoke.hdr";
    const std::filesystem::path ktxWhitePath = findAssetPath(std::filesystem::path{"assets/models/sponza/white.ktx"});
    const std::filesystem::path ktxSponzaPath =
        findAssetPath(std::filesystem::path{"assets/models/sponza/10381718147657362067.ktx"});
    const std::filesystem::path missingPath = tempDir / "ark_texture_loader_missing.png";

    std::filesystem::remove(ldrPath);
    std::filesystem::remove(hdrPath);
    std::filesystem::remove(missingPath);

    const std::array<unsigned char, 23> ppmData{
        'P', '6', '\n', '2', ' ', '2', '\n', '2', '5', '5', '\n',
        255, 0,   0,    0,   255, 0,    0,   0,   255, 255, 255, 255,
    };

    constexpr std::string_view hdrHeader = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
    std::string hdrData{hdrHeader};
    hdrData.push_back(static_cast<char>(128));
    hdrData.push_back(static_cast<char>(0));
    hdrData.push_back(static_cast<char>(0));
    hdrData.push_back(static_cast<char>(129));

    const bool ldrWritten = writeBinaryFile(ldrPath, ppmData);
    const bool hdrWritten =
        writeBinaryFile(hdrPath, reinterpret_cast<const unsigned char*>(hdrData.data()), hdrData.size());

    const bool ldrValid = ldrWritten && validateLdrRgba8(ldrPath);
    const bool hdrRejected = hdrWritten && validateHdrRejected(hdrPath);
    const bool hdrValid = hdrWritten && validateHdrRgba32F(hdrPath);
    const bool ktxWhiteValid = !ktxWhitePath.empty() && validateKtxRgba8(ktxWhitePath, 4, 4);
    const bool ktxSponzaValid = !ktxSponzaPath.empty() && validateKtxRgba8(ktxSponzaPath, 1024, 1024);
    const ark::asset::ImageData autoKtxImage = ark::asset::loadImageAuto(ktxWhitePath);
    const bool autoKtxValid = autoKtxImage.width == 4 && autoKtxImage.height == 4;
    const bool ldrRejectedByHdrLoader = ark::asset::loadImageHdrRgba32F(ldrPath).empty();
    const bool missingRejected = ark::asset::loadImageRgba8(missingPath).empty();

    std::filesystem::remove(ldrPath);
    std::filesystem::remove(hdrPath);

    return ldrValid && hdrRejected && hdrValid && ktxWhiteValid && ktxSponzaValid && autoKtxValid &&
                   ldrRejectedByHdrLoader && missingRejected
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
