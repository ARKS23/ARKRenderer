# Codex Handoff Summary

更新时间：2026-06-09

## 1. 当前状态

ARKRenderer 当前已经完成 Phase 0.10。默认渲染主线已经从早期 pass 内部硬编码资源，推进到 renderer 层的 glTF scene/node transform、RenderScene、RenderQueue 和 ForwardPass 闭环：

```text
Vulkan Dynamic Rendering
    -> Renderer
        -> RenderScene
        -> RenderQueue
    -> FrameRenderer
        -> prepare() upload stage
            -> MeshResource GPU-only vertex/index upload
            -> MaterialResource RGBA8 texture upload
        -> beginRendering(color + depth)
        -> ClearPass
        -> ForwardPass
            -> RenderView camera uniform
            -> per-draw object uniform
            -> sampled image + sampler descriptor
            -> mesh pipeline
            -> indexed textured mesh draw(s)
        -> endRendering()
        -> Present
```

当前默认 sandbox 资产路径：

```text
assets/models/forward_multinode_fixture.gltf
assets/textures/xiaowei.png
shaders/mesh.vert.hlsl
shaders/mesh.frag.hlsl
```

`ForwardPass` 现在只消费 `RenderQueue`，不负责文件查找、glTF 加载或 fallback mesh/material。app 传入的 scene 为空时，`DefaultRenderer` 会使用内部默认 `ModelResource + RenderScene`，加载 `forward_multinode_fixture.gltf` 验证默认多 node / 多 instance draw 路径。

## 2. 最近提交与工作区

最近已推送提交：

```text
a763cb0 完成 Phase09 多 draw 场景队列闭环
0c1f98e 启动 Phase09 场景队列结构
1d2134b 完成 Phase08 glTF ForwardPass 闭环
1dae07f 启动 Phase08 mesh 资源闭环
d9b80af 实现 DescriptorManager 可增长 pool
```

当前 Phase 0.10 相关改动仍在工作区，尚未提交。接手时先执行：

```powershell
git status -sb
git diff --stat
git log --oneline -n 5
```

注意：

- `assets/models/DamagedHelmet/` 当前是用户放入的未跟踪真实模型资产，本次 Phase 0.10 没有纳入默认 fixture 或测试。
- `assets/models/forward_multinode_fixture.gltf` 是 Phase 0.10 新增的可控测试 fixture，应纳入提交。

## 3. 已完成能力

### Phase 0.7

- `asset::ImageData` 和 `TextureLoader` 使用 `stb_image` 加载 LDR RGBA8。
- HDR 输入显式失败，不静默量化到 RGBA8。
- `TextureUploadDesc` 补齐 row pitch、slice pitch、offset、mip、array layer 等字段。
- `BufferUploadDesc` 和 `DeviceContext::uploadBufferData()` 已落地。
- GPU-only vertex/index buffer 通过 staging buffer 上传。
- texture upload 使用 `Undefined -> CopyDst -> ShaderResource`。
- staging buffer 通过 frame-local deferred deletion 延迟释放。
- `VulkanDescriptorManager` 支持可增长 descriptor pool。
- CMake post-build 会复制 `assets/` 到 sandbox 输出目录。

### Phase 0.8

- `asset::MeshVertex`、`MeshPrimitiveData`、`MaterialData`、`ModelData` 已建立。
- `MeshResource` 负责从 CPU mesh 创建 GPU-only vertex/index buffer 和 staging upload。
- `MaterialResource` 负责从 CPU material 创建 texture/view/sampler/staging，并写入 sampled image/sampler descriptor。
- `ForwardPass` 完成 textured mesh indexed draw 闭环。
- `GltfLoader` 建立 glTF 2.0 最小加载路径，图片像素仍交给 `TextureLoader`。

### Phase 0.9

- `RenderScene` 支持 model 级 `SceneModel` 和 primitive 级 `SceneObject`。
- `RenderQueue` 从 scene 生成 flat `DrawItem` list，并展开 `ModelResource` primitives。
- `ModelResource` 从 `asset::ModelData` 创建多个 `MeshResource` 和多个 `MaterialResource`。
- `ForwardPass` 消费 `RenderQueue`：`prepare()` 上传 mesh/material 并准备 per-draw descriptor resources，`execute()` 遍历 draw items 执行 indexed draw。
- `CameraUniform` 和 `ObjectUniform` 已拆分：

```text
set 0 binding 0: CameraUniformBuffer
set 0 binding 1: SampledImage
set 0 binding 2: Sampler
set 0 binding 3: ObjectUniformBuffer
```

- 每个 draw item 每个 frame slot 使用独立 object uniform buffer，避免覆盖 GPU 仍可能读取的数据。
- `mesh.vert.hlsl` 已同步为 camera/object uniform 分离。
- `GltfLoader` 支持遍历 glTF model 中所有 mesh primitive，并扁平化输出到 `ModelData::meshes`。
- `GltfLoader` 只加载被 primitive 实际使用到的 material，并把 glTF 原始 material index remap 到 `ModelData::materials` 的连续索引。
- `assets/models/forward_multidraw_fixture.gltf` 保留为 Phase 0.9 多 primitive / 多 material 对照 fixture。

### Phase 0.10

- `asset::ModelData` 新增 `instances`，用于表达 glTF node 对 primitive 的实例化。
- 新增 `asset::TransformData`，使用 column-major `float[16]` 保存 CPU 侧 transform，asset 层仍不依赖 renderer/RHI/Vulkan。
- 新增 `asset::MeshPrimitiveInstanceData`，用 `meshIndex` 指向 `ModelData::meshes`。
- `GltfLoader` 支持 glTF 2.0 default scene；没有 default scene 时使用第一个 scene。
- `GltfLoader` 递归遍历 scene root nodes。
- `GltfLoader` 支持 node `matrix` 和 TRS，TRS 按 `T * R * S` 组合。
- `GltfLoader` 支持 node 引用 mesh，并为 mesh 内每个 primitive 生成 primitive instance。
- `ModelResource` 新增 `ModelPrimitiveInstance` 和 `instances()`。
- `ModelResource::create()` 将 `asset::MeshPrimitiveInstanceData` 转换为 renderer 层 `glm::mat4`。
- 当 `ModelData::instances` 为空时，`ModelResource` 会为每个 primitive 生成 identity instance，兼容旧数据路径。
- `RenderQueue::build()` 已改为展开 `ModelResource::instances()`。
- `DrawItem::modelMatrix` 使用 `sceneModel.transform * instance.localTransform`。
- `RenderView` 已补齐 view/projection matrix。
- `RenderView::setDefaultPerspective()` 提供默认 camera。
- `Application` 初始化和 resize 时会更新默认 `RenderView`。
- `ForwardPass::makeCameraUniform()` 已改为从 `FrameContext::view` 读取 view/projection，不再硬编码 camera。
- 新增 `assets/models/forward_multinode_fixture.gltf`。
- 默认 sandbox model 已切换为 `assets/models/forward_multinode_fixture.gltf`。
- `gltf_loader_smoke` 已验证 forward fixture、multidraw fixture 和 multinode fixture。
- `model_resource_smoke` 已验证 scene transform 与 local transform 的组合结果。
- `docs/phase/phase10.md` 已同步到 0.10.6 完成状态。

## 4. 关键代码阅读顺序

建议按以下顺序审核当前 Phase 0.10 闭环：

1. `docs/phase/phase10.md`
   - 确认 Phase 0.10 完成状态、限制和后续 TODO。
2. `src/asset/MeshData.h`
   - 看 `TransformData`、`MeshPrimitiveInstanceData`、`ModelData::instances`。
3. `src/asset/GltfLoader.h/.cpp`
   - 看 glTF 2.0 scene/defaultScene、node hierarchy、matrix/TRS 和 instance 生成。
4. `src/renderer/ModelResource.h/.cpp`
   - 看 CPU `ModelData` 如何创建 mesh/material resource，并保存 primitive instances。
5. `src/renderer/RenderQueue.h/.cpp`
   - 看 scene model transform 与 model-local instance transform 如何组合成 `DrawItem::modelMatrix`。
6. `src/renderer/RenderView.h`
   - 看最小 camera view/projection 输入。
7. `src/app/Application.cpp`
   - 看默认 `RenderView` 如何初始化并在 resize 后更新 aspect。
8. `src/renderer/Renderer.cpp`
   - 看默认 scene 如何加载 `forward_multinode_fixture.gltf`。
9. `src/renderer/passes/ForwardPass.h/.cpp`
   - 看 queue draw、per-draw descriptor、camera/object uniform、pipeline 和 draw 调用。
10. `src/renderer/FrameRenderer.cpp`
    - 确认 `prepare()` 仍在 `beginRendering()` 前，upload 不进入 dynamic rendering scope。
11. `src/rhi/vulkan/VulkanCommandContext.cpp`
    - 复查 buffer / texture upload barrier 和 copy 命令。

## 5. 必须继续遵守的架构边界

后续开发前继续阅读：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase07.md
docs/phase/phase08.md
docs/phase/phase09.md
docs/phase/phase10.md
```

硬性边界：

- 只有 `src/rhi/vulkan/` 可以包含 Vulkan 头文件和 `Vk*` 类型。
- `asset/` 只解析外部文件并输出 CPU 数据，不创建 RHI/GPU 资源。
- `renderer/` 可以创建和持有 RHI 资源，但不能接触 Vulkan 类型。
- `RenderScene` 保存 scene 语义，不创建 GPU 资源。
- `RenderQueue` 是 draw list，不拥有底层 GPU 资源。
- `ModelResource` 是 renderer 层 GPU resource owner，通过 RHI 创建资源。
- `ForwardPass` 只负责 pipeline、descriptor、per-frame/per-draw binding 和 draw，不负责 asset loading。
- upload 命令必须记录在 dynamic rendering scope 外。
- 当前继续使用 Vulkan Dynamic Rendering，不引入传统 `VkRenderPass` / `VkFramebuffer`。
- 日志输出使用英文。
- 必要注释使用简洁中文。

## 6. 已知风险和 TODO

P0 / 下一阶段优先：

- material / texture cache 尚未实现；多个 material 指向同一贴图时仍可能重复创建 GPU texture resource。
- baseColor texture 当前仍使用 `RGBA8Unorm`，sRGB 边界尚未落地。
- texture upload 仍主要覆盖 RGBA8、mip0、array layer 0 路径；mipmap、HDR、压缩纹理未设计完成。
- deferred deletion 当前主要覆盖 upload staging buffer；完整 GPU object deferred destruction 尚未系统化。
- `GltfLoader` 仍不支持 skin、animation、morph target、glTF extensions、embedded image、data URI image。
- `RenderView` 仍是最小 camera 数据容器，还没有 camera controller、scene camera 或 app 级 camera API。

P1：

- `CubePass` 仍保留为阶段性对照/debug pass；后续应决定保留、隐藏还是清理。
- `VulkanDescriptorManager` 有 growable pools，但尚无 free/reset 策略和容量统计。
- shader binding 与 descriptor layout 仍靠人工一致，后续可考虑 reflection 或测试校验。
- `Renderer` 内部默认 scene 是 sandbox 过渡方案；真正的资源/场景加载入口仍需后续设计。
- per-draw descriptor/object uniform 策略简单直接，draw 数量上升时应评估 push constants、dynamic uniform buffer 或 storage buffer。

P2：

- Vulkan debug object names。
- RenderDoc capture / screenshot / pixel smoke。
- ResourceManager handle 系统。
- RenderGraph 第一版 pass/resource 声明。
- PBR、IBL、tone mapping。

## 7. 最近验证记录

Phase 0.10 收尾时已运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

结果：

```text
build passed
CTest: 8/8 passed
```

sandbox smoke 通过。日志确认：

```text
Loaded glTF model: assets/models/forward_multinode_fixture.gltf (primitives=1, materials=1, instances=2)
```

0.10.6 本身只同步文档；上述验证来自 0.10.0-0.10.5 实现完成后的验证记录。

## 8. 推荐下一步

建议下一阶段不要直接进入完整 PBR / RenderGraph / bindless。优先考虑：

1. 最小 `TextureResource` / `TextureCache`，避免重复加载同一 texture。
2. texture color space：`RGBA8Unorm` 与 `RGBA8Srgb` 的边界。
3. mipmap upload / generation 策略。
4. 更完整的 GPU object deferred destruction。
5. 真正的 renderer 资源/场景加载入口，替代内部默认 scene 过渡方案。
6. PBR material 参数与基础光照。

## 9. 下一次 Codex 启动提示

```text
请先阅读 docs/codex_handoff.md，理解 ARKRenderer 当前 Phase 0.10 完成状态、默认 scene / queue / ForwardPass 渲染路径、glTF 2.0 scene/node transform 最小加载范围、RenderView camera、已知风险和后续建议。

然后阅读：
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase07.md
docs/phase/phase08.md
docs/phase/phase09.md
docs/phase/phase10.md

当前默认渲染路径已经是 Vulkan Dynamic Rendering + ClearPass + ForwardPass + RenderScene + RenderQueue + ModelResource + MeshResource + MaterialResource + glTF scene/node primitive instances + RenderView camera uniform + per-draw object uniform + sampled image + sampler + indexed textured multi draw + depth attachment。

不要重复 Phase 0.5 / 0.6 / 0.7 / 0.8 / 0.9 / 0.10 已完成工作。

下一步优先考虑 TextureResource / TextureCache、texture color space、mipmap 策略、更完整的 GPU object deferred destruction 和真正的资源/场景加载入口。不要提前引入完整 RenderGraph、bindless、复杂 PBR 或 glTF 扩展。

如果实现方向与既有设计文档冲突，先说明并更新设计文档，再修改代码。新增代码保持现有风格：左大括号不换行，namespace 内缩进，日志输出用英文，必要注释用简洁中文。不确定的地方写 TODO 或记录到文档，不要假装完成。

先执行并查看：
git status -sb
git diff --stat
git log --oneline -n 5
```
