#include "app/GlfwWindow.h"

#include "core/Log.h"

#include <GLFW/glfw3.h>

#include <glm/vec2.hpp>

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

        GlfwWindow* getWindowOwner(GLFWwindow* window) {
            return static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
        }

        void scrollCallback(GLFWwindow* window, double xOffset, double yOffset) {
            GlfwWindow* owner = getWindowOwner(window);
            if (owner) {
                owner->onScroll(xOffset, yOffset);
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
        glfwSetWindowUserPointer(window, this);
        glfwSetScrollCallback(window, scrollCallback);
        updateInputSnapshot();
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
        updateInputSnapshot();
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

    InputSnapshot GlfwWindow::getInputSnapshot() const {
        return m_Input;
    }

    void GlfwWindow::updateInputSnapshot() {
        GLFWwindow* window = static_cast<GLFWwindow*>(m_Window);
        if (!window) {
            m_Input = {};
            return;
        }

        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        const glm::vec2 cursorPosition{static_cast<float>(cursorX), static_cast<float>(cursorY)};

        m_Input.cursorDelta = m_HasPreviousCursorPosition ? cursorPosition - m_PreviousCursorPosition : glm::vec2{0.0f};
        m_Input.cursorPosition = cursorPosition;
        m_Input.scrollDelta = m_PendingScrollDelta;
        m_PendingScrollDelta = glm::vec2{0.0f};

        m_Input.leftMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        m_Input.rightMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        m_Input.middleMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
        m_Input.shiftDown =
            glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

        const bool resetDown = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
        m_Input.resetPressed = resetDown && !m_PreviousResetDown;
        m_PreviousResetDown = resetDown;

        m_PreviousCursorPosition = cursorPosition;
        m_HasPreviousCursorPosition = true;
    }

    void GlfwWindow::onScroll(double xOffset, double yOffset) {
        m_PendingScrollDelta += glm::vec2{static_cast<float>(xOffset), static_cast<float>(yOffset)};
    }
} // namespace ark
