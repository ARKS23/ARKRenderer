# Phase 0.4.1：顶点输入布局设计

本阶段是 Phase 0.4 的小延伸。Phase 0.4 已经完成最小三角形绘制闭环，当前代码中已经存在 `VertexAttributeDesc`、`VertexBufferLayoutDesc` 和 `DeviceContext::setVertexBuffer()`，可以把一个固定三角形画出来。

但是从引擎架构角度看，顶点输入还没有形成独立、可复用、可缓存、可校验的概念。Phase 0.4.1 的目标不是立刻引入完整 Mesh / GLTF / Material 系统，而是先把顶点输入布局整理成清晰的 RHI 语义，为后续静态网格、实例化、PipelineCache 和 ShaderReflection 做准备。

## 当前状态

当前顶点输入能力已经具备以下基础：

- `VertexAttributeDesc` 描述单个 shader input location 的格式和偏移。
- `VertexBufferLayoutDesc` 描述一个 vertex buffer binding 的 stride、input rate 和 attributes。
- `GraphicsPipelineDesc::vertexBuffers` 在创建 graphics pipeline 时传入顶点布局。
- `VulkanPipelineState` 会把 RHI 描述翻译为 `VkVertexInputBindingDescription` 和 `VkVertexInputAttributeDescription`。
- `DeviceContext::setVertexBuffer()` 对应 Vulkan 的 `vkCmdBindVertexBuffers()`。
- `TrianglePass` 已经使用 binding 0，location 0/1，绘制 `position + color` 三角形。

当前问题：

- 顶点输入布局直接散落在 `GraphicsPipelineDesc` 中，不是一个独立概念。
- 没有标准 Mesh 顶点格式约定。
- 没有 `VertexBufferView` / `IndexBufferView`，Mesh 层还无法清晰表达 draw 所需的 buffer、offset、count。
- 没有 layout hash，后续 `PipelineCache` 难以稳定缓存 pipeline。
- 没有 shader input reflection 校验，shader location 和 vertex layout 是否匹配完全靠人工约定。

## 设计目标

Phase 0.4.1 建议完成以下目标：

- 把顶点输入布局从 `GraphicsPipelineDesc` 中抽象为独立的 `VertexInputLayoutDesc`。
- 保留 Desc 风格，和现有 `BufferDesc`、`GraphicsPipelineDesc`、`ShaderDesc` 保持一致。
- 支持多 vertex buffer stream，例如 position 一条 stream，normal/uv 一条 stream。
- 支持 per-vertex 和 per-instance 两种 input rate。
- 为后续 `Mesh`、`GLTF`、`PipelineCache` 和 `ShaderReflection` 留出自然扩展点。
- 不暴露 Vulkan 类型到 RHI 或 renderer 层。
- 不在本阶段引入完整 RenderGraph、MaterialSystem 或 GLTF 加载。

## 推荐 RHI 结构

建议新增或整理为以下结构。为了保持职责清晰，可以把顶点输入相关描述放到独立头文件中：

```text
src/rhi/
|-- VertexInput.h
|-- PipelineState.h
`-- DeviceContext.h
```

推荐第一版设计：

```cpp
namespace ark::rhi {
    enum class VertexInputRate {
        PerVertex,
        PerInstance,
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

    struct VertexInputLayoutDesc {
        std::vector<VertexBufferLayoutDesc> buffers;
    };
}
```

然后把 `GraphicsPipelineDesc` 调整为：

```cpp
struct GraphicsPipelineDesc {
    std::string debugName;
    Shader* vertexShader = nullptr;
    Shader* fragmentShader = nullptr;
    PipelineLayout* layout = nullptr;

    VertexInputLayoutDesc vertexInput;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    RasterStateDesc rasterState;
    DepthStencilStateDesc depthStencilState;
    BlendStateDesc blendState;

    Format colorFormat = Format::Unknown;
    Format depthFormat = Format::Unknown;
};
```

这样做的好处是：`GraphicsPipelineDesc` 继续表达 pipeline 状态，而顶点输入布局成为独立状态对象，后续可以单独 hash、复用、校验和文档化。

## Mesh 侧建议

顶点输入布局描述的是 pipeline 期望怎样解释 vertex buffer。Mesh 侧还需要描述实际绑定哪些 buffer。

建议后续在 renderer 或 asset/runtime mesh 层引入：

```cpp
struct VertexBufferView {
    rhi::Buffer* buffer = nullptr;
    u64 offset = 0;
    u32 binding = 0;
};

struct IndexBufferView {
    rhi::Buffer* buffer = nullptr;
    u64 offset = 0;
    rhi::IndexType indexType = rhi::IndexType::UInt32;
};

struct MeshDrawView {
    std::vector<VertexBufferView> vertexBuffers;
    IndexBufferView indexBuffer;
    u32 vertexCount = 0;
    u32 indexCount = 0;
    u32 firstVertex = 0;
    u32 firstIndex = 0;
};
```

注意这里不建议把 `VertexInputLayoutDesc` 放进每个 `MeshDrawView`。更合理的关系是：

```text
PipelineState
    owns/uses VertexInputLayoutDesc

MeshDrawView
    provides actual Buffer bindings

DeviceContext
    setPipeline(pipeline)
    setVertexBuffer(binding, buffer, offset)
    setIndexBuffer(buffer, indexType, offset)
    draw/drawIndexed()
```

也就是说，pipeline 规定“怎么读”，mesh 提供“读什么”。

## 标准顶点格式

第一版可以先定义少量标准顶点格式，不要一开始就设计过度灵活的 vertex declaration 系统。

建议从这两个格式开始：

```cpp
struct SimpleVertex {
    float position[3];
    float color[3];
};

struct StaticMeshVertex {
    float position[3];
    float normal[3];
    float tangent[4];
    float texCoord0[2];
};
```

推荐 location 约定：

```text
location 0: position
location 1: normal or color
location 2: tangent
location 3: texCoord0
location 4: texCoord1
```

如果某个 shader 不需要 normal 或 tangent，可以用更小的 layout。不要强制所有 pass 使用同一个超大顶点格式。

## Vulkan 后端映射

Vulkan 后端的映射逻辑应该保持在 `src/rhi/vulkan/` 内部：

```text
VertexInputLayoutDesc
    -> VkVertexInputBindingDescription[]
    -> VkVertexInputAttributeDescription[]
    -> VkPipelineVertexInputStateCreateInfo
```

设计要点：

- `VertexBufferLayoutDesc::binding` 映射到 `VkVertexInputBindingDescription::binding`。
- `VertexBufferLayoutDesc::stride` 映射到 `VkVertexInputBindingDescription::stride`。
- `VertexInputRate::PerVertex` 映射到 `VK_VERTEX_INPUT_RATE_VERTEX`。
- `VertexInputRate::PerInstance` 映射到 `VK_VERTEX_INPUT_RATE_INSTANCE`。
- `VertexAttributeDesc::location` 必须和 shader input location 对齐。
- `VertexAttributeDesc::format` 必须能映射到合法 `VkFormat`。
- `VertexAttributeDesc::offset` 是相对当前 binding 的字节偏移。

本阶段不建议引入 Vulkan dynamic vertex input state。固定 vertex input 更简单，也更适合当前 pipeline cache 设计。等后续确实需要减少 pipeline permutation 时，再评估动态顶点输入。

## PipelineCache 关系

顶点输入布局是 graphics pipeline 不可变状态的一部分，因此它必须参与 pipeline cache key。

推荐后续缓存 key 包含：

```text
GraphicsPipelineKey
|-- vertex shader identity
|-- fragment shader identity
|-- pipeline layout identity
|-- vertex input layout hash
|-- topology
|-- raster state
|-- depth stencil state
|-- blend state
|-- color format
`-- depth format
```

Phase 0.4.1 可以先不实现完整 `PipelineCache`，但应确保 `VertexInputLayoutDesc` 是稳定、可比较、可 hash 的结构。后续可以新增：

```cpp
u64 hashVertexInputLayout(const VertexInputLayoutDesc& desc);
bool operator==(const VertexInputLayoutDesc& lhs, const VertexInputLayoutDesc& rhs);
```

第一版也可以先只实现内部 helper，不急着暴露为公共 API。

## ShaderReflection 关系

当前阶段可以继续手工约定 shader location。后续引入 SPIRV-Reflect 后，可以做自动校验：

```text
Shader input reflection
    location 0 R32G32B32Float
    location 1 R32G32B32Float
    location 3 R32G32Float

VertexInputLayoutDesc
    location 0 R32G32B32Float
    location 1 R32G32B32Float
    location 3 R32G32Float
```

校验规则：

- shader 需要的 location 必须存在。
- shader location 的 format 应与 vertex attribute format 兼容。
- 未被 shader 使用的 vertex attribute 可以允许存在，方便复用更大的 mesh layout。
- 不建议在 RHI 后端里做 shader reflection，reflection 更适合放在 asset 或 renderer/material 构建 pipeline 的阶段。

## 和 GLTF 的关系

GLTF loader 不应该直接创建 RHI buffer。它应该输出 CPU 侧 mesh 数据，例如：

```text
asset::MeshData
|-- positions
|-- normals
|-- tangents
|-- texcoords
|-- indices
`-- material index
```

然后由 renderer 侧或 ResourceManager 决定：

- 是否把数据打包成 interleaved vertex buffer。
- 是否拆成多个 vertex buffer stream。
- 使用哪套 `VertexInputLayoutDesc`。
- 创建哪些 GPU buffer。

第一版建议用 interleaved static mesh layout，简单稳定：

```text
binding 0:
    position
    normal
    tangent
    texCoord0
```

等后续需要优化带宽或支持 meshlet / skinning 时，再拆多 stream。

## 实施步骤

建议 Phase 0.4.1 按以下顺序落地：

1. 新增 `src/rhi/VertexInput.h`。
2. 将 `VertexInputRate`、`VertexAttributeDesc`、`VertexBufferLayoutDesc` 移入 `VertexInput.h`。
3. 新增 `VertexInputLayoutDesc`。
4. 修改 `GraphicsPipelineDesc`，把 `std::vector<VertexBufferLayoutDesc> vertexBuffers` 替换为 `VertexInputLayoutDesc vertexInput`。
5. 修改 `VulkanPipelineState`，从 `m_Desc.vertexInput.buffers` 生成 Vulkan binding / attribute descriptions。
6. 修改 `TrianglePass`，使用 `pipelineDesc.vertexInput.buffers.push_back(vertexLayout)`。
7. 更新 `framework_headers_smoke.cpp`，覆盖 `VertexInputLayoutDesc`。
8. 在 `docs/phase/phase04.md` 或本文件记录最终实现结果。
9. 构建并运行测试。

建议验证命令：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

## 验收标准

完成后应满足：

- `TrianglePass` 仍然能正常绘制彩色三角形。
- `GraphicsPipelineDesc` 不再直接暴露 `vertexBuffers` 散装字段，而是通过 `vertexInput` 表达。
- `VulkanPipelineState` 的顶点输入映射逻辑保持清晰。
- renderer 和 app 层不出现 Vulkan 类型。
- smoke test 能编译通过。
- 文档能清楚说明 pipeline 期望的 layout 与 mesh 实际绑定的 buffer 不是同一件事。

## 当前实现结果

本阶段已经完成以下代码改动：

- 新增 `src/rhi/VertexInput.h`，集中放置 `VertexInputRate`、`VertexAttributeDesc`、`VertexBufferLayoutDesc` 和 `VertexInputLayoutDesc`。
- `GraphicsPipelineDesc` 已从 `vertexBuffers` 改为 `vertexInput`，顶点输入布局成为独立的 pipeline 状态描述。
- `VulkanPipelineState` 已改为从 `m_Desc.vertexInput.buffers` 生成 Vulkan binding / attribute descriptions。
- `TrianglePass` 已改为通过 `pipelineDesc.vertexInput.buffers` 设置三角形顶点布局。
- `framework_headers_smoke.cpp` 已显式包含并覆盖 `VertexInputLayoutDesc`。

本阶段暂不实现以下内容：

- 不新增 `MeshDrawView` / `VertexBufferView`，避免在 Mesh 系统落地前引入未使用对象。
- 不实现 `VertexInputLayoutDesc` hash，等 `PipelineCache` 真正接入时统一设计 key。
- 不实现 shader reflection 校验，后续在 asset 或 material pipeline 构建阶段接入 SPIRV-Reflect。

## 后续方向

Phase 0.4.1 完成后，后续可以继续推进：

- `PipelineCache`：让 `VertexInputLayoutDesc` 参与 pipeline key。
- `MeshDrawView`：让 renderer 能表达真实 mesh draw。
- `StaticMesh`：引入标准静态网格顶点格式。
- `ShaderReflection`：校验 shader input 和 vertex layout。
- `GLTF`：把 glTF attribute 转换为引擎标准 mesh 数据。
- `Descriptor`：进入 camera、uniform buffer、material 参数绑定阶段。

我的建议是先做 Phase 0.4.1，再进入更完整的 Phase 0.5。顶点输入布局是 mesh、pipeline 和 shader 之间的交界点，把它先整理干净，后续接 GLTF 和材质系统会顺很多。
