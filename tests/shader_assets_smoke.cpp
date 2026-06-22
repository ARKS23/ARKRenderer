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
            !containsText(shaderSource, "[[vk::binding(18, 0)]]") ||
            !containsText(shaderSource, "TextureCube<float4> g_PrefilteredSpecularCube") ||
            !containsText(shaderSource, "[[vk::binding(19, 0)]]") ||
            !containsText(shaderSource, "SamplerState g_PrefilteredSpecularSampler") ||
            !containsText(shaderSource, "[[vk::binding(20, 0)]]") ||
            !containsText(shaderSource, "Texture2D<float4> g_BrdfLut") ||
            !containsText(shaderSource, "[[vk::binding(21, 0)]]") ||
            !containsText(shaderSource, "SamplerState g_BrdfLutSampler") ||
            !containsText(shaderSource, "[[vk::binding(22, 0)]]") ||
            !containsText(shaderSource, "Texture2D<float> g_ShadowMap") ||
            !containsText(shaderSource, "[[vk::binding(23, 0)]]") ||
            !containsText(shaderSource, "SamplerState g_ShadowSampler") ||
            !containsText(shaderSource, "[[vk::binding(24, 0)]]") ||
            !containsText(shaderSource, "Texture2DArray<float> g_CascadeShadowMap") ||
            !containsText(shaderSource, "lightViewProjection") ||
            !containsText(shaderSource, "cascadeLightViewProjections") ||
            !containsText(shaderSource, "cascadeSplits") ||
            !containsText(shaderSource, "MaxShadowCascadeCount") ||
            !containsText(shaderSource, "sampleShadowVisibility") ||
            !containsText(shaderSource, "sampleCascadeShadowVisibility") ||
            !containsText(shaderSource, "ShadowFilterPcf3x3") ||
            !containsText(shaderSource, "ShadowFilterPcf5x5") ||
            !containsText(shaderSource, "sampleShadowCompare") ||
            !containsText(shaderSource, "sampleShadowPcf") ||
            !containsText(shaderSource, "sampleCascadeShadowPcf") ||
            !containsText(shaderSource, "GetDimensions") ||
            !containsText(shaderSource, "g_Camera.view") ||
            !containsText(shaderSource, "g_Lighting.shadow.z") ||
            !containsText(shaderSource, "g_Lighting.shadow.w") ||
            !containsText(shaderSource, "g_Lighting.cascadeShadow.w") ||
            !containsText(shaderSource, "ShadowDebugModeCascadeColor") ||
            !containsText(shaderSource, "ShadowDebugModeShadowFactor") ||
            !containsText(shaderSource, "ShadowDebugModeLightDepth") ||
            !containsText(shaderSource, "cascadeDebugColor") ||
            !containsText(shaderSource, "resolveDebugCascadeIndex") ||
            !containsText(shaderSource, "applyShadowDebugOverlay") ||
            !containsText(shaderSource, "normalize(normal)") ||
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
            !containsText(shaderSource, "fresnelSchlickRoughness") ||
            !containsText(shaderSource, "nDotV") ||
            !containsText(shaderSource, "vDotH") ||
            !containsText(shaderSource, "f0") ||
            !containsText(shaderSource, "specularDenominator") ||
            !containsText(shaderSource, "directionToEquirectUv") ||
            !containsText(shaderSource, "sampleEnvironment") ||
            !containsText(shaderSource, "sampleIrradiance") ||
            !containsText(shaderSource, "sampleDiffuseIbl") ||
            !containsText(shaderSource, "samplePrefilteredSpecular") ||
            !containsText(shaderSource, "sampleBrdfLut") ||
            !containsText(shaderSource, "SampleLevel") ||
            !containsText(shaderSource, "reflect") ||
            !containsText(shaderSource, "evaluateIndirectLighting") ||
            !containsText(shaderSource, "g_Lighting.environment") ||
            !containsText(shaderSource, "g_Lighting.environment.z") ||
            !containsText(shaderSource, "g_Lighting.environment.w") ||
            !containsText(shaderSource, "environmentSpecular") ||
            !containsText(shaderSource, "shadowVisibility")) {
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
            !containsText(shaderSource, "g_ToneMapping.operatorType") ||
            !containsText(shaderSource, "applyToneMapping") ||
            !containsText(shaderSource, "toneMapLinear") ||
            !containsText(shaderSource, "toneMapReinhard") ||
            !containsText(shaderSource, "toneMapACES") ||
            !containsText(shaderSource, "ACES fitted approximation") ||
            !containsText(shaderSource, "linearToOutput") ||
            !containsText(shaderSource, "pow") ||
            !containsText(shaderSource, "Sample")) {
            std::cerr << "Tone mapping fragment shader does not expose expected HDR sampling path\n";
            return false;
        }

        return true;
    }

    bool validateShadowVertexShaderSource() {
        const ark::Path shaderPath = findShaderSource("shadow.vert.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find shadow vertex shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read shadow vertex shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "struct ShadowUniform") ||
            !containsText(shaderSource, "lightViewProjection") ||
            !containsText(shaderSource, "float4x4 model") ||
            !containsText(shaderSource, "[[vk::location(1)]] float2 uv0") ||
            !containsText(shaderSource, "[[vk::location(2)]] float2 uv1") ||
            !containsText(shaderSource, "[[vk::binding(0, 0)]]") ||
            !containsText(shaderSource, "mul(g_Shadow.lightViewProjection")) {
            std::cerr << "Shadow vertex shader does not expose expected depth transform path\n";
            return false;
        }

        return true;
    }

    bool validateShadowFragmentShaderSource() {
        const ark::Path shaderPath = findShaderSource("shadow.frag.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find shadow fragment shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read shadow fragment shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "Texture2D<float4> g_BaseColorTexture") ||
            !containsText(shaderSource, "SamplerState g_BaseColorSampler") ||
            !containsText(shaderSource, "struct ShadowMaterialUniform") ||
            !containsText(shaderSource, "alphaCutoff") ||
            !containsText(shaderSource, "AlphaModeMask") ||
            !containsText(shaderSource, "discard")) {
            std::cerr << "Shadow fragment shader does not expose expected alpha mask caster path\n";
            return false;
        }

        return true;
    }

    bool validateBloomFragmentShaderSource() {
        const ark::Path shaderPath = findShaderSource("bloom.frag.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find bloom fragment shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read bloom fragment shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "linear HDR scene color") ||
            !containsText(shaderSource, "Texture2D<float4> g_Source0") ||
            !containsText(shaderSource, "Texture2D<float4> g_Source1") ||
            !containsText(shaderSource, "SamplerState g_BloomSampler") ||
            !containsText(shaderSource, "struct BloomUniform") ||
            !containsText(shaderSource, "g_Bloom.intensity") ||
            !containsText(shaderSource, "g_Bloom.scatter") ||
            !containsText(shaderSource, "g_Bloom.threshold") ||
            !containsText(shaderSource, "g_Bloom.softKnee") ||
            !containsText(shaderSource, "softThresholdResponse") ||
            !containsText(shaderSource, "sampleBloomFilter") ||
            !containsText(shaderSource, "prefilterBloom") ||
            !containsText(shaderSource, "downsampleBloom") ||
            !containsText(shaderSource, "upsampleBloom") ||
            !containsText(shaderSource, "compositeBloom") ||
            !containsText(shaderSource, "BloomModePrefilter") ||
            !containsText(shaderSource, "BloomModeComposite")) {
            std::cerr << "Bloom fragment shader does not expose expected HDR bloom path\n";
            return false;
        }

        if (containsText(shaderSource, "linearToOutput") ||
            containsText(shaderSource, "applyToneMapping")) {
            std::cerr << "Bloom fragment shader should output linear HDR without tone mapping\n";
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
            !containsText(shaderSource, "case 0:") ||
            !containsText(shaderSource, "case 1:") ||
            !containsText(shaderSource, "case 2:") ||
            !containsText(shaderSource, "case 3:") ||
            !containsText(shaderSource, "case 4:") ||
            !containsText(shaderSource, "return normalize(float3(1.0f, -y, -x))") ||
            !containsText(shaderSource, "return normalize(float3(-x, -y, -1.0f))") ||
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
            !containsText(shaderSource, "Face order: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z") ||
            !containsText(shaderSource, "case 0:") ||
            !containsText(shaderSource, "case 1:") ||
            !containsText(shaderSource, "case 2:") ||
            !containsText(shaderSource, "case 3:") ||
            !containsText(shaderSource, "case 4:") ||
            !containsText(shaderSource, "return normalize(float3(1.0f, -y, -x))") ||
            !containsText(shaderSource, "return normalize(float3(-x, -y, -1.0f))") ||
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

    bool validateSpecularPrefilterVertexShaderSource() {
        const ark::Path shaderPath = findShaderSource("specular_prefilter.vert.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find specular prefilter vertex shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read specular prefilter vertex shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "SV_VertexID") ||
            !containsText(shaderSource, "[[vk::location(0)]] float2 uv") ||
            !containsText(shaderSource, "positions[3]")) {
            std::cerr << "Specular prefilter vertex shader does not expose expected fullscreen triangle path\n";
            return false;
        }

        return true;
    }

    bool validateSpecularPrefilterFragmentShaderSource() {
        const ark::Path shaderPath = findShaderSource("specular_prefilter.frag.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find specular prefilter fragment shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read specular prefilter fragment shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "struct SpecularPrefilterUniform") ||
            !containsText(shaderSource, "roughness") ||
            !containsText(shaderSource, "sampleCount") ||
            !containsText(shaderSource, "mipLevel") ||
            !containsText(shaderSource, "TextureCube<float4> g_SourceEnvironmentCube") ||
            !containsText(shaderSource, "SamplerState g_SourceSampler") ||
            !containsText(shaderSource, "faceUvToDirection") ||
            !containsText(shaderSource, "Face order: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z") ||
            !containsText(shaderSource, "case 0:") ||
            !containsText(shaderSource, "case 1:") ||
            !containsText(shaderSource, "case 2:") ||
            !containsText(shaderSource, "case 3:") ||
            !containsText(shaderSource, "case 4:") ||
            !containsText(shaderSource, "return normalize(float3(1.0f, -y, -x))") ||
            !containsText(shaderSource, "return normalize(float3(-x, -y, -1.0f))") ||
            !containsText(shaderSource, "Hammersley") ||
            !containsText(shaderSource, "ImportanceSampleGGX") ||
            !containsText(shaderSource, "SampleLevel") ||
            !containsText(shaderSource, "PI")) {
            std::cerr << "Specular prefilter fragment shader does not expose expected GGX prefilter path\n";
            return false;
        }

        if (containsText(shaderSource, "linearToOutput") || containsText(shaderSource, "applyToneMapping")) {
            std::cerr << "Specular prefilter fragment shader should output linear HDR without tone mapping\n";
            return false;
        }

        return true;
    }

    bool validateBrdfLutVertexShaderSource() {
        const ark::Path shaderPath = findShaderSource("brdf_lut.vert.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find BRDF LUT vertex shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read BRDF LUT vertex shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "SV_VertexID") ||
            !containsText(shaderSource, "[[vk::location(0)]] float2 uv") ||
            !containsText(shaderSource, "positions[3]")) {
            std::cerr << "BRDF LUT vertex shader does not expose expected fullscreen triangle path\n";
            return false;
        }

        return true;
    }

    bool validateBrdfLutFragmentShaderSource() {
        const ark::Path shaderPath = findShaderSource("brdf_lut.frag.hlsl");
        if (shaderPath.empty()) {
            std::cerr << "Failed to find BRDF LUT fragment shader source\n";
            return false;
        }

        const std::vector<ark::u8> shaderSource = ark::readBinaryFile(shaderPath);
        if (shaderSource.empty()) {
            std::cerr << "Failed to read BRDF LUT fragment shader source\n";
            return false;
        }

        if (!containsText(shaderSource, "struct BrdfLutUniform") ||
            !containsText(shaderSource, "sampleCount") ||
            !containsText(shaderSource, "roughness") ||
            !containsText(shaderSource, "nDotV") ||
            !containsText(shaderSource, "Hammersley") ||
            !containsText(shaderSource, "ImportanceSampleGGX") ||
            !containsText(shaderSource, "GeometrySmith") ||
            !containsText(shaderSource, "IntegrateBRDF") ||
            !containsText(shaderSource, "geometryVisibility") ||
            !containsText(shaderSource, "float4(brdf.x, brdf.y, 0.0f, 1.0f)") ||
            !containsText(shaderSource, "PI")) {
            std::cerr << "BRDF LUT fragment shader does not expose expected split-sum integration path\n";
            return false;
        }

        if (containsText(shaderSource, "TextureCube") ||
            containsText(shaderSource, "Texture2D") ||
            containsText(shaderSource, "Sample(") ||
            containsText(shaderSource, "linearToOutput") ||
            containsText(shaderSource, "applyToneMapping")) {
            std::cerr << "BRDF LUT fragment shader should be generated data without texture sampling or tone mapping\n";
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
    const bool shadowVertexShaderValid = validateCompiledShader("shadow.vert.spv");
    const bool shadowFragmentShaderValid = validateCompiledShader("shadow.frag.spv");
    const bool toneMappingVertexShaderValid = validateCompiledShader("tonemap.vert.spv");
    const bool toneMappingFragmentShaderValid = validateCompiledShader("tonemap.frag.spv");
    const bool bloomFragmentShaderValid = validateCompiledShader("bloom.frag.spv");
    const bool brdfLutVertexShaderValid = validateCompiledShader("brdf_lut.vert.spv");
    const bool brdfLutFragmentShaderValid = validateCompiledShader("brdf_lut.frag.spv");
    const bool equirectToCubeVertexShaderValid = validateCompiledShader("equirect_to_cube.vert.spv");
    const bool equirectToCubeFragmentShaderValid = validateCompiledShader("equirect_to_cube.frag.spv");
    const bool irradianceVertexShaderValid = validateCompiledShader("irradiance_convolve.vert.spv");
    const bool irradianceFragmentShaderValid = validateCompiledShader("irradiance_convolve.frag.spv");
    const bool specularPrefilterVertexShaderValid = validateCompiledShader("specular_prefilter.vert.spv");
    const bool specularPrefilterFragmentShaderValid = validateCompiledShader("specular_prefilter.frag.spv");
    const bool skyboxVertexShaderValid = validateCompiledShader("skybox.vert.spv");
    const bool skyboxFragmentShaderValid = validateCompiledShader("skybox.frag.spv");

    return vertexShaderValid && fragmentShaderValid && cubeVertexShaderValid && cubeFragmentShaderValid &&
                   texturedCubeVertexShaderValid && texturedCubeFragmentShaderValid && meshVertexShaderValid &&
                   meshFragmentShaderValid && shadowVertexShaderValid && shadowFragmentShaderValid &&
                   toneMappingVertexShaderValid && toneMappingFragmentShaderValid &&
                   bloomFragmentShaderValid &&
                   brdfLutVertexShaderValid && brdfLutFragmentShaderValid &&
                   equirectToCubeVertexShaderValid && equirectToCubeFragmentShaderValid &&
                   irradianceVertexShaderValid && irradianceFragmentShaderValid &&
                   specularPrefilterVertexShaderValid && specularPrefilterFragmentShaderValid &&
                   skyboxVertexShaderValid && skyboxFragmentShaderValid &&
                   validateMeshVertexShaderSource() && validateMeshFragmentShaderSource() &&
                   validateShadowVertexShaderSource() && validateShadowFragmentShaderSource() &&
                   validateToneMappingVertexShaderSource() && validateToneMappingFragmentShaderSource() &&
                   validateBloomFragmentShaderSource() &&
                   validateBrdfLutVertexShaderSource() && validateBrdfLutFragmentShaderSource() &&
                   validateEquirectToCubeVertexShaderSource() && validateEquirectToCubeFragmentShaderSource() &&
                   validateIrradianceVertexShaderSource() && validateIrradianceFragmentShaderSource() &&
                   validateSpecularPrefilterVertexShaderSource() &&
                   validateSpecularPrefilterFragmentShaderSource() &&
                   validateSkyboxVertexShaderSource() && validateSkyboxFragmentShaderSource()
               ? 0
               : 1;
}
