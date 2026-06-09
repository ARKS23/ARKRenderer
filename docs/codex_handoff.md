# Codex Handoff Summary

更新时间：2026-06-09

## 1. 当前状态

ARKRenderer 当前推进到 Phase 0.9 收尾状态。默认渲染主线已经从 Phase 0.8 的 `ForwardPass` 内部单资源加载，迁移为 renderer 层的最小 scene / queue / multi draw 闭环：

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
            -> per-frame camera uniform
            -> per-draw object uniform
            -> sampled image + sampler descriptor
            -> mesh pipeline
            -> indexed textured mesh draw(s)
        -> endRendering()
        -> Present
```

当前默认 sandbox 路径：

```text
assets/models/forward_multidraw_fixture.gltf
assets/textures/xiaowei.png
shaders/mesh.vert.hlsl
shaders/mesh.frag.hlsl
```

`ForwardPass` 现在只消费 `RenderQueue`，不再负责文件查找、glTF 加载或 fallback mesh/material。app 传入的 scene 为空时，`DefaultRenderer` 会使用内部默认 `ModelResource + RenderScene`，加载 `forward_multidraw_fixture.gltf` 来验证默认多 draw 路径。

## 2. 最近提交与当前工作区

最近已推送提交：

```text
0c1f98e 启动 Phase09 场景队列结构
1d2134b 完成 Phase08 glTF ForwardPass 闭环
1dae07f 启动 Phase08 mesh 资源闭环
d9b80af 实现 DescriptorManager 可增长 pool
5d7cea8 接入 Phase07 真实纹理资源
```

当前 Phase 0.9.2-0.9.6 相关改动仍处于待提交状态。接手时先执行：

```powershell
git status -sb
git diff --stat
git log --oneline -n 5
```

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
- `ForwardPass` 曾完成单 mesh / 单 material 的 indexed textured draw 闭环。
- `GltfLoader` 建立 glTF 2.0 最小加载路径，图片像素仍交给 `TextureLoader`。

### Phase 0.9

- `RenderScene` 支持 model 级 `SceneModel` 和 primitive 级 `SceneObject`。
- `RenderQueue` 从 scene 生成 flat `DrawItem` list，并展开 `ModelResource` primitives。
- `ModelResource` 从 `asset::ModelData` 创建多个 `MeshResource` 和多个 `MaterialResource`。
- `ForwardPass` 消费 `RenderQueue`，在 `prepare()` 上传 mesh/material 并准备 per-draw descriptor resources，在 `execute()` 遍历 draw items 执行 indexed draw。
- `CameraUniform` 与 `ObjectUniform` 已拆分：

```text
set 0 binding 0: CameraUniformBuffer
set 0 binding 1: SampledImage
set 0 binding 2: Sampler
set 0 binding 3: ObjectUniformBuffer
```

- 每个 draw item 每个 frame slot 使用独立 object uniform buffer，避免覆盖 GPU 仍可能读取的数据。
- `mesh.vert.hlsl` 已同步为 camera/object uniform 分离。
- `GltfLoader` 支持遍历 glTF model 中所有 mesh primitive，并扁平化输出到 `ModelData::meshes`。
- `GltfLoader` 只加载被 primitive 实际使用到的 material，并把 glTF 原始 material index remap 到 `ModelData::materials` 连续索引。
- 新增 `assets/models/forward_multidraw_fixture.gltf`，包含两个 primitive 和两个 material。
- 默认 sandbox 使用 scene / queue 路径，不再依赖 `ForwardPass` 内部 fixture loading。

## 4. 关键代码阅读顺序

建议按以下顺序审核当前 Phase 0.9 闭环：

1. `docs/phase/phase09.md`
   - 先确认 Phase 0.9 目标、非目标、当前限制和完成标准。
2. `src/asset/MeshData.h`
   - 看 CPU mesh/material/model 数据形态。
3. `src/asset/GltfLoader.h/.cpp`
   - 看 glTF 2.0 多 primitive / 多 material 如何转换为 `ModelData`。
4. `src/renderer/ModelResource.h/.cpp`
   - 看 CPU `ModelData` 如何创建多个 renderer/RHI mesh/material resource。
5. `src/renderer/RenderScene.h/.cpp`
   - 看 scene 如何保存 model instance / primitive object 引用。
6. `src/renderer/RenderQueue.h/.cpp`
   - 看 scene 如何展开成 draw item list。
7. `src/renderer/Renderer.cpp`
   - 看每帧如何选择外部 scene 或默认 scene，并构建 `RenderQueue`。
8. `src/renderer/passes/ForwardPass.h/.cpp`
   - 看 queue draw、per-draw descriptor、object uniform、pipeline 和 draw 调用。
9. `src/renderer/FrameRenderer.cpp`
   - 确认 `prepare()` 仍在 `beginRendering()` 前，upload 不进入 dynamic rendering scope。
10. `src/rhi/vulkan/VulkanCommandContext.cpp`
    - 复查 buffer / texture upload barrier 和 copy 命令。
11. `src/rhi/vulkan/VulkanDescriptorManager.cpp`
    - 复查 growable descriptor pool 分配路径。

## 5. 必须继续遵守的架构边界

后续开发前继续阅读：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase07.md
docs/phase/phase08.md
docs/phase/phase09.md
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

- `GltfLoader` 仍不解析 node hierarchy、node transform、scene selection、mesh instance、skin、animation 或 glTF 扩展。
- 默认 multidraw fixture 当前验证一个 model 内两个 primitive，不验证多个 instance 的不同 transform。
- material / texture cache 尚未实现；多个 material 指向同一贴图时仍可能重复创建 GPU texture resource。
- texture path 当前要求外部 image URI；embedded image / data URI image 暂不支持。
- material 当前仍是最小 base color texture resource，不支持 PBR 参数。
- texture upload 仍主要覆盖 RGBA8、mip0、array layer 0 路径；mipmap、sRGB、HDR、压缩纹理未设计完成。
- per-draw descriptor/object uniform 策略简单直接，draw 数量上升时应评估 push constants、dynamic uniform buffer 或 storage buffer。

P1：

- `CubePass` 仍保留为阶段性对照/debug pass；后续应决定保留、隐藏还是清理。
- deferred deletion 当前主要覆盖 upload staging buffer；完整 GPU object deferred destruction 尚未系统化。
- `VulkanDescriptorManager` 有 growable pools，但尚无 free/reset 策略和容量统计。
- shader binding 与 descriptor layout 仍靠人工一致，后续可考虑 reflection 或测试校验。
- `Renderer` 内部默认 scene 是 sandbox 过渡方案；真正的资源/场景加载入口仍需后续设计。

P2：

- Vulkan debug object names。
- RenderDoc capture / screenshot / pixel smoke。
- ResourceManager handle 系统。
- RenderGraph 第一版 pass/resource 声明。
- PBR、IBL、tone mapping。

## 7. 最近验证记录

Phase 0.9 收尾前已运行：

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
Loaded glTF model: assets/models/forward_multidraw_fixture.gltf (primitives=2, materials=2)
```

## 8. 推荐下一步

建议下一阶段不要直接进入完整 PBR / RenderGraph / bindless。优先考虑：

1. glTF node hierarchy、node transform 和 scene selection 的最小支持。
2. 多 model instance 的默认 sandbox fixture，验证不同 transform 的多 draw。
3. 最小 ResourceManager / texture cache，避免重复加载同一贴图。
4. texture color space 策略：RGBA8 Unorm 与 sRGB 的边界。
5. mipmap 设计或至少补齐 mipmap 非目标/限制记录。
6. 更系统的 GPU object deferred destruction。
7. descriptor layout / shader binding 的校验机制。

## 9. 下一次 Codex 启动提示

```text
请先阅读 docs/codex_handoff.md，理解 ARKRenderer 当前 Phase 0.9 完成状态、默认 scene / queue / ForwardPass 渲染路径、glTF 2.0 多 primitive / 多 material 最小加载范围、已知风险和后续建议。

然后阅读：
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase07.md
docs/phase/phase08.md
docs/phase/phase09.md

当前默认渲染路径已经是 Vulkan Dynamic Rendering + ClearPass + ForwardPass + RenderScene + RenderQueue + ModelResource + MeshResource + MaterialResource + per-frame camera uniform + per-draw object uniform + sampled image + sampler + indexed textured multi draw + depth attachment。

不要重复 Phase 0.5 / 0.6 / 0.7 / 0.8 / 0.9 已完成工作。

下一步优先考虑 glTF node hierarchy / transform / scene selection、ResourceManager / texture cache、texture color space、mipmap 策略和更完整的 deferred destruction。不要提前引入完整 RenderGraph、bindless、复杂 PBR 或 glTF 扩展。

如果实现方向与既有设计文档冲突，先说明并更新设计文档，再修改代码。新增代码保持现有风格：左大括号不换行，namespace 内缩进，日志输出用英文，必要注释用简洁中文。不确定的地方写 TODO 或记录到文档，不要假装完成。

先执行并查看：
git status -sb
git diff --stat
git log --oneline -n 5
```
