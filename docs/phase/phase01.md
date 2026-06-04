# Phase 0.1：Application + Window + Log

本阶段目标是让 ARKRenderer 从“框架骨架”进入“可启动程序”状态。当前阶段不创建 Vulkan device，也不接入 swapchain，只完成应用生命周期、日志系统、GLFW 窗口和最小主循环。

## 阶段目标

实现一个最小可运行程序：

```text
启动程序
-> 初始化日志
-> 创建 Application
-> 创建 GLFW Window
-> 进入主循环
-> poll window events
-> 关闭窗口后正常退出
-> 关闭日志
```

完成后应能运行：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

并看到一个窗口打开，关闭窗口后程序正常退出，控制台输出基本日志。

## 本次完成内容

本阶段已经完成以下代码工作：

1. `ark_renderer` 从纯 header/interface 目标调整为真实静态库目标。
2. CMake 开始自动收集并编译 `src/**/*.cpp`。
3. 新增 `ark_sandbox` 可执行程序。
4. 实现 `core/Log.h/.cpp`：
   - 使用 spdlog 初始化默认彩色控制台 logger。
   - 提供 `Log::initialize()` 和 `Log::shutdown()`。
   - 保留 `ARK_TRACE`、`ARK_DEBUG`、`ARK_INFO`、`ARK_WARN`、`ARK_ERROR` 宏。
5. 实现 `app/Application.h/.cpp`：
   - 管理日志生命周期。
   - 创建窗口。
   - 驱动最小主循环。
   - 捕获运行期异常并输出错误日志。
6. 新增并实现 `app/GlfwWindow.h/.cpp`：
   - 封装 GLFW 初始化和关闭。
   - 创建无 OpenGL 上下文窗口，为后续 Vulkan surface 做准备。
   - 实现 `Window` 接口。
   - 通过 `NativeWindowHandle` 暴露 GLFW native handle，不暴露 Vulkan 类型。
7. 新增 `apps/sandbox/main.cpp`：
   - 创建 `ark::Application`。
   - 调用 `Application::run()`。
8. 更新 `tests/framework_headers_smoke.cpp`：
   - 覆盖新加入的 `GlfwWindow.h`。

## 新增文件

```text
apps/
`-- sandbox/
    `-- main.cpp

src/
|-- app/
|   |-- Application.cpp
|   |-- GlfwWindow.h
|   `-- GlfwWindow.cpp
|
`-- core/
    `-- Log.cpp
```

## 调整文件

```text
CMakeLists.txt
src/app/Application.h
src/core/Log.h
tests/framework_headers_smoke.cpp
```

## 关键接口

`Log`：

```cpp
class Log
{
public:
    static void initialize();
    static void shutdown();
};
```

`GlfwWindow`：

```cpp
class GlfwWindow final : public Window
{
public:
    explicit GlfwWindow(const WindowDesc& desc);
    ~GlfwWindow() override;

    bool shouldClose() const override;
    void pollEvents() override;

    rhi::NativeWindowHandle getNativeWindowHandle() const override;
    rhi::Extent2D getExtent() const override;
};
```

`Application`：

```cpp
class Application
{
public:
    explicit Application(ApplicationDesc desc = {});
    ~Application();

    int run();
};
```

## 当前主循环

```text
Application::run()
    -> Log::initialize()
    -> 创建 GlfwWindow
    -> while (!window.shouldClose())
        -> window.pollEvents()
    -> 销毁窗口
    -> Log::shutdown()
```

## 验证记录

已完成自动验证：

```powershell
cmake --preset msvc-vcpkg-local
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

测试结果：

```text
100% tests passed, 0 tests failed out of 2
```

当前自动测试包括：

- `ark_dependency_smoke`
- `ark_framework_headers_smoke`

`ark_sandbox.exe` 是交互式窗口程序，自动测试中不直接启动，避免测试流程被窗口主循环阻塞。手动验收时运行：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

## 验收标准

- CMake configure 成功。
- Debug 构建成功。
- `ctest` 全部通过。
- `ark_sandbox.exe` 可以启动。
- 窗口可以正常显示。
- 关闭窗口后程序正常退出。
- 控制台有启动、窗口创建和退出日志。
- 本阶段不引入 Vulkan instance、device、surface 或 swapchain。

## 暂不实现

本阶段不做以下内容：

- Vulkan instance / device。
- Vulkan surface。
- swapchain。
- command buffer。
- ClearPass。
- ImGuiPass。
- RenderGraph 真实执行。
- 输入系统。
- 事件分发系统。

这些内容进入后续 Phase 0.2 和 Phase 0.3。

## 风险点

- GLFW 生命周期要避免重复初始化和重复 terminate。
- `Window` 不能暴露 Vulkan 类型。
- `GlfwWindow.cpp` 可以包含 GLFW 头文件，但 `Window.h` 只暴露 RHI 中的 `NativeWindowHandle`。
- `ark_sandbox` 只负责驱动 Application，不承载渲染逻辑。
