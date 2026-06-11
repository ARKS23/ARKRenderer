#include "asset/ShaderLoader.h"
#include "core/FileSystem.h"

#include <array>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
    constexpr ark::u32 SpirvMagic = 0x07230203;

    bool validateCompiledShader(std::string_view fileName) {
        const std::vector<ark::u32> bytecode = ark::asset::loadCompiledShader(fileName);
        if (bytecode.empty()) {
            std::cerr << "Failed to load shader asset: " << fileName << '\n';
            return false;
        }

        if (bytecode.front() != SpirvMagic) {
            std::cerr << "Invalid shader asset magic: " << fileName << '\n';
            return false;
        }

        return true;
    }

    bool containsText(const std::vector<ark::u8>& data, std::string_view needle) {
        if (needle.empty() || data.size() < needle.size()) {
            return false;
        }

        const std::string_view text{reinterpret_cast<const char*>(data.data()), data.size()};
        return text.find(needle) != std::string_view::npos;
    }

    ark::Path findShaderSource(std::string_view fileName) {
        const ark::Path relative = ark::Path{"shaders"} / ark::Path{fileName};
        const std::array<ark::Path, 3> candidates{
            relative,
            ark::Path{"../"} / relative,
            ark::Path{"../../"} / relative,
        };

        return ark::findFirstExistingPath(candidates);
    }

    bool validateMeshFragmentShaderSource() {
        const ark::Path shaderPath = findShaderSource("mesh.frag.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find mesh fragment shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read mesh fragment shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "struct PbrInputs") ||
            !containsText(shaderSource, "buildWorldNormal") ||
            !containsText(shaderSource, "evaluateDirectLighting") ||
            !containsText(shaderSource, "[[vk::binding(13, 0)]]") ||
            !containsText(shaderSource, "alphaCutoff") ||
            !containsText(shaderSource, "AlphaModeMask") ||
            !containsText(shaderSource, "discard") ||
            !containsText(shaderSource, "AlphaModeBlend") ||
            !containsText(shaderSource, "selectUv") ||
            !containsText(shaderSource, "transformUv") ||
            !containsText(shaderSource, "baseColorTexCoord") ||
            !containsText(shaderSource, "normalTexCoord") ||
            !containsText(shaderSource, "metallicRoughnessTexCoord") ||
            !containsText(shaderSource, "occlusionTexCoord") ||
            !containsText(shaderSource, "emissiveTexCoord") ||
            !containsText(shaderSource, "baseColorUvTransform0") ||
            !containsText(shaderSource, "normalUvTransform0") ||
            !containsText(shaderSource, "metallicRoughnessUvTransform0") ||
            !containsText(shaderSource, "occlusionUvTransform0") ||
            !containsText(shaderSource, "emissiveUvTransform0") ||
            !containsText(shaderSource, "PI") ||
            !containsText(shaderSource, "distributionGGX") ||
            !containsText(shaderSource, "geometrySchlickGGX") ||
            !containsText(shaderSource, "geometrySmith") ||
            !containsText(shaderSource, "fresnelSchlick") ||
            !containsText(shaderSource, "nDotV") ||
            !containsText(shaderSource, "vDotH") ||
            !containsText(shaderSource, "f0") ||
            !containsText(shaderSource, "specularDenominator")) {
            std::cerr << "Mesh fragment shader does not expose expected BRDF, alpha and UV selection path\n";
            return false;
        }

        return true;
    }

    bool validateMeshVertexShaderSource() {
        const ark::Path shaderPath = findShaderSource("mesh.vert.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find mesh vertex shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read mesh vertex shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "float4x4 normalMatrix") ||
            !containsText(shaderSource, "g_Object.normalMatrix") ||
            !containsText(shaderSource, "[[vk::location(3)]] float2 uv1") ||
            !containsText(shaderSource, "[[vk::location(4)]] float4 tangent") ||
            !containsText(shaderSource, "output.uv1 = input.uv1")) {
            std::cerr << "Mesh vertex shader does not expose expected normal matrix and UV1 path\n";
            return false;
        }

        return true;
    }

    bool validateToneMappingVertexShaderSource() {
        const ark::Path shaderPath = findShaderSource("tonemap.vert.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find tone mapping vertex shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read tone mapping vertex shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "SV_VertexID") ||
            !containsText(shaderSource, "[[vk::location(0)]] float2 uv") ||
            !containsText(shaderSource, "positions[3]")) {
            std::cerr << "Tone mapping vertex shader does not expose expected fullscreen triangle path\n";
            return false;
        }

        return true;
    }

    bool validateToneMappingFragmentShaderSource() {
        const ark::Path shaderPath = findShaderSource("tonemap.frag.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find tone mapping fragment shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read tone mapping fragment shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "[[vk::binding(0, 0)]]") ||
            !containsText(shaderSource, "Texture2D<float4> g_SceneColor") ||
            !containsText(shaderSource, "[[vk::binding(1, 0)]]") ||
            !containsText(shaderSource, "SamplerState g_SceneSampler") ||
            !containsText(shaderSource, "Exposure") ||
            !containsText(shaderSource, "applyToneMapping") ||
            !containsText(shaderSource, "linearToSrgb") ||
            !containsText(shaderSource, "pow") ||
            !containsText(shaderSource, "Sample")) {
            std::cerr << "Tone mapping fragment shader does not expose expected HDR sampling path\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    const bool vertexShaderValid = validateCompiledShader("triangle.vert.spv");
    const bool fragmentShaderValid = validateCompiledShader("triangle.frag.spv");
    const bool cubeVertexShaderValid = validateCompiledShader("cube.vert.spv");
    const bool cubeFragmentShaderValid = validateCompiledShader("cube.frag.spv");
    const bool texturedCubeVertexShaderValid = validateCompiledShader("textured_cube.vert.spv");
    const bool texturedCubeFragmentShaderValid = validateCompiledShader("textured_cube.frag.spv");
    const bool meshVertexShaderValid = validateCompiledShader("mesh.vert.spv");
    const bool meshFragmentShaderValid = validateCompiledShader("mesh.frag.spv");
    const bool toneMappingVertexShaderValid = validateCompiledShader("tonemap.vert.spv");
    const bool toneMappingFragmentShaderValid = validateCompiledShader("tonemap.frag.spv");

    return vertexShaderValid && fragmentShaderValid && cubeVertexShaderValid && cubeFragmentShaderValid &&
                   texturedCubeVertexShaderValid && texturedCubeFragmentShaderValid && meshVertexShaderValid &&
                   meshFragmentShaderValid && toneMappingVertexShaderValid && toneMappingFragmentShaderValid &&
                   validateMeshVertexShaderSource() && validateMeshFragmentShaderSource() &&
                   validateToneMappingVertexShaderSource() && validateToneMappingFragmentShaderSource()
               ? 0
               : 1;
}
