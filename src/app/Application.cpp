#include "app/Application.h"

#include "app/GlfwWindow.h"
#include "core/Log.h"

#include <cstdlib>
#include <exception>
#include <utility>

namespace ark {
    Application::Application(ApplicationDesc desc) : m_Desc(std::move(desc)) {
    }

    Application::~Application() = default;

    int Application::run() {
        Log::initialize();
        ARK_INFO("ARKRenderer started");

        try {
            m_Window = std::make_unique<GlfwWindow>(m_Desc.window);

            while (!m_Window->shouldClose()) {
                m_Window->pollEvents();
            }

            // 先释放窗口，再关闭日志，确保窗口析构过程中的日志仍然可见。
            m_Window.reset();
            ARK_INFO("ARKRenderer exited normally");
            Log::shutdown();
            return EXIT_SUCCESS;
        } catch (const std::exception& exception) {
            ARK_ERROR("Application failed: {}", exception.what());
            m_Window.reset();
            Log::shutdown();
            return EXIT_FAILURE;
        }
    }
} // namespace ark
