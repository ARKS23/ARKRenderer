# Phase 0.9 RenderScene、RenderQueue 与多 Draw 最小闭环

## 阶段判断

Phase 0.8 已经完成从真实资产到 `ForwardPass` 的最小闭环：

```text
glTF 2.0 fixture
    -> asset::ModelData
    -> MeshResource / MaterialResource
    -> ForwardPass
    -> indexed textured mesh draw
```

Phase 0.9 启动前，这条闭环仍然是“单模型、单 mesh、单 material、pass 内部硬编码加载”的阶段性实现：

- `RenderScene`、`RenderQueue`、`MaterialSystem` 还是空壳。
- `FrameContext` 已预留 `scene` 和 `queue`，但没有真正构建 render queue。
- `ForwardPass` 直接查找 `assets/models/forward_fixture.gltf`，并持有单个 `MeshResource` / `MaterialResource`。
- `ForwardPass` 只取 `ModelData::meshes.front()` 和 `ModelData::materials.front()`。
- `GltfLoader` 只加载第一个 mesh 的第一个 primitive，不表达 node transform。
- `CameraUniform` 同时包含 camera 与 model matrix，无法直接支持多个 draw item 的不同 transform。

因此 Phase 0.9 的主线应是：把 Phase 0.8 的单资源 pass 闭环升级为 renderer 层的最小 scene / queue / draw item 闭环。目标不是立刻进入完整 glTF scene graph、PBR、ResourceManager 或 RenderGraph，而是先让 `ForwardPass` 从“自己加载并绘制一个资源”变成“消费 render queue 并绘制多个 draw item”。

## 目标

Phase 0.9 目标是在不引入过重架构的前提下完成以下能力：

- 新增最小 `RenderScene` 数据承载能力，表达一组可绘制对象。
- 新增最小 `RenderQueue`，把 scene 转换为 flat draw item list。
- 新增 renderer 层 `ModelResource` 或等价封装，把 `asset::ModelData` 转换为多个 `MeshResource` / `MaterialResource`。
- 让 `ForwardPass` 消费 `RenderQueue`，不再直接加载 fixture 或只绘制 `front()`。
- 支持一个 model 中多个 mesh primitive 和多个 material 的绘制。
- 建立 per-object transform 的最小 uniform 策略，支持多个 draw item 使用不同 model matrix。
- 扩展 glTF 2.0 loader 到多 primitive / 多 material 的最小子集。
- 默认 sandbox 使用一个多 draw fixture 或多对象 scene 验证完整数据流。

## 非目标

Phase 0.9 暂不做以下内容：

- 不做完整 glTF 2.0 scene graph、animation、skin、morph target。
- 不做 KHR / EXT 扩展、Draco、embedded image 或 data URI image。
- 不做完整 ResourceManager handle 系统。
- 不做完整 MaterialSystem、shader variant 或 PBR。
- 不做 RenderGraph、自动 barrier、transient resource aliasing。
- 不做 bindless texture、global descriptor array 或 descriptor indexing。
- 不做 async upload、transfer queue、timeline semaphore。
- 不做 mipmap 生成、sRGB 自动推断、HDR texture、压缩纹理。
- 不做 frustum culling、sorting、batching 或 instancing。
- 不做 hot reload、editor UI 或复杂 asset database。

这些内容需要等多 draw 的 scene / queue 职责稳定后再进入后续阶段。

## 模块边界

Phase 0.9 必须继续遵守：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase07.md
docs/phase/phase08.md
```

### asset/

`asset/` 继续只输出 CPU 数据。

允许扩展：

```text
src/asset/MeshData.h
src/asset/GltfLoader.h/.cpp
```

建议扩展方向：

- `ModelData::meshes` 可以包含多个 primitive。
- `ModelData::materials` 可以包含多个 `MaterialData`。
- 每个 `MeshPrimitiveData::materialIndex` 指向 `ModelData::materials`。
- 初期可以继续不表达完整 node hierarchy，但应为后续 node transform 留出设计空间。

约束：

- `asset/` 不依赖 renderer / RHI / Vulkan。
- glTF index 仍在 asset 层统一转换为 `u32`。
- glTF texture path 继续基于 glTF 文件所在目录解析。
- unsupported feature 必须明确失败或记录 TODO，不能假装完整支持。

### renderer/

`renderer/` 是 Phase 0.9 的主要改动层。

建议新增或补齐：

```text
src/renderer/RenderScene.h/.cpp
src/renderer/RenderQueue.h/.cpp
src/renderer/ModelResource.h/.cpp
```

建议职责：

- `RenderScene`：保存 scene object 列表，表达 model resource 与 transform。
- `ModelResource`：持有多个 `MeshResource` 和多个 `MaterialResource`，负责由 `asset::ModelData` 创建 renderer/RHI 资源。
- `RenderQueue`：从 `RenderScene` 生成 draw item list，供 `ForwardPass` 消费。
- `ForwardPass`：只负责 pipeline、descriptor、per-frame/per-object binding 和 draw，不负责资产查找或文件加载。

### renderer/material/

Phase 0.9 不要求完整 `MaterialSystem`，但应开始明确材质资源共享边界：

- 初期 `ModelResource` 可以直接持有 `MaterialResource`。
- 如果两个 material 指向同一个 texture，Phase 0.9 可以先重复创建，记录 TODO。
- 不在本阶段实现全局 texture cache 或 material cache，除非实现非常小且边界清晰。

### rhi/

Phase 0.9 默认不扩展 RHI。

唯一需要重点评估的是 per-object data 绑定方式：

- 如果继续使用 uniform buffer，需要避免每 draw 覆写同一个 GPU 仍可能读取的 buffer 区域。
- 如果引入 push constants，需要先在公共 RHI 设计中表达 API 无关语义，再由 Vulkan 后端实现。
- 如果引入 dynamic uniform buffer / storage buffer，复杂度会明显上升，不建议作为 Phase 0.9 首选。

建议 Phase 0.9 使用最小 per-draw object uniform buffer 或 per-frame object uniform buffer slices；如果当前 RHI 不支持 dynamic offset，则优先采用“每个 draw item 每个 frame slot 一个 object uniform buffer”的简单方案。

### rhi/vulkan/

Phase 0.9 目标是尽量少动 Vulkan 后端。

允许的后端改动：

- 修复因多 draw 暴露出的 descriptor / buffer update / lifetime 问题。
- 如果文档确认引入 push constants，再补 Vulkan pipeline layout 和 command context 支持。

禁止事项：

- 不把 Vulkan descriptor set、layout、buffer handle 暴露给 renderer。
- 不为了多 draw 提前实现 bindless 或 descriptor indexing。

## 推荐数据流

Phase 0.9 目标数据流：

```text
Application / sandbox
    -> create or load asset::ModelData
    -> renderer::ModelResource::create(device, modelData)
    -> RenderScene::addModel(modelResource, transform)
    -> Renderer::render(scene, view)
        -> RenderQueue::build(scene)
        -> FrameRenderer::render(frameContext)
            -> ForwardPass::prepare(queue)
                -> upload pending mesh/material resources
            -> beginRendering()
            -> ForwardPass::execute(queue)
                -> for drawItem in queue:
                    -> update / bind object data
                    -> bind material descriptors
                    -> bind mesh
                    -> drawIndexed
```

注意：

- 文件解析仍在 `asset/`。
- GPU 资源创建仍在 renderer resource 层，通过 RHI 完成。
- upload 仍在 `prepare()`，不能进入 dynamic rendering scope。
- draw 仍在 `execute()`，必须发生在 dynamic rendering scope 内。

## 建议数据结构草案

以下只是设计草案，实际实现应贴合现有代码风格。

### RenderScene

```cpp
namespace ark {
    struct SceneObject {
        ModelResource* model = nullptr;
        glm::mat4 transform{1.0f};
        std::string debugName;
    };

    class RenderScene {
    public:
        void addModel(ModelResource& model, const glm::mat4& transform, std::string debugName = {});
        std::span<const SceneObject> objects() const;
        void clear();
    };
}
```

### RenderQueue

```cpp
namespace ark {
    struct DrawItem {
        MeshResource* mesh = nullptr;
        MaterialResource* material = nullptr;
        glm::mat4 modelMatrix{1.0f};
        std::string debugName;
    };

    class RenderQueue {
    public:
        void build(const RenderScene& scene);
        std::span<const DrawItem> drawItems() const;
        void clear();
    };
}
```

### ModelResource

```cpp
namespace ark {
    class ModelResource {
    public:
        bool create(rhi::RenderDevice& device, const asset::ModelData& model);
        bool upload(rhi::DeviceContext& context);
        usize meshCount() const;
        MeshResource* mesh(usize index);
        MaterialResource* material(usize index);
    };
}
```

## Per-object uniform 策略

当前 `ForwardPass` 的 uniform：

```cpp
struct CameraUniform {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};
```

这个设计只适合单 draw。Phase 0.9 应拆分为：

```cpp
struct CameraUniform {
    glm::mat4 view;
    glm::mat4 projection;
};

struct ObjectUniform {
    glm::mat4 model;
};
```

最小实现方案：

- set 0：per-frame camera + material texture，延续当前 binding 0/1/2。
- object uniform 可以先放到同一个 descriptor set 的新增 binding，或新增 set 1。
- 为避免 dynamic offset 复杂度，初期每个 draw item 每个 frame slot 分配一个 object uniform buffer 和 descriptor set。

推荐初期 descriptor 设计：

```text
set 0 binding 0: CameraUniformBuffer
set 0 binding 1: SampledImage
set 0 binding 2: Sampler
set 0 binding 3: ObjectUniformBuffer
```

缺点：

- descriptor set 数量随 draw item 增长。
- 不是最终高性能方案。

优点：

- 不需要新增 RHI dynamic offset。
- 不需要 push constants。
- 容易验证多 draw correctness。

后续如果 draw item 数量上升，再评估 push constants、dynamic uniform buffer 或 storage buffer。

## 实施顺序

### 0.9.0 文档与范围确认

工作内容：

- 新增 `docs/phase/phase09.md`。
- 明确 Phase 0.9 目标是 scene / queue / multi draw 最小闭环。
- 明确不进入完整 PBR、RenderGraph、ResourceManager 或完整 glTF scene graph。

验收：

- 文档与既有模块职责一致。
- 明确 per-object data 的最小策略。

当前实现状态：

- 已新增 `docs/phase/phase09.md`。
- 已明确 Phase 0.9 主线是 `RenderScene -> RenderQueue -> ForwardPass` 的多 draw 最小闭环。
- 已明确 0.9 不进入完整 PBR、RenderGraph、ResourceManager、bindless 或完整 glTF scene graph。
- 已记录 per-object uniform 的阶段性策略：先避免覆写同一个 in-flight uniform buffer，后续再评估 push constants / dynamic uniform buffer / storage buffer。

### 0.9.1 RenderScene 与 RenderQueue 最小结构

工作内容：

- 补齐 `RenderScene`，支持添加 model instance 和 transform。
- 补齐 `RenderQueue`，从 scene 生成 flat draw item list。
- `FrameContext::queue` 开始被实际使用。
- 增加 smoke test 验证 scene -> queue 的 draw item 数量和 transform。

验收：

- `RenderScene` 和 `RenderQueue` 不依赖 Vulkan。
- `RenderQueue` 不创建 GPU 资源，只保存 draw 所需引用。
- `Renderer` 或 `FrameRenderer` 负责在每帧构建 queue。

当前实现状态：

- 已补齐 `src/renderer/RenderScene.h/.cpp`。
- 已补齐 `src/renderer/RenderQueue.h/.cpp`。
- `RenderScene` 当前同时支持 model 级 `SceneModel` 和 primitive 级 `SceneObject`。`SceneModel` 保存 `ModelResource*`、transform 和 debug name；`SceneObject` 保存 `MeshResource*`、`MaterialResource*`、transform 和 debug name。
- `RenderQueue::build()` 当前从 scene 生成 flat `DrawItem` list；它会展开 model primitives，并过滤无效 mesh/material 引用。
- `DrawItem` 当前保存 `MeshResource*`、`MaterialResource*`、model matrix 和 debug name。
- 已新增 `tests/render_scene_queue_smoke.cpp`，验证 scene 添加对象、queue 构建、资源引用、model matrix 和清空行为。
- 已在 `CMakeLists.txt` 接入 `ark_render_scene_queue_smoke`。
- `ForwardPass` 已在 0.9.3 兼容路径中消费 `RenderQueue`。

### 0.9.2 ModelResource 最小封装

工作内容：

- 新增 `ModelResource`。
- 从 `asset::ModelData` 创建多个 `MeshResource` 和 `MaterialResource`。
- `upload()` 遍历上传所有 mesh / material。
- 支持 material index 映射。

验收：

- `ForwardPass` 不再直接持有单个 `MeshResource` / `MaterialResource`。
- 多 primitive 的 model 可以转换成多个 draw item。
- staging buffer 仍走 deferred deletion。

当前实现状态：

- 已新增 `src/renderer/ModelResource.h/.cpp`。
- `ModelResource::create()` 从 `asset::ModelData` 创建多个 `MeshResource` 和多个 `MaterialResource`，并保存 primitive 到 material 的索引映射。
- `ModelResource::upload()` 遍历上传所有 mesh/material，复用 `MeshResource::upload()`、`MaterialResource::upload()` 和 staging deferred deletion。
- `RenderScene::addModel()` 已支持把 `ModelResource` 作为 scene model instance 加入场景。
- `RenderQueue::build()` 已支持展开 `ModelResource` 的 primitive 列表，并为每个 primitive 生成 draw item。
- 已新增 `tests/model_resource_smoke.cpp`，用 fake RHI device/context 验证 model resource 创建、上传、scene addModel 和 queue 展开。
- 已在 `CMakeLists.txt` 接入 `ark_model_resource_smoke`。

当前限制：

- `ModelResource` 当前不做 texture/material cache；多个 material 指向同一贴图时仍可能重复创建资源。
- `ModelResource` 当前不表达 node transform 或 scene graph，只处理 `ModelData` 已经扁平化后的 primitive/material 数据。

### 0.9.3 ForwardPass 消费 RenderQueue

工作内容：

- 移除 `ForwardPass` 内部 asset path 查找和 fixture fallback。
- `ForwardPass::prepare()` 遍历 queue 或 scene resource，上传 pending resources。
- `ForwardPass::execute()` 遍历 draw items 并 draw。
- 拆分 camera uniform 和 object uniform。

验收：

- `ForwardPass` 只组织 draw，不加载文件。
- 多个 draw item 可以使用不同 model matrix。
- descriptor layout 与 shader binding 更新同步。

当前实现状态：

- `Renderer` 已在每帧调用 `RenderQueue::build(renderScene)`，并通过 `FrameContext::queue` 传递给 `FrameRenderer` / `ForwardPass`。
- `ForwardPass::prepare()` 已优先遍历 `RenderQueue` 中的 draw item，上传每个 item 的 mesh/material，并为当前 frame slot 准备 per-draw descriptor resources。
- `ForwardPass::execute()` 已优先遍历 `RenderQueue` 并逐项执行 indexed draw。
- `ForwardPass` 已移除内部 asset path 查找、fixture fallback、单个 `MeshResource` / `MaterialResource` 持有逻辑；空 queue 表示本帧没有 forward draw。
- `CameraUniform` 已拆分为只包含 view/projection。
- 新增 per-draw `ObjectUniform`，保存 model matrix；当前每个 draw item 每个 frame slot 拥有独立 object uniform buffer，避免多 draw 覆写同一个 in-flight uniform。
- `mesh.vert.hlsl` 已同步为 camera/object uniform 分离：

```text
set 0 binding 0: CameraUniformBuffer
set 0 binding 1: SampledImage
set 0 binding 2: Sampler
set 0 binding 3: ObjectUniformBuffer
```

- `ModelResource` 已接入 `RenderScene` / `RenderQueue` 数据结构；默认 sandbox 的外部 scene 为空时，`Renderer` 会使用内部默认 scene 构建 queue。

### 0.9.4 glTF 2.0 多 primitive / 多 material

工作内容：

- `GltfLoader` 支持第一个 mesh 下多个 primitive，或支持多个 mesh 的 primitive 扁平化。
- 支持多个 material 的 baseColorTexture。
- 每个 primitive 保留正确 `materialIndex`。
- 继续不做完整 node hierarchy；如果遇到 node transform，先记录 TODO 或只支持根节点 identity。

验收：

- asset 层输出多个 `MeshPrimitiveData` 和多个 `MaterialData`。
- unsupported feature 行为明确。
- 新增 glTF loader smoke test。

当前实现状态：

- `GltfLoader` 已从“第一个 mesh 的第一个 primitive”扩展为遍历 glTF model 中所有 mesh primitive，并扁平化输出到 `ModelData::meshes`。
- `GltfLoader` 只加载 primitive 实际使用到的 material，并把 glTF 原始 material index remap 到 `ModelData::materials` 的连续索引。
- 每个 `MeshPrimitiveData::materialIndex` 已指向 remap 后的 `ModelData::materials`。
- glTF 2.0 仍要求 POSITION / NORMAL / TEXCOORD_0 / indices，primitive mode 必须是 TRIANGLES。
- material 当前仍要求外部 `baseColorTexture`，不支持 embedded image、data URI image、PBR 参数或 texture cache。
- 已新增 `assets/models/forward_multidraw_fixture.gltf`，包含两个 primitive 和两个 material。
- `tests/gltf_loader_smoke.cpp` 已同时验证单 draw fixture 和 multidraw fixture。

当前限制：

- loader 当前不解析 node hierarchy、node transform、scene selection 或 mesh instance；多个 mesh/primitive 直接扁平化为 model primitives。
- 多个 material 指向同一贴图时，renderer 层仍可能重复创建 texture resource，后续由 ResourceManager 或 texture cache 收口。

### 0.9.5 sandbox 多 draw fixture

工作内容：

- 新增一个多 primitive 或多对象 fixture。
- sandbox 默认 scene 使用 `RenderScene` 添加至少两个 draw item。
- 验证默认 sandbox 通过 scene / queue 路径生成至少两个 draw item。

验收：

- build 通过。
- CTest 通过。
- sandbox smoke 通过。
- 默认画面不再只证明单 draw。

当前实现状态：

- 新增 `assets/models/forward_multidraw_fixture.gltf` 作为默认多 draw fixture。
- `DefaultRenderer` 内部创建 `ModelResource m_DefaultModel` 和 `RenderScene m_DefaultScene`；当 app 传入的 scene 为空时，renderer 使用默认 scene 构建 `RenderQueue`。
- 默认 scene 由 renderer 持有 GPU model resource，避免 sandbox/app 层直接管理 RHI resource 生命周期。
- `ForwardPass` 不再加载 fixture；默认 sandbox 的 draw item 来自 `Renderer -> RenderScene -> RenderQueue`。
- `DefaultRenderer` 析构时先 `waitIdle()`，再释放 `FrameRenderer`、默认 scene 和默认 model，最后释放 `RenderBackend`。

当前限制：

- 默认 scene 目前只放入一个 model instance；多 draw 来自该 model 内部的两个 primitive，而不是多个 transform 不同的 model instance。
- 0.9.5 尚未做可视化断言，只通过 loader test、queue path 和 sandbox smoke 验证闭环。

### 0.9.6 Phase 0.9 收尾

工作内容：

- 更新 `docs/phase/phase09.md` 当前实现状态。
- 记录 remaining TODO。
- 如果用户明确要求，再同步 `docs/codex_handoff.md`。

验收：

- 文档、代码和测试状态一致。
- 不隐藏仍未支持的 glTF / material / descriptor 限制。

当前实现状态：

- `docs/phase/phase09.md` 已同步到 0.9.0-0.9.6 当前状态。
- `docs/codex_handoff.md` 已按用户要求同步到 Phase 0.9 完成后的交接状态。
- Phase 0.9 主线已经从 `ForwardPass` 内部单资源加载，收口为 `RenderScene -> RenderQueue -> ForwardPass` 的多 draw 最小闭环。
- 当前代码仍是待提交状态；最近已推送提交停留在 `0c1f98e 启动 Phase09 场景队列结构`。

验证记录：

```text
cmake --build --preset msvc-vcpkg-local-debug: passed
ctest --preset msvc-vcpkg-local-debug: 8/8 passed
sandbox smoke: passed
```

sandbox smoke 日志确认：

```text
Loaded glTF model: assets/models/forward_multidraw_fixture.gltf (primitives=2, materials=2)
```

剩余限制：

- 默认 multidraw fixture 当前验证一个 model 内两个 primitive，不验证多个 instance 的不同 transform。
- `GltfLoader` 仍不解析 node hierarchy、node transform、scene selection、skin、animation 或 glTF 扩展。
- material/texture cache 尚未实现；多个 material 指向同一贴图时仍可能重复创建 GPU texture resource。
- 当前 per-draw descriptor/object uniform 策略偏简单，后续 draw 数量上升时应评估 push constants、dynamic uniform buffer 或 storage buffer。

## 审核检查点

- `asset/` 仍不依赖 renderer、RHI 或 Vulkan。
- `RenderScene` 保存 scene 语义，不创建 GPU 资源。
- `RenderQueue` 是 draw list，不拥有底层 GPU 资源。
- `ModelResource` 是 renderer 层资源 owner，通过 RHI 创建资源。
- `ForwardPass` 不再负责文件查找或 asset loading。
- upload 仍发生在 `beginRendering()` 前。
- draw 仍发生在 dynamic rendering scope 内。
- 多 draw 时 object uniform 不覆盖 GPU 仍可能读取的数据。
- descriptor layout、descriptor write 和 shader binding 一致。
- 不支持的 glTF feature 明确失败或记录 TODO。
- 不引入 Vulkan 类型到 renderer / asset / app。

## 验证计划

每个子阶段完成后运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

涉及默认渲染路径变化后运行 sandbox smoke：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

建议额外检查：

- 多 draw item 是否都执行了 `drawIndexed()`。
- 每个 draw item 的 model matrix 是否不同且生效。
- material index 是否能正确选择 base color texture。
- staging buffer 是否仍进入 deferred deletion。
- validation layer 是否没有 descriptor / buffer lifetime 错误。

## 完成标准

Phase 0.9 完成时应满足：

- `RenderScene` 可以保存多个 model instance。
- `RenderQueue` 可以从 scene 生成多个 draw item。
- `ModelResource` 可以从 `ModelData` 创建多个 mesh/material GPU resource。
- `ForwardPass` 可以遍历 queue 绘制多个 draw item。
- per-draw object uniform 支持不同 transform；默认 multidraw fixture 当前验证多 primitive draw，暂不验证多 instance transform。
- glTF 2.0 loader 支持多 primitive / 多 material 的最小路径。
- 默认 sandbox 使用 scene / queue 路径，不再依赖 `ForwardPass` 内部 fixture loading。
- build、CTest 和 sandbox smoke 通过。

## 后续 Phase 建议

Phase 1.0 或后续阶段可以考虑：

- 更完整的 glTF node hierarchy 和 transform。
- 最小 ResourceManager handle 系统。
- material / texture cache。
- sRGB、mipmap 和 texture color space。
- PBR 参数与基础光照。
- RenderGraph 第一版 pass/resource 声明。
- push constants 或 dynamic uniform buffer。
- frustum culling、sorting、batching。

Phase 0.9 完成后，下一阶段仍不建议直接进入完整 PBR 或完整 RenderGraph；更稳妥的顺序是先补齐 glTF transform、资源缓存、texture color space 和更系统的资源销毁策略。
