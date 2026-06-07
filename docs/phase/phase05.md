# Phase 0.5：Descriptor、Camera Uniform、Depth Buffer 与旋转 Cube

Phase 0.4 已经完成最小三角形绘制闭环：可以创建 `Buffer`、`Shader`、`PipelineLayout`、`PipelineState`，并通过 `DeviceContext` 绑定 pipeline / vertex buffer 后提交 `draw()`。Phase 0.4.1 又把顶点输入布局整理为独立的 `VertexInputLayoutDesc`。

Phase 0.5 的目标是从“固定 NDC 三角形”推进到“带相机矩阵、资源绑定、深度测试的旋转 cube”。这一步不是为了做复杂场景，而是为了验证现代渲染器最基础的资源绑定链路：

```text
CPU frame data
    -> Uniform Buffer
    -> DescriptorSet
    -> PipelineLayout
    -> Shader
    -> DrawIndexed cube
    -> Depth test
```

完成后，sandbox 窗口中应该能看到一个稳定旋转的彩色 cube。它应该使用 MVP 矩阵、index buffer、depth buffer 和 descriptor set，而不是继续依赖写死的 NDC 坐标。

## 阶段目标

本阶段建议完成以下能力：

- RHI 层补齐最小 descriptor 描述结构。
- Vulkan 后端实现 `VkDescriptorSetLayout`、`VkDescriptorPool`、`VkDescriptorSet` 的最小闭环。
- `PipelineLayoutDesc` 支持 descriptor set layout。
- `DeviceContext::bindDescriptorSet()` 真正绑定 Vulkan descriptor set。
- 支持 uniform buffer descriptor，用于传递 per-frame MVP 矩阵。
- 实现 owned `VulkanTexture`，让 swapchain 能创建默认 depth buffer。
- `FrameRenderer` 的 dynamic rendering 接入 depth attachment。
- `GraphicsPipelineDesc.depthStencilState` 开启 depth test / depth write。
- 新增或演进一个 `CubePass`，使用 vertex buffer、index buffer、uniform buffer 和 descriptor set 绘制旋转 cube。

## 不做的内容

为了保持阶段边界清晰，Phase 0.5 暂不做：

- 不做 texture sampling。
- 不做 `TextureLoader`。
- 不做 `Sampler` 完整功能。
- 不做 bindless。
- 不做完整 `ResourceManager` handle 系统。
- 不做 GLTF 加载。
- 不做材质系统。
- 不做 RenderGraph 自动 barrier。
- 不做 shader hot reload。
- 不做多线程 command recording。

这些内容建议留到后续 Phase 0.6 / Phase 0.7。Phase 0.5 只关注 uniform buffer、descriptor、depth 和 indexed draw 的最小闭环。

## 推荐目录变化

```text
src/
|-- renderer/
|   |-- passes/
|   |   |-- CubePass.h
|   |   `-- CubePass.cpp
|   |
|   `-- FrameRenderer.cpp
|
|-- rhi/
|   |-- DescriptorSetLayout.h
|   |-- DescriptorSet.h
|   |-- PipelineLayout.h
|   |-- Texture.h
|   |-- TextureView.h
|   `-- DeviceContext.h
|
|-- rhi/vulkan/
|   |-- VulkanDescriptorSetLayout.h
|   |-- VulkanDescriptorSetLayout.cpp
|   |-- VulkanDescriptorSet.h
|   |-- VulkanDescriptorSet.cpp
|   |-- VulkanDescriptorManager.h
|   |-- VulkanDescriptorManager.cpp
|   |-- VulkanPipelineLayout.cpp
|   |-- VulkanTexture.cpp
|   |-- VulkanTextureView.cpp
|   |-- VulkanSwapChain.cpp
|   `-- VulkanCommandContext.cpp
|
`-- shaders/
    |-- cube.vert.hlsl
    `-- cube.frag.hlsl
```

说明：

- `CubePass` 可以是阶段验证用 pass，不代表最终正式 `ForwardPass`。
- 如果希望保留 `TrianglePass` 作为最小 draw 示例，可以在 `FrameRenderer` 中暂时切换为 `CubePass`，而不是删除 `TrianglePass`。
- `Sampler` 可以继续保持骨架，Phase 0.5 不需要 sampled image。

## RHI Descriptor 设计

第一版 descriptor 只需要支持 uniform buffer。不要一开始就把 sampled image、storage image、dynamic uniform buffer、bindless 全塞进去。

推荐公共描述：

```cpp
enum class DescriptorType {
    UniformBuffer,
};

enum class ShaderStageFlags : u32 {
    None = 0,
    Vertex = 1 << 0,
    Fragment = 1 << 1,
    Compute = 1 << 2,
};

struct DescriptorBindingDesc {
    u32 binding = 0;
    DescriptorType type = DescriptorType::UniformBuffer;
    u32 count = 1;
    ShaderStageFlags stages = ShaderStageFlags::None;
};

struct DescriptorSetLayoutDesc {
    std::string debugName;
    std::vector<DescriptorBindingDesc> bindings;
};
```

`DescriptorSetLayout` 应该保留 desc 查询：

```cpp
class DescriptorSetLayout {
public:
    virtual ~DescriptorSetLayout() = default;

    virtual const DescriptorSetLayoutDesc& getDesc() const = 0;
};
```

`PipelineLayoutDesc` 需要引用 descriptor set layout：

```cpp
struct PipelineLayoutDesc {
    std::string debugName;
    std::vector<DescriptorSetLayout*> descriptorSetLayouts;
};
```

注意：`PipelineLayout` 不拥有 `DescriptorSetLayout`。创建 Vulkan pipeline layout 时会读取 layout handle，但公共层语义上 descriptor set layout 的生命周期必须长于使用它创建的 pipeline layout 和 pipeline。

## DescriptorSet 更新接口

第一版可以让 `DescriptorSet` 自己提供更新 uniform buffer 的接口：

```cpp
struct BufferDescriptor {
    Buffer* buffer = nullptr;
    u64 offset = 0;
    u64 range = 0;
};

class DescriptorSet {
public:
    virtual ~DescriptorSet() = default;

    virtual void updateUniformBuffer(u32 binding, const BufferDescriptor& buffer) = 0;
};
```

这个设计简单，适合 Phase 0.5。后续如果 descriptor 类型变多，可以再升级为：

```cpp
struct WriteDescriptorSetDesc {
    u32 binding = 0;
    DescriptorType type = DescriptorType::UniformBuffer;
    BufferDescriptor buffer;
    TextureDescriptor texture;
};
```

第一版不建议过度设计。现在只需要把 uniform buffer 绑定给 vertex shader。

## Vulkan Descriptor 实现策略

### VulkanDescriptorSetLayout

职责：

- 持有 `VkDescriptorSetLayout`。
- 保存 `DescriptorSetLayoutDesc`。
- 析构时销毁 `VkDescriptorSetLayout`。

映射关系：

```text
DescriptorType::UniformBuffer -> VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
ShaderStageFlags::Vertex      -> VK_SHADER_STAGE_VERTEX_BIT
ShaderStageFlags::Fragment    -> VK_SHADER_STAGE_FRAGMENT_BIT
```

### VulkanDescriptorManager

第一版可以做一个非常简单的 descriptor pool 管理器，由 `VulkanDevice` 持有：

```text
VulkanDevice
    owns VulkanDescriptorManager
        owns VkDescriptorPool
```

第一版 pool 可以固定容量，例如：

```text
uniform buffer descriptors: 256
max sets: 256
```

这不是最终方案，但足够 Phase 0.5。后续再做 pool 增长、多 pool、frame-local descriptor 或 bindless。

### VulkanDescriptorSet

职责：

- 持有 `VkDescriptorSet`。
- 借用 `VkDevice`。
- 借用或记录对应的 `VulkanDescriptorSetLayout`。
- `updateUniformBuffer()` 调用 `vkUpdateDescriptorSets()`。

注意：`VkDescriptorSet` 从 `VkDescriptorPool` 分配，第一版可以由 pool 统一销毁，不必单独 free。`VulkanDescriptorSet` 不应该销毁 descriptor pool。

### VulkanPipelineLayout

当前 `VulkanPipelineLayout` 创建的是空 layout。Phase 0.5 需要改为：

```text
PipelineLayoutDesc.descriptorSetLayouts
    -> VkDescriptorSetLayout[]
    -> vkCreatePipelineLayout()
```

`VulkanPipelineLayout` 仍然只拥有 `VkPipelineLayout`，不拥有 descriptor set layout。

## Uniform Buffer 与 Frame 数据

Phase 0.5 建议先使用一个简单的 per-frame uniform buffer：

```cpp
struct CameraUniform {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};
```

或者直接使用：

```cpp
struct CameraUniform {
    glm::mat4 mvp;
};
```

第一版为了少踩坑，建议使用 `model/view/projection` 三个矩阵，shader 中显式相乘，便于调试。

资源所有权建议：

```text
CubePass
    owns vertex buffer
    owns index buffer
    owns uniform buffer
    owns descriptor set layout
    owns descriptor set
    owns pipeline layout
    owns pipeline state
```

第一版 uniform buffer 可以使用 `MemoryUsage::CpuToGpu`，每帧直接更新。当前 `Buffer` 还没有公共 map/update 接口，如果不想暴露 map，可以在 `DeviceContext` 增加：

```cpp
virtual bool updateBuffer(Buffer& buffer, const void* data, u64 size, u64 offset = 0) = 0;
```

但从职责看，`DeviceContext` 负责 map/update 是合理的，因为它是“使用资源”的对象。后续如果做 upload system，再把细节收进专门的 uploader。

## Depth Buffer 方案

`SwapChain` 已经有：

```cpp
virtual TextureView* getDepthBufferView() = 0;
```

但当前 Vulkan 后端还没有真正创建默认 depth image。Phase 0.5 需要补齐：

- `VulkanTexture` 支持 owned image。
- owned image 使用 VMA allocation，而不是裸 `vkCreateImage()`。
- `VulkanSwapChain` 在创建 / resize 时创建默认 depth texture 和 depth view。
- `FrameRenderer` 在 `RenderingDesc` 中传入 `depthStencilAttachment`。
- `VulkanCommandContext::beginRendering()` 将 depth view 转换为 `VkRenderingAttachmentInfo`。

推荐默认 depth 格式：

```text
Format::D32Float
```

`TextureUsage::DepthStencil` 应映射到：

```text
VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
```

depth image 初始状态建议为 `ResourceState::Undefined`，第一帧使用前通过 barrier 转到 `ResourceState::DepthStencilWrite` 或等价状态。

当前 `ResourceState` 已经包含 depth/stencil 相关枚举：

```cpp
DepthStencilWrite,
DepthStencilRead,
```

并在 Vulkan barrier 映射到：

```text
DepthStencilWrite:
    layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    stage  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
    access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
```

## RenderingDesc 扩展

当前 `RenderingDesc` 已经有：

```cpp
TextureView* depthStencilAttachment = nullptr;
```

Phase 0.5 建议把它扩展成完整 attachment 描述，避免 depth 无法表达 clear/load/store：

```cpp
struct DepthStencilAttachmentDesc {
    TextureView* view = nullptr;
    LoadOp loadOp = LoadOp::Clear;
    StoreOp storeOp = StoreOp::Store;
    float clearDepth = 1.0f;
    u32 clearStencil = 0;
};

struct RenderingDesc {
    Extent2D extent;
    RenderingAttachmentDesc colorAttachment;
    DepthStencilAttachmentDesc depthStencilAttachment;
};
```

如果想保持本阶段改动更小，也可以先沿用 `TextureView* depthStencilAttachment`，并在 VulkanCommandContext 中固定 clear depth = 1.0。  
但从长期清晰度看，建议直接补成 desc。

## CubePass 设计

`CubePass` 第一版建议非常直接：

```text
setup(RenderDevice&)
    create vertex buffer
    create index buffer
    create uniform buffer
    create descriptor set layout
    create descriptor set
    update descriptor set -> uniform buffer
    create pipeline layout
    load cube shaders

execute(FrameContext&)
    update uniform buffer
    ensure pipeline
    setPipeline
    bindDescriptorSet(0)
    setVertexBuffer(0)
    setIndexBuffer
    drawIndexed(36)
```

cube vertex 可以先定义为：

```cpp
struct CubeVertex {
    float position[3];
    float color[3];
};
```

vertex input：

```text
location 0: position, R32G32B32Float
location 1: color,    R32G32B32Float
```

index buffer 使用 `u16` 即可，也可以先用 `u32` 减少格式分支。

## Shader 方案

新增：

```text
shaders/cube.vert.hlsl
shaders/cube.frag.hlsl
```

vertex shader 输入：

```hlsl
struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 color : COLOR0;
};
```

uniform 绑定建议：

```hlsl
struct CameraUniform {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> g_Camera;
```

注意 HLSL 矩阵和 GLM 矩阵的行列主序问题。第一版可以通过测试画面验证，如果 cube 变形或不可见，优先检查矩阵转置、投影矩阵 Y 翻转和 near/far 设置。

Vulkan NDC 与 GLM 透视矩阵需要注意：

```cpp
projection[1][1] *= -1.0f;
```

这是 Vulkan clip space 与 OpenGL 习惯差异导致的常见处理。

## FrameRenderer 调整

当前 `FrameRenderer` 大致流程：

```text
backbuffer -> RenderTarget
beginRendering(color)
setViewport / setScissor
ClearPass
TrianglePass
endRendering
backbuffer -> Present
```

Phase 0.5 需要调整为：

```text
backbuffer -> RenderTarget
depth -> DepthStencilWrite
beginRendering(color + depth)
setViewport / setScissor
ClearPass
CubePass
endRendering
backbuffer -> Present
```

注意：

- color clear 可以继续通过 color attachment `loadOp=Clear`。
- depth clear 应通过 depth attachment `loadOp=Clear`，clearDepth = 1.0。
- `ClearPass` 仍然可以暂时保留为空语义 pass，真实 clear 由 `beginRendering()` 表达。

## 实施顺序

建议把 Phase 0.5 拆成多个可验证的小阶段推进。每个小阶段完成后都应该能构建通过，尽量不要把 descriptor、uniform、depth 和 cube 一次性混在一起调。

### 0.5.0 文档与边界确认

目标：

- 确认 Phase 0.5 只做 uniform buffer、descriptor、depth 和 indexed cube。
- 明确不做 texture sampling、GLTF、材质系统和 RenderGraph。
- 明确 `renderer/` 继续只通过 RHI 使用资源，不包含 Vulkan 头文件。

验收：

- `docs/phase/phase05.md` 记录阶段目标、实施顺序和风险点。
- 文档与 `framework.md`、`module_responsibility.md`、`file_system_and_shader_loading.md` 的依赖边界一致。

### 0.5.1 RHI 描述结构补齐

先只改公共 RHI 描述，不写 Vulkan 复杂逻辑。

工作内容：

- 补齐 `DescriptorType`、`ShaderStageFlags`、`DescriptorBindingDesc`、`DescriptorSetLayoutDesc`。
- 补齐 `BufferDescriptor` 和 `DescriptorSet::updateUniformBuffer()`。
- 扩展 `PipelineLayoutDesc`，支持 `std::vector<DescriptorSetLayout*> descriptorSetLayouts`。
- 扩展 `RenderingDesc`，把 depth attachment 从裸 `TextureView*` 演进为 `DepthStencilAttachmentDesc`。
- 确认沿用现有 `ResourceState::DepthStencilWrite` 和 `ResourceState::DepthStencilRead`。
- 根据需要补充 `DeviceContext::updateBuffer()`，用于 CPU 每帧更新 uniform buffer。
- 更新 `framework_headers_smoke.cpp` 覆盖新增 RHI 描述。

验收：

- 公共 RHI 层不暴露 Vulkan 类型。
- smoke test 能编译通过。
- 还不要求 sandbox 画 cube。

当前实现更新：

- 已补齐 `DescriptorType`、`ShaderStageFlags`、`DescriptorBindingDesc` 和 `DescriptorSetLayoutDesc`。
- `DescriptorSetLayout` 已增加 `getDesc()` 查询接口。
- 已补齐 `BufferDescriptor` 和 `DescriptorSet::updateUniformBuffer()`。
- `PipelineLayoutDesc` 已支持 descriptor set layout 引用列表。
- `RenderingDesc` 已将 depth attachment 整理为 `DepthStencilAttachmentDesc`。
- `DeviceContext::updateBuffer()` 已在 0.5.4 落地，用于 CPU 可见 buffer 的直接更新。
- `ResourceState` 沿用既有 `DepthStencilWrite` / `DepthStencilRead`，不新增重复命名。
- `framework_headers_smoke.cpp` 已覆盖新增 RHI 描述。

### 0.5.2 Vulkan Descriptor 最小闭环

只实现 uniform buffer descriptor，不处理 sampled image。

工作内容：

- 实现 `VulkanDescriptorSetLayout`。
- 实现 `VulkanDescriptorManager`，第一版持有一个固定容量 `VkDescriptorPool`。
- 实现 `VulkanDescriptorSet` 分配。
- 实现 `VulkanDescriptorSet::updateUniformBuffer()`。
- 接入 `VulkanDevice::createDescriptorSetLayout()` 和 `VulkanDevice::createDescriptorSet()`。

所有权约定：

- `VulkanDevice` 拥有 `VulkanDescriptorManager`。
- `VulkanDescriptorManager` 拥有 `VkDescriptorPool`。
- `VulkanDescriptorSetLayout` 拥有 `VkDescriptorSetLayout`。
- `VulkanDescriptorSet` 借用 descriptor pool 分配出来的 `VkDescriptorSet`，第一版不单独 free，随 pool 销毁。

验收：

- 能创建 descriptor set layout 和 descriptor set。
- 能把 uniform buffer 写入 descriptor set。
- descriptor pool 生命周期集中在 `VulkanDescriptorManager`。

当前实现更新：

- 已实现 `VulkanDescriptorSetLayout`，负责创建、持有和销毁 `VkDescriptorSetLayout`。
- 已实现 `VulkanDescriptorManager`，第一版创建固定容量 descriptor pool，支持分配 uniform buffer descriptor set。
- 已实现 `VulkanDescriptorSet`，支持 `updateUniformBuffer()` 写入 `VkDescriptorSet`。
- `VulkanDevice::createDescriptorSetLayout()` 和 `VulkanDevice::createDescriptorSet()` 已接入真实 Vulkan 对象创建。
- `VkDescriptorPool` 由 `VulkanDescriptorManager` 统一拥有；`VulkanDescriptorSet` 不单独 free descriptor set，pool 销毁时统一释放。
- `PipelineLayoutDesc.descriptorSetLayouts` 到 `VkPipelineLayout` 的接入，以及命令录制阶段的 `bindDescriptorSet()` 已在 0.5.3 完成。

### 0.5.3 PipelineLayout 与 bindDescriptorSet

在 descriptor 对象能创建之后，再接入 pipeline layout 和命令绑定。

工作内容：

- 修改 `VulkanPipelineLayout`，从 `PipelineLayoutDesc.descriptorSetLayouts` 收集 `VkDescriptorSetLayout`。
- `VulkanPipelineLayout` 仍只拥有 `VkPipelineLayout`，不拥有 descriptor set layout。
- 实现 `VulkanCommandContext::bindDescriptorSet()`。
- 记录或校验当前绑定的 `VulkanPipelineState` / `VkPipelineLayout`，确保绑定 descriptor set 时使用正确 layout。

验收：

- `PipelineLayoutDesc` 能创建带 set layout 的 Vulkan pipeline layout。
- `bindDescriptorSet(0, descriptorSet)` 会调用 `vkCmdBindDescriptorSets()`。
- renderer/pass 仍然不接触 Vulkan 类型。

当前实现更新：

- `VulkanPipelineLayout` 已经从 `PipelineLayoutDesc.descriptorSetLayouts` 收集 `VkDescriptorSetLayout`，创建带 descriptor set layout 的 `VkPipelineLayout`。
- `VulkanPipelineLayout` 仍然只拥有 `VkPipelineLayout`，不拥有 `DescriptorSetLayout`；公共层继续约定 descriptor set layout 生命周期长于 pipeline layout 和 pipeline。
- `VulkanCommandContext::setPipeline()` 会记录当前图形 pipeline 的 `VkPipelineLayout`，供后续 descriptor set 绑定使用。
- `VulkanCommandContext::bindDescriptorSet()` 已实现，会在 active command buffer 和 active rendering 内调用 `vkCmdBindDescriptorSets()`。
- renderer/pass 层仍然只通过 `DeviceContext::bindDescriptorSet()` 使用 descriptor set，不包含 Vulkan 头文件。

### 0.5.4 Uniform Buffer 更新路径

在 cube pass 前先把 CPU 到 GPU 的常量数据更新路径打通。

工作内容：

- 为 `DeviceContext::updateBuffer()` 或等价接口实现 Vulkan 后端。
- 第一版只要求支持 `MemoryUsage::CpuToGpu` buffer。
- 更新时校验 size / offset 不越界。
- 必要时处理 VMA mapped memory / flush。

验收：

- 可以创建 `CpuToGpu + Uniform` buffer。
- 每帧能写入 `CameraUniform`。
- 日志字符串使用英文，注释说明非显然内存同步规则。

当前实现更新：

- `DeviceContext` 已新增 `updateBuffer(Buffer&, const void*, u64, u64)`，作为 CPU 写入 buffer 的公共 RHI 入口。
- `VulkanCommandContext::updateBuffer()` 已接入，会校验目标对象是 `VulkanBuffer` 后转交给 buffer 自身处理。
- `VulkanBuffer::updateData()` 已支持 `MemoryUsage::CpuToGpu` buffer 的 map / copy / flush 路径，并检查空数据、有效 allocation 和 offset / size 边界。
- 当前不支持通过该接口更新 `GpuOnly` buffer；GPU-only 资源上传仍留给后续 upload system。
- 同步边界由调用方负责：更新 per-frame uniform 时应避免覆盖 GPU 仍在读取的 in-flight 数据。

### 0.5.5 Cube Shader 与 CMake 接入

资源绑定通路具备后，再新增 shader。

工作内容：

- 新增 `shaders/cube.vert.hlsl`。
- 新增 `shaders/cube.frag.hlsl`。
- CMake 新增 cube shader 编译规则。
- shader 通过 `[[vk::binding(0, 0)]]` 读取 `CameraUniform`。
- `CubePass` 后续通过 `asset::ShaderLoader` 读取 `.spv`，不在 pass 内直接做文件 IO。

验收：

- `ark_shaders` 能输出 `cube.vert.spv` 和 `cube.frag.spv`。
- `shader_assets_smoke` 覆盖 cube shader 文件存在和 magic number。

当前实现更新：

- 已新增 `shaders/cube.vert.hlsl` 和 `shaders/cube.frag.hlsl`。
- `cube.vert.hlsl` 定义 `CameraUniform`，使用 descriptor set 0 / binding 0 读取 model、view、projection 矩阵。
- CMake 已把 cube vertex / fragment shader 加入 `ark_shaders` 编译目标，输出 `cube.vert.spv` 和 `cube.frag.spv`。
- `shader_assets_smoke` 已覆盖 triangle 和 cube 四个 SPIR-V 产物的加载与 magic number 校验。
- 本小阶段只完成 shader 资产闭环，实际创建 cube 资源和调用 `drawIndexed(36)` 留到 0.5.6。

### 0.5.6 CubePass：先不接 depth

先验证 descriptor + uniform + indexed draw，暂时不开 depth。这样可以先排除 descriptor 和矩阵问题。

工作内容：

- 新增 `CubePass`。
- 创建 cube vertex buffer。
- 创建 cube index buffer。
- 创建 camera uniform buffer。
- 创建 descriptor set layout / descriptor set。
- 创建带 descriptor set layout 的 pipeline layout。
- 每帧更新 `CameraUniform`。
- 绑定 pipeline、descriptor set、vertex buffer、index buffer。
- 调用 `drawIndexed(36)`。
- `FrameRenderer` 暂时用 `CubePass` 替代或并列 `TrianglePass`。

验收：

- sandbox 能看到 cube 或至少能看到随时间变化的几何体。
- `drawIndexed()` 路径被真实使用。
- descriptor / uniform 绑定没有 validation error。

当前实现更新：

- 已新增 `CubePass`，它拥有 cube vertex buffer、index buffer、per-frame uniform buffer、descriptor set layout、descriptor set、pipeline layout 和 graphics pipeline。
- `CubePass` 使用两个 frame slot 的 uniform buffer / descriptor set，避免一帧更新常量数据时覆盖 GPU 仍在读取的上一帧数据。
- `CubePass` 每帧通过 `DeviceContext::updateBuffer()` 写入 model / view / projection 矩阵，并通过 descriptor set 0 / binding 0 绑定给 vertex shader。
- `CubePass` 使用 `setVertexBuffer()`、`setIndexBuffer()` 和 `drawIndexed(36)` 走 indexed draw 路径。
- 默认 `FrameRenderer` 已从 `TrianglePass` 切换到 `CubePass`；`TrianglePass` 继续保留为 Phase 0.4 的最小三角形示例。
- 本小阶段暂不接入 depth attachment，因此 cube 面遮挡仍不作为验收标准。

### 0.5.7 Owned Texture 与 SwapChain Depth

cube 基础绘制稳定后，再接 depth 资源。

工作内容：

- 让 `VulkanTexture` 支持 owned image。
- owned image 使用 VMA allocation。
- `TextureUsage::DepthStencil` 映射到 depth attachment usage。
- `VulkanTextureView` 正确处理 depth aspect。
- `VulkanSwapChain` 在 create / resize 时创建默认 depth texture 和 depth view。
- swapchain image 继续保持 borrowed texture，不能由 wrapper 销毁。

所有权约定：

- swapchain color images 由 `VkSwapchainKHR` 拥有，`VulkanTexture` 只借用。
- default depth image 由 `VulkanSwapChain` 语义拥有，resize 时随 swapchain 一起重建。
- owned texture 的 Vulkan image 和 VMA allocation 由 `VulkanTexture` 释放。

验收：

- `SwapChain::getDepthBufferView()` 返回有效 depth view。
- resize 后 depth buffer 随尺寸重建。
- borrowed swapchain image 不被 `VulkanTexture` 销毁。

当前实现更新：

- `TextureUsage` 已补齐位标志工具函数，便于公共 RHI 描述组合 usage。
- `VulkanTexture` 已支持 owned image 创建，owned image 使用 VMA allocation 管理；borrowed swapchain image 仍然只包装外部 `VkImage`，不会销毁它。
- `VulkanDevice::createTexture()` 和 `VulkanDevice::createTextureView()` 已接入真实 Vulkan texture / image view 创建。
- `VulkanTextureView` 创建 image view 时会根据 format 选择 color / depth / depth-stencil aspect mask。
- `VulkanSwapChain` 已在 create / resize 时创建默认 depth texture 和 depth view，`getDepthBufferView()` 返回有效 view。
- depth texture 由 swapchain 语义拥有，销毁顺序为 depth view -> depth texture -> swapchain color views / swapchain。
- 本小阶段只完成 depth 资源生命周期；depth barrier、dynamic rendering depth attachment 和 depth test 仍留到 0.5.8。

### 0.5.8 Dynamic Rendering Depth 接入

最后把 depth 接入 render scope 和 pipeline state。

工作内容：

- `FrameRenderer` 在一帧开始时 transition depth 到 `DepthStencilWrite`。
- `RenderingDesc` 传入 depth attachment。
- `VulkanCommandContext::beginRendering()` 填充 depth `VkRenderingAttachmentInfo`。
- `GraphicsPipelineDesc.depthStencilState` 开启 depth test / depth write。
- `VulkanPipelineState` 正确设置 depth stencil state 和 depth format。

验收：

- cube 面遮挡关系正确。
- depth clear 正常，窗口连续帧不留下上一帧深度。
- resize 后仍能正常绘制。
- Debug 构建下无明显 validation error。

当前实现更新：

- `VulkanCommandContext::pipelineBarrier()` 已支持 `DepthStencilWrite` / `DepthStencilRead` 的 layout、access mask 和 pipeline stage 映射。
- texture barrier 的 image aspect 已从固定 color aspect 改为按 texture format 选择，depth image 会使用 depth aspect。
- `VulkanCommandContext::beginRendering()` 已支持可选 depth attachment，并从 `DepthStencilAttachmentDesc` 读取 load/store 和 clear depth/stencil。
- `FrameRenderer` 每帧会把 swapchain depth buffer transition 到 `DepthStencilWrite`，并传入 `RenderingDesc.depthStencilAttachment`。
- `CubePass` 创建 graphics pipeline 时已经传入 swapchain depth format，并开启 depth test / depth write。
- 目前默认 depth format 为 `D32Float`，stencil attachment 仍未启用；`D24S8` 的 stencil 写入留到后续阶段再细化。

### 0.5.9 收尾与文档同步

工作内容：

- 更新 `phase05.md` 的当前实现状态。
- 增加代码阅读指南。
- 更新 smoke test。
- 保留 `TrianglePass` 作为最小三角形示例，或者在文档中明确它暂时不参与默认帧流程。

验收：

- `cmake --build --preset msvc-vcpkg-local-debug` 通过。
- `ctest --preset msvc-vcpkg-local-debug` 通过。
- sandbox 手动或隐藏窗口烟测通过。

当前实现更新：

- `phase05.md` 已记录 0.5.0 到 0.5.8 的当前实现状态和阶段边界。
- `framework_headers_smoke.cpp` 已覆盖 depth texture 描述、depth resource barrier、depth pipeline state 和 cube pass 头文件。
- `shader_assets_smoke.cpp` 已覆盖 triangle / cube 四个 SPIR-V shader 产物。
- `TrianglePass` 保留为 Phase 0.4 的最小三角形示例；默认 `FrameRenderer` 当前使用 `CubePass`。
- 已通过 Debug 构建、CTest 和隐藏窗口 sandbox smoke。

## Phase 0.5 完成摘要

Phase 0.5 已经完成从固定三角形到带 uniform、descriptor、indexed draw 和 depth test 的默认 cube 渲染闭环：

```text
Renderer
    -> FrameRenderer
        -> backbuffer/depth barrier
        -> beginRendering(color + depth)
        -> ClearPass
        -> CubePass
            -> update camera uniform
            -> bind pipeline
            -> bind descriptor set
            -> bind vertex/index buffer
            -> drawIndexed(36)
        -> endRendering
        -> backbuffer barrier to Present
```

当前默认帧流程已经验证以下能力：

- `DeviceContext::updateBuffer()` 可以更新 `CpuToGpu` uniform buffer。
- `DescriptorSetLayout` / `DescriptorSet` / `PipelineLayout` / `bindDescriptorSet()` 已形成最小 Vulkan descriptor 闭环。
- `CubePass` 使用 per-frame uniform buffer 和 descriptor set，避免覆盖 GPU 仍在读取的上一帧数据。
- `VulkanTexture` 支持 VMA owned image；swapchain color image 继续保持 borrowed。
- `VulkanSwapChain` 拥有默认 depth texture / depth view，并在 resize 时重建。
- dynamic rendering 同时接入 color attachment 和 depth attachment。
- `CubePass` pipeline 已开启 depth test / depth write。

## 代码阅读指南

建议按下面顺序阅读 Phase 0.5 的代码：

1. `src/renderer/Renderer.cpp`

   先看一帧外壳：`beginFrame()`、`acquireNextImage()`、`context.begin()`、`FrameRenderer::render()`、`context.end()`、`submit()`、`present()`。这里负责帧生命周期，不直接做具体 draw。

2. `src/renderer/FrameRenderer.cpp`

   再看 render scope：backbuffer 和 depth buffer 的 barrier、`RenderingDesc` 组装、`beginRendering()`、固定 pass 顺序、`endRendering()`、present 前 barrier。这个文件是 RenderGraph 落地前的手动一帧调度器。

3. `src/renderer/passes/CubePass.cpp`

   看 pass 自己拥有的资源：vertex/index buffer、per-frame uniform buffer、descriptor set layout、descriptor set、pipeline layout、shader 和 pipeline。重点看 `setup()`、`execute()`、`ensurePipeline()`、`updateCameraUniform()`。

4. `src/rhi/DeviceContext.h`

   看 renderer 层能使用的命令接口边界。Phase 0.5 新增的关键接口是 `updateBuffer()`、`bindDescriptorSet()`、`setIndexBuffer()` 和 `drawIndexed()`。

5. `src/rhi/vulkan/VulkanCommandContext.cpp`

   看 Vulkan 命令录制细节：dynamic rendering attachment 转换、descriptor set 绑定、buffer 更新转发、depth barrier 映射。这里是 RHI 命令语义落到 Vulkan command buffer 的地方。

6. `src/rhi/vulkan/VulkanDescriptorSetLayout.cpp`、`VulkanDescriptorSet.cpp`、`VulkanDescriptorManager.cpp`

   看 descriptor 最小闭环：layout 创建、pool 管理、descriptor set 分配、uniform buffer 写入。

7. `src/rhi/vulkan/VulkanPipelineLayout.cpp` 和 `VulkanPipelineState.cpp`

   看 descriptor set layout 如何进入 `VkPipelineLayout`，以及 dynamic rendering 的 color/depth format 如何进入 graphics pipeline。

8. `src/rhi/vulkan/VulkanTexture.cpp`、`VulkanTextureView.cpp`、`VulkanSwapChain.cpp`

   最后看 texture 生命周期：swapchain color image 是 borrowed，default depth image 是 owned；depth view 跟随 swapchain create / resize / destroy。

阅读时可以抓住三条主线：

- 资源创建主线：`CubePass::setup()` -> `RenderDevice` -> `VulkanDevice` -> Vulkan RAII wrapper。
- 每帧命令主线：`Renderer::render()` -> `FrameRenderer::render()` -> `CubePass::execute()` -> `VulkanCommandContext`。
- depth 主线：`VulkanSwapChain::createDepthResources()` -> `FrameRenderer` depth barrier / attachment -> `VulkanCommandContext::beginRendering()` -> `CubePass` depth pipeline state。

## 与设计文档对齐检查

Phase 0.5 方案需要继续遵守既有设计文档中的边界。

### 与 framework.md 对齐

- `RenderDevice` 继续作为资源工厂，负责创建 Buffer、Texture、TextureView、DescriptorSetLayout、DescriptorSet、PipelineLayout 和 PipelineState。
- `DeviceContext` 继续作为资源使用与命令录制对象，负责 update buffer、bind descriptor set、draw indexed 和 barrier。
- `SwapChain` 继续负责窗口相关 backbuffer、默认 depth buffer、resize、acquire 和 present。
- `renderer/` 只看到 RHI 概念，不直接包含 Vulkan 头文件。
- `rhi/vulkan/` 是唯一持有 Vulkan 对象和调用 Vulkan API 的位置。

### 与 module_responsibility.md 对齐

- `CubePass` 只拥有 pass-local RHI 资源，并在 `FrameContext` 中借用 `RenderDevice`、`DeviceContext` 和 `SwapChain`。
- `VulkanDevice` 拥有 descriptor manager、allocator 和设备级对象，仍不参与每帧 draw。
- `VulkanDescriptorManager` 管理 descriptor pool / descriptor set 分配，符合 Vulkan 后端职责。
- `VulkanSwapChain` 拥有默认 depth image / view，符合“默认 depth buffer 属于 SwapChain 语义”的约定。
- RHI 公共层不暴露 `VkDescriptorSet`、`VkDescriptorSetLayout`、`VkImage`、layout、access mask 或 pipeline stage。

### 与 file_system_and_shader_loading.md 对齐

- `cube.vert.hlsl` 和 `cube.frag.hlsl` 由 CMake 编译为 SPIR-V。
- `CubePass` 通过 `asset::ShaderLoader` 读取 `.spv` bytecode。
- pass 不直接包含 `<fstream>`，也不维护 shader fallback 路径。
- `asset/ShaderLoader` 不创建 RHI `Shader`，只返回 bytecode。

### 有意暂缓的设计项

- `ResourceManager` / handle 系统暂不引入，Phase 0.5 仍由 pass 持有局部 RHI 资源。
- `PipelineCache` 暂不实现，pipeline 继续由 pass 懒创建。
- bindless 暂不实现，只做普通 descriptor set。
- sampled texture / sampler 暂不实现，留给 Phase 0.6。

## 验收标准

Phase 0.5 完成后应满足：

- sandbox 能显示旋转 cube。
- cube 使用 uniform buffer 传入矩阵。
- cube 使用 descriptor set 绑定 uniform buffer。
- cube 使用 vertex buffer + index buffer + `drawIndexed()`。
- dynamic rendering 同时使用 color attachment 和 depth attachment。
- depth test / depth write 生效，cube 面遮挡关系正确。
- resize 后 depth buffer 跟随 swapchain 重建。
- renderer/app 层仍然不包含 Vulkan 头文件。
- RHI 层不暴露 `VkDescriptorSet`、`VkDescriptorSetLayout`、`VkImage` 等 Vulkan 类型。
- Debug 构建下无明显 validation error。
- 日志字符串保持英文，中文只出现在注释和文档中。

验证命令：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

手动或烟测运行：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

## 风险点

本阶段最容易出错的地方：

- descriptor set layout 与 shader binding 不一致。
- pipeline layout 没有包含 descriptor set layout。
- `bindDescriptorSet()` 使用了错误的 pipeline bind point 或 pipeline layout。
- uniform buffer 数据没有正确写入或 flush。
- GLM 矩阵与 HLSL 矩阵主序不匹配。
- Vulkan projection 没有处理 Y 翻转。
- depth image usage / aspect / view format 不正确。
- depth image layout transition 不正确。
- resize 后 depth view 被旧 command 或旧 pass 资源引用。
- descriptor set 持有的 uniform buffer 生命周期不足。

建议实现时每完成一个子能力就构建运行一次，不要等 descriptor、depth、cube 全部写完再一起调。

## Phase 0.6 建议方向

Phase 0.5 完成后，Phase 0.6 建议进入 texture sampling：

- `Sampler` 完整描述和 Vulkan 实现。
- `TextureLoader` 读取图片。
- sampled image descriptor。
- staging upload 到 GPU-only texture。
- textured cube。
- 为后续 glTF PBR 材质做准备。

也就是说，Phase 0.5 先解决“常量数据和深度”，Phase 0.6 再解决“贴图采样”。这个顺序更稳。
