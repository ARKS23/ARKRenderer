#include "app/Application.h"

#include "app/GlfwWindow.h"
#include "core/Log.h"
#include "core/Memory.h"
#include "core/ScopeExit.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/Renderer.h"

#include <cstdlib>
#include <utility>

namespace ark {
    Application::Application(ApplicationDesc desc) : m_Desc(std::move(desc)) {
    }

    Application::~Application() = default;

    int Application::run() {
        Log::initialize();
        auto shutdownLog = makeScopeExit([]() {
            Log::shutdown();
        });

        auto cleanup = makeScopeExit([this]() {
            // Renderer 依赖窗口创建出的 surface，必须在窗口销毁前先释放。
            m_Renderer.reset();
            m_Window.reset();
        });

        ARK_INFO("ARKRenderer started");

        m_Window = makeScope<GlfwWindow>(m_Desc.window);

        RendererDesc rendererDesc{};
        rendererDesc.nativeWindow = m_Window->getNativeWindowHandle();
        rendererDesc.extent = m_Window->getExtent();
        rendererDesc.defaultModelPath = m_Desc.defaultModelPath;
#ifndef NDEBUG
        rendererDesc.enableValidation = true;
#endif

        m_Renderer = createRenderer(rendererDesc);

        RenderScene scene;
        RenderView view;
        rhi::Extent2D currentExtent = rendererDesc.extent;
        view.setDefaultPerspective(currentExtent);
        while (!m_Window->shouldClose()) {
            m_Window->pollEvents();

            // 窗口大小变化resize处理
            const rhi::Extent2D windowExtent = m_Window->getExtent();
            if (windowExtent.width != currentExtent.width || windowExtent.height != currentExtent.height) {
                currentExtent = windowExtent;
                m_Renderer->resize(currentExtent.width, currentExtent.height);
                view.setDefaultPerspective(currentExtent);
            }

            m_Renderer->render(scene, view);
        }

        ARK_INFO("ARKRenderer exited normally");
        return EXIT_SUCCESS;
    }
} // namespace ark
