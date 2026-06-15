#include "app/SandboxLaunchOptions.h"
#include "asset/GltfLoader.h"
#include "core/FileSystem.h"
#include "renderer/RendererPreset.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

namespace {
    bool near(float lhs, float rhs, float epsilon = 0.0001f) {
        return std::fabs(lhs - rhs) <= epsilon;
    }

    ark::Path findFixturePath(const ark::Path& relative) {
        const std::array<ark::Path, 3> candidates{
            relative,
            ark::Path{"../"} / relative,
            ark::Path{"../../"} / relative,
        };

        return ark::findFirstExistingPath(candidates);
    }

    bool validateFixtureLoad() {
        const ark::Path path = findFixturePath(ark::Path{"assets/models/bloom_validation_fixture.gltf"});
        if (path.empty()) {
            std::cerr << "Failed to find bloom validation fixture\n";
            return false;
        }

        const ark::asset::ModelData model = ark::asset::loadGltfModel(path);
        if (model.empty() || model.meshes.size() != 4 || model.materials.size() != 4 ||
            model.cameras.size() != 1 || model.sceneCameras.size() != 1) {
            std::cerr << "Unexpected bloom validation model shape\n";
            return false;
        }

        const ark::asset::MaterialData& warmMaterial = model.materials[1];
        const ark::asset::MaterialData& coolMaterial = model.materials[2];
        if (!warmMaterial.hasEmissiveTexture() || !coolMaterial.hasEmissiveTexture() ||
            warmMaterial.emissiveTexturePath.filename() != "xiaowei.png" ||
            coolMaterial.emissiveTexturePath.filename() != "xiaowei.png") {
            std::cerr << "Bloom emissive textures are missing\n";
            return false;
        }

        if (!near(warmMaterial.emissiveFactor[0], 8.0f) ||
            !near(warmMaterial.emissiveFactor[1], 5.0f) ||
            !near(warmMaterial.emissiveFactor[2], 2.0f) ||
            !near(coolMaterial.emissiveFactor[0], 1.5f) ||
            !near(coolMaterial.emissiveFactor[1], 4.0f) ||
            !near(coolMaterial.emissiveFactor[2], 10.0f)) {
            std::cerr << "Bloom emissive factors are invalid\n";
            return false;
        }

        if (!near(model.materials.front().baseColorFactor[0], 0.015f) ||
            !near(model.materials.front().roughnessFactor, 0.85f)) {
            std::cerr << "Bloom dark material is invalid\n";
            return false;
        }

        return true;
    }

    bool validatePresetResolution() {
        ark::RendererPresetDesc preset{};
        preset.scene = ark::RendererScenePreset::BloomValidation;
        const ark::ResolvedRendererPreset resolved = ark::resolveRendererPreset(preset);
        if (resolved.scene.modelPath.filename() != "bloom_validation_fixture.gltf" ||
            resolved.scene.sceneName != "BloomValidationScene" ||
            resolved.scene.modelName != "BloomValidationModel" ||
            resolved.scene.environmentName != "BloomValidationEnvironment") {
            std::cerr << "Bloom validation preset resolution is invalid\n";
            return false;
        }

        if (ark::parseRendererScenePreset("bloom") != ark::RendererScenePreset::BloomValidation ||
            ark::parseRendererScenePreset("emissive-bloom") !=
                ark::RendererScenePreset::BloomValidation) {
            std::cerr << "Bloom validation preset parsing is invalid\n";
            return false;
        }

        return true;
    }

    bool validateSandboxLaunchOptions() {
        constexpr std::array<std::string_view, 4> args{
            "--preset",
            "bloom-validation",
            "--bloom",
            "--bloom-intensity=0.12",
        };
        const ark::SandboxLaunchOptions options =
            ark::parseSandboxLaunchOptions(std::span<const std::string_view>{args});
        const ark::ApplicationDesc desc = ark::makeSandboxApplicationDesc(options);
        if (!desc.postProcessing.bloom.enabled || !near(desc.postProcessing.bloom.intensity, 0.12f) ||
            desc.defaultModelPath.filename() != "bloom_validation_fixture.gltf") {
            std::cerr << "Bloom validation sandbox options are invalid\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateFixtureLoad() && validatePresetResolution() && validateSandboxLaunchOptions()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
