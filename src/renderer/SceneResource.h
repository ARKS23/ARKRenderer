#pragma once

#include "asset/MeshData.h"
#include "core/FileSystem.h"
#include "renderer/EnvironmentResource.h"
#include "renderer/ModelResource.h"
#include "renderer/RenderScene.h"

#include <string>

namespace ark::rhi {
    class RenderDevice;
} // namespace ark::rhi

namespace ark {
    enum class SceneModelFallbackPolicy {
        None,
        DefaultSandboxModel,
    };

    enum class SceneEnvironmentFallbackPolicy {
        None,
        DefaultHdrThenProcedural,
        ProceduralOnly,
        DebugOrientation,
    };

    enum class SceneModelSource {
        None,
        RequestedPath,
        DefaultFallback,
    };

    enum class SceneEnvironmentSource {
        None,
        RequestedHdr,
        DefaultHdr,
        Procedural,
        DebugOrientation,
    };

    struct SceneResourceLoadDesc {
        Path modelPath;
        SceneModelFallbackPolicy modelFallback = SceneModelFallbackPolicy::DefaultSandboxModel;

        Path environmentPath;
        SceneEnvironmentFallbackPolicy environmentFallback =
            SceneEnvironmentFallbackPolicy::DefaultHdrThenProcedural;

        std::string sceneName = "DefaultScene";
        std::string modelName = "DefaultModel";
        std::string environmentName = "DefaultEnvironment";
        float environmentIntensity = 1.0f;
    };

    struct SceneResourceLoadReport {
        bool modelLoaded = false;
        bool environmentLoaded = false;
        Path resolvedModelPath;
        Path resolvedEnvironmentPath;
        SceneModelSource modelSource = SceneModelSource::None;
        SceneEnvironmentSource environmentSource = SceneEnvironmentSource::None;
    };

    class SceneResource final {
    public:
        SceneResource() = default;

        bool load(rhi::RenderDevice& device, const SceneResourceLoadDesc& desc);
        void resetImmediate();

        RenderScene& scene() {
            return m_Scene;
        }

        const RenderScene& scene() const {
            return m_Scene;
        }

        const asset::ModelData& modelData() const {
            return m_ModelData;
        }

        ModelResource* model() {
            return m_Report.modelLoaded ? &m_Model : nullptr;
        }

        const ModelResource* model() const {
            return m_Report.modelLoaded ? &m_Model : nullptr;
        }

        EnvironmentResource* environment() {
            return m_Report.environmentLoaded ? &m_Environment : nullptr;
        }

        const EnvironmentResource* environment() const {
            return m_Report.environmentLoaded ? &m_Environment : nullptr;
        }

        const SceneResourceLoadReport& report() const {
            return m_Report;
        }

        bool hasScene() const {
            return m_Report.modelLoaded && !m_Scene.empty();
        }

    private:
        asset::ModelData m_ModelData;
        ModelResource m_Model;
        EnvironmentResource m_Environment;
        RenderScene m_Scene;
        SceneResourceLoadReport m_Report;
    };
} // namespace ark

