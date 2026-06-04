# ARKRenderer 模块职责与边界

本文档用于约束 Vulkan 阶段开发前后的模块职责、依赖方向和对象所有权。目标是让代码保持高内聚、低耦合：每个模块只做自己该做的事，跨模块通信通过稳定接口完成，Vulkan 细节只停留在后端实现层。

## framework.md 审核

`docs/design/framework.md` 的总体设计方向是合理的：

- `app/`、`renderer/`、`rhi/`、`rhi/vulkan/`、`asset/`、`core/` 的分层清晰。
- `Window` 不暴露 Vulkan 类型，这个边界是正确的。
- `FrameContext` 和 `FrameResource` 分离是正确的，前者表达一帧的渲染语义，后者表达底层 GPU 同步和命令资源。
- `RenderGraph` 第一版保持轻量是正确的，当前不应该过早实现复杂资源别名、自动 barrier 和 async compute。
- `DeviceContext` 隐藏 command buffer / queue 细节是正确的，上层不应该直接操作 `VkCommandBuffer`。

需要在 Vulkan 阶段特别注意的一点：

- physical device 和 present queue 选择通常依赖 `VkSurfaceKHR`。因此 surface 的创建时机应早于 logical device 创建。第一版推荐由 `VulkanDevice` 在初始化阶段通过 `NativeWindowHandle` 创建 surface，并用该 surface 完成设备和队列选择；`VulkanSwapChain` 借用这个 surface 创建 swapchain。
- `RenderDevice` 不应该设计成每帧执行器。它负责创建设备对象、保存设备能力信息和管理设备级资源生命周期；每帧命令录制、提交和资源状态转换属于 `DeviceContext`；窗口 backbuffer、默认 depth buffer、resize、acquire 和 present 属于 `SwapChain`。

## 总体原则

```text
app
 -> renderer
    -> rhi
       -> rhi/vulkan

asset -> core
rhi   -> core
core  -> no upper layer
```

开发时遵守以下规则：

- 依赖只能从上层指向下层，不能反向依赖。
- 上层表达“要做什么”，下层决定“怎么做”。
- Vulkan 类型只能出现在 `src/rhi/vulkan/`。
- 资源创建集中在 RHI 层，资源使用发生在 renderer 层。
- CPU 侧资产解析与 GPU 资源创建分离。
- 生命周期由 owning object 负责，借用对象不释放资源。
- 第一版优先跑通稳定闭环，不为未来功能提前制造复杂抽象。

## 模块职责

### core/

职责：

- 日志、断言、基础类型、计时器、文件系统和通用工具。
- 提供所有模块都可以使用的轻量基础设施。

允许依赖：

- C++ 标准库。
- 必要的基础第三方库，例如 `spdlog`。

禁止事项：

- 不依赖 `app/`、`renderer/`、`rhi/`、`asset/`。
- 不包含 Vulkan、GLFW、ImGui 等平台或渲染后端头文件。
- 不保存渲染状态。

### app/

职责：

- 应用生命周期。
- 主循环。
- 窗口创建和销毁。
- 输入与平台事件轮询。
- 把窗口尺寸和 native window handle 提供给 renderer。

典型对象：

- `Application`
- `Window`
- `GlfwWindow`

允许依赖：

- `renderer/`
- `core/`
- `rhi/RHICommon.h` 中的平台无关描述，例如 `NativeWindowHandle`、`Extent2D`。

禁止事项：

- 不创建 `VkInstance`、`VkDevice`、`VkSurfaceKHR`、swapchain。
- 不包含 Vulkan 头文件。
- 不直接调用 RenderPass 或底层 RHI 命令。
- 不保存 GPU 资源。

边界说明：

`Window` 只负责返回 native handle，不负责创建 Vulkan surface：

```text
Window
 -> NativeWindowHandle
    -> renderer
       -> rhi/vulkan creates VkSurfaceKHR
```

### renderer/

职责：

- 渲染系统门面。
- 组织一帧渲染流程。
- 管理场景视图、渲染队列、FrameContext、pass 调度和材质系统。
- 通过 RHI 接口创建和使用 GPU 资源。

典型对象：

- `Renderer`
- `FrameRenderer`
- `FrameContext`
- `RenderScene`
- `RenderView`
- `RenderQueue`
- `RenderPass`
- `RenderGraph`

允许依赖：

- `rhi/`
- `asset/`
- `core/`

禁止事项：

- 不包含 Vulkan 头文件。
- 不使用 `VkImage`、`VkBuffer`、`VkCommandBuffer` 等后端类型。
- 不直接创建 GLFW window。
- 不解析 glTF、图片文件或 shader 源文件的具体格式细节。

边界说明：

`renderer/` 应该只看到 RHI 概念，例如 `Texture`、`TextureView`、`Buffer`、`PipelineState`。如果某个 renderer 对象必须知道 `VkImageLayout` 或 `VkDescriptorSet`，说明抽象边界已经泄漏。

### renderer/passes/

职责：

- 实现具体渲染 pass 的高层逻辑。
- 声明或持有该 pass 所需的 RHI 资源。
- 在 `FrameContext` 提供的上下文中执行渲染命令。

典型对象：

- `ClearPass`
- `ForwardPass`
- `ShadowPass`
- `SkyboxPass`
- `BloomPass`
- `ToneMappingPass`
- `ImGuiPass`

允许依赖：

- `renderer/`
- `rhi/`
- `core/`

禁止事项：

- 不包含 Vulkan 头文件。
- 不直接操作 command buffer 后端对象。
- 不负责 swapchain acquire / present。
- 不负责全局设备生命周期。

第一版建议：

- `ClearPass` 只表达清屏颜色和目标。
- `ImGuiPass` 不直接包含 `imgui_impl_vulkan.h`，具体 Vulkan ImGui backend 放在 `rhi/vulkan/`。
- pass 的执行顺序先由 `FrameRenderer` 手动调度，RenderGraph 暂时只保留轻量接口。

### renderer/material/

职责：

- 管理材质描述、shader 变体、参数绑定语义。
- 把材质语义转换为 RHI 层可理解的 pipeline、descriptor layout 和资源绑定。

允许依赖：

- `rhi/`
- `asset/`
- `core/`

禁止事项：

- 不解析完整 glTF 文件。
- 不直接分配 Vulkan descriptor set。
- 不暴露 Vulkan descriptor layout。

### asset/

职责：

- 读取和解析外部资产。
- 输出 CPU 侧中间数据。
- 编译或加载 shader bytecode。

典型对象：

- `GltfLoader`
- `TextureLoader`
- `ShaderCompiler`

输出数据示例：

- `MeshData`
- `ImageData`
- `MaterialData`
- `ShaderBytecode`

允许依赖：

- `core/`
- 文件格式相关第三方库。

禁止事项：

- 不创建 GPU buffer。
- 不创建 GPU texture。
- 不依赖 `renderer/`。
- 不依赖 `rhi/`，除非只是使用非常稳定的无后端枚举；第一版建议避免。

### rhi/

职责：

- 定义平台无关的渲染硬件接口。
- 定义资源描述、资源状态、提交描述、swapchain 描述和抽象资源对象。
- 向 renderer 提供稳定 API。

典型对象：

- `RenderDevice`
- `DeviceContext`
- `SwapChain`
- `Buffer`
- `Texture`
- `TextureView`
- `Sampler`
- `Shader`
- `PipelineLayout`
- `PipelineState`
- `DescriptorSetLayout`
- `DescriptorSet`
- `ResourceBarrier`
- `FrameResource`

允许依赖：

- `core/`

禁止事项：

- 不包含 Vulkan 头文件。
- 不暴露 Vulkan layout、access mask、pipeline stage。
- 不暴露 `VkDescriptorSet`、`VkCommandBuffer`、`VkFence`。
- 不依赖 `app/Window.h`。

设计要点：

- `RenderDevice` 是设备与资源工厂，负责创建 Buffer、Texture、Shader、PipelineState、Sampler、Fence、PipelineResourceSignature 或 PipelineLayout / DescriptorSetLayout 等对象。
- `RenderDevice` 保存设备能力信息，管理 allocator、descriptor allocator、pipeline cache 和设备级资源生命周期。
- `RenderDevice` 不负责每帧 draw，不负责 command buffer 录制，不负责 acquire / present。
- `DeviceContext` 是资源使用与命令执行对象，维护当前绑定状态，负责 draw、dispatch、copy、map/update、resource transition、command recording 和 queue submit。
- `DeviceContext` 对应 Vulkan 的 command buffer / command encoder / queue submit 这一侧。
- `SwapChain` 负责窗口相关的 backbuffer、默认 depth buffer、resize、acquire 和 present。
- `SwapChain` 对上层提供当前 backbuffer render target view 和 depth stencil view，命名可以是 `getCurrentBackBufferView()` / `getDepthBufferView()`，也可以后续演进为 `GetCurrentBackBufferRTV()` / `GetDepthBufferDSV()` 风格。
- `FrameResource` 在公共层保持抽象，具体同步对象放在 `rhi/vulkan/`；它是 `DeviceContext` 或 `Renderer` 内部 frame scheduler 的实现细节，不属于 `RenderDevice` 的核心职责。

### rhi/vulkan/

职责：

- 实现 RHI 接口。
- 持有和销毁 Vulkan 对象。
- 处理 Vulkan 同步、内存分配、descriptor 分配、pipeline 创建、swapchain 重建和延迟销毁。

典型对象：

- `VulkanDevice`
- `VulkanCommandContext`
- `VulkanSwapChain`
- `VulkanFrameResource`
- `VulkanResourceManager`
- `VulkanDescriptorManager`
- `VulkanBindlessResourceManager`
- `VulkanPipelineCache`
- `VulkanAllocator`
- `VulkanDeletionQueue`
- `VulkanCommandPool`
- `VulkanCommandBuffer`
- `VulkanCommandQueue`

允许依赖：

- `rhi/`
- `core/`
- Vulkan SDK / Volk / Vulkan-Headers。
- VMA。
- vk-bootstrap。
- Vulkan backend 需要的第三方实现。

禁止事项：

- 不依赖 `app/Window.h`。
- 不依赖 `renderer/`。
- 不保存 RenderScene、RenderView、Material 等高层对象。
- 不让 Vulkan 类型逃逸到公共 RHI 接口。

设计要点：

- `VulkanDevice` 实现公共层 `RenderDevice`，拥有 instance、debug messenger、surface、physical device reference、logical device、queue handles、allocator、descriptor manager、bindless resource manager、pipeline cache 和设备级 deletion queue。
- `VulkanDevice` 负责创建 Buffer、Texture、Shader、Pipeline、Sampler、Fence 等设备对象，并保存设备能力信息。
- `VulkanDevice` 不拥有 `VulkanFrameResource[]`，也不负责每帧 begin / end。
- `VulkanSwapChain` 拥有 swapchain、backbuffer image views、默认 depth image / depth view，以及 swapchain 相关格式尺寸信息。
- swapchain image 本体由 Vulkan swapchain 拥有，wrapper 只借用 image handle；image view 由后端 wrapper 拥有。
- 默认 depth image 不属于 Vulkan swapchain 本体，但属于 ARKRenderer 的 `SwapChain` 语义，因为它随窗口尺寸和 backbuffer 一起重建。
- `VulkanCommandContext` 实现公共层 `DeviceContext`，使用当前 `VulkanFrameResource` 的 command buffer 录制和提交命令，维护绑定状态和 submit 所需状态。
- `VulkanFrameResource` 应由 `VulkanCommandContext` 或 `Renderer` 内部 frame scheduler 管理；如果后续独立出 `FrameScheduler`，再把所有 per-frame command / sync 对象集中到那里。
- `VulkanResourceManager` 管理资源 handle、资源查找、生命周期和延迟销毁。
- `VulkanDescriptorManager` 管理 descriptor pool、descriptor set 和 descriptor set layout 的分配策略。
- `VulkanBindlessResourceManager` 管理 bindless index、global bindless descriptor set 更新和 slot 回收。
- `VulkanPipelineCache` 缓存 graphics / compute pipeline，并可持有 `VkPipelineCache`。
- 延迟销毁由 `VulkanDeletionQueue` 统一处理，避免 GPU 仍在使用资源时提前释放。

### apps/

职责：

- 放置示例程序、sandbox 和手动验证入口。
- 使用 engine 的公开接口验证功能。

允许依赖：

- `app/`
- `renderer/`
- `core/`

禁止事项：

- 不直接包含 `rhi/vulkan/` 内部头文件。
- 不绕过 engine public API 创建 Vulkan 对象。

## 对象所有权

```text
Application
    owns Window
    owns Renderer

Renderer
    owns RenderDevice
    owns DeviceContext
    owns SwapChain
    owns FrameRenderer
    owns MaterialSystem
    owns or coordinates FrameScheduler

VulkanDevice
    owns VkInstance
    owns VkDebugUtilsMessengerEXT
    owns VkSurfaceKHR
    owns VkPhysicalDevice reference
    owns VkDevice
    owns queues references
    owns VulkanAllocator
    owns VulkanDescriptorManager
    owns VulkanBindlessResourceManager
    owns VulkanPipelineCache
    owns VulkanResourceManager
    owns VulkanDeletionQueue

VulkanSwapChain
    borrows VkDevice
    borrows VkSurfaceKHR
    owns VkSwapchainKHR
    borrows swapchain VkImage[]
    owns backbuffer VkImageView[]
    owns default depth image
    owns default depth image view

FrameScheduler or VulkanCommandContext
    owns VulkanFrameResource[]

VulkanFrameResource
    owns command pool
    owns command buffer
    owns in-flight fence
    owns image-available semaphore
    owns render-finished semaphore
    owns per-frame deferred deletion queue

RenderPass
    owns pass-local RHI resources
    borrows FrameContext during execute()
```

原则：

- 谁创建，谁销毁。
- 借用对象不能释放资源。
- `VkPhysicalDevice` 和 queue handle 是引用性质，不需要销毁。
- swapchain image 由 `VkSwapchainKHR` 管理，不能手动销毁。
- 默认 depth image 由 `SwapChain` 语义拥有，resize 时随 backbuffer 一起重建。
- `FrameResource` 不由 `RenderDevice` 拥有，它属于命令录制/提交侧或 Renderer 的帧调度侧。
- image view、buffer、texture、sampler、pipeline 等 wrapper 必须负责释放自身拥有的后端对象。
- 需要等待 GPU 完成后才能释放的对象进入 deletion queue。

## 初始化顺序

第一版 Vulkan 初始化建议顺序：

```text
Application::run()
    -> Log::initialize()
    -> create Window
    -> create Renderer
        -> create VulkanDevice(nativeWindow)
            -> create instance
            -> create debug messenger
            -> create surface
            -> select physical device with surface support
            -> create logical device
            -> get graphics / present queue
            -> create allocator
            -> create resource manager
            -> create descriptor manager
            -> create bindless resource manager
            -> create pipeline cache
        -> create VulkanSwapChain(surface, window extent)
            -> create backbuffer image views
            -> create default depth image / depth view
        -> create VulkanCommandContext
            -> create or prepare per-frame command resources
        -> create FrameRenderer
    -> main loop
```

关闭顺序必须反向：

```text
Renderer shutdown
    -> device.waitIdle()
    -> destroy pass resources
    -> destroy FrameRenderer
    -> destroy FrameScheduler / frame resources
    -> destroy VulkanCommandContext / DeviceContext
    -> destroy SwapChain
    -> destroy VulkanDevice / RenderDevice internals
Application shutdown
    -> destroy Window
    -> Log::shutdown()
```

## 一帧职责划分

```text
Application
    poll events
    ask Renderer to render

Renderer
    assemble FrameContext
    coordinate acquire / record / submit / present
    choose current frame resource through FrameScheduler if needed

RenderDevice
    create GPU resources
    expose device capabilities
    manage device-level lifetime
    provide waitIdle

SwapChain
    acquire image
    expose current backbuffer view
    expose default depth buffer view
    present image
    resize backbuffer and default depth buffer
    handle out-of-date / suboptimal result

DeviceContext
    prepare current command resources
    begin command recording
    record barriers, clears, draw calls, dispatches
    end command recording
    submit command buffer

FrameRenderer
    execute ordered render passes

RenderPass
    record pass-specific RHI commands
```

第一版应先跑通：

```text
select current frame resource
-> acquire image
-> begin command buffer
-> transition backbuffer Present/Undefined -> RenderTarget
-> clear backbuffer
-> transition backbuffer RenderTarget -> Present
-> end command buffer
-> submit
-> present
-> advance frame resource
```

## Resize 职责

触发来源：

- 窗口尺寸变化。
- `acquireNextImage()` 返回 out-of-date。
- `present()` 返回 out-of-date 或 suboptimal。

职责划分：

- `Application` 只感知窗口尺寸变化，不直接重建 swapchain。
- `Renderer` 负责统一处理 resize 请求。
- `SwapChain` 负责销毁旧 swapchain 资源并创建新 swapchain。
- `SwapChain` 负责重建默认 depth buffer / depth stencil view。
- `FrameRenderer` 和各个 pass 负责重建尺寸相关资源。
- `RenderDevice` 提供 `waitIdle()` 或更细粒度同步能力。

第一版可以采用保守策略：

```text
Renderer::resize(width, height)
    -> if width == 0 or height == 0: skip rendering
    -> device.waitIdle()
    -> swapChain.resize(width, height)
    -> frameRenderer.resize(width, height)
```

## 错误处理与日志

原则：

- 日志输出文本使用英文，避免控制台编码问题。
- 代码注释和文档可以使用中文。
- 构造阶段的不可恢复错误可以抛异常或返回 `Result`，但同一模块内要保持一致。
- Vulkan 调用失败必须带上下文信息，不能只输出错误码。
- Debug 构建启用 validation layer。

建议日志位置：

- `Application`：启动、退出、异常。
- `Window`：窗口创建、销毁、尺寸变化。
- `VulkanDevice`：instance、device、queue、allocator 初始化结果。
- `VulkanSwapChain`：format、present mode、extent、image count、resize。
- `Renderer`：初始化、resize、shutdown。

## 接口演进顺序

为了避免过早设计，Vulkan 阶段建议按以下顺序推进：

1. 补齐 `RenderDeviceDesc`、`RenderDeviceCaps`、`SwapChainDesc`、`SwapChainStatus` 等最小描述结构。
2. 实现 `VulkanDevice` 初始化和销毁。
3. 实现 `VulkanSwapChain` 创建、backbuffer image view、默认 depth buffer 和 resize。
4. 实现 `VulkanFrameResource` 和最小 `VulkanCommandContext`，再补齐 `SubmitDesc`、`AcquireResult`、`PresentResult`。
5. 实现 acquire / submit / present 的第一帧闭环。
6. 在后端内跑通 clear backbuffer。
7. 把 clear 逻辑上移到 `FrameRenderer + ClearPass`。
8. 引入 `VulkanResourceManager`，统一资源 handle 和生命周期。
9. 引入 `VulkanDescriptorManager` 和 `VulkanBindlessResourceManager`。
10. 引入 `VulkanPipelineCache`。
11. 接入 ImGui backend。
12. 再实现 Buffer、Shader、PipelineState、DescriptorSet 和 ForwardPass。
13. 多 pass 稳定后再升级 `RenderGraph`，由它管理 pass 顺序、资源读写声明和 barrier。

每一步都应该保持可构建、可运行、validation layer 尽量干净。

## 开发检查清单

写新代码前检查：

- 这个类应该属于哪一层？
- 它是否包含了不该包含的上层头文件？
- 它是否把 Vulkan 类型暴露给了 `renderer/` 或 `app/`？
- 它是资源 owner，还是只是 borrower？
- 析构时 GPU 是否可能仍在使用该资源？
- resize 时这个资源是否需要重建？
- 这个接口表达的是渲染语义，还是 Vulkan 实现细节？
- 第一版是否真的需要这个抽象？

如果某个改动需要打破本文档的边界，先更新设计文档，再写代码。
