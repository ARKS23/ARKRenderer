#pragma once

#include "app/Window.h"
#include "core/Memory.h"

namespace ark {
    class Renderer;

    struct ApplicationDesc {
        WindowDesc window;
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
