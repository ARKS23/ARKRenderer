#include "app/GlfwWindow.h"

#include "core/Log.h"

#include <GLFW/glfw3.h>

#include <stdexcept>

namespace ark {
    namespace {
        int g_GlfwWindowCount = 0;

        void initializeGlfw() {
            if (g_GlfwWindowCount == 0) {
                if (glfwInit() != GLFW_TRUE) {
                    throw std::runtime_error("glfwInit failed");
                }

                // Phase 0.1 不创建 OpenGL 上下文，后续 Vulkan surface 会使用该 native window。
                glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
                glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
            }

            ++g_GlfwWindowCount;
        }

        void shutdownGlfw() {
            if (g_GlfwWindowCount <= 0) {
                return;
            }

            --g_GlfwWindowCount;
            if (g_GlfwWindowCount == 0) {
                glfwTerminate();
            }
        }
    } // namespace

    GlfwWindow::GlfwWindow(const WindowDesc& desc) : m_Extent(desc.extent) {
        initializeGlfw();

        GLFWwindow* window = glfwCreateWindow(static_cast<int>(desc.extent.width), static_cast<int>(desc.extent.height),
                                              desc.title.c_str(), nullptr, nullptr);

        if (!window) {
            shutdownGlfw();
            throw std::runtime_error("glfwCreateWindow failed");
        }

        m_Window = window;
        ARK_INFO("Created GLFW window: {} ({}x{})", desc.title, desc.extent.width, desc.extent.height);
    }

    GlfwWindow::~GlfwWindow() {
        if (m_Window) {
            glfwDestroyWindow(static_cast<GLFWwindow*>(m_Window));
            m_Window = nullptr;
        }

        shutdownGlfw();
    }

    bool GlfwWindow::shouldClose() const {
        return glfwWindowShouldClose(static_cast<GLFWwindow*>(m_Window)) == GLFW_TRUE;
    }

    void GlfwWindow::pollEvents() {
        glfwPollEvents();
    }

    rhi::NativeWindowHandle GlfwWindow::getNativeWindowHandle() const {
        return rhi::NativeWindowHandle{
            .type = rhi::NativeWindowType::GLFW,
            .handle = m_Window,
        };
    }

    rhi::Extent2D GlfwWindow::getExtent() const {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(static_cast<GLFWwindow*>(m_Window), &width, &height);

        return rhi::Extent2D{
            .width = width > 0 ? static_cast<u32>(width) : 0,
            .height = height > 0 ? static_cast<u32>(height) : 0,
        };
    }
} // namespace ark
