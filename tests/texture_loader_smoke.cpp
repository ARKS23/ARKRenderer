#include "asset/TextureLoader.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
} // namespace

int main() {
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path();
    const std::filesystem::path ldrPath = tempDir / "ark_texture_loader_smoke.ppm";
    const std::filesystem::path hdrPath = tempDir / "ark_texture_loader_smoke.hdr";
    const std::filesystem::path missingPath = tempDir / "ark_texture_loader_missing.png";

    std::filesystem::remove(ldrPath);
    std::filesystem::remove(hdrPath);
    std::filesystem::remove(missingPath);

    const std::array<unsigned char, 23> ppmData{
        'P', '6', '\n', '2', ' ', '2', '\n', '2', '5', '5', '\n',
        255, 0,   0,    0,   255, 0,    0,   0,   255, 255, 255, 255,
    };

    constexpr std::string_view hdrHeader = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";

    const bool ldrWritten = writeBinaryFile(ldrPath, ppmData);
    const bool hdrWritten =
        writeBinaryFile(hdrPath, reinterpret_cast<const unsigned char*>(hdrHeader.data()), hdrHeader.size());

    const bool ldrValid = ldrWritten && validateLdrRgba8(ldrPath);
    const bool hdrRejected = hdrWritten && validateHdrRejected(hdrPath);
    const bool missingRejected = ark::asset::loadImageRgba8(missingPath).empty();

    std::filesystem::remove(ldrPath);
    std::filesystem::remove(hdrPath);

    return ldrValid && hdrRejected && missingRejected ? EXIT_SUCCESS : EXIT_FAILURE;
}
