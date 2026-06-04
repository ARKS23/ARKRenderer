#pragma once

#include "app/Window.h"

#include <memory>

namespace ark {
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
        std::unique_ptr<Window> m_Window;
    };
} // namespace ark
