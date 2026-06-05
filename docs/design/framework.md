# ARKRenderer 框架设计

本文档记录第一版渲染器框架设计。当前目标是先搭建一个清晰、可运行、便于后续扩展的 Vulkan-first 渲染器骨架。

设计取向：

- 借鉴 Diligent Engine 的 RHI 边界：`RenderDevice`、`DeviceContext`、`SwapChain`。
- 借鉴 Granite 的 Vulkan-first 后端：显式资源、显式同步、延迟销毁、descriptor allocator、VMA。
- 第一阶段优先跑通窗口、Vulkan device、swapchain、ClearPass 和 ImGuiPass。
- RenderGraph 先保留轻量接口，不在第一版实现复杂依赖分析和自动同步。

相关文档：

- `docs/design/module_responsibility.md`：记录各模块职责、依赖边界、对象所有权和 Vulkan 阶段开发守则。

## 目录结构

```text
src/
|-- app/
|   |-- Application.h
|   `-- Window.h
|
|-- renderer/
|   |-- Renderer.h
|   |-- FrameRenderer.h
|   |-- FrameContext.h
|   |-- RenderScene.h
|   |-- RenderView.h
|   |-- RenderQueue.h
|   |-- RenderPass.h
|   |-- RenderGraph.h
|   |
|   |-- passes/
|   |   |-- ClearPass.h
|   |   |-- ForwardPass.h
|   |   |-- ShadowPass.h
|   |   |-- SkyboxPass.h
|   |   |-- BloomPass.h
|   |   |-- ToneMappingPass.h
|   |   `-- ImGuiPass.h
|   |
|   `-- material/
|       |-- Material.h
|       `-- MaterialSystem.h
|
|-- rhi/
|   |-- RHICommon.h
|   |-- RenderBackend.h
|   |-- RenderDevice.h
|   |-- DeviceContext.h
|   |-- SwapChain.h
|   |
|   |-- Buffer.h
|   |-- Texture.h
|   |-- TextureView.h
|   |-- Sampler.h
|   |-- Shader.h
|   |-- PipelineLayout.h
|   |-- PipelineState.h
|   |-- DescriptorSetLayout.h
|   |-- DescriptorSet.h
|   |-- ResourceBarrier.h
|   |-- Fence.h
|   |-- FrameResource.h
|   |
|   `-- vulkan/
|       |-- VulkanDevice.h
|       |-- VulkanCommandContext.h
|       |-- VulkanSwapChain.h
|       |
|       |-- VulkanBuffer.h
|       |-- VulkanTexture.h
|       |-- VulkanTextureView.h
|       |-- VulkanSampler.h
|       |-- VulkanShader.h
|       |-- VulkanPipelineLayout.h
|       |-- VulkanPipelineState.h
|       |-- VulkanDescriptorSetLayout.h
|       |-- VulkanDescriptorSet.h
|       |
|       |-- VulkanAllocator.h
|       |-- VulkanBackendFactory.cpp
|       |-- VulkanResourceManager.h
|       |-- VulkanDescriptorManager.h
|       |-- VulkanBindlessResourceManager.h
|       |-- VulkanPipelineCache.h
|       |-- VulkanCommandPool.h
|       |-- VulkanCommandBuffer.h
|       |-- VulkanCommandQueue.h
|       |-- VulkanSync.h
|       |-- VulkanFrameResource.h
|       |-- VulkanImGuiBackend.h
|       `-- VulkanDeletionQueue.h
|
|-- asset/
|   |-- GltfLoader.h
|   |-- TextureLoader.h
|   `-- ShaderCompiler.h
|
`-- core/
    |-- Log.h
    |-- Assert.h
    |-- FileSystem.h
    |-- Timer.h
    |-- Types.h
    |-- NonCopyable.h
    `-- Result.h
```

说明：

- 第一版不在 `rhi/` 公共层暴露 `CommandBuffer.h` 和 `CommandQueue.h`。上层通过 `DeviceContext` 录制和提交命令。
- `VulkanCommandBuffer`、`VulkanCommandQueue` 暂时属于 Vulkan 后端实现细节。
- 等后续需要多线程录制、async compute 或多队列调度时，再考虑抽象 `CommandList` / `CommandQueue` 到 RHI 公共层。

## 分层职责

`app/` 负责应用生命周期、窗口、输入和主循环。

`renderer/` 负责高层渲染流程，包括一帧如何组织、场景如何转成渲染队列、pass 如何执行。

`rhi/` 定义渲染硬件接口。第一版采用 Vulkan-first 设计，不追求过早支持多后端，但上层不直接接触 Vulkan 类型。

`rhi/vulkan/` 是 Vulkan 后端实现，是项目中唯一允许直接包含 Vulkan 头文件的位置。

Vulkan 后端目标架构采用 `VulkanDevice + VulkanCommandContext + VulkanSwapChain` 三个核心对象：

- `VulkanDevice` 对应公共 RHI 的 `RenderDevice` 实现，负责设备创建、设备能力查询、资源创建和设备级生命周期。
- `VulkanCommandContext` 对应公共 RHI 的 `DeviceContext` 实现，负责 command buffer 录制、资源状态转换、绑定状态、draw / dispatch / copy / submit。
- `VulkanSwapChain` 对应公共 RHI 的 `SwapChain` 实现，负责窗口 surface、backbuffer、默认 depth buffer、acquire、present 和 resize。

代码结构应直接使用 `VulkanDevice`、`VulkanCommandContext` 和 `VulkanSwapChain` 作为后端核心类型，避免再引入容易模糊职责的旧命名。

`asset/` 只负责解析文件并输出 CPU 侧中间数据，例如 `ImageData`、`MeshData`、`MaterialData`、`ShaderBytecode`。GPU 资源创建由 `renderer/` 调用 `RenderDevice` 完成。

`core/` 提供日志、断言、文件系统、时间、基础类型和通用工具，不依赖任何上层模块。

## 依赖规则

```text
app/          -> renderer/, core/
renderer/     -> rhi/, asset/, core/
asset/        -> core/
rhi/          -> core/
rhi/vulkan/   -> rhi/, core/
core/         -> no upper layer
```

硬性规则：

- 只有 `src/rhi/vulkan/` 可以包含 Vulkan 头文件。
- `renderer/` 不允许直接使用 `VkImage`、`VkBuffer`、`VkCommandBuffer` 等 Vulkan 类型。
- `asset/` 不直接创建 GPU 资源，只输出 CPU 侧数据。
- `app/` 不关心 Vulkan swapchain、queue、command buffer 等后端细节。
- `rhi/vulkan/` 不直接依赖 `app/Window.h`，只能通过 RHI 层定义的 native window 描述创建 surface。
- `rhi/` 公共层尽量表达渲染概念，不泄露 Vulkan layout、access mask、pipeline stage 等后端细节。

## 关键对象

```text
Renderer
    渲染系统门面。
    负责初始化、关闭、resize 和 render(scene, view)。
    持有 RenderDevice、DeviceContext、SwapChain、FrameRenderer 和 MaterialSystem。

FrameRenderer
    负责组织一帧的渲染流程。
    第一版手动调度 ClearPass、ImGuiPass，后续加入 ForwardPass、ShadowPass 等。
    未来可以逐步转向 RenderGraph 驱动。

FrameContext
    renderer 层的一帧逻辑上下文。
    传递 frame index、delta time、scene、view、queue、device、context 和 swapchain。

RenderGraph
    第一版作为轻量 pass 调度器。
    暂不负责自动 barrier、资源别名、transient resource 和 async compute。
    后续再升级为真正的 frame graph。

RenderDevice
    设备与资源工厂。
    负责创建 Buffer、Texture、TextureView、Sampler、Shader、Fence、
    PipelineLayout、PipelineState、DescriptorSetLayout、DescriptorSet 等对象。
    保存设备能力信息，管理 allocator、descriptor allocator、pipeline cache 和设备级资源生命周期。
    一般不负责每帧 draw，不负责 command buffer 录制，也不负责 acquire / present。

DeviceContext
    资源使用与命令执行对象。
    维护当前绑定状态，录制和提交命令，包括 draw、dispatch、copy、map/update、
    resource transition、begin/end rendering。
    对应 Vulkan 的 command buffer / command encoder / queue submit 这一侧。

SwapChain
    管理窗口相关的 surface、backbuffer、默认 depth buffer、resize、acquire 和 present。
    对 renderer 层提供当前 backbuffer render target view 和 depth stencil view。
```

## Vulkan 后端目标架构

Vulkan 后端按以下对象分工落地：

```text
VulkanDevice
    create Buffer / Texture / Shader / Pipeline / Sampler / Fence
    query and store device capabilities
    own VkInstance / VkDevice / VkSurfaceKHR / queues
    own allocator, descriptor manager, bindless manager, pipeline cache
    provide waitIdle

VulkanCommandContext
    beginRendering / endRendering
    setPipeline
    bindDescriptorSet / bindGlobalBindlessSet
    setVertexBuffer / setIndexBuffer
    draw / drawIndexed / dispatch
    copy / update / map if needed
    pipelineBarrier / resource transition
    submit command buffer

VulkanSwapChain
    acquire
    getCurrentBackBuffer
    getDepthBuffer
    present
    resize

VulkanFrameResource
    command buffer
    imageAvailableSemaphore
    renderFinishedSemaphore
    inFlightFence
    per-frame deferred deletion

VulkanResourceManager
    manage resource handles
    track resource lifetime
    route delayed destruction to current frame deferred deletion

VulkanDescriptorManager
    allocate descriptor pools / descriptor sets
    manage descriptor set layouts where needed

VulkanBindlessResourceManager
    allocate bindless indices
    update global bindless descriptor set
    recycle bindless slots safely

VulkanPipelineCache
    cache graphics pipelines
    cache compute pipelines
    own VkPipelineCache if enabled

RenderGraph
    added later
    manage pass order, resource read/write declarations and barriers
```

命名规则：

- 公共 RHI 层继续使用 `RenderDevice`、`DeviceContext`、`SwapChain`。
- Vulkan 后端实现建议使用 `VulkanDevice`、`VulkanCommandContext`、`VulkanSwapChain`。
- renderer 层的 `FrameContext` 保留为高层一帧语义上下文。
- 底层每帧 GPU 对象使用 `VulkanFrameResource`，不要命名为 `FrameContext`，避免和 renderer 层混淆。

所有权规则：

- 项目代码使用 `core/Memory.h` 中的 `Scope<T>` 表达唯一所有权，使用 `makeScope<T>()` 创建对象。
- `Ref<T>` 只用于确实需要共享生命周期的对象，例如后续 asset cache 中的共享材质或纹理资源。
- RHI 设备、swapchain、后端 RAII 对象默认使用 `Scope<T>`；GPU 资源后续优先收敛到 `ResourceManager + handle`，避免大量共享指针导致释放时机不清晰。
- `RenderBackend` 是公共 RHI 层的运行期后端对象，统一持有 `RenderDevice + SwapChain + DeviceContext`；具体 Vulkan 创建逻辑放在 `rhi/vulkan/VulkanBackendFactory.cpp`，避免 `renderer/` 直接包含 `rhi/vulkan/` 头文件。

落地顺序：

1. `VulkanDevice + VulkanSwapChain`：只完成设备、surface、swapchain、backbuffer 和默认 depth buffer。
2. `VulkanCommandContext + VulkanFrameResource`：完成 command buffer、semaphore、fence、acquire / submit / present 和清屏。
3. `VulkanResourceManager`：引入资源 handle、生命周期和延迟销毁。
4. `VulkanDescriptorManager + VulkanBindlessResourceManager`：引入 descriptor set、bindless index 和全局 bindless set。
5. `VulkanPipelineCache`：缓存 graphics / compute pipeline。
6. `RenderGraph`：多 pass、资源读写声明和自动 barrier 后期再加。

## Native Window Handle

`rhi/vulkan/` 不直接依赖 `app/Window`。`Window` 只负责返回平台窗口句柄，Vulkan 后端根据该句柄创建 `VkSurfaceKHR`。

注意：Vulkan physical device 和 present queue 的选择通常需要 `VkSurfaceKHR`。因此第一版推荐在 `VulkanDevice` 初始化阶段根据 `NativeWindowHandle` 创建 surface，并用它完成 physical device / queue family 选择；`VulkanSwapChain` 借用该 surface 创建 swapchain。后续如果支持多窗口，再独立抽象 `Surface` 对象。

RHI 层可以定义轻量 native window 描述：

```cpp
enum class NativeWindowType
{
    GLFW,
    Win32,
    SDL,
};

struct NativeWindowHandle
{
    NativeWindowType type = NativeWindowType::GLFW;
    void* handle = nullptr;
};
```

`Window` 提供：

```cpp
NativeWindowHandle Window::getNativeWindowHandle() const;
```

`RenderDeviceCreateInfo` 持有 native window 描述，因为第一版由 `VulkanDevice` 在初始化阶段创建 `VkSurfaceKHR`，并用该 surface 完成 physical device / present queue 选择：

```cpp
struct RenderDeviceCreateInfo
{
    RenderDeviceDesc desc;
    NativeWindowHandle nativeWindow;
};
```

`SwapChainDesc` 只描述 swapchain 自身的持久属性，不保存窗口句柄：

```cpp
struct SwapChainDesc
{
    Extent2D extent;
    Format colorFormat = Format::Unknown;
    Format depthFormat = Format::D32Float;
    uint32_t imageCount = 2;
    bool enableVSync = true;
};

struct SwapChainCreateInfo
{
    SwapChainDesc desc;
    RenderDevice* device = nullptr;
};
```

依赖方向保持为：

```text
app/Window
    -> 返回 NativeWindowHandle

renderer/
    -> 将 NativeWindowHandle 填入 RenderDeviceCreateInfo
    -> 将 SwapChainDesc + RenderDevice 填入 SwapChainCreateInfo

rhi/vulkan/
    -> VulkanDevice 根据 NativeWindowHandle 创建 VkSurfaceKHR
    -> VulkanSwapChain 借用已有 surface 创建 VkSwapchainKHR
```

这样可以守住 `rhi/vulkan/ -> rhi/, core/` 的依赖规则，同时避免 `app/` 或 `renderer/` 直接接触 Vulkan surface。

## FrameContext 与 FrameResource

`FrameContext` 属于 `renderer/` 层，是 pass 执行时使用的一帧逻辑上下文。

示例字段：

```cpp
struct FrameContext
{
    uint64_t frameIndex = 0;
    float deltaTime = 0.0f;

    RenderScene* scene = nullptr;
    RenderView* view = nullptr;
    RenderQueue* queue = nullptr;

    rhi::RenderDevice* device = nullptr;
    rhi::DeviceContext* context = nullptr;
    rhi::SwapChain* swapChain = nullptr;
};
```

`FrameResource` 是每帧命令录制和 GPU 提交需要的底层资源集合。它不属于 `RenderDevice` 的核心职责，第一版可以作为 `DeviceContext` 或 `Renderer` 内部 frame scheduler 的实现细节。

公共层的 `FrameResource.h` 应保持抽象，不包含 `VkFence`、`VkSemaphore`、`VkCommandPool` 等后端类型。具体 Vulkan 内容放在 `VulkanFrameResource.h`。

典型 Vulkan 内容：

- command pool
- command buffer
- in-flight fence
- image available semaphore
- render finished semaphore
- upload buffer / upload allocator
- delayed deletion queue index

两者不要混用：

- `FrameContext` 回答“这一帧要画什么，pass 可以访问哪些高层对象”。
- `FrameResource` 回答“这一帧命令录制和 GPU 提交需要哪些底层资源和同步对象”。

## Frame Sync Ownership

第一版同步对象可以由 `FrameResource` 持有，但 `FrameResource` 不应由 `RenderDevice` 直接驱动。更清晰的划分是：

- `Renderer` 或内部 frame scheduler 负责选择当前 frame，并协调 acquire、record、submit、present 的顺序。
- `SwapChain` 负责 acquire / present，以及窗口 backbuffer 和默认 depth buffer 的 resize。
- `DeviceContext` 负责 command buffer 录制、资源状态转换和 queue submit。
- `RenderDevice` 只提供设备能力、资源创建、设备级生命周期管理和 `waitIdle()`。

第一版一帧同步流程示例：

```cpp
FrameResource& frame = frameScheduler.currentFrame();

if (!swapChain.acquireNextImage(frame.imageAvailableSemaphore))
{
    device.waitIdle();
    renderer.resize(window.getWidth(), window.getHeight());
    return;
}

context.begin(frame);
frameRenderer.render(frameContext);
context.end();

SubmitDesc submit;
submit.waitSemaphore = frame.imageAvailableSemaphore;
submit.signalSemaphore = frame.renderFinishedSemaphore;
submit.fence = frame.inFlightFence;

context.submit(submit);
swapChain.present(frame.renderFinishedSemaphore);

frameScheduler.advance();
```

如果 `acquireNextImage()` 或 `present()` 返回 out-of-date / suboptimal，第一版可以跳过当前帧绘制并触发 resize。注意：acquire 失败后不能继续录制绘制命令。

## Window 与 Vulkan Surface

`Window` 不应该暴露 Vulkan 类型。不要设计如下接口：

```cpp
VkSurfaceKHR Window::createSurface(VkInstance instance);
```

推荐让 `Window` 只暴露 native window handle 和尺寸：

```cpp
class Window
{
public:
    NativeWindowHandle getNativeWindowHandle() const;
    uint32_t getWidth() const;
    uint32_t getHeight() const;
};
```

Vulkan surface 由 `rhi/vulkan/` 创建。第一版推荐由 `VulkanDevice` 或它内部的 helper 在设备初始化早期创建 surface，因为 physical device / present queue 选择需要查询 surface 支持；`VulkanSwapChain` 只负责基于已有 surface 创建和重建 swapchain。

这样可以守住边界：

- `app/` 不知道 Vulkan。
- `renderer/` 不知道 Vulkan。
- 只有 `rhi/vulkan/` 知道 Vulkan surface 的创建细节。

## ImGui Backend Boundary

`renderer/passes/ImGuiPass` 不直接包含 `imgui_impl_vulkan.h`，也不直接接触 `VkCommandBuffer`、`VkDescriptorPool` 等 Vulkan 类型。

推荐结构：

```text
renderer/passes/ImGuiPass
    -> 调用 RHI 层或 Renderer 持有的 ImGui backend 抽象

rhi/vulkan/VulkanImGuiBackend
    -> 负责 imgui_impl_vulkan 初始化、descriptor pool、font upload 和 draw data 渲染
```

第一版可以让 `ImGuiPass` 调用高层接口：

```cpp
class ImGuiPass
{
public:
    void execute(FrameContext& frameContext);
};
```

具体 Vulkan backend 实现必须放在 `rhi/vulkan/`，避免 ImGui Vulkan backend 污染 `renderer/`。

## ResourceBarrier

`ResourceBarrier.h` 不直接暴露 Vulkan layout、access mask 和 pipeline stage。RHI 层先定义自己的资源状态：

```cpp
enum class ResourceState
{
    Undefined,

    Present,

    RenderTarget,
    DepthStencilWrite,
    DepthStencilRead,

    ShaderResource,
    UnorderedAccess,

    CopySrc,
    CopyDst,

    VertexBuffer,
    IndexBuffer,
    ConstantBuffer,
    IndirectArgument,
};
```

`ResourceState` 是 RHI 层语义状态，不等同于 Vulkan image layout。Vulkan 后端根据资源类型、old state、new state 推导：

- `VkImageLayout`
- `VkAccessFlags2`
- `VkPipelineStageFlags2`

这样上层 pass 只需要表达渲染意图，例如 `ResourceState::RenderTarget`，不需要直接写 `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL`。

## RenderGraph 第一版定位

第一阶段的 `RenderGraph` 只负责：

- 记录 pass 顺序。
- 保存共享的命名资源。
- 顺序执行 pass。

第一阶段暂不负责：

- 自动 barrier。
- transient texture allocator。
- 资源别名。
- async compute。
- 跨 queue 同步。

建议第一版接口保持很轻：

```cpp
class RenderGraph
{
public:
    void addPass(RenderPass* pass);
    void execute(FrameContext& frameContext);
};
```

等 `ForwardPass`、`ShadowPass`、`BloomPass`、`ToneMappingPass` 都有真实资源依赖后，再把它升级为完整 frame graph。

## 一帧流程

第一阶段采用手动 pass 调度，一帧流程如下：

```text
Application::run()
    -> Window::pollEvents()
    -> Renderer::render(scene, view)
        -> SwapChain::acquireNextImage()
        -> DeviceContext::begin()
        -> FrameRenderer::render(frameContext)
            -> ClearPass::execute(frameContext)
            -> ImGuiPass::execute(frameContext)
        -> DeviceContext::end()
        -> DeviceContext::submit()
        -> SwapChain::present()
```

职责说明：

- `Renderer` 或内部 frame scheduler 选择当前 frame resource，并协调一帧执行顺序。
- `SwapChain::acquireNextImage()` 获取当前 backbuffer。
- `DeviceContext::begin()` / `end()` 录制当前帧 command buffer。
- `DeviceContext::submit()` 提交 command buffer，并处理 wait/signal semaphore。
- `SwapChain::present()` 呈现当前 backbuffer。
- `RenderDevice` 不参与每帧 draw，只提供资源创建、设备能力查询、设备级生命周期管理和 `waitIdle()`。

如果 acquire 或 present 失败，当前帧不能继续正常绘制，应进入 resize / recreate swapchain 流程。

## Resize 规则

当窗口尺寸变化、swapchain out-of-date 或 suboptimal 时：

1. 等待 GPU 完成当前相关工作。
2. 销毁旧 swapchain 相关资源。
3. 重建 swapchain backbuffer。
4. 由 `SwapChain` 重建默认 depth buffer / depth stencil view。
5. 重建其他依赖 swapchain 尺寸的 render targets，例如 HDR color texture。
6. 通知 `FrameRenderer`、`RenderGraph` 和 pass 更新尺寸相关资源。
7. 更新 ImGui 相关 framebuffer 状态。

第一版可以使用简单策略：

```cpp
device.waitIdle();
swapChain.resize(width, height);
frameRenderer.resize(width, height);
```

后续再优化为更细粒度的同步，避免全局 `waitIdle()`。

## 第一阶段目标

第一阶段只实现最小可运行渲染闭环：

```text
Application
-> Window
-> Renderer
-> VulkanDevice
-> VulkanSwapChain
-> ClearPass
-> ImGuiPass
-> Present
```

Milestones：

1. `Window + Log + Application` 主循环。
2. Vulkan instance、validation layer、physical device、logical device、graphics queue。
3. Swapchain、acquire、present。
4. DeviceContext、command buffer begin/end/submit。
5. 清屏 backbuffer。
   - 5.1 先在 Vulkan 后端手写 command buffer clear backbuffer。
   - 5.2 再接入 DeviceContext + ClearPass 封装。
6. ImGuiPass。
7. Resize handling。

验收标准：

- 可以打开窗口。
- 可以创建 Vulkan instance、device、queue 和 swapchain。
- 可以 clear backbuffer。
- 可以显示 ImGui。
- resize 不崩溃。
- validation layer 没有明显错误。

完成这个阶段后，再逐步实现 `Buffer`、`Shader`、`PipelineState`、`DescriptorSet` 和 `ForwardPass`。

## 暂不实现

第一版暂不实现以下内容：

- 完整 RenderGraph 依赖分析。
- 自动资源 aliasing。
- async compute。
- 多线程 command recording。
- 完整 PBR 材质系统。
- glTF 场景完整导入。
- Shadow / Bloom / ToneMapping 的完整效果链。

这些系统等第一帧稳定跑起来后，再随着真实需求逐步加入。
