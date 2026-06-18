#include "app/Application.h"

#include "app/GlfwWindow.h"
#include "app/SandboxCameraController.h"
#include "app/SandboxDebugUi.h"
#include "app/SandboxRuntimeSettings.h"
#include "core/Log.h"
#include "core/Memory.h"
#include "core/ScopeExit.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/Renderer.h"

#include <cstdlib>
#include <utility>

namespace ark {
    namespace {
        SandboxCameraControllerDesc makeSandboxCameraControllerDesc(const OrbitCameraProfileDesc& camera) {
            SandboxCameraControllerDesc desc{};
            desc.target = camera.target;
            desc.distance = camera.distance;
            desc.yaw = camera.yawRadians;
            desc.pitch = camera.pitchRadians;
            desc.verticalFovRadians = camera.verticalFovRadians;
            desc.nearPlane = camera.nearPlane;
            desc.farPlane = camera.farPlane;
            return desc;
        }
    } // namespace

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
        rendererDesc.defaultScene = m_Desc.defaultScene;
        rendererDesc.quality = m_Desc.rendererQuality;
#ifndef NDEBUG
        rendererDesc.enableValidation = true;
#endif

        m_Renderer = createRenderer(rendererDesc);

        RenderScene scene;
        SandboxRuntimeSettings runtimeSettings = makeSandboxRuntimeSettings(m_Desc);
        RenderView view;
        applySandboxRuntimeSettings(view, runtimeSettings);
        SandboxCameraController cameraController{makeSandboxCameraControllerDesc(runtimeSettings.camera)};
        Scope<SandboxDebugUi> debugUi;
        if (m_Desc.debugUiEnabled) {
            debugUi = makeScope<SandboxDebugUi>(*m_Window, runtimeSettings);
        }
        rhi::Extent2D currentExtent = rendererDesc.extent;
        cameraController.setViewportExtent(currentExtent);
        cameraController.writeTo(view);
        while (!m_Window->shouldClose()) {
            m_Window->pollEvents();

            // 窗口大小变化resize处理
            const rhi::Extent2D windowExtent = m_Window->getExtent();
            if (windowExtent.width != currentExtent.width || windowExtent.height != currentExtent.height) {
                currentExtent = windowExtent;
                m_Renderer->resize(currentExtent.width, currentExtent.height);
                cameraController.setViewportExtent(currentExtent);
            }

            InputSnapshot input = m_Window->getInputSnapshot();
            if (debugUi && input.debugUiTogglePressed) {
                runtimeSettings.uiVisible = !runtimeSettings.uiVisible;
            }

            if (debugUi) {
                debugUi->beginFrame();
                debugUi->buildPanels();
                debugUi->endFrame();
                input = filterSandboxInputForUiCapture(input,
                                                       debugUi->wantsCaptureMouse(),
                                                       debugUi->wantsCaptureKeyboard());
            }

            cameraController.update(input);
            cameraController.writeTo(view);
            applySandboxRuntimeSettings(view, runtimeSettings);
            m_Renderer->render(scene, view, debugUi.get());
        }

        ARK_INFO("ARKRenderer exited normally");
        return EXIT_SUCCESS;
    }
} // namespace ark
