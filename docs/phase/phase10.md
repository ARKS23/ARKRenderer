# Phase 0.10 glTF Scene Transform 与 Camera 最小闭环

## 阶段判断

Phase 0.9 已经完成 `RenderScene -> RenderQueue -> ForwardPass` 的多 draw 最小闭环：

```text
asset::ModelData
    -> ModelResource
    -> RenderScene
    -> RenderQueue
    -> ForwardPass
    -> indexed textured multi draw
```

当前默认路径已经不再由 `ForwardPass` 加载 glTF，也不再只绘制单个 mesh/material。这个边界是正确的，后续不应回退到 pass 内部硬编码资产加载。

但 Phase 0.9 之后仍有几个直接影响真实资产能力的缺口：

- `GltfLoader` 仍然把所有 mesh primitive 扁平化为 `ModelData::meshes`，没有解析 glTF scene/node hierarchy。
- glTF node transform、scene selection、mesh instance 还没有进入 CPU asset 数据。
- `RenderQueue` 展开 `ModelResource` 时，model 内所有 primitive 使用同一个 scene model transform。
- `RenderView` 仍是空接口，`ForwardPass` 内部硬编码 view/projection。
- material/texture 仍是最小 RGBA8 base color 路径，同一 texture 被多个 material 引用时可能重复创建 GPU resource。
- GPU deferred deletion 目前主要覆盖 upload staging buffer，还没有系统化覆盖 texture/view/sampler/pipeline 等运行期释放对象。

因此 Phase 0.10 的主线不应是 PBR、RenderGraph 或 bindless，而应先把真实 glTF scene 语义、camera 输入和资源复用边界补齐。

## 目标

Phase 0.10 的目标是在不引入过重 ResourceManager 或完整 glTF 引擎的前提下，完成以下能力：

- 支持 glTF 2.0 default scene 或第一个 scene 的 node 遍历。
- 支持 glTF node 的 `matrix` 与 TRS 变换。
- 支持 node 引用 mesh，并把 mesh primitive 转换为可绘制 instance。
- `ModelData` 能表达 primitive resource 与 primitive instance 的区别。
- `ModelResource` 能保存 primitive instance 的局部 transform。
- `RenderQueue` 展开 model 时使用 `sceneModel.transform * primitiveInstance.localTransform`。
- `RenderView` 提供最小 view/projection 数据，`ForwardPass` 不再硬编码 camera。
- 默认 sandbox 增加多 node / 多 instance fixture，验证不同 transform 的多 draw。
- 明确 texture cache、sRGB、mipmap 和完整 deferred destruction 的后续边界。

## 非目标

Phase 0.10 暂不做以下内容：

- 不做 glTF skin、animation、morph target、camera、light。
- 不做 KHR / EXT 扩展、Draco、embedded image 或 data URI image。
- 不做完整 PBR material。
- 不做 IBL、HDR pipeline、tone mapping。
- 不做完整 ResourceManager handle 系统。
- 不做 bindless descriptor、global texture array 或 descriptor indexing。
- 不做 RenderGraph 自动 barrier、transient resource aliasing 或 async compute。
- 不做 transfer queue、timeline semaphore 或异步 streaming。
- 不做完整 mipmap 生成。
- 不做 frustum culling、sorting、batching、instancing 优化。

这些内容需要等 scene/camera/resource ownership 基础稳定后再进入后续阶段。

## 模块边界

Phase 0.10 继续遵守：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase07.md
docs/phase/phase08.md
docs/phase/phase09.md
```

硬性边界：

- `asset/` 只解析外部文件并输出 CPU 数据，不创建 RHI/GPU 资源。
- `renderer/` 可以创建和持有 RHI 资源，但不暴露或使用 Vulkan 类型。
- `RenderScene` 保存 scene 语义，不创建 GPU 资源。
- `RenderQueue` 是 draw list，不拥有底层 GPU 资源。
- `ModelResource` 是 renderer 层 GPU resource owner。
- `ForwardPass` 只负责 pipeline、descriptor、uniform binding 和 draw。
- upload 仍然必须在 dynamic rendering scope 外记录。
- draw 仍然必须在 dynamic rendering scope 内发生。
- 如果实现方向需要打破上述边界，先更新设计文档，再修改代码。

## 推荐数据流

Phase 0.10 目标数据流：

```text
glTF 2.0 file
    -> asset::ModelData
        -> meshes/materials
        -> primitive instances with local transform
    -> renderer::ModelResource
        -> MeshResource / MaterialResource
        -> primitive resources / primitive instances
    -> RenderScene::addModel(model, sceneTransform)
    -> RenderQueue::build(scene)
        -> DrawItem(mesh, material, sceneTransform * instanceTransform)
    -> ForwardPass
        -> CameraUniform from RenderView
        -> ObjectUniform from DrawItem
        -> drawIndexed
```

注意：

- glTF node transform 属于 asset scene 语义，应先进入 CPU 数据。
- scene 中同一个 mesh 被多个 node 引用时，不应重复创建 mesh GPU buffer。
- renderer 层只把 CPU 数据转换为 RHI resource，不重新解释 glTF 文件。
- `ForwardPass` 不应知道 glTF node 或 material index remap 的细节。

## 数据结构建议

以下是设计草案，实际实现应贴合现有代码风格。

### asset::ModelData

当前 `ModelData` 只有：

```cpp
std::vector<MeshPrimitiveData> meshes;
std::vector<MaterialData> materials;
```

Phase 0.10 建议补充 CPU 侧 instance 数据：

```cpp
namespace ark::asset {
    struct TransformData {
        // glTF 与 glm 默认都可按 column-major 矩阵传递；renderer 层再转换为 glm::mat4。
        float matrix[16]{};
    };

    struct MeshPrimitiveInstanceData {
        u32 meshIndex = 0;
        TransformData localTransform;
        std::string debugName;
    };

    struct ModelData {
        std::vector<MeshPrimitiveData> meshes;
        std::vector<MaterialData> materials;
        std::vector<MeshPrimitiveInstanceData> instances;
        std::string debugName;
    };
}
```

说明：

- `asset/` 不建议直接依赖 renderer 层 math 类型。
- 如果不引入全局 math module，先用 `float[16]` 保持数据纯净。
- `instances` 为空时，renderer 可为每个 mesh primitive 生成 identity instance，兼容 Phase 0.9 fixture。

### renderer::ModelResource

Phase 0.10 建议区分 resource primitive 与 draw instance：

```cpp
struct ModelPrimitiveResource {
    u32 meshIndex = 0;
    u32 materialIndex = 0;
    std::string debugName;
};

struct ModelPrimitiveInstance {
    u32 primitiveIndex = 0;
    glm::mat4 localTransform{1.0f};
    std::string debugName;
};
```

`ModelResource` 继续拥有：

- `std::vector<MeshResource>`
- `std::vector<MaterialResource>`
- primitive resource mapping
- primitive instance mapping

`RenderQueue::build()` 展开 model 时：

```cpp
DrawItem.modelMatrix = sceneModel.transform * modelInstance.localTransform;
```

## glTF 加载策略

Phase 0.10 只支持 glTF 2.0 core profile 的最小 scene 子集：

- 使用 `model.defaultScene`，如果没有 default scene，则使用第一个 scene。
- 递归遍历 scene root nodes。
- 支持 node `matrix`。
- 支持 node TRS：`translation`、`rotation`、`scale`。
- `matrix` 与 TRS 同时存在时，按 glTF 规范优先使用 `matrix`，并记录注释或 TODO。
- 支持 node 引用 mesh。
- 对 mesh 内所有 primitives 生成 instance。
- 继续要求 primitive mode 为 `TRIANGLES`。
- 继续要求 POSITION / NORMAL / TEXCOORD_0 / indices。
- material remap 继续由 asset 层完成。

不支持的情况：

- skin / animation / morph target：失败或 TODO。
- node camera / light：忽略并记录 TODO。
- embedded image / data URI image：继续不支持。
- glTF extensions：继续不支持。

## RenderView 最小落地

当前 `RenderView` 是空接口，`ForwardPass` 内部硬编码 camera。Phase 0.10 应补齐最小 camera 数据：

```cpp
class RenderView {
public:
    const glm::mat4& viewMatrix() const;
    const glm::mat4& projectionMatrix() const;
};
```

或先使用简单 struct：

```cpp
struct RenderView {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
};
```

推荐先保持简单：

- `Application` 或 sandbox 构造默认 camera。
- `Renderer::render(scene, view)` 继续传入 `RenderView`。
- `ForwardPass::makeCameraUniform()` 改为读取 `frameContext.view`。
- 如果 view 为空或无效，可使用默认 view/projection fallback，但应记录 TODO。

## Texture Cache 与 ResourceManager 边界

Phase 0.10 不建议直接实现完整 ResourceManager handle 系统，但应明确后续方向：

当前问题：

- 多个 `MaterialResource` 指向同一 texture path 时，会重复创建 texture/staging/view/sampler。
- `MaterialResource` 同时负责 texture loading、texture resource、sampler 和 descriptor 写入，后续 PBR 会膨胀。

建议后续最小演进：

```text
TextureResource
    owns texture / texture view / staging upload state

TextureCache
    path -> TextureResource

MaterialResource
    references TextureResource
    owns material descriptor write semantics
```

Phase 0.10 可以先不实现 cache，但文档和 TODO 应明确：

- texture cache 属于 renderer/resource 层，不属于 asset 层。
- cache key 初期可使用规范化后的 texture path。
- cache 生命周期应由 Renderer 或后续 ResourceManager 管理。
- runtime unload/reload 前，需要更完整的 GPU object deferred destruction。

## Texture Color Space 与 Mipmap 边界

当前 base color texture 使用 `RGBA8Unorm`。真实 PBR 前至少需要明确：

- baseColor texture 默认应按 sRGB 采样。
- non-color data texture 才应使用 Unorm/linear。
- RHI 后续需要补充 `RGBA8Srgb` 或 material texture color space 描述。
- mipmap 生成需要 `TransferSrc` usage、blit/copy 支持和 per-mip barrier。

Phase 0.10 不要求实现 mipmap，但应在文档中继续标注：

- 当前 texture upload 仍是 mip0/layer0。
- 当前 sampler mip filter 仍是最小路径。
- PBR 前需要补齐 sRGB 和 mipmap 策略。

## 实施顺序

### 0.10.0 文档与范围确认

工作内容：

- 新增 `docs/phase/phase10.md`。
- 明确 Phase 0.10 主线是 glTF scene/node transform 与 RenderView camera。
- 明确不进入完整 PBR、RenderGraph、bindless 或完整 ResourceManager。

验收：

- 文档与既有模块职责一致。
- 不重复 Phase 0.7/0.8/0.9 已完成工作。

当前实现状态：

- 已新增 `docs/phase/phase10.md`。
- 已确认本阶段只推进 glTF scene/node transform、RenderView camera、ModelResource primitive instance 和 sandbox 多 node fixture。
- 未引入 PBR、RenderGraph、bindless 或完整 ResourceManager。

### 0.10.1 ModelData instance 数据结构

工作内容：

- 在 `asset::ModelData` 中增加 primitive instance 数据。
- 增加 transform CPU 表达。
- 保持 `asset/` 不依赖 renderer/RHI/Vulkan。
- 更新 mesh/model smoke test。

验收：

- 旧 fixture 没有 node instance 数据时仍可兼容。
- 新数据结构能表达同一 mesh primitive 被多个 node instance 引用。

当前实现状态：

- `asset::ModelData` 已新增 `instances`。
- 已新增 `asset::TransformData`，使用 column-major `float[16]` 表达 CPU 侧 transform。
- 已新增 `asset::MeshPrimitiveInstanceData`，用 `meshIndex` 指向 `ModelData::meshes`。
- `asset/` 仍不依赖 renderer/RHI/Vulkan。

### 0.10.2 glTF scene/node transform 加载

工作内容：

- `GltfLoader` 支持 default scene / first scene。
- 递归遍历 node hierarchy。
- 支持 node matrix 和 TRS。
- 生成 primitive instance。
- 增加 glTF fixture 和 smoke test。

验收：

- 多 node fixture 能输出多个 instance。
- 不同 node transform 能被测试验证。
- 不支持的 glTF feature 行为明确。

当前实现状态：

- `GltfLoader` 已支持 glTF 2.0 default scene；没有 default scene 时使用第一个 scene。
- 已递归遍历 scene root nodes。
- 已支持 node `matrix`。
- 已支持 node TRS，并按 `T * R * S` 组合。
- 已支持 node 引用 mesh，并为 mesh 内每个 primitive 生成 primitive instance。
- 旧的 `forward_fixture.gltf` 和 `forward_multidraw_fixture.gltf` 会通过 scene node 生成 identity instance。
- `skin` 当前显式失败；node camera 当前忽略并输出 warning；embedded image / data URI image 仍不支持。

### 0.10.3 ModelResource 与 RenderQueue instance transform

工作内容：

- `ModelResource` 保存 primitive resource 与 primitive instance。
- `RenderQueue::build()` 展开 model instances。
- draw item model matrix 使用 scene transform 与 model-local transform 组合。
- 更新 `model_resource_smoke` 或新增 queue transform test。

验收：

- 同一个 mesh primitive 可生成多个 draw item。
- 不同 instance transform 进入 `DrawItem::modelMatrix`。
- `RenderQueue` 仍不拥有 GPU resource。

当前实现状态：

- `ModelResource` 已新增 `ModelPrimitiveInstance` 和 `instances()`。
- `ModelResource::create()` 已把 `asset::MeshPrimitiveInstanceData` 转换为 renderer 层 `glm::mat4`。
- 当 `ModelData::instances` 为空时，`ModelResource` 会为每个 primitive 生成 identity instance，兼容旧数据路径。
- `RenderQueue::build()` 已改为展开 `ModelResource::instances()`。
- `DrawItem::modelMatrix` 当前使用 `sceneModel.transform * instance.localTransform`。
- `model_resource_smoke` 已验证 scene transform 与 local transform 的组合结果。

### 0.10.4 RenderView camera 最小落地

工作内容：

- 补齐 `RenderView` 的 view/projection 数据。
- `ForwardPass` 从 `FrameContext::view` 读取 camera uniform。
- 默认 sandbox 创建明确 camera。

验收：

- `ForwardPass` 不再硬编码 camera。
- resize 后 projection aspect 可按 viewport 更新。
- shader binding 不变。

当前实现状态：

- `RenderView` 已补齐 view/projection matrix。
- `RenderView::setDefaultPerspective()` 已提供默认 camera。
- `Application` 初始化和 resize 时会更新默认 `RenderView`。
- `ForwardPass::makeCameraUniform()` 已改为从 `FrameContext::view` 读取 view/projection。
- shader binding 保持不变。

### 0.10.5 sandbox 多 node fixture

工作内容：

- 新增或更新默认 glTF fixture，包含至少两个 node instance。
- 默认 sandbox 路径继续走 `Renderer -> RenderScene -> RenderQueue -> ForwardPass`。
- 保留 `forward_multidraw_fixture.gltf` 作为 Phase 0.9 对照 fixture，或明确迁移。

验收：

- build 通过。
- CTest 通过。
- sandbox smoke 通过。
- 日志能确认加载多 node / 多 instance fixture。

当前实现状态：

- 已新增 `assets/models/forward_multinode_fixture.gltf`。
- 默认 sandbox model 已切换为 `assets/models/forward_multinode_fixture.gltf`。
- `forward_multidraw_fixture.gltf` 保留为 Phase 0.9 多 primitive / 多 material 对照 fixture。
- `gltf_loader_smoke` 已验证 multinode fixture 输出 1 个 primitive、1 个 material、2 个 instance，并验证 translation / matrix scale。
- sandbox smoke 日志已确认：

```text
Loaded glTF model: assets/models/forward_multinode_fixture.gltf (primitives=1, materials=1, instances=2)
```

### 0.10.6 Phase 0.10 收尾

工作内容：

- 更新 `docs/phase/phase10.md` 当前实现状态。
- 按用户要求同步 `docs/codex_handoff.md`。
- 记录 texture cache、sRGB、mipmap、deferred destruction 的后续 TODO。

验收：

- 文档、代码和测试状态一致。
- 不把未完成能力写成已完成。

当前实现状态：

- 0.10.0 到 0.10.6 已完成。
- 已同步 `docs/codex_handoff.md` 到 Phase 0.10 完成后的交接状态。
- texture cache、sRGB、mipmap、完整 GPU object deferred destruction 仍保留为后续 TODO。

## 审核检查点

- `asset/` 没有依赖 renderer/RHI/Vulkan。
- `GltfLoader` 明确支持 glTF 2.0 scene/node transform 的最小子集。
- node transform 的矩阵乘法顺序明确，并有测试覆盖。
- mesh primitive resource 与 primitive instance 没有混淆。
- `RenderQueue` 仍只生成 draw list，不拥有 GPU 资源。
- `ForwardPass` 仍只消费 queue，不加载资产。
- `ForwardPass` 的 camera uniform 来自 `RenderView`。
- upload 仍然发生在 `prepare()`，不进入 dynamic rendering scope。
- draw 仍然发生在 `execute()`，位于 dynamic rendering scope 内。
- 不支持的 glTF feature 有日志或 TODO。
- 不引入 Vulkan 类型到 renderer / asset / app。

## 验证计划

每个实现子阶段完成后运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

涉及默认渲染路径变化后运行 sandbox smoke：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

建议新增或扩展测试：

- `gltf_loader_smoke`：验证 scene/node instance count 与 transform。
- `model_resource_smoke`：验证 primitive instance 展开和 transform。
- `render_scene_queue_smoke`：验证 scene transform 与 local transform 组合。

当前验证记录：

```text
cmake --build --preset msvc-vcpkg-local-debug: passed
ctest --preset msvc-vcpkg-local-debug: 8/8 passed
sandbox smoke: passed
```

## 完成标准

Phase 0.10 完成时应满足：

- glTF 2.0 loader 支持 default scene / first scene 的 node 遍历。
- node matrix/TRS transform 能进入 `ModelData`。
- `ModelResource` 能保留 primitive instance。
- `RenderQueue` 能生成不同 transform 的多 draw item。
- `RenderView` 提供 camera uniform 数据。
- `ForwardPass` 不再硬编码 view/projection。
- 默认 sandbox 能验证多 node / 多 instance draw。
- build、CTest 和 sandbox smoke 通过。

## 后续 Phase 建议

Phase 0.10 完成后，建议按以下顺序继续：

1. 最小 TextureResource / TextureCache，避免重复加载同一 texture。
2. texture color space：`RGBA8Unorm` 与 `RGBA8Srgb` 的边界。
3. mipmap upload / generation 策略。
4. 更完整的 GPU object deferred destruction。
5. PBR material 参数与基础光照。
6. ResourceManager handle 系统。
7. RenderGraph 第一版 pass/resource 声明。

不建议在 Phase 0.10 完成前直接进入完整 PBR 或 RenderGraph，因为当前更关键的是 scene transform、camera 输入和资源生命周期边界。
