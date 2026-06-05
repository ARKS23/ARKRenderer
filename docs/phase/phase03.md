# Phase 0.3：第一帧清屏闭环

本阶段目标是让 ARKRenderer 从“可以初始化 Vulkan 后端”进入“可以提交一帧 GPU 命令并 present 到窗口”的状态。完成后窗口不再只是空转，而是能被 Vulkan 稳定清成固定颜色。

本阶段重点不是绘制网格，也不是建立完整渲染管线，而是把每帧同步、命令录制、backbuffer 状态转换、提交和呈现闭环打稳。

## 阶段目标

实现最小一帧渲染流程：

```text
Application::run()
-> Window::pollEvents()
-> Renderer::render(scene, view)
    -> 选择当前 FrameResource
    -> SwapChain acquire next image
    -> DeviceContext begin command recording
    -> transition backbuffer: Undefined/Present -> ClearDst/RenderTarget
    -> clear backbuffer
    -> transition backbuffer: ClearDst/RenderTarget -> Present
    -> DeviceContext end command recording
    -> DeviceContext submit
    -> SwapChain present
    -> 推进当前 FrameResource
```

完成后应能运行：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

并看到窗口被清成固定颜色。关闭窗口后程序正常退出，Debug 构建下 validation layer 不应出现明显同步、layout 或对象生命周期错误。

## 设计边界

本阶段继续遵守现有架构边界：

- `Application` 只负责窗口事件和主循环，不直接接触 Vulkan。
- `Renderer` 负责组织一帧流程，但只调用公共 RHI 接口。
- `SwapChain` 负责 backbuffer、acquire、present、resize 和默认 depth buffer。
- `DeviceContext` 负责 command buffer 录制、resource transition、clear、submit。
- `RenderDevice` 继续只负责设备、能力查询、资源创建和 `waitIdle()`，不承担每帧 draw。
- Vulkan 类型只允许出现在 `src/rhi/vulkan/`。

## 错误处理原则

从本阶段开始，每帧路径尽量不用异常：

- `acquire()`、`present()`、`resize()` 返回状态。
- out-of-date、suboptimal、surface lost 通过 `SwapChainStatus` 或结果结构表达。
- Vulkan 初始化阶段的不可恢复错误可以暂时保留异常。
- 每帧路径中的 Vulkan 调用失败应转换为明确状态或记录英文日志。
- 程序员错误使用断言，例如空指针、非法 desc、生命周期顺序错误。

## 本阶段要完成的代码工作

1. 补齐 `SwapChain` 每帧接口：
   - `acquireNextImage(...)`
   - `present(...)`
   - 查询当前 backbuffer index。
   - out-of-date / suboptimal / surface lost 状态返回。
   - resize 时重建 backbuffer views 和默认 depth buffer。

2. 补齐 `FrameResource` 语义：
   - 公共 RHI 层的 `FrameResource` 保持后端无关，只作为一帧 token。
   - Vulkan 后端实现 `VulkanFrameResource`，持有真实 command 和 sync 对象。
   - 每个 frame resource 至少包含：
     - command pool
     - command buffer
     - image available semaphore
     - render finished semaphore
     - in-flight fence
     - per-frame deferred deletion queue 预留

3. 实现最小 `VulkanCommandContext`：
   - 创建并管理固定数量的 `VulkanFrameResource`。
   - 选择当前 frame resource。
   - 等待当前 in-flight fence。
   - reset command pool 或 reset command buffer。
   - begin / end command buffer。
   - 提交 command buffer。
   - 提交时等待 image available semaphore。
   - 提交后 signal render finished semaphore。
   - 推进 frame index。

4. 扩展公共 `DeviceContext`：
   - 当前接口已经有 `begin()`、`end()`、`submit()`、`pipelineBarrier()`。
   - 本阶段需要让这些接口能和当前 `FrameResource` 对齐。
   - 如有必要，可增加 `currentFrameResource()` 或 `advanceFrame()`。
   - 先只实现 clear 所需的最小能力，draw / pipeline / descriptor 可以继续保留空壳或未实现。

5. 补齐 backbuffer 的 `Texture` 包装：
   - `Texture` 对应 Vulkan 的 `VkImage`。
   - `TextureView` 对应 Vulkan 的 `VkImageView`。
   - SwapChain image 由 `VkSwapchainKHR` 拥有，`VulkanTexture` 只能借用 `VkImage`，不能销毁它。
   - 普通 depth texture 由 ARKRenderer 创建并拥有，需要在 resize 时销毁和重建。
   - `TextureView` 应能回溯到它所属的 `Texture`，方便 barrier 找到底层 image。

6. 补齐资源状态转换：
   - 公共层继续使用 `ResourceState` 表达渲染语义。
   - Vulkan 后端把 `ResourceState` 转换为：
     - `VkImageLayout`
     - `VkAccessFlags2`
     - `VkPipelineStageFlags2`
   - 本阶段至少支持：
     - `Undefined`
     - `Present`
     - `RenderTarget`
     - `CopyDst`
   - 第一版可以只处理 color backbuffer，depth 的 barrier 后续随深度清理和 ForwardPass 补齐。

7. 实现 clear backbuffer：
   - 推荐先用最小路径验证同步和 present。
   - 可选实现方式一：使用 `vkCmdClearColorImage`。
   - 可选实现方式二：使用 dynamic rendering，设置 color attachment clear load op。
   - 第一版建议优先选择更少依赖的方式，先把一帧闭环跑稳。
   - 后续再把 clear 逻辑上移到 `FrameRenderer + ClearPass`。

8. 接入 `Renderer::render()`：
   - 空实现改为真实一帧流程。
   - acquire 失败时跳过当前帧并触发 resize。
   - present 返回 out-of-date / suboptimal 时触发 resize。
   - 窗口最小化导致 extent 为 0 时暂停渲染，不重建 swapchain。

9. 更新测试和文档：
   - 更新 `framework_headers_smoke.cpp`，覆盖新增接口头文件。
   - 自动测试不启动长时间窗口主循环。
   - 文档记录最终实现与阶段边界。

## 当前实现记录

本阶段已经完成第一帧清屏闭环的基础实现：

- `src/rhi/SwapChain.h`：新增 `AcquireResult`、`InvalidBackBufferIndex`、`getCurrentBackBufferIndex()`、`acquireNextImage()` 和 `present()`，让 swapchain 明确承担每帧 acquire / present 职责。
- `src/rhi/FrameResource.h`：将公共 `FrameResource` 明确为后端无关的一帧 token，增加 `frameSlot` 和 `frameIndex`，并允许 Vulkan 后端继承扩展。
- `src/rhi/DeviceContext.h`：补齐 `beginFrame()`、`begin()`、`end()`、`submit()`、`advanceFrame()`、`pipelineBarrier()` 和 `clearRenderTarget()`，用于最小一帧命令录制和提交。
- `src/rhi/ResourceBarrier.h`：补齐 barrier 目标 texture，使后端能从 RHI barrier 找到具体 GPU image。
- `src/rhi/Texture.h` / `src/rhi/TextureView.h`：补齐 `Texture` 与 `TextureView` 的关系，`TextureView` 可以回溯所属 `Texture`，`Texture` 暴露当前 RHI 资源状态。
- `src/rhi/RenderBackend.h/.cpp`：`RenderBackend` 统一持有 `RenderDevice + SwapChain + DeviceContext`，renderer 仍只依赖公共 RHI；公开头文件只暴露 `createRenderBackend()`，底层对象工厂隐藏到 `rhi/detail`。
- `src/rhi/detail/RenderBackendFactory.h`：内部后端工厂声明，只服务 `RenderBackend` 拼装流程，不作为 renderer 层公共 API。
- `src/rhi/vulkan/VulkanFrameResource.h/.cpp`：Vulkan 帧资源拥有 per-frame command pool、command buffer、image available semaphore、render finished semaphore、in-flight fence 和 deferred deletion queue。
- `src/rhi/vulkan/VulkanSync.h/.cpp`：补齐 Vulkan semaphore / fence 的 RAII 包装，供后续 `VulkanCommandContext` 创建每帧同步对象。
- `src/rhi/vulkan/VulkanCommandPool.h/.cpp` / `src/rhi/vulkan/VulkanCommandBuffer.h/.cpp`：补齐 command pool 和 primary command buffer 的轻量 RAII 包装。
- `src/rhi/vulkan/VulkanCommandContext.h/.cpp`：实现最小 command context，负责 frame fence 等待、command pool reset、command buffer begin/end、barrier、`vkCmdClearColorImage` 和 queue submit。
- `src/rhi/vulkan/VulkanTexture.h/.cpp`：补齐 Vulkan texture 包装。当前 swapchain image 以 borrowed texture 形式接入，不销毁 `VkImage`。
- `src/rhi/vulkan/VulkanTextureView.h/.cpp`：补齐 image view 到 texture 的关联，供 clear 和 barrier 查找底层 image。
- `src/rhi/vulkan/VulkanSwapChain.h/.cpp`：实现 `acquireNextImage()`、`present()`、当前 backbuffer index 查询和 Vulkan `VkResult` 到 `SwapChainStatus` 的状态转换；swapchain image usage 增加 `VK_IMAGE_USAGE_TRANSFER_DST_BIT`，用于 `vkCmdClearColorImage`。
- `src/rhi/vulkan/VulkanBackendFactory.cpp`：集中创建 `VulkanDevice`、`VulkanCommandContext` 和 `VulkanSwapChain`，避免 renderer 直接依赖 Vulkan 后端。
- `src/renderer/Renderer.cpp`：`Renderer::render()` 接入 acquire、barrier、clear、barrier、submit、present 和 swapchain 状态处理。
- `src/app/Application.cpp` / `src/app/GlfwWindow.cpp`：主循环感知 framebuffer extent 变化并调用 `Renderer::resize()`；窗口最小化时 renderer 暂停渲染。
- `tests/framework_headers_smoke.cpp`：补充 `AcquireResult`、公共 `FrameResource` 和 `VulkanFrameResource` 的头文件构造覆盖。

当前清屏路径仍是后端级最小实现：clear 直接由 `VulkanCommandContext::clearRenderTarget()` 调用 `vkCmdClearColorImage` 完成，尚未上移到 `FrameRenderer + ClearPass`。这符合 Phase 0.3 的边界，后续可在 Phase 0.4 前半段把 clear 逻辑整理到 renderer pass 层。

## UML 类图

下面的类图描述 Phase 0.3 已经落地的核心关系。重点不是列出所有成员函数，而是看清三层边界：

- `renderer/` 只组织一帧流程，不直接依赖 Vulkan。
- `rhi/` 定义公共接口和资源语义。
- `rhi/vulkan/` 实现具体 Vulkan 对象，并把 `Vk*` 生命周期封装在后端内部。

![Phase 0.3 UML 类图](assets/phase03_class_diagram.svg)

关系阅读顺序：

- `DefaultRenderer` 拥有一个 `RenderBackend`，但不直接创建或持有任何 Vulkan 对象。
- `RenderBackend` 统一拥有 `RenderDevice`、`DeviceContext` 和 `SwapChain`，并通过内部 `RenderBackendFactory` 创建具体后端对象。
- `VulkanCommandContext` 实现 `DeviceContext`，并拥有一个固定大小的 `VulkanFrameResource` 环形队列。
- `VulkanSwapChain` 实现 `SwapChain`，拥有 backbuffer 的 `TextureView`，但只借用 swapchain image 的 `VkImage`。
- `TextureView` 可以回溯到 `Texture`，因此 clear 和 barrier 可以从公共 RHI 对象找到后端 image。
- `ResourceBarrier` 只表达 RHI 语义，具体 Vulkan layout / access / stage 转换由 `VulkanCommandContext` 完成。

## Resize 规则

resize 来源：

- 窗口大小变化。
- `acquireNextImage()` 返回 out-of-date。
- `present()` 返回 out-of-date 或 suboptimal。
- 窗口从最小化恢复。

第一版采用保守策略：

```text
Renderer::resize(width, height)
-> if width == 0 or height == 0: mark rendering paused
-> device.waitIdle()
-> swapChain.resize(newExtent)
-> reset frame-related swapchain state if needed
```

本阶段不追求最优同步，只要求正确。后续再把全局 `waitIdle()` 优化成更细粒度的 frame fence 等待。

## 验证方式

自动验证：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

手动验证：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

观察内容：

- 窗口能正常打开。
- 窗口背景被清成固定颜色。
- 拖动和 resize 不崩溃。
- 最小化窗口不崩溃。
- 关闭窗口后程序正常退出。
- Debug 构建下 validation layer 没有明显错误。
- 日志输出使用英文，避免控制台编码问题。

当前验证记录：

- `cmake --build --preset msvc-vcpkg-local-debug` 通过。
- `ctest --preset msvc-vcpkg-local-debug` 通过。
- `ark_sandbox.exe` 已进行 3 秒短启动 smoke check：程序没有提前异常退出，随后由脚本停止。

## 验收标准

- Debug 构建成功。
- `ctest` 全部通过。
- `ark_sandbox.exe` 可以启动。
- 能成功创建每帧 command pool / command buffer。
- 能成功创建 semaphore 和 fence。
- 能成功 acquire swapchain image。
- 能成功录制 command buffer。
- 能成功完成 backbuffer layout transition。
- 能成功 clear backbuffer。
- 能成功 submit command buffer。
- 能成功 present 当前 backbuffer。
- resize 和窗口最小化不会导致崩溃。
- 资源销毁顺序正确，退出时没有明显 validation error。
- `app/` 和 `renderer/` 不包含 Vulkan 头文件。
- Vulkan 类型只出现在 `src/rhi/vulkan/`。

## 暂不实现

本阶段不做以下内容：

- Mesh / Material / Camera 的真实渲染。
- Shader 编译和加载。
- Graphics pipeline 创建。
- Descriptor set 和 bindless 系统。
- Vertex buffer / index buffer 上传。
- glTF 加载。
- ImGui backend。
- 完整 RenderGraph。
- 自动资源别名。
- async compute。
- 多线程 command recording。

这些内容等第一帧清屏闭环稳定后再推进。

## 完成后的下一阶段

Phase 0.4 建议进入资源系统和基础绘制：

```text
VulkanAllocator
-> VulkanResourceManager
-> Buffer / Texture owned resource
-> Shader bytecode
-> PipelineLayout / PipelineState
-> DescriptorSetLayout / DescriptorSet
-> 简单三角形或全屏三角形
```

如果 Phase 0.3 中已经顺手完成了 `ClearPass`，Phase 0.4 可以直接从 `Buffer + Shader + PipelineState` 开始；如果 Phase 0.3 只完成了后端 clear，则 Phase 0.4 前半段先把 clear 上移到 `FrameRenderer + ClearPass`。

## 本轮代码阅读指南

本轮代码的目标是把 Phase 0.3 的第一帧清屏闭环跑通。阅读时建议先看整体调用链，再看 RHI 公共接口，最后看 Vulkan 后端实现。

### 1. 先看主流程

建议从 `src/renderer/Renderer.cpp` 开始。

重点阅读：

- `DefaultRenderer::render()`：这是本轮最核心的一帧流程。
- `DefaultRenderer::resize()`：处理窗口尺寸变化、最小化和 swapchain 重建。
- `handleSwapChainStatus()`：把 acquire / present 的状态转换为 resize、暂停或错误日志。

当前一帧流程是：

```text
Renderer::render()
-> context.beginFrame()
-> swapChain.acquireNextImage(frame)
-> context.begin(frame)
-> backbuffer Present/Undefined -> CopyDst
-> context.clearRenderTarget()
-> backbuffer CopyDst -> Present
-> context.end()
-> context.submit()
-> swapChain.present(frame)
-> context.advanceFrame()
```

本阶段还没有真正的 `ClearPass`。清屏命令仍然直接由 `Renderer` 组织，并通过 RHI 调用到 `VulkanCommandContext`。

### 2. 再看应用层 resize 接入

阅读文件：

- `src/app/Application.cpp`
- `src/app/GlfwWindow.cpp`

本轮新增的关键点是：

- `Application` 每帧读取窗口 framebuffer extent。
- 如果 extent 变化，则调用 `Renderer::resize()`。
- `GlfwWindow::getExtent()` 改为实时读取 framebuffer size，而不是只返回创建时的初始尺寸。

这样做是为了让 resize 和窗口最小化进入 renderer，而不是让 Vulkan swapchain 在旧尺寸下继续渲染。

### 3. 阅读公共 RHI 接口

这些文件定义了 renderer 能看到的抽象边界：

- `src/rhi/RenderBackend.h`
- `src/rhi/DeviceContext.h`
- `src/rhi/SwapChain.h`
- `src/rhi/FrameResource.h`
- `src/rhi/Texture.h`
- `src/rhi/TextureView.h`
- `src/rhi/ResourceBarrier.h`

重点理解以下职责：

- `RenderBackend` 统一持有 `RenderDevice + SwapChain + DeviceContext`。
- `RenderDevice` 仍然不参与每帧命令录制。
- `SwapChain` 负责 `acquireNextImage()`、`present()`、backbuffer 查询和 resize。
- `DeviceContext` 负责命令录制、barrier、clear 和 submit。
- `FrameResource` 是公共层的一帧 token，不暴露 Vulkan 对象。
- `Texture` 表示 GPU image 语义，`TextureView` 表示 image view 语义。
- `ResourceBarrier` 使用 RHI 的 `ResourceState`，不暴露 Vulkan layout。

这里的边界很重要：`renderer/` 只看这些公共接口，不应该包含 `rhi/vulkan/` 头文件。

### 4. 阅读 Vulkan 后端工厂

阅读文件：

- `src/rhi/detail/RenderBackendFactory.h`
- `src/rhi/vulkan/VulkanBackendFactory.cpp`

这两个文件负责把 `RenderBackend` 的内部创建请求分发到 Vulkan 后端：

- `createRenderDevice()` 创建 `VulkanDevice`。
- `createSwapChain()` 创建 `VulkanSwapChain`。
- `createDeviceContext()` 创建 `VulkanCommandContext`。

这些函数已经从公开的 `RenderBackend.h` 中移出。renderer 层只应该调用 `createRenderBackend()` 和 `RenderBackend` 成员函数，不应该直接创建 `RenderDevice`、`DeviceContext` 或 `SwapChain`。

### 5. 阅读 Vulkan swapchain

阅读文件：

- `src/rhi/vulkan/VulkanSwapChain.h`
- `src/rhi/vulkan/VulkanSwapChain.cpp`

重点阅读：

- `create()`：创建 swapchain，拿到 backbuffer images 和 image views。
- `acquireNextImage()`：调用 `vkAcquireNextImageKHR`，更新当前 backbuffer index。
- `present()`：调用 `vkQueuePresentKHR`。
- `resize()` / `destroy()`：重建和销毁 swapchain 相关资源。

本轮关键设计：

- swapchain image 本体由 `VkSwapchainKHR` 拥有。
- `VulkanTexture` 只借用 swapchain 的 `VkImage`，不销毁它。
- `VulkanTextureView` 拥有 `VkImageView`，负责销毁 image view。
- swapchain image usage 增加 `VK_IMAGE_USAGE_TRANSFER_DST_BIT`，因为清屏使用 `vkCmdClearColorImage`。

### 6. 阅读 Vulkan frame 和同步

阅读文件：

- `src/rhi/vulkan/VulkanFrameResource.h`
- `src/rhi/vulkan/VulkanFrameResource.cpp`
- `src/rhi/vulkan/VulkanSync.h`
- `src/rhi/vulkan/VulkanSync.cpp`

每个 `VulkanFrameResource` 当前拥有：

- command pool
- command buffer
- image available semaphore
- render finished semaphore
- in-flight fence
- deferred deletion queue 预留

同步关系是：

```text
acquire image
-> signal imageAvailableSemaphore
-> queue submit waits imageAvailableSemaphore
-> queue submit signals renderFinishedSemaphore
-> present waits renderFinishedSemaphore
```

`inFlightFence` 用于 CPU 等待当前 frame resource 不再被 GPU 使用。

### 7. 阅读 Vulkan command context

阅读文件：

- `src/rhi/vulkan/VulkanCommandContext.h`
- `src/rhi/vulkan/VulkanCommandContext.cpp`
- `src/rhi/vulkan/VulkanCommandPool.h/.cpp`
- `src/rhi/vulkan/VulkanCommandBuffer.h/.cpp`

重点阅读：

- `beginFrame()`：选择当前 frame resource，并等待对应 fence。
- `begin()`：reset command pool，开始录制 command buffer。
- `pipelineBarrier()`：把 RHI `ResourceState` 转换为 Vulkan image layout / access / stage。
- `clearRenderTarget()`：使用 `vkCmdClearColorImage` 清屏。
- `submit()`：提交 command buffer，串接 semaphore 和 fence。
- `advanceFrame()`：推进 frame index 和 frame slot。

当前 barrier 只覆盖 Phase 0.3 需要的最小状态：

- `Undefined`
- `Present`
- `CopyDst`
- `RenderTarget`

后续进入 depth、shader resource、copy、compute 等功能时，需要继续扩展状态映射。

### 8. 阅读 Vulkan texture / view

阅读文件：

- `src/rhi/vulkan/VulkanTexture.h`
- `src/rhi/vulkan/VulkanTexture.cpp`
- `src/rhi/vulkan/VulkanTextureView.h`
- `src/rhi/vulkan/VulkanTextureView.cpp`

重点理解：

- `VulkanTexture` 对应 `VkImage`。
- `VulkanTextureView` 对应 `VkImageView`。
- `VulkanTextureOwnership::Borrowed` 表示只包装外部 image，不负责销毁。
- swapchain backbuffer 使用 borrowed texture。
- `TextureView` 可以回溯到 `Texture`，这样 clear 和 barrier 能从 view 找到底层 image。

### 9. 审核重点

审核代码时建议重点看这些点：

- `app/` 和 `renderer/` 是否仍然没有包含 Vulkan 头文件。
- `RenderDevice` 是否仍然不负责每帧命令录制和 submit。
- `SwapChain` 是否只负责 acquire / present / resize / backbuffer。
- `DeviceContext` 是否只负责命令录制、barrier、clear 和 submit。
- swapchain image 是否没有被手动销毁。
- image view 是否在 swapchain 销毁前释放。
- acquire 失败后是否跳过当前帧。
- present 返回 out-of-date / suboptimal 后是否能触发 resize。
- 窗口 extent 为 0 时是否暂停渲染。
- 每帧路径是否没有新增 `try/catch`。
- 日志输出是否使用英文。

### 10. 当前限制

当前实现仍然是 Phase 0.3 的最小闭环，有几个刻意保留的限制：

- clear 还没有上移到 `FrameRenderer + ClearPass`。
- depth buffer 仍未真实分配。
- `ResourceState` 到 Vulkan barrier 的映射还很少。
- `VulkanCommandQueue` 仍未独立抽象，submit 暂时在 `VulkanCommandContext` 内。
- `VulkanTextureOwnership::Owned` 目前只是预留语义，普通 texture 的 VMA 分配还未实现。
- 每帧路径已经尽量使用状态返回，但初始化阶段仍保留早期已有异常，后续可逐步改为 `Result/Status`。
