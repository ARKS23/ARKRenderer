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
            !containsText(shaderSource, "[[vk::binding(14, 0)]]") ||
            !containsText(shaderSource, "Texture2D<float4> g_EnvironmentTexture") ||
            !containsText(shaderSource, "[[vk::binding(15, 0)]]") ||
            !containsText(shaderSource, "SamplerState g_EnvironmentSampler") ||
            !containsText(shaderSource, "[[vk::binding(16, 0)]]") ||
            !containsText(shaderSource, "TextureCube<float4> g_IrradianceCube") ||
            !containsText(shaderSource, "[[vk::binding(17, 0)]]") ||
            !containsText(shaderSource, "SamplerState g_IrradianceSampler") ||
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
            !containsText(shaderSource, "specularDenominator") ||
            !containsText(shaderSource, "directionToEquirectUv") ||
            !containsText(shaderSource, "sampleEnvironment") ||
            !containsText(shaderSource, "sampleIrradiance") ||
            !containsText(shaderSource, "evaluateAmbientLighting") ||
            !containsText(shaderSource, "g_Lighting.environment") ||
            !containsText(shaderSource, "g_Lighting.environment.z")) {
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
            !containsText(shaderSource, "struct ToneMappingUniform") ||
            !containsText(shaderSource, "[[vk::binding(2, 0)]]") ||
            !containsText(shaderSource, "g_ToneMapping.exposure") ||
            !containsText(shaderSource, "g_ToneMapping.inverseOutputGamma") ||
            !containsText(shaderSource, "applyToneMapping") ||
            !containsText(shaderSource, "linearToOutput") ||
            !containsText(shaderSource, "pow") ||
            !containsText(shaderSource, "Sample")) {
            std::cerr << "Tone mapping fragment shader does not expose expected HDR sampling path\n";
            return false;
        }

        return true;
    }

    bool validateEquirectToCubeVertexShaderSource() {
        const ark::Path shaderPath = findShaderSource("equirect_to_cube.vert.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find equirect-to-cube vertex shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read equirect-to-cube vertex shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "SV_VertexID") ||
            !containsText(shaderSource, "[[vk::location(0)]] float2 uv") ||
            !containsText(shaderSource, "positions[3]")) {
            std::cerr << "Equirect-to-cube vertex shader does not expose expected fullscreen triangle path\n";
            return false;
        }

        return true;
    }

    bool validateEquirectToCubeFragmentShaderSource() {
        const ark::Path shaderPath = findShaderSource("equirect_to_cube.frag.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find equirect-to-cube fragment shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read equirect-to-cube fragment shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "struct EquirectToCubeUniform") ||
            !containsText(shaderSource, "[[vk::binding(0, 0)]]") ||
            !containsText(shaderSource, "g_Conversion.faceIndex") ||
            !containsText(shaderSource, "[[vk::binding(1, 0)]]") ||
            !containsText(shaderSource, "Texture2D<float4> g_SourceEnvironment") ||
            !containsText(shaderSource, "[[vk::binding(2, 0)]]") ||
            !containsText(shaderSource, "SamplerState g_SourceSampler") ||
            !containsText(shaderSource, "directionToEquirectUv") ||
            !containsText(shaderSource, "faceUvToDirection") ||
            !containsText(shaderSource, "Face order: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z") ||
            !containsText(shaderSource, "Sample")) {
            std::cerr << "Equirect-to-cube fragment shader does not expose expected conversion path\n";
            return false;
        }

        if (containsText(shaderSource, "pow") || containsText(shaderSource, "linearToOutput") ||
            containsText(shaderSource, "applyToneMapping")) {
            std::cerr << "Equirect-to-cube fragment shader should output linear HDR without tone mapping\n";
            return false;
        }

        return true;
    }

    bool validateSkyboxVertexShaderSource() {
        const ark::Path shaderPath = findShaderSource("skybox.vert.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find skybox vertex shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read skybox vertex shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "SV_VertexID") ||
            !containsText(shaderSource, "[[vk::location(0)]] float2 uv") ||
            !containsText(shaderSource, "positions[3]")) {
            std::cerr << "Skybox vertex shader does not expose expected fullscreen triangle path\n";
            return false;
        }

        return true;
    }

    bool validateSkyboxFragmentShaderSource() {
        const ark::Path shaderPath = findShaderSource("skybox.frag.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find skybox fragment shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read skybox fragment shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "struct SkyboxUniform") ||
            !containsText(shaderSource, "inverseProjection") ||
            !containsText(shaderSource, "inverseViewRotation") ||
            !containsText(shaderSource, "TextureCube<float4> g_SkyboxCube") ||
            !containsText(shaderSource, "SamplerState g_SkyboxSampler") ||
            !containsText(shaderSource, "reconstructWorldDirection") ||
            !containsText(shaderSource, "Sample")) {
            std::cerr << "Skybox fragment shader does not expose expected cubemap sampling path\n";
            return false;
        }

        if (containsText(shaderSource, "pow") || containsText(shaderSource, "linearToOutput") ||
            containsText(shaderSource, "applyToneMapping")) {
            std::cerr << "Skybox fragment shader should output linear HDR without tone mapping\n";
            return false;
        }

        return true;
    }

    bool validateIrradianceVertexShaderSource() {
        const ark::Path shaderPath = findShaderSource("irradiance_convolve.vert.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find irradiance vertex shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read irradiance vertex shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "SV_VertexID") ||
            !containsText(shaderSource, "[[vk::location(0)]] float2 uv") ||
            !containsText(shaderSource, "positions[3]")) {
            std::cerr << "Irradiance vertex shader does not expose expected fullscreen triangle path\n";
            return false;
        }

        return true;
    }

    bool validateIrradianceFragmentShaderSource() {
        const ark::Path shaderPath = findShaderSource("irradiance_convolve.frag.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find irradiance fragment shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read irradiance fragment shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "struct IrradianceUniform") ||
            !containsText(shaderSource, "sampleDelta") ||
            !containsText(shaderSource, "TextureCube<float4> g_SourceEnvironmentCube") ||
            !containsText(shaderSource, "SamplerState g_SourceSampler") ||
            !containsText(shaderSource, "faceUvToDirection") ||
            !containsText(shaderSource, "buildBasis") ||
            !containsText(shaderSource, "cos") ||
            !containsText(shaderSource, "sin") ||
            !containsText(shaderSource, "PI") ||
            !containsText(shaderSource, "Sample")) {
            std::cerr << "Irradiance fragment shader does not expose expected convolution path\n";
            return false;
        }

        if (containsText(shaderSource, "linearToOutput") || containsText(shaderSource, "applyToneMapping")) {
            std::cerr << "Irradiance fragment shader should output linear HDR without tone mapping\n";
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
    const bool equirectToCubeVertexShaderValid = validateCompiledShader("equirect_to_cube.vert.spv");
    const bool equirectToCubeFragmentShaderValid = validateCompiledShader("equirect_to_cube.frag.spv");
    const bool irradianceVertexShaderValid = validateCompiledShader("irradiance_convolve.vert.spv");
    const bool irradianceFragmentShaderValid = validateCompiledShader("irradiance_convolve.frag.spv");
    const bool skyboxVertexShaderValid = validateCompiledShader("skybox.vert.spv");
    const bool skyboxFragmentShaderValid = validateCompiledShader("skybox.frag.spv");

    return vertexShaderValid && fragmentShaderValid && cubeVertexShaderValid && cubeFragmentShaderValid &&
                   texturedCubeVertexShaderValid && texturedCubeFragmentShaderValid && meshVertexShaderValid &&
                   meshFragmentShaderValid && toneMappingVertexShaderValid && toneMappingFragmentShaderValid &&
                   equirectToCubeVertexShaderValid && equirectToCubeFragmentShaderValid &&
                   irradianceVertexShaderValid && irradianceFragmentShaderValid &&
                   skyboxVertexShaderValid && skyboxFragmentShaderValid &&
                   validateMeshVertexShaderSource() && validateMeshFragmentShaderSource() &&
                   validateToneMappingVertexShaderSource() && validateToneMappingFragmentShaderSource() &&
                   validateEquirectToCubeVertexShaderSource() && validateEquirectToCubeFragmentShaderSource() &&
                   validateIrradianceVertexShaderSource() && validateIrradianceFragmentShaderSource() &&
                   validateSkyboxVertexShaderSource() && validateSkyboxFragmentShaderSource()
               ? 0
               : 1;
}
