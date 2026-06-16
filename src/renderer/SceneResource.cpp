#include "renderer/SceneResource.h"

#include "asset/GltfLoader.h"
#include "asset/TextureLoader.h"
#include "core/Log.h"
#include "renderer/SandboxEnvironment.h"
#include "rhi/RenderDevice.h"

#include <glm/mat4x4.hpp>

#include <algorithm>
#include <array>
#include <utility>

namespace ark {
    namespace {
        constexpr const char* PreferredDefaultModelAssetPath = "assets/models/DamagedHelmet/DamagedHelmet.gltf";
        constexpr const char* FallbackDefaultModelAssetPath = "assets/models/forward_multinode_fixture.gltf";
        constexpr const char* DefaultEnvironmentAssetPath = "assets/HDR/2k.hdr";

        Path findResourceFile(const Path& requestedPath) {
            if (requestedPath.empty()) {
                return {};
            }

            if (requestedPath.is_absolute()) {
                return fileExists(requestedPath) ? requestedPath : Path{};
            }

            const std::array<Path, 5> candidates{
                requestedPath,
                Path{"../"} / requestedPath,
                Path{"../../"} / requestedPath,
                Path{"../../../"} / requestedPath,
                Path{"../../../../"} / requestedPath,
            };

            return findFirstExistingPath(candidates);
        }

        const char* toString(SceneModelSource source) {
            switch (source) {
            case SceneModelSource::None:
                return "None";
            case SceneModelSource::RequestedPath:
                return "RequestedPath";
            case SceneModelSource::DefaultFallback:
                return "DefaultFallback";
            }

            return "Unknown";
        }

        const char* toString(SceneEnvironmentSource source) {
            switch (source) {
            case SceneEnvironmentSource::None:
                return "None";
            case SceneEnvironmentSource::RequestedHdr:
                return "RequestedHdr";
            case SceneEnvironmentSource::DefaultHdr:
                return "DefaultHdr";
            case SceneEnvironmentSource::Procedural:
                return "Procedural";
            case SceneEnvironmentSource::DebugOrientation:
                return "DebugOrientation";
            }

            return "Unknown";
        }

        Path findDefaultModelFallback() {
            Path modelPath = findResourceFile(Path{PreferredDefaultModelAssetPath});
            if (!modelPath.empty()) {
                return modelPath;
            }

            return findResourceFile(Path{FallbackDefaultModelAssetPath});
        }

        Path resolveModelPath(const SceneResourceLoadDesc& desc, SceneResourceLoadReport& report) {
            if (!desc.modelPath.empty()) {
                Path requestedPath = findResourceFile(desc.modelPath);
                if (!requestedPath.empty()) {
                    report.modelSource = SceneModelSource::RequestedPath;
                    report.resolvedModelPath = requestedPath;
                    return requestedPath;
                }

                ARK_WARN("Requested scene model was not found: {}", desc.modelPath.string());
            }

            if (desc.modelFallback == SceneModelFallbackPolicy::DefaultSandboxModel) {
                Path fallbackPath = findDefaultModelFallback();
                if (!fallbackPath.empty()) {
                    report.modelSource = SceneModelSource::DefaultFallback;
                    report.resolvedModelPath = fallbackPath;
                    return fallbackPath;
                }

                ARK_WARN("Default scene model fallbacks were not found: {} or {}",
                         PreferredDefaultModelAssetPath,
                         FallbackDefaultModelAssetPath);
            }

            return {};
        }

        Path resolveAdditionalModelPath(const SceneAdditionalModelDesc& desc) {
            if (desc.modelPath.empty()) {
                return {};
            }

            Path requestedPath = findResourceFile(desc.modelPath);
            if (requestedPath.empty()) {
                ARK_WARN("Requested additional scene model was not found: {}", desc.modelPath.string());
            }

            return requestedPath;
        }

        asset::ImageData loadEnvironmentImage(const Path& path) {
            if (path.empty()) {
                return {};
            }

            asset::ImageData image = asset::loadImageHdrRgba32F(path);
            if (image.empty()) {
                ARK_WARN("Scene environment HDR image is empty or failed to load: {}", path.string());
            }

            return image;
        }

        asset::ImageData resolveEnvironmentImage(const SceneResourceLoadDesc& desc,
                                                 SceneResourceLoadReport& report) {
            if (desc.environmentFallback == SceneEnvironmentFallbackPolicy::None) {
                if (desc.environmentPath.empty()) {
                    return {};
                }

                const Path environmentPath = findResourceFile(desc.environmentPath);
                if (environmentPath.empty()) {
                    ARK_WARN("Requested scene environment was not found: {}", desc.environmentPath.string());
                    return {};
                }

                report.environmentSource = SceneEnvironmentSource::RequestedHdr;
                report.resolvedEnvironmentPath = environmentPath;
                return loadEnvironmentImage(environmentPath);
            }

            if (desc.environmentFallback == SceneEnvironmentFallbackPolicy::DebugOrientation) {
                report.environmentSource = SceneEnvironmentSource::DebugOrientation;
                return makeDebugOrientationEnvironmentImage();
            }

            if (!desc.environmentPath.empty()) {
                const Path environmentPath = findResourceFile(desc.environmentPath);
                if (!environmentPath.empty()) {
                    asset::ImageData requestedImage = loadEnvironmentImage(environmentPath);
                    if (!requestedImage.empty()) {
                        report.environmentSource = SceneEnvironmentSource::RequestedHdr;
                        report.resolvedEnvironmentPath = environmentPath;
                        return requestedImage;
                    }
                } else {
                    ARK_WARN("Requested scene environment was not found: {}", desc.environmentPath.string());
                }
            }

            if (desc.environmentFallback == SceneEnvironmentFallbackPolicy::DefaultHdrThenProcedural) {
                const Path defaultEnvironmentPath = findResourceFile(Path{DefaultEnvironmentAssetPath});
                if (!defaultEnvironmentPath.empty()) {
                    asset::ImageData defaultImage = loadEnvironmentImage(defaultEnvironmentPath);
                    if (!defaultImage.empty()) {
                        report.environmentSource = SceneEnvironmentSource::DefaultHdr;
                        report.resolvedEnvironmentPath = defaultEnvironmentPath;
                        return defaultImage;
                    }
                } else {
                    ARK_INFO("Default scene environment HDR was not found: {}", DefaultEnvironmentAssetPath);
                }
            }

            report.environmentSource = SceneEnvironmentSource::Procedural;
            return makeProceduralSandboxEnvironmentImage();
        }
    } // namespace

    bool SceneResource::load(rhi::RenderDevice& device, const SceneResourceLoadDesc& desc) {
        resetImmediate();

        const Path modelPath = resolveModelPath(desc, m_Report);
        if (modelPath.empty()) {
            ARK_ERROR("SceneResource load failed because no model path could be resolved");
            return false;
        }

        m_ModelData = asset::loadGltfModel(modelPath);
        if (m_ModelData.empty()) {
            ARK_ERROR("SceneResource failed to load model data: {}", modelPath.string());
            resetImmediate();
            return false;
        }

        if (!m_Model.create(device, m_ModelData)) {
            ARK_ERROR("SceneResource failed to create model resources: {}", modelPath.string());
            resetImmediate();
            return false;
        }

        m_Report.modelLoaded = true;
        m_Report.loadedModelCount = 1;
        m_Scene.addModel(m_Model, glm::mat4{1.0f}, desc.modelName);

        m_AdditionalModelData.reserve(desc.additionalModels.size());
        m_AdditionalModels.reserve(desc.additionalModels.size());
        m_Report.resolvedAdditionalModelPaths.reserve(desc.additionalModels.size());
        for (const SceneAdditionalModelDesc& additionalDesc : desc.additionalModels) {
            const Path additionalModelPath = resolveAdditionalModelPath(additionalDesc);
            if (additionalModelPath.empty()) {
                ARK_ERROR("SceneResource failed to resolve additional model: {}",
                          additionalDesc.modelPath.string());
                resetImmediate();
                return false;
            }

            asset::ModelData additionalModelData = asset::loadGltfModel(additionalModelPath);
            if (additionalModelData.empty()) {
                ARK_ERROR("SceneResource failed to load additional model data: {}",
                          additionalModelPath.string());
                resetImmediate();
                return false;
            }

            Scope<ModelResource> additionalModel = makeScope<ModelResource>();
            if (!additionalModel->create(device, additionalModelData)) {
                ARK_ERROR("SceneResource failed to create additional model resources: {}",
                          additionalModelPath.string());
                resetImmediate();
                return false;
            }

            m_AdditionalModelData.push_back(std::move(additionalModelData));
            m_AdditionalModels.push_back(std::move(additionalModel));
            m_Report.resolvedAdditionalModelPaths.push_back(additionalModelPath);
            m_Scene.addModel(*m_AdditionalModels.back(), additionalDesc.transform, additionalDesc.modelName);
            ++m_Report.loadedModelCount;
        }

        asset::ImageData environmentImage = resolveEnvironmentImage(desc, m_Report);
        if (!environmentImage.empty()) {
            EnvironmentResourceDesc environmentDesc{};
            environmentDesc.debugName = desc.environmentName;
            if (!m_Environment.create(device, environmentImage, environmentDesc)) {
                ARK_ERROR("SceneResource failed to create environment resource");
                resetImmediate();
                return false;
            }

            m_Report.environmentLoaded = true;
            SceneEnvironment environment{};
            environment.environment = &m_Environment;
            environment.intensity = std::max(desc.environmentIntensity, 0.0f);
            m_Scene.setEnvironment(environment);
        }

        ARK_INFO("SceneResource loaded modelSource={}, environmentSource={}",
                 toString(m_Report.modelSource),
                 toString(m_Report.environmentSource));
        return true;
    }

    void SceneResource::resetImmediate() {
        m_Scene.clear();
        m_Scene.clearEnvironment();
        m_Environment.resetImmediate();
        for (Scope<ModelResource>& additionalModel : m_AdditionalModels) {
            if (additionalModel) {
                additionalModel->reset();
            }
        }
        m_AdditionalModels.clear();
        m_AdditionalModelData.clear();
        m_Model.reset();
        m_ModelData = {};
        m_Report = {};
    }
} // namespace ark
