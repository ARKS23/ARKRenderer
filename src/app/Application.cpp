#include "app/Application.h"

#include "app/GlfwWindow.h"
#include "core/Log.h"
#include "core/Memory.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/Renderer.h"

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
            m_Window = makeScope<GlfwWindow>(m_Desc.window);

            RendererDesc rendererDesc{};
            rendererDesc.nativeWindow = m_Window->getNativeWindowHandle();
            rendererDesc.extent = m_Window->getExtent();
#ifndef NDEBUG
            rendererDesc.enableValidation = true;
#endif

            m_Renderer = createRenderer(rendererDesc);

            RenderScene scene;
            RenderView view;
            while (!m_Window->shouldClose()) {
                m_Window->pollEvents();
                m_Renderer->render(scene, view);
            }

            // Renderer 依赖窗口创建出的 surface，必须在窗口销毁前先释放。
            m_Renderer.reset();
            m_Window.reset();
            ARK_INFO("ARKRenderer exited normally");
            Log::shutdown();
            return EXIT_SUCCESS;
        } catch (const std::exception& exception) {
            ARK_ERROR("Application failed: {}", exception.what());
            m_Renderer.reset();
            m_Window.reset();
            Log::shutdown();
            return EXIT_FAILURE;
        }
    }
} // namespace ark
