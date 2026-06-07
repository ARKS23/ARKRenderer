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
|       |-- VulkanAllocator.h
|       |-- VulkanAllocator.cpp
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
    |-- triangle.vert.hlsl
    `-- triangle.frag.hlsl
```

说明：

- `TrianglePass` 是阶段验证用 pass，不代表最终渲染架构中的正式 ForwardPass。
- 如果希望命名更贴近最终架构，也可以用 `ForwardPass` 替代 `TrianglePass`，但实现内容仍应保持最小。
- `shaders/` 下保留 HLSL 源文件，构建阶段通过 DXC 输出 SPIR-V 到 build 目录。

## RHI 接口设计

### Buffer

本阶段需要让 RHI 表达最小 buffer 创建和绑定语义：

```cpp
enum class BufferUsage : u32 {
    None = 0,
    Vertex = 1 << 0,
    Index = 1 << 1,
    Uniform = 1 << 2,
    Storage = 1 << 3,
    TransferSrc = 1 << 4,
    TransferDst = 1 << 5,
};

enum class MemoryUsage {
    CpuToGpu,
    GpuOnly,
};

struct BufferDesc {
    std::string debugName;
    u64 size = 0;
    BufferUsage usage = BufferUsage::None;
    MemoryUsage memoryUsage = MemoryUsage::GpuOnly;
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
    std::string debugName;
    ShaderStage stage = ShaderStage::Vertex;
    std::string entryPoint = "main";
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

    virtual const PipelineLayoutDesc& getDesc() const = 0;
};
```

后续 Phase 0.5 或 Phase 0.6 再把 descriptor set layout、push constant、bindless layout 纳入 `PipelineLayoutDesc`。

### PipelineState

`PipelineState` 表达 graphics pipeline 的不可变状态：

```cpp
enum class PrimitiveTopology {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
};

enum class CullMode {
    None,
    Front,
    Back,
};

enum class VertexInputRate {
    PerVertex,
    PerInstance,
};

enum class PolygonMode {
    Fill,
    Line,
};

enum class FrontFace {
    CounterClockwise,
    Clockwise,
};

enum class CompareOp {
    Less,
    Always,
};

struct VertexAttributeDesc {
    u32 location = 0;
    Format format = Format::Unknown;
    u32 offset = 0;
};

struct VertexBufferLayoutDesc {
    u32 binding = 0;
    u32 stride = 0;
    VertexInputRate inputRate = VertexInputRate::PerVertex;
    std::vector<VertexAttributeDesc> attributes;
};

struct RasterStateDesc {
    PolygonMode polygonMode = PolygonMode::Fill;
    CullMode cullMode = CullMode::None;
    FrontFace frontFace = FrontFace::CounterClockwise;
};

struct DepthStencilStateDesc {
    bool enableDepthTest = false;
    bool enableDepthWrite = false;
    CompareOp depthCompareOp = CompareOp::Less;
};

struct ColorBlendAttachmentDesc {
    bool enableBlend = false;
};

struct BlendStateDesc {
    ColorBlendAttachmentDesc colorAttachment;
};

struct GraphicsPipelineDesc {
    std::string debugName;
    Shader* vertexShader = nullptr;
    Shader* fragmentShader = nullptr;
    PipelineLayout* layout = nullptr;
    std::vector<VertexBufferLayoutDesc> vertexBuffers;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    RasterStateDesc rasterState;
    DepthStencilStateDesc depthStencilState;
    BlendStateDesc blendState;
    Format colorFormat = Format::Unknown;
    Format depthFormat = Format::Unknown;
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
    TextureView* depthStencilAttachment = nullptr;
};

struct DrawDesc {
    u32 vertexCount = 0;
    u32 instanceCount = 1;
    u32 firstVertex = 0;
    u32 firstInstance = 0;
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
    virtual void setVertexBuffer(u32 slot, Buffer& buffer, u64 offset = 0) = 0;
    virtual void setIndexBuffer(Buffer& buffer, IndexType indexType = IndexType::UInt32, u64 offset = 0) = 0;
    virtual void draw(const DrawDesc& desc) = 0;
    virtual void drawIndexed(const DrawIndexedDesc& desc) = 0;
};
```

第一版三角形可以只用 `DrawDesc{.vertexCount = 3}`，`setIndexBuffer()` 和 `drawIndexed()` 可以同时补接口但暂不在 sandbox 中使用。

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

第一版在 CMake 中增加 shader 编译目标：

```text
shaders/triangle.vert.hlsl
shaders/triangle.frag.hlsl
    -> build/.../shaders/triangle.vert.spv
    -> build/.../shaders/triangle.frag.spv
```

当前方案：

- 使用 vcpkg 中的 `directx-dxc`，通过 `DIRECTX_DXC_TOOL` 编译 HLSL。
- CMake 输出目录为 `${CMAKE_CURRENT_BINARY_DIR}/shaders`，例如 `build/msvc-vcpkg/shaders`。
- `ark_renderer` 编译时注入 `ARK_SHADER_OUTPUT_DIR`，运行时由 `asset::ShaderLoader` 读取 SPIR-V bytecode，不在 RHI 后端中硬编码项目路径。
- 不手写 SPIR-V 字节数组，避免后续维护成本过高。

运行时 shader 读取路径建议由 `apps/sandbox` 传入或由 `core/FileSystem` 提供资源目录查询，避免在 RHI 后端中硬编码项目路径。

## 本阶段要完成的代码工作

1. 整理 renderer 层一帧结构：
   - 新增 `FrameContext`。
   - 新增 `FrameRenderer`。
   - 新增最小 `RenderPass` 接口。
   - 新增 `ClearPass`，把 Phase 0.3 的 clear 语义从 `Renderer` 中移出。
   - 新增 `TrianglePass` 或最小 `ForwardPass`，负责创建三角形资源并执行 draw。

   当前实现更新：

   - `FrameContext` 已扩展为 renderer 层一帧逻辑上下文，集中传递 `RenderDevice`、`DeviceContext`、`SwapChain`、`FrameResource`、backbuffer view、extent、clear color、scene 和 view。
   - `RenderPass` 已补齐 `setup(RenderDevice&)` 和 `execute(FrameContext&)`，其中 `execute()` 返回 `bool`，便于 pass 失败时中止当前帧。
   - `FrameRenderer` 已落地为 RenderGraph 前的轻量手动调度器，当前固定执行 `ClearPass -> TrianglePass`。
   - `ClearPass` 已接管 Phase 0.3 的清屏命令，`Renderer` 不再直接调用 `clearRenderTarget()`。
   - `TrianglePass` 已建立结构位置，当前暂不发出 draw；RHI / Vulkan draw path 已补齐，后续补齐 shader、pipeline 和 vertex buffer 后在这里提交真实绘制。
   - `Renderer::render()` 现在只保留 acquire、begin、FrameRenderer 调度、end、submit、present 和 swapchain 状态处理，职责边界更接近后续 RenderGraph 替换路径。

2. 补齐 RHI 资源描述：
   - `BufferDesc`
   - `ShaderDesc`
   - `PipelineLayoutDesc`
   - `GraphicsPipelineDesc`
   - vertex input 描述
   - viewport / scissor 描述
   - load / store op 描述

   当前实现更新：

   - `RHICommon.h` 已补充 `ClearColor`、`LoadOp`、`StoreOp`、`Viewport` 和 `ScissorRect`，这些是后续 dynamic rendering 和 draw path 会共用的轻量 RHI 描述。
   - `BufferDesc` 已补齐 `debugName`、`size`、`usage`、`memoryUsage` 和 `initialData`；`BufferUsage` 支持位组合，并提供 `hasBufferUsage()` 辅助检查。
   - `ShaderDesc` 已补齐 `debugName`、`stage`、`entryPoint` 和 SPIR-V `bytecode`。
   - `PipelineLayoutDesc` 已补齐 `debugName`，descriptor set layout / push constant 后续阶段再扩展。
   - `GraphicsPipelineDesc` 已补齐 shader、pipeline layout、vertex input、topology、raster state、depth stencil state、blend state、color format 和 depth format。
   - `DeviceContext.h` 中的 `RenderingDesc` 已补齐 extent、color attachment、load/store op 和 clear color 描述；真实 `beginRendering()` 已在第 3 项落地。
   - `framework_headers_smoke.cpp` 已覆盖新增描述结构，确保后续修改 RHI 描述时能被编译检查捕获。

3. 补齐 RHI 命令接口：
   - `beginRendering()`
   - `endRendering()`
   - `setViewport()`
   - `setScissorRect()`
   - `setPipeline()`
   - `setVertexBuffer()`
   - `draw()`
   - 预留 `setIndexBuffer()` / `drawIndexed()`

   当前实现更新：

   - `DeviceContext` 已补齐 `beginRendering()`、`endRendering()`、`setViewport()`、`setScissorRect()`、`setPipeline()`、`setVertexBuffer()`、`setIndexBuffer()`、`draw()` 和 `drawIndexed()`。
   - `beginRendering()` 改为返回 `bool`，让 attachment 无效、命令缓冲未开始等错误能反馈给 renderer/pass 调度层。
   - `DrawDesc` 已加入 RHI，非索引绘制不再用散落参数表达；`DrawIndexedDesc` 继续保留给后续 mesh/index buffer。
   - `VulkanCommandContext` 已实现 dynamic rendering begin/end、动态 viewport/scissor、graphics pipeline 绑定、vertex/index buffer 绑定和 draw/drawIndexed 命令。
   - `FrameRenderer` 主路径已从 `vkCmdClearColorImage` 过渡到 dynamic rendering：backbuffer 先 transition 到 `RenderTarget`，再通过 `loadOp=Clear` 完成清屏，最后 transition 回 `Present`。
   - `ClearPass` 当前保留语义位置，真实清屏由 `FrameRenderer::beginRendering(loadOp=Clear)` 表达，方便后续迁移到 RenderGraph。

4. 实现 Vulkan 资源对象：
   - `VulkanBuffer`
   - `VulkanShader`
   - `VulkanPipelineLayout`
   - `VulkanPipelineState`

   当前实现更新：

   - 新增 `VulkanAllocator`，由 `VulkanDevice` 持有 VMA allocator，并通过 `vmaImportVulkanFunctionsFromVolk()` 复用项目的 volk 加载策略。
   - `VulkanBuffer` 已实现 `VkBuffer + VMA allocation`，支持 RHI `BufferUsage` 到 Vulkan usage 的映射；`MemoryUsage::CpuToGpu` 支持 `initialData` 直接初始化。
   - `VulkanShader` 已实现 `VkShaderModule` RAII，保存 `ShaderDesc`、shader stage 和 entry point。
   - `VulkanPipelineLayout` 已实现空 `VkPipelineLayout`，descriptor set layout / push constant 后续阶段再扩展。
   - `VulkanPipelineState` 已实现最小 graphics pipeline，使用 dynamic rendering 的 `VkPipelineRenderingCreateInfo`，viewport/scissor 作为 dynamic state。
   - `VulkanDevice::createBuffer()`、`createShader()`、`createPipelineLayout()` 和 `createGraphicsPipeline()` 已接入真实 Vulkan 对象创建。
   - `Format` 已补充 `R32G32Float`、`R32G32B32Float`、`R32G32B32A32Float`，用于后续 position/color 顶点输入。
   - 仍未纳入本阶段的 `Texture`、`Sampler`、`DescriptorSet`、`Fence` 工厂继续保持明确的未实现报错。

5. 实现 Vulkan command draw path：
   - dynamic rendering begin / end。
   - pipeline bind。
   - vertex buffer bind。
   - viewport / scissor。
   - draw call。

   当前实现更新：

   - `FrameRenderer` 已在 `beginRendering()` 后设置默认 viewport 和 scissor，尺寸来自当前 `FrameContext::extent`，resize 后不会沿用旧动态状态。
   - `VulkanCommandContext` 的 viewport/scissor、pipeline bind、vertex/index buffer bind、draw/drawIndexed 都要求处于 active rendering 内，调用顺序错误会输出明确英文日志。
   - `beginRendering()` 增加 extent 有效性检查，避免 0 尺寸 render area 进入 Vulkan 命令录制。
   - `draw()` / `drawIndexed()` 增加非零 vertex/index count 与 instance count 检查，避免无意义 draw command 混入命令流。
   - `TrianglePass` 已在第 6、7 项中接入 shader / pipeline / vertex buffer，并通过 `draw()` 提交三角形绘制。

6. 新增 shader：
   - `triangle.vert.hlsl`
   - `triangle.frag.hlsl`
   - CMake shader 编译或复制输出规则。

   当前实现更新：

   - 新增 `shaders/triangle.vert.hlsl` 和 `shaders/triangle.frag.hlsl`。
   - CMake 新增 `ark_shaders` target，使用 vcpkg 的 `directx-dxc` 编译 HLSL 到 SPIR-V。
   - shader 输出到 `build/.../shaders/triangle.vert.spv` 和 `build/.../shaders/triangle.frag.spv`。
   - `ark_renderer` 注入 `ARK_SHADER_OUTPUT_DIR`，运行时由 `asset::ShaderLoader` 从该目录读取 SPIR-V。

7. 接入 sandbox：
   - 创建一个固定三角形。
   - 顶点包含 position 和 color。
   - 不使用 uniform buffer。
   - 不使用 descriptor。
   - 窗口 resize 后 pipeline 和 backbuffer format 仍然保持正确。

   当前实现更新：

   - `TrianglePass::setup()` 已创建 CPU 可见 vertex buffer、vertex shader、fragment shader 和空 pipeline layout。
   - `TrianglePass::execute()` 会根据当前 swapchain color format 懒创建 graphics pipeline，并在颜色格式变化时重建。
   - 三角形顶点格式为 `float2 position + float3 color`，对应 shader location 0 和 1。
   - 当前三角形不使用 uniform buffer、descriptor、index buffer 或 depth buffer。
   - `TrianglePass` 在 `FrameRenderer` 打开的 dynamic rendering 作用域内绑定 pipeline / vertex buffer 并调用 `draw(3)`。

8. 更新测试和文档：
   - 更新 `framework_headers_smoke.cpp` 覆盖新增 RHI 头文件。
   - 保留 dependency smoke。
   - 文档记录最终实现、限制和下一阶段计划。

   当前实现更新：

   - `framework_headers_smoke.cpp` 已覆盖新增 RHI 描述和 draw 描述结构。
   - 新增 `shader_assets_smoke.cpp`，检查 CMake 输出的三角形 SPIR-V 文件存在且按 `u32` word 对齐。
   - dependency smoke 保持通过，用于确认 DXC、SPIRV-Reflect、Vulkan 等第三方依赖仍可用。
   - 本文档已记录 shader 构建方式、TrianglePass 接入方式、当前限制和后续 Phase 0.5 方向。

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

## 代码阅读指南

建议按“应用层 -> renderer 调度 -> pass 资源 -> RHI 接口 -> Vulkan 后端”的顺序阅读，这样比较容易看清每一层的职责边界。

### 1. 构建与 shader 资源

先读 `CMakeLists.txt` 中的 `ark_compile_hlsl_shader()` 和 `ark_shaders` target。

重点看三件事：

- HLSL 源文件位于 `shaders/triangle.vert.hlsl` 和 `shaders/triangle.frag.hlsl`。
- DXC 在构建阶段输出 `triangle.vert.spv` 和 `triangle.frag.spv`。
- `ARK_SHADER_OUTPUT_DIR` 被注入到 `ark_renderer`，运行时 `TrianglePass` 通过 `asset::ShaderLoader` 读取 SPIR-V。

### 2. 一帧外壳

从 `src/renderer/Renderer.cpp` 的 `DefaultRenderer::render()` 开始读。

这一层只负责：

- `SwapChain::acquireNextImage()`。
- `DeviceContext::begin()` / `end()` / `submit()`。
- 调用 `FrameRenderer::render()`。
- `SwapChain::present()`。
- resize / out-of-date / suboptimal 状态处理。

这里不应该出现 Vulkan 类型，也不应该创建具体 GPU 资源。

### 3. 一帧内部调度

然后读 `src/renderer/FrameRenderer.cpp`。

这层负责把 backbuffer 放进一次 render scope：

```text
backbuffer -> RenderTarget
beginRendering(loadOp=Clear)
setViewport / setScissorRect
ClearPass
TrianglePass
endRendering
backbuffer -> Present
```

`FrameRenderer` 当前是 RenderGraph 前的手动调度器。后续 RenderGraph 落地后，最可能替换的就是这里的 pass 顺序和 barrier 逻辑。

### 4. 三角形 pass

重点读 `src/renderer/passes/TrianglePass.cpp`。

`setup()` 负责创建长期资源：

- CPU 可见 vertex buffer。
- vertex shader / fragment shader。
- 空 pipeline layout。

`execute()` 负责每帧绘制：

- 确保 pipeline 存在。
- 绑定 pipeline。
- 绑定 vertex buffer。
- 调用 `draw(3)`。

`ensurePipeline()` 是本阶段最值得仔细看的函数。graphics pipeline 创建时需要固定 swapchain color format，所以这里按当前 swapchain format 懒创建 pipeline，并在 format 变化时重建。

### 5. RHI 接口

接着读这些公共接口：

- `src/rhi/Buffer.h`
- `src/rhi/Shader.h`
- `src/rhi/PipelineLayout.h`
- `src/rhi/PipelineState.h`
- `src/rhi/DeviceContext.h`
- `src/rhi/RHICommon.h`

阅读重点：

- `RenderDevice` 创建资源。
- `DeviceContext` 使用资源并录制命令。
- `SwapChain` 管理 backbuffer acquire / present / resize。
- RHI 描述只表达引擎语义，不暴露 Vulkan 类型。

### 6. Vulkan 资源对象

再读 Vulkan 资源实现：

- `src/rhi/vulkan/VulkanBuffer.cpp`
- `src/rhi/vulkan/VulkanShader.cpp`
- `src/rhi/vulkan/VulkanPipelineLayout.cpp`
- `src/rhi/vulkan/VulkanPipelineState.cpp`
- `src/rhi/vulkan/VulkanAllocator.cpp`
- `src/rhi/vulkan/VulkanDevice.cpp`

阅读重点：

- `VulkanDevice` 是资源工厂。
- `VulkanAllocator` 持有 VMA allocator。
- `VulkanBuffer` 负责 `VkBuffer + VMA allocation`。
- `VulkanShader` 负责 `VkShaderModule`。
- `VulkanPipelineState` 把 `GraphicsPipelineDesc` 翻译成 Vulkan graphics pipeline，并使用 dynamic rendering。

### 7. Vulkan 命令路径

最后读 `src/rhi/vulkan/VulkanCommandContext.cpp`。

建议按这个顺序看：

1. `beginFrame()` / `begin()` / `end()` / `submit()`：每帧 command buffer 和同步。
2. `beginRendering()` / `endRendering()`：RHI render scope 到 Vulkan dynamic rendering 的映射。
3. `setViewport()` / `setScissorRect()`：动态状态。
4. `setPipeline()` / `setVertexBuffer()` / `draw()`：最小三角形 draw path。
5. `pipelineBarrier()`：backbuffer 状态转换。

这里的 `requireActiveCommandBuffer()` 和 `requireActiveRendering()` 是调用顺序保护。后续如果某个 pass 在 `beginRendering()` 外调用 draw，会先在这里报错。

### 8. 审核检查点

审核这版代码时，可以重点看这些问题：

- renderer/app 层是否仍然没有包含 Vulkan 头文件。
- `TrianglePass` 是否只通过 RHI 接口创建和使用资源。
- shader location 是否和 `VertexBufferLayoutDesc` 对齐。
- pipeline color format 是否来自当前 swapchain。
- resize 后是否会重新使用正确的 viewport / scissor。
- Vulkan 对象是否用 RAII 管理，是否没有裸露的手动销毁遗漏。
- 日志字符串是否保持英文，中文是否只出现在注释和文档中。
