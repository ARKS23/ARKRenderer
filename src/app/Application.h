#pragma once

#include "app/SandboxCameraController.h"
#include "app/Window.h"
#include "core/FileSystem.h"
#include "core/Memory.h"
#include "renderer/PostProcessingSettings.h"
#include "renderer/RendererQuality.h"
#include "renderer/RenderView.h"
#include "renderer/SceneResource.h"

#include <vector>

namespace ark {
    class Renderer;

    struct ApplicationDesc {
        WindowDesc window;
        Path defaultModelPath;
        std::vector<SceneAdditionalModelDesc> defaultAdditionalModels;
        Path defaultEnvironmentPath;
        RendererQualityDesc rendererQuality;
        ToneMappingSettings toneMapping;
        PostProcessingSettings postProcessing;
        ShadowSettings shadows;
        SandboxCameraControllerDesc camera;
        bool useDebugOrientationEnvironment = false;
    };

    class Application {
    public:
        explicit Application(ApplicationDesc desc = {});
        ~Application();

        int run();

    private:
        ApplicationDesc m_Desc;
        Scope<Window> m_Window;
        Scope<Renderer> m_Renderer;
    };
} // namespace ark
