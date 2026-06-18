#pragma once

#include "app/Window.h"
#include "core/Memory.h"
#include "renderer/RendererPreset.h"

namespace ark {
    class Renderer;

    struct ApplicationDesc {
        WindowDesc window;
        SceneResourceLoadDesc defaultScene;
        RendererQualityDesc rendererQuality;
        RenderViewProfileDesc view;
        OrbitCameraProfileDesc camera;
        bool useDebugOrientationEnvironment = false;
        bool debugUiEnabled = true;
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
