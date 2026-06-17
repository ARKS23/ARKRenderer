#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif

#include "asset/GltfLoader.h"
#include "asset/TextureLoader.h"
#include "core/FileSystem.h"
#include "core/Memory.h"
#include "renderer/EnvironmentCubeConverter.h"
#include "renderer/EnvironmentCubeResource.h"
#include "renderer/EnvironmentBrdfLutGenerator.h"
#include "renderer/EnvironmentBrdfLutResource.h"
#include "renderer/EnvironmentIrradianceGenerator.h"
#include "renderer/EnvironmentResource.h"
#include "renderer/EnvironmentSpecularPrefilterGenerator.h"
#include "renderer/FrameContext.h"
#include "renderer/ModelResource.h"
#include "renderer/RenderQueue.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/RendererPreset.h"
#include "renderer/SandboxEnvironment.h"
#include "renderer/passes/BloomPass.h"
#include "renderer/passes/ForwardPass.h"
#include "renderer/passes/ShadowPass.h"
#include "renderer/passes/SkyboxPass.h"
#include "renderer/passes/ToneMappingPass.h"
#include "rhi/Buffer.h"
#include "rhi/DeviceContext.h"
#include "rhi/RenderBackend.h"
#include "rhi/RenderDevice.h"
#include "rhi/ResourceBarrier.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <GLFW/glfw3.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
    constexpr ark::rhi::Extent2D FrameExtent{256, 144};
    constexpr ark::u32 HdrFrameBytesPerPixel = 8;
    constexpr ark::u32 LdrFrameBytesPerPixel = 4;
    constexpr ark::u64 HdrFrameByteSize =
        static_cast<ark::u64>(FrameExtent.width) * FrameExtent.height * HdrFrameBytesPerPixel;
    constexpr ark::u64 LdrFrameByteSize =
        static_cast<ark::u64>(FrameExtent.width) * FrameExtent.height * LdrFrameBytesPerPixel;
    constexpr float GoldenMeanAbsErrorThreshold = 0.02f;
    constexpr float GoldenMaxChannelErrorThreshold = 0.25f;
    constexpr float GoldenMismatchedPixelRatioThreshold = 0.10f;
    constexpr ark::u8 GoldenMismatchByteThreshold = 8;

    struct FrameValidationOptions {
        bool updateGolden = false;
        ark::Path artifactRoot;
    };

    class HiddenGlfwWindow final {
    public:
        HiddenGlfwWindow() {
            if (glfwInit() != GLFW_TRUE) {
                throw std::runtime_error("glfwInit failed");
            }

            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

            m_Window = glfwCreateWindow(64, 64, "ARK frame validation smoke", nullptr, nullptr);
            if (!m_Window) {
                glfwTerminate();
                throw std::runtime_error("glfwCreateWindow failed");
            }
        }

        ~HiddenGlfwWindow() {
            if (m_Window) {
                glfwDestroyWindow(m_Window);
                m_Window = nullptr;
            }
            glfwTerminate();
        }

        ark::rhi::NativeWindowHandle nativeHandle() const {
            return ark::rhi::NativeWindowHandle{
                .type = ark::rhi::NativeWindowType::GLFW,
                .handle = m_Window,
            };
        }

    private:
        GLFWwindow* m_Window = nullptr;
    };

    struct FrameColorStats {
        ark::u64 pixelCount = 0;
        ark::u64 finitePixelCount = 0;
        ark::u64 nonBlackPixelCount = 0;
        glm::vec3 minRgb{std::numeric_limits<float>::max()};
        glm::vec3 maxRgb{std::numeric_limits<float>::lowest()};
        glm::vec3 meanRgb{0.0f};
        float meanLuminance = 0.0f;
        float maxLuminance = 0.0f;
    };

    struct LdrFrameColorStats {
        ark::u64 pixelCount = 0;
        ark::u64 finitePixelCount = 0;
        ark::u64 nonBlackPixelCount = 0;
        glm::vec3 minRgb{std::numeric_limits<float>::max()};
        glm::vec3 maxRgb{std::numeric_limits<float>::lowest()};
        glm::vec3 meanRgb{0.0f};
        float meanLuminance = 0.0f;
        float maxLuminance = 0.0f;
        float minAlpha = std::numeric_limits<float>::max();
        float meanAlpha = 0.0f;
    };

    struct ImageDiffStats {
        ark::u64 pixelCount = 0;
        ark::u64 channelCount = 0;
        ark::u64 mismatchedPixelCount = 0;
        float meanAbsError = 0.0f;
        float maxChannelError = 0.0f;
        float mismatchedPixelRatio = 0.0f;
    };

    enum class FrameValidationSceneMode {
        FixtureModel,
        RendererPreset,
    };

    struct FrameValidationCaseDesc {
        FrameValidationSceneMode sceneMode = FrameValidationSceneMode::FixtureModel;
        ark::Path fixtureRelativePath;
        const char* fixtureName = "";
        const char* fixtureId = "";
        const char* sceneModelName = "";
        ark::RendererScenePreset scenePreset = ark::RendererScenePreset::Default;
        ark::ToneMappingSettings toneMapping;
        ark::PostProcessingSettings postProcessing;
        ark::ShadowSettings shadowSettings;
        bool useSceneCamera = true;
        bool useFixedCamera = false;
        bool enableEnvironmentBake = false;
        bool requireSceneBounds = false;
        bool requireAdditionalModel = false;
        bool requireShadowMap = false;
        bool validateCompositeStats = false;
        glm::vec3 cameraTarget{0.0f};
        float cameraDistance = 4.0f;
        float cameraYawRadians = 0.0f;
        float cameraPitchRadians = 0.0f;
        float cameraFovYRadians = glm::radians(60.0f);
        float cameraNearPlane = 0.1f;
        float cameraFarPlane = 100.0f;
        bool compareGolden = true;
        bool writeArtifact = true;
    };

    struct FrameValidationResult {
        std::vector<ark::u8> hdrBytes;
        std::vector<ark::u8> ldrBytes;
        FrameColorStats hdrStats;
        LdrFrameColorStats ldrStats;
    };

    bool isCMakeConfigDirectoryName(std::string_view name) {
        return name == "Debug" || name == "Release" || name == "RelWithDebInfo" ||
               name == "MinSizeRel";
    }

    ark::Path artifactRootFromExecutablePath(std::string_view executablePath) {
        std::error_code error;
        ark::Path executable =
            executablePath.empty() ? std::filesystem::current_path(error)
                                   : std::filesystem::absolute(ark::Path{std::string{executablePath}}, error);
        if (error) {
            executable = std::filesystem::current_path();
        }

        ark::Path outputRoot = executable.has_parent_path() ? executable.parent_path()
                                                            : std::filesystem::current_path();
        if (isCMakeConfigDirectoryName(outputRoot.filename().string()) && outputRoot.has_parent_path()) {
            outputRoot = outputRoot.parent_path();
        }

        return outputRoot / "test_artifacts" / "frame_validation";
    }

    bool parseOptions(int argc, char** argv, FrameValidationOptions& options) {
        options.artifactRoot = artifactRootFromExecutablePath(argc > 0 && argv[0] ? argv[0] : "");

        for (int index = 1; index < argc; ++index) {
            const std::string_view argument{argv[index] ? argv[index] : ""};
            if (argument == "--update-golden") {
                options.updateGolden = true;
                continue;
            }

            std::cerr << "Unknown argument: " << argument << '\n'
                      << "Usage: ark_frame_validation_smoke [--update-golden]\n";
            return false;
        }

        return true;
    }

    float halfToFloat(ark::u16 value) {
        const ark::u32 sign = static_cast<ark::u32>(value & 0x8000u) << 16u;
        const ark::u32 exponent = (value >> 10u) & 0x1fu;
        const ark::u32 mantissa = value & 0x03ffu;

        if (exponent == 0u) {
            if (mantissa == 0u) {
                return sign != 0u ? -0.0f : 0.0f;
            }

            const float magnitude = std::ldexp(static_cast<float>(mantissa), -24);
            return sign != 0u ? -magnitude : magnitude;
        }

        if (exponent == 0x1fu) {
            if (mantissa == 0u) {
                return sign != 0u ? -std::numeric_limits<float>::infinity()
                                  : std::numeric_limits<float>::infinity();
            }

            return std::numeric_limits<float>::quiet_NaN();
        }

        const float magnitude =
            std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                       static_cast<int>(exponent) - 15);
        return sign != 0u ? -magnitude : magnitude;
    }

    ark::u16 readU16LE(const std::vector<ark::u8>& bytes, ark::usize offset) {
        return static_cast<ark::u16>(bytes[offset]) |
               static_cast<ark::u16>(static_cast<ark::u16>(bytes[offset + 1]) << 8u);
    }

    FrameColorStats computeFrameColorStats(const std::vector<ark::u8>& bytes, ark::rhi::Extent2D extent) {
        FrameColorStats stats{};
        stats.pixelCount = static_cast<ark::u64>(extent.width) * extent.height;
        glm::vec3 sumRgb{0.0f};
        double luminanceSum = 0.0;

        for (ark::u64 pixelIndex = 0; pixelIndex < stats.pixelCount; ++pixelIndex) {
            const ark::usize offset = static_cast<ark::usize>(pixelIndex * HdrFrameBytesPerPixel);
            const float r = halfToFloat(readU16LE(bytes, offset + 0));
            const float g = halfToFloat(readU16LE(bytes, offset + 2));
            const float b = halfToFloat(readU16LE(bytes, offset + 4));
            const float a = halfToFloat(readU16LE(bytes, offset + 6));
            const glm::vec3 rgb{r, g, b};

            if (std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a)) {
                ++stats.finitePixelCount;
            }

            stats.minRgb = glm::min(stats.minRgb, rgb);
            stats.maxRgb = glm::max(stats.maxRgb, rgb);
            sumRgb += rgb;

            const float luminance = glm::dot(rgb, glm::vec3{0.2126f, 0.7152f, 0.0722f});
            if (luminance > 1.0e-5f) {
                ++stats.nonBlackPixelCount;
            }
            stats.maxLuminance = std::max(stats.maxLuminance, luminance);
            luminanceSum += luminance;
        }

        if (stats.pixelCount > 0) {
            const float invPixelCount = 1.0f / static_cast<float>(stats.pixelCount);
            stats.meanRgb = sumRgb * invPixelCount;
            stats.meanLuminance = static_cast<float>(luminanceSum / static_cast<double>(stats.pixelCount));
        }

        return stats;
    }

    LdrFrameColorStats computeLdrFrameColorStats(const std::vector<ark::u8>& bytes,
                                                 ark::rhi::Extent2D extent) {
        LdrFrameColorStats stats{};
        stats.pixelCount = static_cast<ark::u64>(extent.width) * extent.height;
        glm::vec3 sumRgb{0.0f};
        double luminanceSum = 0.0;
        double alphaSum = 0.0;

        for (ark::u64 pixelIndex = 0; pixelIndex < stats.pixelCount; ++pixelIndex) {
            const ark::usize offset = static_cast<ark::usize>(pixelIndex * LdrFrameBytesPerPixel);
            const float r = static_cast<float>(bytes[offset + 0]) / 255.0f;
            const float g = static_cast<float>(bytes[offset + 1]) / 255.0f;
            const float b = static_cast<float>(bytes[offset + 2]) / 255.0f;
            const float a = static_cast<float>(bytes[offset + 3]) / 255.0f;
            const glm::vec3 rgb{r, g, b};

            if (std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a) &&
                r >= 0.0f && r <= 1.0f && g >= 0.0f && g <= 1.0f && b >= 0.0f && b <= 1.0f &&
                a >= 0.0f && a <= 1.0f) {
                ++stats.finitePixelCount;
            }

            stats.minRgb = glm::min(stats.minRgb, rgb);
            stats.maxRgb = glm::max(stats.maxRgb, rgb);
            stats.minAlpha = std::min(stats.minAlpha, a);
            sumRgb += rgb;
            alphaSum += a;

            const float luminance = glm::dot(rgb, glm::vec3{0.2126f, 0.7152f, 0.0722f});
            if (luminance > 1.0e-4f) {
                ++stats.nonBlackPixelCount;
            }
            stats.maxLuminance = std::max(stats.maxLuminance, luminance);
            luminanceSum += luminance;
        }

        if (stats.pixelCount > 0) {
            const float invPixelCount = 1.0f / static_cast<float>(stats.pixelCount);
            stats.meanRgb = sumRgb * invPixelCount;
            stats.meanLuminance = static_cast<float>(luminanceSum / static_cast<double>(stats.pixelCount));
            stats.meanAlpha = static_cast<float>(alphaSum / static_cast<double>(stats.pixelCount));
        }

        return stats;
    }

    void printStats(const FrameColorStats& stats) {
        const double nonBlackRatio = stats.pixelCount == 0
                                         ? 0.0
                                         : static_cast<double>(stats.nonBlackPixelCount) /
                                               static_cast<double>(stats.pixelCount);
        std::cerr << "Frame stats: pixels=" << stats.pixelCount
                  << " finite=" << stats.finitePixelCount
                  << " nonBlackRatio=" << nonBlackRatio
                  << " meanRgb=(" << stats.meanRgb.r << ", " << stats.meanRgb.g << ", " << stats.meanRgb.b << ")"
                  << " minRgb=(" << stats.minRgb.r << ", " << stats.minRgb.g << ", " << stats.minRgb.b << ")"
                  << " maxRgb=(" << stats.maxRgb.r << ", " << stats.maxRgb.g << ", " << stats.maxRgb.b << ")"
                  << " meanLum=" << stats.meanLuminance
                  << " maxLum=" << stats.maxLuminance << '\n';
    }

    void printLdrStats(const LdrFrameColorStats& stats) {
        const double nonBlackRatio = stats.pixelCount == 0
                                         ? 0.0
                                         : static_cast<double>(stats.nonBlackPixelCount) /
                                               static_cast<double>(stats.pixelCount);
        std::cerr << "LDR frame stats: pixels=" << stats.pixelCount
                  << " finite=" << stats.finitePixelCount
                  << " nonBlackRatio=" << nonBlackRatio
                  << " meanRgb=(" << stats.meanRgb.r << ", " << stats.meanRgb.g << ", " << stats.meanRgb.b << ")"
                  << " minRgb=(" << stats.minRgb.r << ", " << stats.minRgb.g << ", " << stats.minRgb.b << ")"
                  << " maxRgb=(" << stats.maxRgb.r << ", " << stats.maxRgb.g << ", " << stats.maxRgb.b << ")"
                  << " meanLum=" << stats.meanLuminance
                  << " maxLum=" << stats.maxLuminance
                  << " minAlpha=" << stats.minAlpha
                  << " meanAlpha=" << stats.meanAlpha << '\n';
    }

    bool validateStats(const FrameColorStats& stats) {
        if (stats.pixelCount == 0 || stats.finitePixelCount != stats.pixelCount) {
            std::cerr << "Frame validation read non-finite pixels\n";
            printStats(stats);
            return false;
        }

        const double nonBlackRatio =
            static_cast<double>(stats.nonBlackPixelCount) / static_cast<double>(stats.pixelCount);
        if (nonBlackRatio < 0.05) {
            std::cerr << "Frame validation output is too close to black\n";
            printStats(stats);
            return false;
        }

        if (stats.maxLuminance < 0.01f || stats.meanLuminance <= 0.001f ||
            stats.meanLuminance > 20.0f) {
            std::cerr << "Frame validation luminance is outside the expected smoke range\n";
            printStats(stats);
            return false;
        }

        const glm::vec3 channelRange = stats.maxRgb - stats.minRgb;
        if (std::max({channelRange.r, channelRange.g, channelRange.b}) < 0.01f) {
            std::cerr << "Frame validation output has insufficient color variation\n";
            printStats(stats);
            return false;
        }

        return true;
    }

    bool validateLdrStats(const LdrFrameColorStats& stats) {
        if (stats.pixelCount == 0 || stats.finitePixelCount != stats.pixelCount) {
            std::cerr << "LDR frame validation read invalid pixels\n";
            printLdrStats(stats);
            return false;
        }

        const double nonBlackRatio =
            static_cast<double>(stats.nonBlackPixelCount) / static_cast<double>(stats.pixelCount);
        if (nonBlackRatio < 0.05) {
            std::cerr << "LDR frame validation output is too close to black\n";
            printLdrStats(stats);
            return false;
        }

        if (stats.maxLuminance < 0.01f || stats.meanLuminance <= 0.001f ||
            stats.meanLuminance > 0.98f) {
            std::cerr << "LDR frame validation luminance is outside the expected smoke range\n";
            printLdrStats(stats);
            return false;
        }

        const glm::vec3 channelRange = stats.maxRgb - stats.minRgb;
        if (std::max({channelRange.r, channelRange.g, channelRange.b}) < 0.01f) {
            std::cerr << "LDR frame validation output has insufficient color variation\n";
            printLdrStats(stats);
            return false;
        }

        if (stats.minAlpha < 0.99f || stats.meanAlpha < 0.99f) {
            std::cerr << "LDR frame validation alpha is not opaque\n";
            printLdrStats(stats);
            return false;
        }

        return true;
    }

    float ldrLuminanceAt(const std::vector<ark::u8>& bytes, ark::u32 x, ark::u32 y, ark::rhi::Extent2D extent) {
        const ark::usize offset =
            static_cast<ark::usize>((static_cast<ark::u64>(y) * extent.width + x) * LdrFrameBytesPerPixel);
        const glm::vec3 rgb{
            static_cast<float>(bytes[offset + 0]) / 255.0f,
            static_cast<float>(bytes[offset + 1]) / 255.0f,
            static_cast<float>(bytes[offset + 2]) / 255.0f,
        };
        return glm::dot(rgb, glm::vec3{0.2126f, 0.7152f, 0.0722f});
    }

    float computeLdrRegionMeanLuminance(const std::vector<ark::u8>& bytes,
                                        ark::rhi::Extent2D extent,
                                        ark::u32 minX,
                                        ark::u32 minY,
                                        ark::u32 maxX,
                                        ark::u32 maxY) {
        if (bytes.empty() || minX >= maxX || minY >= maxY || maxX > extent.width || maxY > extent.height) {
            return 0.0f;
        }

        double sum = 0.0;
        ark::u64 count = 0;
        for (ark::u32 y = minY; y < maxY; ++y) {
            for (ark::u32 x = minX; x < maxX; ++x) {
                sum += ldrLuminanceAt(bytes, x, y, extent);
                ++count;
            }
        }

        return count == 0 ? 0.0f : static_cast<float>(sum / static_cast<double>(count));
    }

    bool validateDefaultCompositeStats(const std::vector<ark::u8>& bytes,
                                       const LdrFrameColorStats& stats) {
        const glm::vec3 channelRange = stats.maxRgb - stats.minRgb;
        if (std::max({channelRange.r, channelRange.g, channelRange.b}) < 0.08f ||
            stats.maxLuminance - stats.meanLuminance < 0.015f) {
            std::cerr << "Default composite frame lacks enough HDR/post-process visual variation\n";
            printLdrStats(stats);
            return false;
        }

        const ark::u32 centerMinX = FrameExtent.width / 4u;
        const ark::u32 centerMaxX = FrameExtent.width - centerMinX;
        const ark::u32 centerMinY = FrameExtent.height / 4u;
        const ark::u32 centerMaxY = FrameExtent.height - centerMinY;
        const float centerMean =
            computeLdrRegionMeanLuminance(bytes, FrameExtent, centerMinX, centerMinY, centerMaxX, centerMaxY);
        const float topMean =
            computeLdrRegionMeanLuminance(bytes, FrameExtent, 0u, 0u, FrameExtent.width, FrameExtent.height / 5u);
        if (std::abs(centerMean - topMean) < 0.003f) {
            std::cerr << "Default composite frame center and sky/edge regions are too similar: centerMean="
                      << centerMean << " topMean=" << topMean << '\n';
            printLdrStats(stats);
            return false;
        }

        return true;
    }

    ark::u64 rgba8ByteSize(ark::rhi::Extent2D extent) {
        return static_cast<ark::u64>(extent.width) * static_cast<ark::u64>(extent.height) *
               LdrFrameBytesPerPixel;
    }

    bool writeRgba8Png(const std::vector<ark::u8>& bytes,
                       ark::rhi::Extent2D extent,
                       const ark::Path& path) {
        const ark::u64 expectedByteSize = rgba8ByteSize(extent);
        if (static_cast<ark::u64>(bytes.size()) != expectedByteSize) {
            std::cerr << "PNG write input has unexpected byte size: expected "
                      << expectedByteSize << " got " << bytes.size() << '\n';
            return false;
        }

        const ark::Path parentPath = path.parent_path();
        if (!parentPath.empty()) {
            std::error_code createDirectoryError;
            std::filesystem::create_directories(parentPath, createDirectoryError);
            if (createDirectoryError) {
                std::cerr << "Failed to create PNG output directory: "
                          << parentPath.string() << " (" << createDirectoryError.message() << ")\n";
                return false;
            }
        }

        const int strideBytes = static_cast<int>(extent.width * LdrFrameBytesPerPixel);
        const int writeResult = stbi_write_png(path.string().c_str(),
                                               static_cast<int>(extent.width),
                                               static_cast<int>(extent.height),
                                               static_cast<int>(LdrFrameBytesPerPixel),
                                               bytes.data(),
                                               strideBytes);
        if (writeResult == 0) {
            std::cerr << "Failed to write PNG image: " << path.string() << '\n';
            return false;
        }

        return true;
    }

    ImageDiffStats computeImageDiffStats(const std::vector<ark::u8>& currentBytes,
                                         const std::vector<ark::u8>& goldenBytes,
                                         ark::rhi::Extent2D extent) {
        ImageDiffStats stats{};
        stats.pixelCount = static_cast<ark::u64>(extent.width) * static_cast<ark::u64>(extent.height);
        stats.channelCount = stats.pixelCount * LdrFrameBytesPerPixel;

        double absErrorSum = 0.0;
        for (ark::u64 pixelIndex = 0; pixelIndex < stats.pixelCount; ++pixelIndex) {
            bool pixelMismatched = false;
            const ark::u64 pixelOffset = pixelIndex * LdrFrameBytesPerPixel;

            for (ark::u64 channelIndex = 0; channelIndex < LdrFrameBytesPerPixel; ++channelIndex) {
                const ark::usize byteOffset = static_cast<ark::usize>(pixelOffset + channelIndex);
                const int delta =
                    std::abs(static_cast<int>(currentBytes[byteOffset]) -
                             static_cast<int>(goldenBytes[byteOffset]));
                const float normalizedDelta = static_cast<float>(delta) / 255.0f;
                absErrorSum += normalizedDelta;
                stats.maxChannelError = std::max(stats.maxChannelError, normalizedDelta);
                if (delta > GoldenMismatchByteThreshold) {
                    pixelMismatched = true;
                }
            }

            if (pixelMismatched) {
                ++stats.mismatchedPixelCount;
            }
        }

        if (stats.channelCount > 0) {
            stats.meanAbsError =
                static_cast<float>(absErrorSum / static_cast<double>(stats.channelCount));
        }
        if (stats.pixelCount > 0) {
            stats.mismatchedPixelRatio =
                static_cast<float>(static_cast<double>(stats.mismatchedPixelCount) /
                                   static_cast<double>(stats.pixelCount));
        }

        return stats;
    }

    void printImageDiffStats(const char* label, const ImageDiffStats& stats) {
        std::cerr << "Image diff stats for " << label
                  << ": pixels=" << stats.pixelCount
                  << " meanAbsError=" << stats.meanAbsError
                  << " maxChannelError=" << stats.maxChannelError
                  << " mismatchedPixelRatio=" << stats.mismatchedPixelRatio << '\n';
    }

    bool isProjectRootCandidate(const ark::Path& path) {
        std::error_code error;
        return std::filesystem::is_regular_file(path / "CMakeLists.txt", error) &&
               std::filesystem::is_directory(path / "docs" / "phase", error) &&
               std::filesystem::is_directory(path / "assets" / "models", error);
    }

    ark::Path findProjectRootFromSeed(ark::Path seedPath) {
        std::error_code canonicalError;
        seedPath = std::filesystem::weakly_canonical(seedPath, canonicalError);
        if (canonicalError) {
            seedPath = std::filesystem::absolute(seedPath);
        }

        for (ark::Path candidate = seedPath; !candidate.empty(); candidate = candidate.parent_path()) {
            if (isProjectRootCandidate(candidate)) {
                return candidate;
            }

            if (candidate == candidate.parent_path()) {
                break;
            }
        }

        return {};
    }

    ark::Path projectRootFromFixturePath(const ark::Path& fixturePath) {
        if (ark::Path projectRoot = findProjectRootFromSeed(std::filesystem::current_path());
            !projectRoot.empty()) {
            return projectRoot;
        }

        std::error_code error;
        ark::Path absolutePath = std::filesystem::absolute(fixturePath, error);
        if (error) {
            absolutePath = fixturePath;
        }

        if (ark::Path projectRoot = findProjectRootFromSeed(absolutePath.parent_path());
            !projectRoot.empty()) {
            return projectRoot;
        }

        return absolutePath.parent_path().parent_path().parent_path();
    }

    ark::Path goldenPathForFixture(const ark::Path& fixturePath, const char* fixtureId) {
        return projectRootFromFixturePath(fixturePath) / "tests" / "golden" /
               "frame_validation" / (std::string{fixtureId} + ".png");
    }

    bool compareWithGolden(const std::vector<ark::u8>& ldrBytes,
                           ark::rhi::Extent2D extent,
                           const ark::Path& goldenPath,
                           const char* fixtureId) {
        const ark::asset::ImageData goldenImage = ark::asset::loadImageRgba8(goldenPath);
        if (goldenImage.empty()) {
            std::cerr << "Failed to load golden image: " << goldenPath.string() << '\n';
            return false;
        }

        if (goldenImage.format != ark::asset::ImageFormat::Rgba8Unorm ||
            goldenImage.bytesPerPixel != LdrFrameBytesPerPixel) {
            std::cerr << "Golden image has an unexpected format: " << goldenPath.string() << '\n';
            return false;
        }

        if (goldenImage.width != extent.width || goldenImage.height != extent.height) {
            std::cerr << "Golden image dimensions do not match current frame for " << fixtureId
                      << ": golden=" << goldenImage.width << "x" << goldenImage.height
                      << " current=" << extent.width << "x" << extent.height << '\n';
            return false;
        }

        const ark::u64 expectedByteSize = rgba8ByteSize(extent);
        if (static_cast<ark::u64>(ldrBytes.size()) != expectedByteSize ||
            static_cast<ark::u64>(goldenImage.pixels.size()) != expectedByteSize) {
            std::cerr << "Golden image byte size does not match current frame for " << fixtureId << '\n';
            return false;
        }

        const ImageDiffStats diffStats = computeImageDiffStats(ldrBytes, goldenImage.pixels, extent);
        printImageDiffStats(fixtureId, diffStats);

        if (diffStats.meanAbsError > GoldenMeanAbsErrorThreshold ||
            diffStats.maxChannelError > GoldenMaxChannelErrorThreshold ||
            diffStats.mismatchedPixelRatio > GoldenMismatchedPixelRatioThreshold) {
            std::cerr << "Golden image diff exceeded thresholds for " << fixtureId << '\n';
            return false;
        }

        return true;
    }

    bool writeFrameArtifact(const std::vector<ark::u8>& ldrBytes,
                            ark::rhi::Extent2D extent,
                            const char* fixtureId,
                            const FrameValidationOptions& options) {
        const ark::Path artifactPath = options.artifactRoot / (std::string{fixtureId} + ".png");
        if (!writeRgba8Png(ldrBytes, extent, artifactPath)) {
            return false;
        }

        std::cerr << "Wrote frame validation artifact: " << artifactPath.string() << '\n';
        return true;
    }

    bool validateGolden(const std::vector<ark::u8>& ldrBytes,
                        ark::rhi::Extent2D extent,
                        const ark::Path& fixturePath,
                        const char* fixtureId,
                        const FrameValidationOptions& options) {

        const ark::Path goldenPath = goldenPathForFixture(fixturePath, fixtureId);
        if (options.updateGolden) {
            if (!writeRgba8Png(ldrBytes, extent, goldenPath)) {
                return false;
            }
            std::cerr << "Updated frame validation golden: " << goldenPath.string() << '\n';
        }

        std::error_code existsError;
        const bool goldenExists = std::filesystem::is_regular_file(goldenPath, existsError);
        if (existsError || !goldenExists) {
            std::cerr << "No golden baseline found for " << fixtureId
                      << "; skipping image diff. Expected path: " << goldenPath.string() << '\n';
            return true;
        }

        return compareWithGolden(ldrBytes, extent, goldenPath, fixtureId);
    }

    ark::Scope<ark::rhi::Texture> createSceneColorTexture(ark::rhi::RenderDevice& device) {
        ark::rhi::TextureDesc desc{};
        desc.extent = FrameExtent;
        desc.format = ark::rhi::Format::RGBA16Float;
        desc.usage = ark::rhi::TextureUsage::RenderTarget |
                     ark::rhi::TextureUsage::ShaderResource |
                     ark::rhi::TextureUsage::TransferSrc;
        return device.createTexture(desc);
    }

    ark::Scope<ark::rhi::Texture> createLdrColorTexture(ark::rhi::RenderDevice& device) {
        ark::rhi::TextureDesc desc{};
        desc.extent = FrameExtent;
        desc.format = ark::rhi::Format::RGBA8Unorm;
        desc.usage = ark::rhi::TextureUsage::RenderTarget | ark::rhi::TextureUsage::TransferSrc;
        return device.createTexture(desc);
    }

    ark::Scope<ark::rhi::Texture> createDepthTexture(ark::rhi::RenderDevice& device) {
        ark::rhi::TextureDesc desc{};
        desc.extent = FrameExtent;
        desc.format = ark::rhi::Format::D32Float;
        desc.usage = ark::rhi::TextureUsage::DepthStencil;
        return device.createTexture(desc);
    }

    ark::Scope<ark::rhi::Buffer> createReadbackBuffer(ark::rhi::RenderDevice& device,
                                                      const char* debugName,
                                                      ark::u64 byteSize) {
        ark::rhi::BufferDesc desc{};
        desc.debugName = debugName;
        desc.size = byteSize;
        desc.usage = ark::rhi::BufferUsage::TransferDst;
        desc.memoryUsage = ark::rhi::MemoryUsage::GpuToCpu;
        return device.createBuffer(desc);
    }

    ark::Path findFixturePath(const ark::Path& relative) {
        const std::array<ark::Path, 3> candidates{
            relative,
            ark::Path{"../"} / relative,
            ark::Path{"../../"} / relative,
        };

        return ark::findFirstExistingPath(candidates);
    }

    bool applyFirstSceneCamera(ark::RenderView& view,
                               const ark::asset::ModelData& modelData,
                               ark::rhi::Extent2D extent) {
        for (const ark::asset::SceneCameraData& sceneCamera : modelData.sceneCameras) {
            if (static_cast<ark::usize>(sceneCamera.cameraIndex) >= modelData.cameras.size()) {
                continue;
            }

            const ark::asset::CameraData& camera = modelData.cameras[sceneCamera.cameraIndex];
            if (view.setPerspectiveCamera(camera, sceneCamera.worldTransform, extent)) {
                return true;
            }
        }

        return false;
    }

    void applyFixedOrbitCamera(ark::RenderView& view,
                               const FrameValidationCaseDesc& desc,
                               ark::rhi::Extent2D extent) {
        const float aspect =
            extent.height == 0 ? 1.0f : static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const float cosPitch = std::cos(desc.cameraPitchRadians);
        const glm::vec3 forward = glm::normalize(glm::vec3{
            cosPitch * std::sin(desc.cameraYawRadians),
            std::sin(desc.cameraPitchRadians),
            cosPitch * std::cos(desc.cameraYawRadians),
        });
        const glm::vec3 cameraPosition = desc.cameraTarget - forward * desc.cameraDistance;
        glm::mat4 projection = glm::perspectiveRH_ZO(desc.cameraFovYRadians,
                                                     aspect,
                                                     desc.cameraNearPlane,
                                                     desc.cameraFarPlane);
        projection[1][1] *= -1.0f;
        view.setMatrices(glm::lookAt(cameraPosition,
                                     desc.cameraTarget,
                                     glm::vec3{0.0f, 1.0f, 0.0f}),
                         projection,
                         cameraPosition);
    }

    bool createEnvironmentCubeResource(ark::rhi::RenderDevice& device,
                                       ark::EnvironmentCubeResource& resource,
                                       std::string debugName,
                                       ark::rhi::Extent2D faceExtent,
                                       ark::u32 mipLevels) {
        ark::EnvironmentCubeResourceDesc desc{};
        desc.debugName = std::move(debugName);
        desc.faceExtent = faceExtent;
        desc.format = ark::rhi::Format::RGBA16Float;
        desc.mipLevels = mipLevels;
        return resource.create(device, desc);
    }

    bool createBrdfLutResource(ark::rhi::RenderDevice& device,
                               ark::EnvironmentBrdfLutResource& resource,
                               ark::rhi::Extent2D extent) {
        ark::EnvironmentBrdfLutResourceDesc desc{};
        desc.debugName = "FrameValidationCompositeBrdfLut";
        desc.extent = extent;
        desc.format = ark::rhi::Format::RGBA16Float;
        return resource.create(device, desc);
    }

    bool renderValidationSceneFrame(ark::rhi::RenderDevice& device,
                                    ark::rhi::DeviceContext& context,
                                    ark::rhi::FrameResource& frame,
                                    ark::ShadowPass* shadowPass,
                                    ark::SkyboxPass& skyboxPass,
                                    ark::ForwardPass& forwardPass,
                                    ark::rhi::Texture& sceneColor,
                                    ark::rhi::TextureView& sceneColorView,
                                    ark::rhi::Texture& depth,
                                    ark::rhi::TextureView& depthView,
                                    ark::RenderScene& scene,
                                    ark::EnvironmentCubeResource& skyboxCube,
                                    ark::EnvironmentCubeResource* irradianceCube,
                                    ark::EnvironmentCubeResource* prefilteredSpecularCube,
                                    ark::EnvironmentBrdfLutResource* brdfLut,
                                    ark::RenderView& view,
                                    bool requireShadowMap) {
        ark::RenderQueue queue{};
        queue.build(scene, view.cameraPosition());
        if (queue.empty()) {
            std::cerr << "Frame validation render queue is empty\n";
            return false;
        }

        ark::FrameContext frameContext{};
        frameContext.frameIndex = frame.frameIndex;
        frameContext.scene = &scene;
        frameContext.view = &view;
        frameContext.queue = &queue;
        frameContext.device = &device;
        frameContext.context = &context;
        frameContext.frameResource = &frame;
        frameContext.sceneColorView = &sceneColorView;
        frameContext.environmentCube = &skyboxCube;
        frameContext.irradianceCube = irradianceCube;
        frameContext.prefilteredSpecularCube = prefilteredSpecularCube;
        frameContext.brdfLut = brdfLut;
        frameContext.extent = FrameExtent;
        frameContext.colorFormat = ark::rhi::Format::RGBA16Float;
        frameContext.depthFormat = ark::rhi::Format::D32Float;
        frameContext.clearColor = ark::rhi::ClearColor{0.0f, 0.0f, 0.0f, 1.0f};

        if (shadowPass) {
            if (!shadowPass->prepare(frameContext) || !shadowPass->execute(frameContext)) {
                std::cerr << "Frame validation shadow pass failed\n";
                return false;
            }

            if (requireShadowMap &&
                (!frameContext.shadowMapView || !frameContext.shadowSampler ||
                 frameContext.shadowStrength <= 0.0f)) {
                std::cerr << "Frame validation expected a shadow map, but shadow resources were not bound\n";
                return false;
            }
        }

        if (!skyboxPass.prepare(frameContext) || !forwardPass.prepare(frameContext)) {
            std::cerr << "Frame validation pass prepare failed\n";
            return false;
        }

        const std::array<ark::rhi::ResourceBarrier, 2> toRenderTarget{{
            ark::rhi::ResourceBarrier{
                .texture = &sceneColor,
                .before = sceneColor.getState(),
                .after = ark::rhi::ResourceState::RenderTarget,
            },
            ark::rhi::ResourceBarrier{
                .texture = &depth,
                .before = depth.getState(),
                .after = ark::rhi::ResourceState::DepthStencilWrite,
            },
        }};
        context.pipelineBarrier(toRenderTarget);

        ark::rhi::RenderingDesc renderingDesc{};
        renderingDesc.extent = FrameExtent;
        renderingDesc.colorAttachment.view = &sceneColorView;
        renderingDesc.colorAttachment.loadOp = ark::rhi::LoadOp::Clear;
        renderingDesc.colorAttachment.storeOp = ark::rhi::StoreOp::Store;
        renderingDesc.colorAttachment.clearColor = frameContext.clearColor;
        renderingDesc.depthStencilAttachment.view = &depthView;
        renderingDesc.depthStencilAttachment.loadOp = ark::rhi::LoadOp::Clear;
        renderingDesc.depthStencilAttachment.storeOp = ark::rhi::StoreOp::DontCare;
        renderingDesc.depthStencilAttachment.clearDepth = 1.0f;

        if (!context.beginRendering(renderingDesc)) {
            std::cerr << "Frame validation beginRendering failed\n";
            return false;
        }

        ark::rhi::Viewport viewport{};
        viewport.width = static_cast<float>(FrameExtent.width);
        viewport.height = static_cast<float>(FrameExtent.height);
        context.setViewport(viewport);

        ark::rhi::ScissorRect scissor{};
        scissor.width = FrameExtent.width;
        scissor.height = FrameExtent.height;
        context.setScissorRect(scissor);

        if (!skyboxPass.execute(frameContext) || !forwardPass.execute(frameContext)) {
            context.endRendering();
            std::cerr << "Frame validation pass execute failed\n";
            return false;
        }

        context.endRendering();
        return true;
    }

    bool renderBloomFrame(ark::rhi::RenderDevice& device,
                          ark::rhi::DeviceContext& context,
                          ark::rhi::FrameResource& frame,
                          ark::BloomPass& bloomPass,
                          ark::RenderView& view,
                          ark::rhi::TextureView*& sceneColorView) {
        ark::FrameContext frameContext{};
        frameContext.frameIndex = frame.frameIndex;
        frameContext.device = &device;
        frameContext.context = &context;
        frameContext.frameResource = &frame;
        frameContext.view = &view;
        frameContext.sceneColorView = sceneColorView;
        frameContext.extent = FrameExtent;
        frameContext.colorFormat = ark::rhi::Format::RGBA16Float;
        frameContext.depthFormat = ark::rhi::Format::Unknown;
        frameContext.clearColor = ark::rhi::ClearColor{0.0f, 0.0f, 0.0f, 1.0f};

        if (!bloomPass.prepare(frameContext) || !bloomPass.execute(frameContext)) {
            std::cerr << "Bloom frame validation pass failed\n";
            return false;
        }

        sceneColorView = frameContext.sceneColorView;
        return sceneColorView != nullptr;
    }

    bool renderToneMappedLdrFrame(ark::rhi::RenderDevice& device,
                                  ark::rhi::DeviceContext& context,
                                  ark::rhi::FrameResource& frame,
                                  ark::ToneMappingPass& toneMappingPass,
                                  ark::RenderView& view,
                                  ark::rhi::TextureView& sceneColorView,
                                  ark::rhi::Texture& ldrColor,
                                  ark::rhi::TextureView& ldrColorView) {
        ark::FrameContext frameContext{};
        frameContext.frameIndex = frame.frameIndex;
        frameContext.device = &device;
        frameContext.context = &context;
        frameContext.frameResource = &frame;
        frameContext.view = &view;
        frameContext.sceneColorView = &sceneColorView;
        frameContext.extent = FrameExtent;
        frameContext.colorFormat = ark::rhi::Format::RGBA8Unorm;
        frameContext.depthFormat = ark::rhi::Format::Unknown;
        frameContext.clearColor = ark::rhi::ClearColor{0.0f, 0.0f, 0.0f, 1.0f};

        if (!toneMappingPass.prepare(frameContext)) {
            std::cerr << "Tone-mapped frame validation pass prepare failed\n";
            return false;
        }

        const std::array<ark::rhi::ResourceBarrier, 1> toLdrRenderTarget{{
            ark::rhi::ResourceBarrier{
                .texture = &ldrColor,
                .before = ldrColor.getState(),
                .after = ark::rhi::ResourceState::RenderTarget,
            },
        }};
        context.pipelineBarrier(toLdrRenderTarget);

        ark::rhi::RenderingDesc renderingDesc{};
        renderingDesc.extent = FrameExtent;
        renderingDesc.colorAttachment.view = &ldrColorView;
        renderingDesc.colorAttachment.loadOp = ark::rhi::LoadOp::Clear;
        renderingDesc.colorAttachment.storeOp = ark::rhi::StoreOp::Store;
        renderingDesc.colorAttachment.clearColor = frameContext.clearColor;

        if (!context.beginRendering(renderingDesc)) {
            std::cerr << "Tone-mapped frame validation beginRendering failed\n";
            return false;
        }

        ark::rhi::Viewport viewport{};
        viewport.width = static_cast<float>(FrameExtent.width);
        viewport.height = static_cast<float>(FrameExtent.height);
        context.setViewport(viewport);

        ark::rhi::ScissorRect scissor{};
        scissor.width = FrameExtent.width;
        scissor.height = FrameExtent.height;
        context.setScissorRect(scissor);

        if (!toneMappingPass.execute(frameContext)) {
            context.endRendering();
            std::cerr << "Tone-mapped frame validation pass execute failed\n";
            return false;
        }

        context.endRendering();
        return true;
    }

    bool runFrameValidationCase(const FrameValidationCaseDesc& desc,
                                const FrameValidationOptions& options,
                                FrameValidationResult& result) {
        HiddenGlfwWindow window{};

        ark::rhi::RenderBackendDesc backendDesc{};
        backendDesc.device.desc.applicationName = "ARK Frame Validation Smoke";
        backendDesc.device.nativeWindow = window.nativeHandle();
        backendDesc.swapChain.extent = FrameExtent;
        backendDesc.swapChain.enableVSync = true;

        ark::Scope<ark::rhi::RenderBackend> backend = ark::rhi::createRenderBackend(backendDesc);
        if (!backend) {
            std::cerr << "Failed to create render backend\n";
            return false;
        }

        ark::rhi::RenderDevice& device = backend->device();
        ark::rhi::DeviceContext& context = backend->context();

        ark::Scope<ark::rhi::Texture> sceneColor = createSceneColorTexture(device);
        ark::Scope<ark::rhi::Texture> ldrColor = createLdrColorTexture(device);
        ark::Scope<ark::rhi::Texture> depth = createDepthTexture(device);
        ark::Scope<ark::rhi::Buffer> hdrReadbackBuffer =
            createReadbackBuffer(device, "FrameValidationHdrReadbackBuffer", HdrFrameByteSize);
        ark::Scope<ark::rhi::Buffer> ldrReadbackBuffer =
            createReadbackBuffer(device, "FrameValidationLdrReadbackBuffer", LdrFrameByteSize);
        if (!sceneColor || !ldrColor || !depth || !hdrReadbackBuffer || !ldrReadbackBuffer) {
            std::cerr << "Failed to create frame validation resources\n";
            return false;
        }

        ark::rhi::TextureViewDesc sceneColorViewDesc{};
        sceneColorViewDesc.format = ark::rhi::Format::RGBA16Float;
        ark::Scope<ark::rhi::TextureView> sceneColorView =
            device.createTextureView(*sceneColor, sceneColorViewDesc);

        ark::rhi::TextureViewDesc ldrColorViewDesc{};
        ldrColorViewDesc.format = ark::rhi::Format::RGBA8Unorm;
        ark::Scope<ark::rhi::TextureView> ldrColorView =
            device.createTextureView(*ldrColor, ldrColorViewDesc);

        ark::rhi::TextureViewDesc depthViewDesc{};
        depthViewDesc.format = ark::rhi::Format::D32Float;
        ark::Scope<ark::rhi::TextureView> depthView = device.createTextureView(*depth, depthViewDesc);
        if (!sceneColorView || !ldrColorView || !depthView) {
            std::cerr << "Failed to create frame validation texture views\n";
            return false;
        }

        ark::RenderScene fixtureScene{};
        ark::asset::ModelData fixtureModelData{};
        ark::ModelResource fixtureModel{};
        ark::EnvironmentResource fixtureEnvironment{};
        ark::SceneResource presetSceneResource{};
        ark::RenderScene* activeScene = nullptr;
        ark::EnvironmentResource* activeEnvironment = nullptr;
        const ark::asset::ModelData* cameraModelData = nullptr;
        ark::Path goldenSeedPath;

        if (desc.sceneMode == FrameValidationSceneMode::FixtureModel) {
            const ark::Path modelPath = findFixturePath(desc.fixtureRelativePath);
            if (modelPath.empty()) {
                std::cerr << "Failed to find " << desc.fixtureName << '\n';
                return false;
            }

            fixtureModelData = ark::asset::loadGltfModel(modelPath);
            if (fixtureModelData.empty()) {
                std::cerr << "Failed to load " << desc.fixtureName << '\n';
                return false;
            }

            if (!fixtureModel.create(device, fixtureModelData)) {
                std::cerr << "Failed to create " << desc.fixtureName << " model resource\n";
                return false;
            }

            const ark::asset::ImageData environmentImage = ark::makeProceduralSandboxEnvironmentImage();
            ark::EnvironmentResourceDesc environmentDesc{};
            environmentDesc.debugName = "FrameValidationProceduralEnvironment";
            if (!fixtureEnvironment.create(device, environmentImage, environmentDesc)) {
                std::cerr << "Failed to create frame validation environment\n";
                return false;
            }

            fixtureScene.setEnvironment(ark::SceneEnvironment{
                .environment = &fixtureEnvironment,
                .intensity = 1.0f,
            });
            ark::SceneLighting lighting{};
            lighting.mainLight.direction = glm::vec3{-0.25f, -0.55f, -0.80f};
            lighting.mainLight.color = glm::vec3{1.0f, 0.97f, 0.90f};
            lighting.ambientColor = glm::vec3{0.04f, 0.05f, 0.06f};
            fixtureScene.setLighting(lighting);
            fixtureScene.addModel(fixtureModel, glm::mat4{1.0f}, desc.sceneModelName);

            activeScene = &fixtureScene;
            activeEnvironment = &fixtureEnvironment;
            cameraModelData = &fixtureModelData;
            goldenSeedPath = modelPath;
        } else {
            ark::ResolvedRendererPreset resolved = ark::resolveRendererPreset(ark::RendererPresetDesc{
                .scene = desc.scenePreset,
                .quality = ark::RendererQualityPreset::Low,
            });
            if (!presetSceneResource.load(device, resolved.scene)) {
                std::cerr << "Failed to load frame validation renderer preset scene\n";
                return false;
            }

            const ark::SceneResourceLoadReport& report = presetSceneResource.report();
            if (desc.requireAdditionalModel && report.loadedModelCount < 2) {
                std::cerr << "Frame validation composite scene did not load the expected additional model\n";
                return false;
            }

            if (desc.requireSceneBounds && !presetSceneResource.scene().hasBounds()) {
                std::cerr << "Frame validation composite scene did not produce valid scene bounds\n";
                return false;
            }

            activeScene = &presetSceneResource.scene();
            activeEnvironment = presetSceneResource.environment();
            cameraModelData = &presetSceneResource.modelData();
            goldenSeedPath = report.resolvedModelPath;
        }

        if (!activeScene || !activeEnvironment) {
            std::cerr << "Frame validation scene did not provide an environment\n";
            return false;
        }

        ark::EnvironmentCubeResource skyboxCube{};
        const ark::rhi::Extent2D skyboxFaceExtent =
            desc.enableEnvironmentBake ? ark::rhi::Extent2D{64, 64} : ark::rhi::Extent2D{32, 32};
        if (!createEnvironmentCubeResource(device,
                                           skyboxCube,
                                           "FrameValidationSkyboxCube",
                                           skyboxFaceExtent,
                                           1)) {
            std::cerr << "Failed to create frame validation skybox cube\n";
            return false;
        }

        ark::EnvironmentCubeResource irradianceCube{};
        ark::EnvironmentCubeResource prefilteredSpecularCube{};
        ark::EnvironmentBrdfLutResource brdfLut{};
        if (desc.enableEnvironmentBake) {
            if (!createEnvironmentCubeResource(device,
                                               irradianceCube,
                                               "FrameValidationCompositeIrradianceCube",
                                               ark::rhi::Extent2D{8, 8},
                                               1) ||
                !createEnvironmentCubeResource(device,
                                               prefilteredSpecularCube,
                                               "FrameValidationCompositeSpecularCube",
                                               ark::rhi::Extent2D{32, 32},
                                               ark::rhi::calculateMipLevelCount(ark::rhi::Extent2D{32, 32})) ||
                !createBrdfLutResource(device, brdfLut, ark::rhi::Extent2D{32, 32})) {
                std::cerr << "Failed to create frame validation IBL bake resources\n";
                return false;
            }
        }

        ark::EnvironmentCubeConverter converter{};
        ark::EnvironmentIrradianceGenerator irradianceGenerator{};
        ark::EnvironmentSpecularPrefilterGenerator specularPrefilterGenerator{};
        ark::EnvironmentBrdfLutGenerator brdfLutGenerator{};
        converter.setup(device);
        if (desc.enableEnvironmentBake) {
            irradianceGenerator.setup(device);
            specularPrefilterGenerator.setup(device);
            brdfLutGenerator.setup(device);
        }

        ark::ShadowPass shadowPass{};
        ark::SkyboxPass skyboxPass{};
        ark::ForwardPass forwardPass{};
        ark::BloomPass bloomPass{};
        ark::ToneMappingPass toneMappingPass{};
        if (desc.shadowSettings.enabled) {
            shadowPass.setup(device);
        }
        skyboxPass.setup(device);
        forwardPass.setup(device);
        bloomPass.setup(device);
        toneMappingPass.setup(device);

        ark::rhi::FrameResource& frame = context.beginFrame();
        if (!context.begin(frame)) {
            std::cerr << "Failed to begin frame validation command recording\n";
            return false;
        }

        if (!activeEnvironment->upload(context)) {
            std::cerr << "Failed to upload frame validation environment\n";
            return false;
        }

        ark::EnvironmentCubeConversionDesc conversionDesc{};
        conversionDesc.source = activeEnvironment;
        conversionDesc.target = &skyboxCube;
        conversionDesc.debugName = "FrameValidationSkyboxConversion";
        if (!converter.convert(context, conversionDesc)) {
            std::cerr << "Failed to convert frame validation environment to cubemap\n";
            return false;
        }

        if (desc.enableEnvironmentBake) {
            ark::EnvironmentIrradianceGenerationDesc irradianceDesc{};
            irradianceDesc.source = &skyboxCube;
            irradianceDesc.target = &irradianceCube;
            irradianceDesc.sampleDelta = 0.5f;
            irradianceDesc.debugName = "FrameValidationCompositeIrradiance";
            if (!irradianceGenerator.generate(context, irradianceDesc)) {
                std::cerr << "Failed to generate frame validation irradiance cube\n";
                return false;
            }

            ark::EnvironmentSpecularPrefilterDesc specularDesc{};
            specularDesc.source = &skyboxCube;
            specularDesc.target = &prefilteredSpecularCube;
            specularDesc.sampleCount = 16;
            specularDesc.debugName = "FrameValidationCompositeSpecularPrefilter";
            if (!specularPrefilterGenerator.generate(context, specularDesc)) {
                std::cerr << "Failed to generate frame validation specular prefilter\n";
                return false;
            }

            ark::EnvironmentBrdfLutGenerationDesc brdfDesc{};
            brdfDesc.target = &brdfLut;
            brdfDesc.sampleCount = 64;
            brdfDesc.debugName = "FrameValidationCompositeBrdfLut";
            if (!brdfLutGenerator.generate(context, brdfDesc)) {
                std::cerr << "Failed to generate frame validation BRDF LUT\n";
                return false;
            }
        }

        ark::RenderView view{};
        bool cameraApplied = false;
        if (desc.useFixedCamera) {
            applyFixedOrbitCamera(view, desc, FrameExtent);
            cameraApplied = true;
        } else if (desc.useSceneCamera && cameraModelData) {
            cameraApplied = applyFirstSceneCamera(view, *cameraModelData, FrameExtent);
        }

        if (!cameraApplied) {
            std::cerr << "Frame validation fixture does not provide a usable perspective scene camera\n";
            return false;
        }
        view.setToneMappingSettings(desc.toneMapping);
        view.setPostProcessingSettings(desc.postProcessing);
        view.setShadowSettings(desc.shadowSettings);

        if (!renderValidationSceneFrame(device,
                                        context,
                                        frame,
                                        desc.shadowSettings.enabled ? &shadowPass : nullptr,
                                        skyboxPass,
                                        forwardPass,
                                        *sceneColor,
                                        *sceneColorView,
                                        *depth,
                                        *depthView,
                                        *activeScene,
                                        skyboxCube,
                                        desc.enableEnvironmentBake ? &irradianceCube : nullptr,
                                        desc.enableEnvironmentBake ? &prefilteredSpecularCube : nullptr,
                                        desc.enableEnvironmentBake ? &brdfLut : nullptr,
                                        view,
                                        desc.requireShadowMap)) {
            return false;
        }

        const std::array<ark::rhi::ResourceBarrier, 1> sceneColorToCopySrc{{
            ark::rhi::ResourceBarrier{
                .texture = sceneColor.get(),
                .before = sceneColor->getState(),
                .after = ark::rhi::ResourceState::CopySrc,
            },
        }};
        context.pipelineBarrier(sceneColorToCopySrc);

        ark::rhi::TextureReadbackDesc hdrReadbackDesc{};
        hdrReadbackDesc.texture = sceneColor.get();
        hdrReadbackDesc.destinationBuffer = hdrReadbackBuffer.get();
        hdrReadbackDesc.extent = FrameExtent;
        hdrReadbackDesc.bytesPerPixel = HdrFrameBytesPerPixel;
        if (!context.copyTextureToBuffer(hdrReadbackDesc)) {
            std::cerr << "Failed to copy frame validation scene color to readback buffer\n";
            return false;
        }

        const std::array<ark::rhi::ResourceBarrier, 1> sceneColorToShaderResource{{
            ark::rhi::ResourceBarrier{
                .texture = sceneColor.get(),
                .before = sceneColor->getState(),
                .after = ark::rhi::ResourceState::ShaderResource,
            },
        }};
        context.pipelineBarrier(sceneColorToShaderResource);

        ark::rhi::TextureView* postSceneColorView = sceneColorView.get();
        if (!renderBloomFrame(device, context, frame, bloomPass, view, postSceneColorView)) {
            return false;
        }

        if (!renderToneMappedLdrFrame(device,
                                      context,
                                      frame,
                                      toneMappingPass,
                                      view,
                                      *postSceneColorView,
                                      *ldrColor,
                                      *ldrColorView)) {
            return false;
        }

        const std::array<ark::rhi::ResourceBarrier, 1> ldrColorToCopySrc{{
            ark::rhi::ResourceBarrier{
                .texture = ldrColor.get(),
                .before = ldrColor->getState(),
                .after = ark::rhi::ResourceState::CopySrc,
            },
        }};
        context.pipelineBarrier(ldrColorToCopySrc);

        ark::rhi::TextureReadbackDesc ldrReadbackDesc{};
        ldrReadbackDesc.texture = ldrColor.get();
        ldrReadbackDesc.destinationBuffer = ldrReadbackBuffer.get();
        ldrReadbackDesc.extent = FrameExtent;
        ldrReadbackDesc.bytesPerPixel = LdrFrameBytesPerPixel;
        if (!context.copyTextureToBuffer(ldrReadbackDesc)) {
            std::cerr << "Failed to copy frame validation LDR color to readback buffer\n";
            return false;
        }

        ark::rhi::SubmitDesc submitDesc{};
        submitDesc.frameResource = &frame;
        submitDesc.waitForSwapChainImage = false;
        submitDesc.signalRenderFinished = false;
        if (!context.end() || !context.submit(submitDesc)) {
            std::cerr << "Failed to submit frame validation command buffer\n";
            return false;
        }

        device.waitIdle();

        result.hdrBytes.resize(static_cast<ark::usize>(HdrFrameByteSize));
        if (!hdrReadbackBuffer->readData(result.hdrBytes.data(), HdrFrameByteSize)) {
            std::cerr << "Failed to read HDR frame validation bytes\n";
            return false;
        }

        result.ldrBytes.resize(static_cast<ark::usize>(LdrFrameByteSize));
        if (!ldrReadbackBuffer->readData(result.ldrBytes.data(), LdrFrameByteSize)) {
            std::cerr << "Failed to read LDR frame validation bytes\n";
            return false;
        }

        result.hdrStats = computeFrameColorStats(result.hdrBytes, FrameExtent);
        result.ldrStats = computeLdrFrameColorStats(result.ldrBytes, FrameExtent);
        if (!validateStats(result.hdrStats) || !validateLdrStats(result.ldrStats)) {
            return false;
        }

        if (desc.writeArtifact && !writeFrameArtifact(result.ldrBytes, FrameExtent, desc.fixtureId, options)) {
            return false;
        }

        if (desc.validateCompositeStats && !validateDefaultCompositeStats(result.ldrBytes, result.ldrStats)) {
            return false;
        }

        if (desc.compareGolden &&
            !validateGolden(result.ldrBytes, FrameExtent, goldenSeedPath, desc.fixtureId, options)) {
            return false;
        }

        return true;
    }

    bool validateFrameCase(const FrameValidationCaseDesc& desc, const FrameValidationOptions& options) {
        FrameValidationResult result{};
        return runFrameValidationCase(desc, options, result);
    }

    ark::PostProcessingSettings makeValidationBloomSettings() {
        ark::PostProcessingSettings settings{};
        settings.bloom.enabled = true;
        settings.bloom.intensity = 0.12f;
        settings.bloom.scatter = 0.72f;
        settings.bloom.threshold = 0.65f;
        settings.bloom.softKnee = 0.55f;
        settings.bloom.maxMipCount = 6;
        return settings;
    }

    ark::PostProcessingSettings makeDefaultCompositePostProcessingSettings() {
        ark::PostProcessingSettings settings{};
        settings.bloom.enabled = true;
        settings.bloom.intensity = 0.12f;
        settings.bloom.scatter = 0.6f;
        settings.bloom.threshold = 1.0f;
        settings.bloom.softKnee = 0.5f;
        settings.bloom.maxMipCount = 6;
        return settings;
    }

    ark::ShadowSettings makeDefaultCompositeShadowSettings() {
        ark::ShadowSettings settings{};
        settings.enabled = true;
        settings.strength = 1.0f;
        settings.bias = 0.0015f;
        settings.mapExtent = 1024;
        settings.orthographicHalfExtent = 64.0f;
        settings.nearPlane = 0.1f;
        settings.farPlane = 256.0f;
        settings.lightDistance = 96.0f;
        settings.fitSceneBounds = true;
        return settings;
    }

    FrameValidationCaseDesc makeBloomValidationCase(const char* fixtureId) {
        FrameValidationCaseDesc desc{};
        desc.fixtureRelativePath = ark::Path{"assets/models/bloom_validation_fixture.gltf"};
        desc.fixtureName = "bloom validation fixture";
        desc.fixtureId = fixtureId;
        desc.sceneModelName = "FrameValidationBloomFixture";
        desc.compareGolden = false;
        return desc;
    }

    FrameValidationCaseDesc makeDefaultCompositeCase() {
        FrameValidationCaseDesc desc{};
        desc.sceneMode = FrameValidationSceneMode::RendererPreset;
        desc.fixtureName = "default composite scene";
        desc.fixtureId = "default_composite_scene";
        desc.scenePreset = ark::RendererScenePreset::Default;
        desc.toneMapping = ark::ToneMappingSettings{1.0f, 2.2f, ark::ToneMappingOperator::ACES};
        desc.postProcessing = makeDefaultCompositePostProcessingSettings();
        desc.shadowSettings = makeDefaultCompositeShadowSettings();
        desc.useSceneCamera = false;
        desc.useFixedCamera = true;
        desc.enableEnvironmentBake = true;
        desc.requireSceneBounds = true;
        desc.requireAdditionalModel = true;
        desc.requireShadowMap = true;
        desc.validateCompositeStats = true;
        desc.cameraTarget = glm::vec3{0.0f, 3.2f, 0.6f};
        desc.cameraDistance = 16.0f;
        desc.cameraYawRadians = glm::radians(90.0f);
        desc.cameraPitchRadians = glm::radians(-8.0f);
        desc.cameraNearPlane = 0.05f;
        desc.cameraFarPlane = 512.0f;
        return desc;
    }

    bool validateImageDiffAbove(const char* label,
                                const std::vector<ark::u8>& lhs,
                                const std::vector<ark::u8>& rhs,
                                float minMeanAbsError,
                                float minMismatchRatio) {
        const ImageDiffStats diffStats = computeImageDiffStats(lhs, rhs, FrameExtent);
        printImageDiffStats(label, diffStats);
        if (diffStats.meanAbsError < minMeanAbsError ||
            diffStats.mismatchedPixelRatio < minMismatchRatio) {
            std::cerr << "Frame validation image diff was too small for " << label << '\n';
            return false;
        }

        return true;
    }

    bool validateBloomVisualDiff(const FrameValidationOptions& options) {
        FrameValidationCaseDesc bloomOff = makeBloomValidationCase("bloom_validation_reinhard_bloom_off");
        FrameValidationCaseDesc bloomOn = makeBloomValidationCase("bloom_validation_reinhard_bloom_on");
        bloomOn.postProcessing = makeValidationBloomSettings();

        FrameValidationResult offResult{};
        FrameValidationResult onResult{};
        if (!runFrameValidationCase(bloomOff, options, offResult) ||
            !runFrameValidationCase(bloomOn, options, onResult)) {
            return false;
        }

        const float meanLuminanceDelta =
            onResult.ldrStats.meanLuminance - offResult.ldrStats.meanLuminance;
        if (meanLuminanceDelta < 0.0005f ||
            onResult.ldrStats.maxLuminance + 0.0001f < offResult.ldrStats.maxLuminance) {
            std::cerr << "Bloom validation luminance delta is too small: meanDelta="
                      << meanLuminanceDelta
                      << " offMax=" << offResult.ldrStats.maxLuminance
                      << " onMax=" << onResult.ldrStats.maxLuminance << '\n';
            return false;
        }

        return validateImageDiffAbove("bloom_validation_bloom_on_vs_off",
                                      offResult.ldrBytes,
                                      onResult.ldrBytes,
                                      0.0015f,
                                      0.008f);
    }

    bool validateToneMappingVisualDiff(const FrameValidationOptions& options) {
        FrameValidationCaseDesc reinhard = makeBloomValidationCase("bloom_validation_reinhard_bloom_on");
        FrameValidationCaseDesc aces = makeBloomValidationCase("bloom_validation_aces_bloom_on");
        FrameValidationCaseDesc linear = makeBloomValidationCase("bloom_validation_linear_bloom_on");
        reinhard.writeArtifact = false;
        reinhard.postProcessing = makeValidationBloomSettings();
        aces.postProcessing = makeValidationBloomSettings();
        linear.postProcessing = makeValidationBloomSettings();
        aces.toneMapping.operatorType = ark::ToneMappingOperator::ACES;
        linear.toneMapping.operatorType = ark::ToneMappingOperator::Linear;

        FrameValidationResult reinhardResult{};
        FrameValidationResult acesResult{};
        FrameValidationResult linearResult{};
        if (!runFrameValidationCase(reinhard, options, reinhardResult) ||
            !runFrameValidationCase(aces, options, acesResult) ||
            !runFrameValidationCase(linear, options, linearResult)) {
            return false;
        }

        return validateImageDiffAbove("tone_mapping_aces_vs_reinhard",
                                      reinhardResult.ldrBytes,
                                      acesResult.ldrBytes,
                                      0.001f,
                                      0.005f) &&
               validateImageDiffAbove("tone_mapping_linear_vs_reinhard",
                                      reinhardResult.ldrBytes,
                                      linearResult.ldrBytes,
                                      0.002f,
                                      0.005f);
    }

    bool validateFrameColorReadback(const FrameValidationOptions& options) {
        FrameValidationCaseDesc specular{};
        specular.fixtureRelativePath = ark::Path{"assets/models/specular_ibl_validation_fixture.gltf"};
        specular.fixtureName = "specular IBL validation fixture";
        specular.fixtureId = "specular_ibl_validation_fixture";
        specular.sceneModelName = "FrameValidationSpecularFixture";

        FrameValidationCaseDesc materialBall{};
        materialBall.fixtureRelativePath = ark::Path{"assets/models/material_ball_validation_fixture.gltf"};
        materialBall.fixtureName = "material ball validation fixture";
        materialBall.fixtureId = "material_ball_validation_fixture";
        materialBall.sceneModelName = "FrameValidationMaterialBallFixture";

        return validateFrameCase(specular, options) &&
               validateFrameCase(materialBall, options) &&
               validateFrameCase(makeDefaultCompositeCase(), options) &&
               validateBloomVisualDiff(options) &&
               validateToneMappingVisualDiff(options);
    }
} // namespace

int main(int argc, char** argv) {
    try {
        FrameValidationOptions options{};
        if (!parseOptions(argc, argv, options)) {
            return EXIT_FAILURE;
        }

        return validateFrameColorReadback(options) ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
