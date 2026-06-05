# Phase 0.4：最小图形管线与第一组三角形

本阶段目标是让 ARKRenderer 从“能够清屏并 present”进入“能够通过 RHI 创建最小 graphics pipeline，并提交一次真实 draw call”的状态。完成后 sandbox 窗口中应能看到一个固定彩色三角形，而不只是单纯清屏。

本阶段的重点不是材质系统、RenderGraph、GLTF 或复杂 descriptor，而是把绘制几何体所需的最小闭环打通：`Buffer`、`Shader`、`PipelineLayout`、`PipelineState`、`beginRendering()`、`setPipeline()`、`setVertexBuffer()` 和 `draw()`。

## 阶段目标

实现最小一帧绘制流程：

```text
Application::run()
-> Renderer::render(scene, view)
    -> 选择当前 FrameResource
    -> SwapChain acquire next image
    -> DeviceContext begin command recording
    -> transition backbuffer: Present/Undefined -> RenderTarget
    -> beginRendering(backbuffer)
    -> ClearPass execute()
    -> TrianglePass execute()
        -> setPipeline()
        -> setVertexBuffer()
        -> draw()
    -> endRendering()
    -> transition backbuffer: RenderTarget -> Present
    -> DeviceContext end command recording
    -> DeviceContext submit
    -> SwapChain present
    -> 推进当前 FrameResource
```

完成后应能运行：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

并看到窗口稳定显示一个三角形。关闭窗口后程序正常退出，Debug 构建下 validation layer 不应出现明显的 pipeline、rendering、layout、同步或对象生命周期错误。

## 设计边界

本阶段继续遵守 `docs/design/framework.md` 和 `docs/design/module_responsibility.md` 中的边界：

- `Renderer` 负责组织一帧流程和 pass 调度，但不直接包含 Vulkan 头文件。
- `RenderDevice` 负责创建 `Buffer`、`Shader`、`PipelineLayout`、`PipelineState` 等 GPU 对象。
- `DeviceContext` 负责使用对象，录制 `beginRendering()`、绑定状态和 `draw()`。
- `SwapChain` 继续只负责 backbuffer、acquire、present 和 resize。
- `VulkanDevice` 实现资源创建，不参与每帧 draw。
- `VulkanCommandContext` 实现命令录制、绑定、draw 和 submit。
- `VulkanSwapChain` 继续拥有 backbuffer views，swapchain image 仍然只被 `VulkanTexture` 借用。
- Vulkan 类型只允许出现在 `src/rhi/vulkan/`。

本阶段不引入完整 RenderGraph。可以新增 `FrameRenderer` / `RenderPass` / `ClearPass` / `TrianglePass` 的最小雏形，但 pass 顺序先由代码手动调度。

## 不做的内容

为了保持阶段目标清晰，本阶段暂不做：

- 不做 GLTF 加载。
- 不做材质系统。
- 不做完整 descriptor set / bindless。
- 不做 uniform buffer 和相机矩阵。
- 不做 texture sampling。
- 不做 depth buffer 绘制。
- 不做复杂 pipeline cache 持久化。
- 不做 RenderGraph 自动 barrier。
- 不做多线程 command recording。

这些内容应留到后续阶段逐步引入。Phase 0.4 只关心“能通过 RHI 画出第一组三角形”。

## 推荐目录变化

```text
src/
|-- renderer/
|   |-- FrameRenderer.h
|   |-- FrameRenderer.cpp
|   |-- FrameContext.h
|   |-- RenderPass.h
|   |
|   `-- passes/
|       |-- ClearPass.h
|       |-- ClearPass.cpp
|       |-- TrianglePass.h
|       `-- TrianglePass.cpp
|
|-- rhi/
|   |-- Buffer.h
|   |-- Shader.h
|   |-- PipelineLayout.h
|   |-- PipelineState.h
|   |-- DeviceContext.h
|   |-- ResourceBarrier.h
|   |
|   `-- vulkan/
|       |-- VulkanBuffer.h
|       |-- VulkanBuffer.cpp
|       |-- VulkanShader.h
|       |-- VulkanShader.cpp
|       |-- VulkanPipelineLayout.h
|       |-- VulkanPipelineLayout.cpp
|       |-- VulkanPipelineState.h
|       `-- VulkanPipelineState.cpp
|
`-- shaders/
    |-- triangle.vert
    `-- triangle.frag
```

说明：

- `TrianglePass` 是阶段验证用 pass，不代表最终渲染架构中的正式 ForwardPass。
- 如果希望命名更贴近最终架构，也可以用 `ForwardPass` 替代 `TrianglePass`，但实现内容仍应保持最小。
- `shaders/` 下保留 GLSL 源文件，构建阶段输出 SPIR-V 到 build 目录或运行资源目录。

## RHI 接口设计

### Buffer

本阶段需要让 RHI 表达最小 buffer 创建和绑定语义：

```cpp
enum class BufferUsage {
    Vertex,
    Index,
    Uniform,
    TransferSrc,
    TransferDst,
};

enum class MemoryUsage {
    CpuToGpu,
    GpuOnly,
};

struct BufferDesc {
    u64 size = 0;
    BufferUsage usage = BufferUsage::Vertex;
    MemoryUsage memoryUsage = MemoryUsage::CpuToGpu;
    const void* initialData = nullptr;
};

class Buffer {
public:
    virtual ~Buffer() = default;

    virtual const BufferDesc& getDesc() const = 0;
};
```

第一版可以先使用 CPU 可见 buffer 放三角形顶点，避免过早引入 staging upload 系统。后续进入资源上传阶段时，再把 `GpuOnly + TransferDst` 和 upload queue 设计完整。

### Shader

`Shader` 第一版只表达 SPIR-V bytecode 和 shader stage：

```cpp
enum class ShaderStage {
    Vertex,
    Fragment,
    Compute,
};

struct ShaderDesc {
    ShaderStage stage = ShaderStage::Vertex;
    std::string debugName;
    std::vector<u32> bytecode;
};

class Shader {
public:
    virtual ~Shader() = default;

    virtual const ShaderDesc& getDesc() const = 0;
};
```

注意：

- `asset/ShaderCompiler` 可以后续再完善。
- Phase 0.4 可以先通过 CMake 编译 shader，运行时只加载 SPIR-V 文件。
- 运行时日志仍然使用英文，避免控制台编码问题。

### PipelineLayout

本阶段没有 descriptor，因此 `PipelineLayout` 可以是空 layout：

```cpp
struct PipelineLayoutDesc {
    std::string debugName;
};

class PipelineLayout {
public:
    virtual ~PipelineLayout() = default;
};
```

后续 Phase 0.5 或 Phase 0.6 再把 descriptor set layout、push constant、bindless layout 纳入 `PipelineLayoutDesc`。

### PipelineState

`PipelineState` 表达 graphics pipeline 的不可变状态：

```cpp
enum class PrimitiveTopology {
    TriangleList,
    LineList,
};

enum class CullMode {
    None,
    Front,
    Back,
};

struct VertexAttributeDesc {
    u32 location = 0;
    Format format = Format::Unknown;
    u32 offset = 0;
};

struct VertexBufferLayoutDesc {
    u32 binding = 0;
    u32 stride = 0;
    std::vector<VertexAttributeDesc> attributes;
};

struct GraphicsPipelineDesc {
    std::string debugName;
    Shader* vertexShader = nullptr;
    Shader* fragmentShader = nullptr;
    PipelineLayout* layout = nullptr;
    std::vector<VertexBufferLayoutDesc> vertexBuffers;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    CullMode cullMode = CullMode::Back;
    Format colorFormat = Format::BGRA8_UNorm;
    Format depthFormat = Format::Unknown;
    bool enableDepthTest = false;
    bool enableDepthWrite = false;
};

class PipelineState {
public:
    virtual ~PipelineState() = default;

    virtual const GraphicsPipelineDesc& getDesc() const = 0;
};
```

本阶段建议使用 Vulkan dynamic rendering，因此 `GraphicsPipelineDesc` 必须包含 color / depth format。不要引入传统 `VkRenderPass` 作为公共 RHI 概念。

### DeviceContext

`DeviceContext` 需要补齐最小绘制接口：

```cpp
struct RenderingAttachmentDesc {
    TextureView* view = nullptr;
    LoadOp loadOp = LoadOp::Clear;
    StoreOp storeOp = StoreOp::Store;
    ClearColor clearColor;
};

struct RenderingDesc {
    Extent2D extent;
    RenderingAttachmentDesc colorAttachment;
    TextureView* depthAttachment = nullptr;
};

struct Viewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct ScissorRect {
    i32 x = 0;
    i32 y = 0;
    u32 width = 0;
    u32 height = 0;
};

class DeviceContext {
public:
    virtual bool beginRendering(const RenderingDesc& desc) = 0;
    virtual void endRendering() = 0;
    virtual void setViewport(const Viewport& viewport) = 0;
    virtual void setScissorRect(const ScissorRect& rect) = 0;
    virtual void setPipeline(PipelineState& pipeline) = 0;
    virtual void setVertexBuffer(u32 slot, Buffer& buffer, u64 offset) = 0;
    virtual void setIndexBuffer(Buffer& buffer, IndexType indexType, u64 offset) = 0;
    virtual void draw(u32 vertexCount, u32 firstVertex) = 0;
    virtual void drawIndexed(const DrawIndexedDesc& desc) = 0;
};
```

第一版三角形可以只用 `draw(3, 0)`，`setIndexBuffer()` 和 `drawIndexed()` 可以同时补接口但暂不在 sandbox 中使用。

## Renderer 层设计

Phase 0.3 中 `Renderer::render()` 直接完成 clear。Phase 0.4 建议引入轻量 `FrameRenderer`，把一帧中的 pass 调度从 `Renderer` 中拆出：

```text
Renderer
    owns RenderBackend
    owns FrameRenderer
    handles resize / backend recreation

FrameRenderer
    owns ClearPass
    owns TrianglePass
    execute(FrameContext)

FrameContext
    frameResource
    renderDevice
    deviceContext
    swapChain
    backBufferView
    extent

RenderPass
    setup(RenderDevice)
    execute(FrameContext)
```

第一版 `FrameRenderer` 不做复杂图管理，只做顺序调度：

```text
FrameRenderer::render(frameContext)
    -> context.pipelineBarrier(backbuffer -> RenderTarget)
    -> context.beginRendering(backbuffer)
    -> clearPass.execute(frameContext)
    -> trianglePass.execute(frameContext)
    -> context.endRendering()
    -> context.pipelineBarrier(backbuffer -> Present)
```

这样可以让 `Renderer` 保持门面职责：

- 初始化 backend。
- 处理 resize。
- 组织 acquire / submit / present。
- 把真正的绘制内容交给 `FrameRenderer`。

## Vulkan 后端实现策略

### VulkanBuffer

`VulkanBuffer` 对应 `VkBuffer + VMA allocation`：

- `VulkanDevice::createBuffer()` 创建 buffer。
- `MemoryUsage::CpuToGpu` 使用 CPU 可见内存，方便上传三角形顶点。
- `BufferUsage::Vertex` 映射为 `VK_BUFFER_USAGE_VERTEX_BUFFER_BIT`。
- 后续 upload 阶段再补 `VK_BUFFER_USAGE_TRANSFER_DST_BIT` 和 staging copy。

### VulkanShader

`VulkanShader` 对应 `VkShaderModule`：

- 从 `ShaderDesc::bytecode` 创建 shader module。
- 保存 stage 信息。
- 析构时销毁 `VkShaderModule`。

### VulkanPipelineLayout

`VulkanPipelineLayout` 第一版创建空 `VkPipelineLayout`：

- descriptor set layout count 为 0。
- push constant range count 为 0。
- 后续 descriptor 阶段再扩展。

### VulkanPipelineState

`VulkanPipelineState` 对应 `VkPipeline`：

- 使用 vertex shader 和 fragment shader。
- 使用 dynamic rendering 的 `VkPipelineRenderingCreateInfo` 指定 color / depth format。
- viewport 和 scissor 建议作为 dynamic state。
- 关闭 depth test / depth write。
- 关闭 blending，或者使用最简单 opaque blend。
- rasterizer 使用 fill polygon mode，cull mode 可以先设为 none，避免三角形顶点绕序导致不可见。

建议默认 Vulkan pipeline 状态：

```text
topology        = triangle list
polygon mode    = fill
cull mode       = none
front face      = counter clockwise
depth test      = disabled
depth write     = disabled
blend           = disabled
dynamic states  = viewport, scissor
```

### VulkanCommandContext

需要补齐：

- `beginRendering()`：调用 `vkCmdBeginRendering`。
- `endRendering()`：调用 `vkCmdEndRendering`。
- `setViewport()`：调用 `vkCmdSetViewport`。
- `setScissorRect()`：调用 `vkCmdSetScissor`。
- `setPipeline()`：调用 `vkCmdBindPipeline`。
- `setVertexBuffer()`：调用 `vkCmdBindVertexBuffers`。
- `draw()`：调用 `vkCmdDraw`。
- `drawIndexed()`：调用 `vkCmdDrawIndexed`。

本阶段可以继续沿用 synchronization2 的 barrier 转换，但需要把 `ResourceState::RenderTarget` 稳定映射到：

```text
layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
stage  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
```

## Shader 构建策略

建议第一版在 CMake 中增加 shader 编译目标：

```text
shaders/triangle.vert
shaders/triangle.frag
    -> build/.../shaders/triangle.vert.spv
    -> build/.../shaders/triangle.frag.spv
```

可选方案：

- 优先使用 Vulkan SDK 提供的 `glslc` 或 `glslangValidator`。
- 如果环境中暂时没有 shader 编译器，可以先在文档中记录依赖，代码阶段再决定是否引入 `shaderc`。
- 不建议第一版手写 SPIR-V 字节数组，后续维护成本太高。

运行时 shader 读取路径建议由 `apps/sandbox` 传入或由 `core/FileSystem` 提供资源目录查询，避免在 RHI 后端中硬编码项目路径。

## 本阶段要完成的代码工作

1. 整理 renderer 层一帧结构：
   - 新增 `FrameContext`。
   - 新增 `FrameRenderer`。
   - 新增最小 `RenderPass` 接口。
   - 新增 `ClearPass`，把 Phase 0.3 的 clear 语义从 `Renderer` 中移出。
   - 新增 `TrianglePass` 或最小 `ForwardPass`，负责创建三角形资源并执行 draw。

2. 补齐 RHI 资源描述：
   - `BufferDesc`
   - `ShaderDesc`
   - `PipelineLayoutDesc`
   - `GraphicsPipelineDesc`
   - vertex input 描述
   - viewport / scissor 描述
   - load / store op 描述

3. 补齐 RHI 命令接口：
   - `beginRendering()`
   - `endRendering()`
   - `setViewport()`
   - `setScissorRect()`
   - `setPipeline()`
   - `setVertexBuffer()`
   - `draw()`
   - 预留 `setIndexBuffer()` / `drawIndexed()`

4. 实现 Vulkan 资源对象：
   - `VulkanBuffer`
   - `VulkanShader`
   - `VulkanPipelineLayout`
   - `VulkanPipelineState`

5. 实现 Vulkan command draw path：
   - dynamic rendering begin / end。
   - pipeline bind。
   - vertex buffer bind。
   - viewport / scissor。
   - draw call。

6. 新增 shader：
   - `triangle.vert`
   - `triangle.frag`
   - CMake shader 编译或复制输出规则。

7. 接入 sandbox：
   - 创建一个固定三角形。
   - 顶点包含 position 和 color。
   - 不使用 uniform buffer。
   - 不使用 descriptor。
   - 窗口 resize 后 pipeline 和 backbuffer format 仍然保持正确。

8. 更新测试和文档：
   - 更新 `framework_headers_smoke.cpp` 覆盖新增 RHI 头文件。
   - 保留 dependency smoke。
   - 文档记录最终实现、限制和下一阶段计划。

## 验收标准

本阶段完成后应满足：

- `ark_sandbox.exe` 能显示一个稳定的彩色三角形。
- resize 后能继续渲染。
- 最小化窗口时不触发 swapchain 重建错误。
- `renderer/` 不包含 Vulkan 头文件。
- `app/` 不包含 Vulkan 头文件。
- `RenderDevice` 不参与每帧 draw / submit。
- `DeviceContext` 负责命令录制、绑定和 draw。
- `SwapChain` 负责 acquire / present / resize。
- Debug 构建下没有明显 Vulkan validation error。
- 新增日志输出使用英文。
- 每帧路径不新增 `try/catch`。

建议验证命令：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

手动或烟测运行：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

## 风险点

本阶段最容易出错的点：

- dynamic rendering 所需的 Vulkan feature 没有在 device 创建时启用。
- pipeline color format 与 swapchain format 不一致。
- backbuffer 没有转换到 `RenderTarget` 状态就 begin rendering。
- viewport / scissor 没有设置，导致 draw 不可见。
- 顶点格式和 shader location 不匹配。
- vertex buffer 内存没有正确写入或 flush。
- `VkShaderModule`、`VkPipeline`、`VkPipelineLayout` 销毁时机早于 GPU 使用完成。
- resize 后旧 swapchain view 被 pipeline 或 command path 错误引用。

建议实现时先保持固定颜色 clear，再加入三角形。这样可以快速判断问题发生在 swapchain 闭环还是 pipeline / draw path。

## 阶段完成后的状态

Phase 0.4 完成后，ARKRenderer 应具备以下能力：

- 可以通过 RHI 创建最小 vertex buffer。
- 可以通过 RHI 加载 SPIR-V shader。
- 可以通过 RHI 创建空 pipeline layout。
- 可以通过 RHI 创建最小 graphics pipeline。
- 可以通过 dynamic rendering 把 swapchain backbuffer 作为 color attachment。
- 可以提交一次真实 draw call。
- renderer 层初步从“一坨 render 流程”拆成 `Renderer + FrameRenderer + Pass`。

这时项目仍然不是完整渲染器，但已经完成从 clear 到 geometry 的关键跨越。后续所有 mesh、camera、material、texture、lighting 都可以在这条 draw path 上增量扩展。

## Phase 0.5 建议方向

Phase 0.5 建议进入“相机与常量数据”：

- uniform buffer / constant buffer。
- descriptor set layout 和 descriptor set。
- MVP 矩阵。
- 简单 camera。
- 深度 buffer。
- 绘制一个带深度的旋转 cube。

如果 Phase 0.4 中 `FrameRenderer + Pass` 已经整理清楚，Phase 0.5 就可以专注资源绑定和 per-frame 数据，而不用继续调整基础绘制闭环。
