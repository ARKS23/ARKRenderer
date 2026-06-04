# Phase 0.2：Vulkan Device + Surface + SwapChain

本阶段目标是让 ARKRenderer 从“可启动窗口程序”进入“可初始化 Vulkan 后端”状态。当前阶段只完成 Vulkan instance、debug messenger、surface、physical device、logical device、queue 和 swapchain 的创建与销毁，不进入完整一帧渲染闭环。

本阶段的核心不是画出画面，而是把 Vulkan 后端最容易影响后续架构的生命周期和职责边界打稳。

## 阶段目标

实现一个最小 Vulkan 初始化流程：

```text
启动程序
-> 初始化日志
-> 创建 GLFW Window
-> 创建 Renderer
    -> 创建 VulkanDevice
        -> 创建 VkInstance
        -> Debug 模式启用 validation layer
        -> 创建 debug messenger
        -> 通过 NativeWindowHandle 创建 VkSurfaceKHR
        -> 选择 physical device
        -> 创建 logical device
        -> 获取 graphics / present queue
    -> 创建 VulkanSwapChain
        -> 查询 surface capabilities
        -> 选择 surface format
        -> 选择 present mode
        -> 创建 VkSwapchainKHR
        -> 创建 backbuffer image views
        -> 明确默认 depth buffer 由 SwapChain 管理
-> 进入主循环
-> poll window events
-> 关闭窗口后按正确顺序释放 Vulkan 资源
-> 关闭日志
```

完成后应能运行：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

并看到窗口正常打开。控制台应输出 Vulkan 初始化日志，例如 GPU 名称、Vulkan API 版本、queue family、swapchain format、present mode 和 swapchain extent。

## 本阶段要完成的代码工作

1. 补齐 RHI 层最小描述结构：
   - `RenderDeviceDesc`
   - `RenderDeviceCreateInfo`
   - `SwapChainDesc`
   - `SwapChainCreateInfo`
   - `SwapChainStatus` 或 `Result` 风格的状态枚举

   当前基础版本已完成：`RenderDeviceDesc` 保存设备级配置，`RenderDeviceCreateInfo` 保存创建设备时需要的窗口句柄；`SwapChainDesc` 保存 swapchain 自身的持久描述，`SwapChainCreateInfo` 保存创建 swapchain 时需要的设备依赖。这样可以避免把创建时依赖和对象运行期描述混在一起。
2. 扩展 `RenderDevice` 接口：
   - 提供 `waitIdle()`。
   - 提供设备能力查询，例如 GPU 名称、API 版本、队列能力、支持的格式等。
   - 明确后续资源创建接口归属，例如 Buffer、Texture、Shader、PipelineState、Sampler、Fence、PipelineResourceSignature。
   - 不提供 `beginFrame()` / `endFrame()`。
   - 不保存当前 frame index。
   - 暂时不暴露 Vulkan handle。
3. 扩展 `SwapChain` 接口：
   - 提供 swapchain 描述查询。
   - 提供 backbuffer 数量、当前 backbuffer render target view 查询。
   - 提供默认 depth stencil view 查询。
   - 预留 `resize()`，但第一版可以只实现保守重建。
   - `acquire()` / `present()` 是 `SwapChain` 职责，但完整帧循环放到 Phase 0.3。
4. 实现 `VulkanDevice`：
   - 使用 vk-bootstrap 或 Vulkan API 创建 instance / device。
   - Debug 构建启用 validation layer 和 debug messenger。
   - 根据 `NativeWindowHandle` 创建 `VkSurfaceKHR`。
   - 选择支持 graphics 和 present 的 physical device。
   - 创建 logical device。
   - 获取 graphics queue 和 present queue。
   - 正确管理 Vulkan 对象销毁顺序。
5. 实现 `VulkanSwapChain`：
   - 查询 surface capabilities、formats 和 present modes。
   - 选择合理默认 format，优先 `BGRA8Unorm` 或平台常见 sRGB/unorm 格式。
   - 选择 present mode，优先 mailbox，不可用则 fallback 到 fifo。
   - 根据窗口尺寸创建 swapchain。
   - 获取 swapchain images。
   - 创建 backbuffer image views。
   - 在接口层明确默认 depth buffer / depth view 属于 `SwapChain`；真实 Vulkan depth image 可以在 Phase 0.2 完成，也可以作为 Phase 0.3 清屏闭环前置任务完成。
   - 析构时销毁 image views 和 swapchain。
6. 实现 `Renderer` 的第一版真实类：
   - 通过 `Scope<rhi::RenderDevice>` 持有设备。
   - 通过 `Scope<rhi::SwapChain>` 持有 swapchain。
   - 初始化时接收窗口 native handle 和 extent。
   - `render()` 暂时可以为空实现。
   - `resize()` 暂时可以记录尺寸或触发 swapchain 保守重建。
7. 接入 `Application`：
   - 创建窗口后创建 `Renderer`。
   - 主循环继续只负责 `pollEvents()`。
   - 程序退出时先销毁 `Renderer`，再销毁 `Window`。
8. 更新测试：
   - `framework_headers_smoke.cpp` 覆盖新接口头文件。
   - 如有必要增加轻量构造测试，但不要在自动测试里启动窗口主循环。

## 当前实现记录

本阶段代码已经完成以下内容：

- `src/core/Memory.h`：新增 `Scope<T>`、`Ref<T>`、`makeScope<T>()`、`makeRef<T>()`，统一项目所有权表达。
- `src/rhi/RHICommon.h`：新增 `RenderBackendType`、`isValidExtent()`，并补齐必要中文注释。
- `src/rhi/RenderBackend.h/.cpp`：新增公共 RHI 后端工厂和 `RenderBackend` 运行期对象，统一持有 `RenderDevice + SwapChain`。
- `src/rhi/RenderDevice.h`：补齐 `RenderDeviceDesc`、`RenderDeviceCreateInfo`、`RenderDeviceCaps`，移除每帧 begin/end 职责。
- `src/rhi/SwapChain.h`：补齐 `SwapChainDesc`、`SwapChainCreateInfo`、`SwapChainStatus`，明确默认 depth view 属于 `SwapChain`。
- `src/rhi/vulkan/VulkanCommon.h/.cpp`：集中 Vulkan 格式映射、present mode 名称、版本号格式化等后端工具函数。
- `src/rhi/vulkan/VulkanDevice.h/.cpp`：使用 volk + vk-bootstrap 创建 instance、debug messenger、GLFW surface、physical device、logical device、graphics queue 和 present queue，并在析构时按顺序释放。
- `src/rhi/vulkan/VulkanSwapChain.h/.cpp`：使用 vk-bootstrap 创建 swapchain、获取 backbuffer images、创建 backbuffer image views，并支持保守 `resize()`。
- `src/rhi/vulkan/VulkanTextureView.h/.cpp`：为 swapchain backbuffer image view 提供最小 RAII 包装。
- `src/rhi/vulkan/VulkanRenderBackend.cpp`：实现 Vulkan 后端工厂，集中创建 `VulkanDevice` 和 `VulkanSwapChain`。
- `src/renderer/Renderer.h/.cpp`：新增 `RendererDesc` 和 `createRenderer()`，应用层通过 renderer 工厂创建 renderer；renderer 只依赖公共 RHI，不直接包含 Vulkan 后端头文件。
- `src/app/Application.cpp`：创建窗口后初始化 Renderer；退出时先销毁 Renderer，再销毁 Window。
- `tests/framework_headers_smoke.cpp`：覆盖新增 RHI 描述结构、renderer 描述结构和 Vulkan 公共工具头文件。

当前 `Renderer::render()` 仍为空实现，不执行 acquire / submit / present；这是 Phase 0.2 的刻意边界。默认 depth buffer 的所有权已经归属 `SwapChain`，但真实 depth image / depth view 分配将作为 Phase 0.3 清屏闭环的前置任务完成。

## 建议新增文件

```text
src/
|-- renderer/
|   `-- Renderer.cpp                   已新增：隐藏 Vulkan 后端创建细节
|
`-- rhi/
    |-- RenderBackend.h                已新增：公共后端工厂接口
    |-- RenderBackend.cpp              已新增：公共后端对象和通用创建流程
    |-- RenderDevice.cpp               可选：如果有公共 helper
    |-- SwapChain.cpp                  可选：如果有公共 helper
    |
    `-- vulkan/
        |-- VulkanCommon.h             已新增：Vulkan 格式映射和日志辅助
        |-- VulkanCommon.cpp
        |-- VulkanDevice.cpp
        |-- VulkanRenderBackend.cpp    已新增：Vulkan 后端工厂实现
        |-- VulkanSwapChain.cpp
        |-- VulkanTextureView.cpp
        `-- VulkanSurface.h            可选：如果 surface 生命周期需要独立 helper
```

说明：

- 是否新增 `VulkanSurface.h` 取决于实现复杂度。第一版可以让 `VulkanDevice` 直接持有 `VkSurfaceKHR`，等多窗口或多 swapchain 需求出现后再拆出独立对象。
- 暂未新增 `VulkanRendererFactory.h`，因为当前 `createRenderer()` 已放在 `Renderer.h/.cpp`，足以隐藏 Vulkan 后端类型。后续多后端选择复杂后再拆独立 factory 文件。

## 关键接口草案

`RenderDeviceDesc`：

```cpp
struct RenderDeviceDesc {
    RenderBackendType backend = RenderBackendType::Vulkan;
    bool enableValidation = false;
    std::string applicationName = "ARKRenderer";
    u32 applicationVersion = 0;
    u32 preferredApiVersion = 0;
};
```

`RenderDeviceCreateInfo`：

```cpp
struct RenderDeviceCreateInfo {
    RenderDeviceDesc desc;
    NativeWindowHandle nativeWindow;
};
```

`RenderDeviceCaps`：

```cpp
struct RenderDeviceCaps {
    std::string gpuName;
    u32 apiVersion = 0;
    u32 graphicsQueueFamily = 0;
    u32 presentQueueFamily = 0;
};
```

`SwapChainDesc`：

```cpp
struct SwapChainDesc {
    Extent2D extent;
    Format colorFormat = Format::Unknown;
    Format depthFormat = Format::D32Float;
    u32 imageCount = 2;
    bool enableVSync = true;
};
```

`SwapChainCreateInfo`：

```cpp
struct SwapChainCreateInfo {
    SwapChainDesc desc;
    RenderDevice* device = nullptr;
};
```

`SwapChainStatus`：

```cpp
enum class SwapChainStatus {
    Ready,
    Suboptimal,
    OutOfDate,
    SurfaceLost,
};
```

`RenderDevice`：

```cpp
class RenderDevice {
public:
    virtual ~RenderDevice() = default;

    virtual void waitIdle() = 0;

    virtual const RenderDeviceCaps& getCaps() const = 0;

    // 后续阶段继续补 Buffer / Texture / Shader / PipelineState 等创建接口。
};
```

`SwapChain`：

```cpp
class SwapChain {
public:
    virtual ~SwapChain() = default;

    virtual const SwapChainDesc& getDesc() const = 0;
    virtual u32 getBackBufferCount() const = 0;
    virtual TextureView* getCurrentBackBufferView() = 0;
    virtual TextureView* getDepthBufferView() = 0;

    virtual SwapChainStatus resize(Extent2D extent) = 0;
};
```

这些接口只是 Phase 0.2 的建议起点。实现时如果发现当前需求更少，应优先保持简单。

## Vulkan 后端分阶段边界

本项目采用以下 Vulkan 后端目标架构，但分阶段落地：

```text
Phase 0.2
    VulkanDevice
    VulkanSwapChain

Phase 0.3
    VulkanCommandContext
    VulkanFrameResource
    acquire / submit / present
    clear backbuffer

Phase 0.4+
    VulkanResourceManager
    VulkanDescriptorManager
    VulkanBindlessResourceManager
    VulkanPipelineCache
    ImGuiPass
    ForwardPass

Later
    RenderGraph
```

Phase 0.2 只允许完成 `VulkanDevice` 和 `VulkanSwapChain` 的初始化、销毁和必要接口，不实现命令录制、资源 handle 系统、bindless 或 pipeline cache。

## 当前主循环

本阶段完成后，主循环仍然不做真实绘制：

```text
Application::run()
    -> Log::initialize()
    -> 创建 GlfwWindow
    -> 创建 Renderer
        -> 创建 VulkanDevice
        -> 创建 VulkanSwapChain
    -> while (!window.shouldClose())
        -> window.pollEvents()
        -> renderer.render(emptyScene, defaultView)    可选，当前可为空实现
    -> 销毁 Renderer
    -> 销毁 Window
    -> Log::shutdown()
```

如果 `render()` 暂时为空，仍然应保证 `Renderer` 的构造和析构能完整验证 Vulkan 初始化与释放。

## 验证方式

自动验证：

```powershell
cmake --preset msvc-vcpkg-local
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

手动验证：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

手动验证时观察：

- 窗口可以正常打开。
- 控制台输出 Vulkan 初始化日志。
- GPU 名称、queue family、swapchain format、present mode 和 extent 合理。
- 关闭窗口后程序正常退出。
- Debug 下 validation layer 没有明显错误。

当前验证记录：

- `cmake --build --preset msvc-vcpkg-local-debug` 通过。
- `ctest --preset msvc-vcpkg-local-debug` 通过。
- `ark_sandbox.exe` 已进行 3 秒短启动 smoke check：程序没有提前异常退出，随后由脚本停止。

## 验收标准

- CMake configure 成功。
- Debug 构建成功。
- `ctest` 全部通过。
- `ark_sandbox.exe` 可以启动。
- 可以成功创建 Vulkan instance。
- Debug 构建可以启用 validation layer。
- 可以成功创建 `VkSurfaceKHR`。
- 可以成功选择支持 present 的 physical device。
- 可以成功创建 logical device。
- 可以获取 graphics queue 和 present queue。
- 可以成功创建 swapchain。
- 可以创建 swapchain backbuffer image views。
- `SwapChain` 接口已经明确默认 depth buffer / depth stencil view 的归属。
- 关闭窗口后 Vulkan 资源按正确顺序释放。
- `app/` 和 `renderer/` 不包含 Vulkan 头文件。
- Vulkan 类型只出现在 `src/rhi/vulkan/`。
- `RenderDevice` 不提供 `beginFrame()` / `endFrame()`，不承担每帧命令录制或提交职责。

## 暂不实现

本阶段不做以下内容：

- command pool。
- command buffer。
- semaphore / fence。
- acquire / submit / present 完整帧循环。
- `DeviceContext` 真实命令录制与提交。
- 清屏 backbuffer。
- `ClearPass` 真实执行。
- ImGui backend。
- `Buffer`、`Texture`、`Sampler`、`Shader`、`PipelineState`、`DescriptorSet` 的完整实现。
- `VulkanResourceManager`。
- `VulkanDescriptorManager`。
- `VulkanBindlessResourceManager`。
- `VulkanPipelineCache`。
- RenderGraph 真实执行。
- glTF 加载。
- ForwardPass。

这些内容进入后续 Phase 0.3 和 Phase 0.4。

## 风险点

- physical device 选择需要考虑 surface present 支持，不能只看 graphics queue。
- surface 生命周期必须晚于 instance 创建，早于 device 选择；销毁时必须在 swapchain 销毁之后再销毁 surface。
- swapchain image 由 `VkSwapchainKHR` 拥有，不能手动销毁，只能销毁对应 image view。
- Debug messenger 应在 instance 销毁前销毁。
- `Application` 不应该直接知道 Vulkan 后端类型。
- `Window` 不应该创建 Vulkan surface。
- `Renderer` 不应该接触 `VkSurfaceKHR`、`VkDevice`、`VkSwapchainKHR`。
- `RenderDevice` 不应该拥有或驱动 `FrameResource`，避免设备层变成每帧执行器。
- 默认 depth buffer 应跟随 `SwapChain` resize，而不是散落到 pass 或 Application 里。
- 日志输出内容使用英文，避免控制台编码问题。
- 如果窗口最小化导致 extent 为 0，应跳过 swapchain 创建或等待有效尺寸。

## 完成后的下一阶段

Phase 0.3 建议进入第一帧清屏闭环：

```text
VulkanFrameResource
-> command pool
-> command buffer
-> semaphore / fence
-> acquire image
-> transition backbuffer
-> clear backbuffer
-> submit
-> present
```

到 Phase 0.3 结束时，应该能看到窗口被 Vulkan 清成固定颜色。
