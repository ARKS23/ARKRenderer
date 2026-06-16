#pragma once

#include "app/Window.h"
#include "core/FileSystem.h"
#include "core/Memory.h"
#include "renderer/PostProcessingSettings.h"
#include "renderer/RendererQuality.h"
#include "renderer/RenderView.h"

namespace ark {
    class Renderer;

    struct ApplicationDesc {
        WindowDesc window;
        Path defaultModelPath;
        Path defaultEnvironmentPath;
        RendererQualityDesc rendererQuality;
        ToneMappingSettings toneMapping;
        PostProcessingSettings postProcessing;
        ShadowSettings shadows;
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
