# Codex Handoff Summary

更新时间：2026-06-09

## 1. 当前状态

ARKRenderer 当前已经完成 Phase 0.14。默认渲染主线保持 Vulkan Dynamic Rendering + `RenderScene` / `RenderQueue` / `ForwardPass` 多 draw 闭环；renderer 层资源生命周期已经推进到 model 级 deferred reset；texture sampling 已具备 2D RGBA8 / RGBA8 sRGB GPU mipmap generation 最小闭环。

```text
Vulkan Dynamic Rendering
    -> Renderer
        -> RenderScene
        -> RenderQueue
    -> FrameRenderer
        -> prepare() upload stage
            -> MeshResource GPU-only vertex/index upload
            -> MaterialResource
                -> TextureResource RGBA8/sRGB texture upload
                -> TextureResource GPU mipmap generation
                -> TextureCache path + colorSpace reuse
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

当前测试 fixture：

```text
assets/models/forward_multidraw_fixture.gltf
assets/models/forward_multinode_fixture.gltf
assets/models/texture_cache_fixture.gltf
```

`ForwardPass` 仍只消费 `RenderQueue`，不负责文件查找、glTF 加载、texture cache 或 fallback mesh/material。app 传入的 scene 为空时，`DefaultRenderer` 使用内部默认 `ModelResource + RenderScene` 加载 `forward_multinode_fixture.gltf`，验证默认多 node / 多 instance draw 路径。

## 2. 最近提交与工作区

最近已推送提交：

```text
1ffcd64 启动 Phase14 mip 描述闭环
52e831a 完成 Phase13 model deferred reset 闭环
3dab941 完成 Phase12 deferred destruction 闭环
a99a595 完成 Phase11 texture resource cache 闭环
214b081 完成 Phase10 glTF scene transform 闭环
```

当前 Phase 0.14.2 ~ 0.14.6 相关改动在工作区，等待提交：

```text
docs/phase/phase14.md
docs/codex_handoff.md
src/rhi/DeviceContext.h
src/rhi/vulkan/VulkanCommandContext.h/.cpp
src/renderer/TextureResource.h/.cpp
tests/model_resource_smoke.cpp
```

注意：

- `.gitignore` 当前有用户侧未提交改动：`assets/models/DamagedHelmet/`。
- `assets/models/DamagedHelmet/` 是用户放入的真实模型资产，不要在未明确要求时纳入提交。
- 接手时先执行：

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
- `MaterialResource` 最初完成 textured mesh indexed draw 所需 descriptor 写入。
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

### Phase 0.10

- `asset::ModelData` 新增 `instances`，用于表达 glTF node 对 primitive 的实例化。
- 新增 `asset::TransformData`，使用 column-major `float[16]` 保存 CPU 侧 transform，asset 层仍不依赖 renderer/RHI/Vulkan。
- `GltfLoader` 支持 glTF 2.0 default scene；没有 default scene 时使用第一个 scene。
- `GltfLoader` 递归遍历 scene root nodes。
- `GltfLoader` 支持 node `matrix` 和 TRS，TRS 按 `T * R * S` 组合。
- `GltfLoader` 支持 node 引用 mesh，并为 mesh 内每个 primitive 生成 primitive instance。
- `ModelResource` 新增 `ModelPrimitiveInstance` 和 `instances()`。
- `RenderQueue::build()` 已改为展开 `ModelResource::instances()`。
- `DrawItem::modelMatrix` 使用 `sceneModel.transform * instance.localTransform`。
- `RenderView` 已补齐 view/projection matrix。
- `ForwardPass::makeCameraUniform()` 已改为从 `FrameContext::view` 读取 view/projection，不再硬编码 camera。
- 默认 sandbox model 已切换为 `assets/models/forward_multinode_fixture.gltf`。

### Phase 0.11

- `rhi::Format` 新增 `RGBA8Srgb`。
- Vulkan 后端已补充 `RGBA8Srgb` 的 format name、`toVkFormat()` 和 `fromVkFormat()` 映射。
- `VulkanCommandContext::uploadTextureData()` 已允许 `RGBA8Unorm` 和 `RGBA8Srgb` 两种 GPU texture format，CPU upload 字节布局仍保持 RGBA8。
- 新增 `renderer::TextureResource`，接管 texture/view/sampler/staging buffer 和首次 upload 状态。
- 新增 `renderer::TextureCache`，使用规范化 path + `TextureColorSpace` 作为 key。
- glTF baseColor texture 默认通过 `TextureColorSpace::Srgb` 创建为 `RGBA8Srgb` RHI texture。
- `MaterialResource` 不再直接调用 `TextureLoader`。
- `MaterialResource` 不再拥有 texture/view/sampler/staging buffer，只保存 `TextureResource*` 引用并负责 descriptor 写入。
- `ModelResource` 新增 `create(device, textureCache, model)` 重载。
- `ModelResource` 旧 `create(device, model)` 保留，并使用内部 local `TextureCache` 兼容现有路径。
- `texture_cache_fixture.gltf` 验证两个 material 指向同一 image 时只创建一个 sRGB texture/view/sampler。

### Phase 0.12

- `rhi::DeviceContext` 新增 `deferReleaseTexture()`、`deferReleaseTextureView()`、`deferReleaseSampler()`。
- `VulkanDeletionQueue` 已扩展 buffer / texture view / sampler / texture 队列。
- `VulkanCommandContext` 已实现 texture/view/sampler deferred release，并要求 active command recording 且不在 dynamic rendering scope 内。
- `TextureResource` 新增 `releaseDeferred(context)` 和 `resetImmediate()`。
- `TextureCache` 新增 `clearDeferred(context)`，`clear()` 保留为 shutdown / GPU idle immediate clear。
- `ark_model_resource_smoke` 覆盖 texture resource deferred release 和 texture cache deferred clear。

### Phase 0.13

- 新增 `docs/phase/phase13.md`。
- `MeshResource` 新增 `releaseDeferred(context)`，覆盖 vertex/index GPU buffer 和可能残留的 staging buffer。
- `MeshResource` 新增 `resetImmediate()`。
- `ModelResource` 新增 `resetDeferred(context)`，用于运行期 model unload / replacement。
- `ModelResource` 通过 `m_UsesExternalTextureCache` 区分 local texture cache 和 external texture cache。
- local cache 路径会在 model deferred reset 中调用 `m_LocalTextureCache.clearDeferred(context)`。
- external cache 路径不会由 model 自动清理，仍由外部拥有者负责。
- `ModelResource::reset()` 保留为 shutdown / GPU idle immediate path。
- `ark_model_resource_smoke` 已覆盖 mesh deferred release、local model deferred reset、external cache model deferred reset。

### Phase 0.14

- 新增 `docs/phase/phase14.md`。
- `rhi::calculateMipLevelCount(extent)` 已落地，支持非 2 次幂尺寸。
- `TextureResourceDesc::generateMips` 已落地，默认开启 mip chain。
- `TextureResource` 创建 texture 时会按 image 尺寸计算 mipLevels。
- 多 mip texture usage 包含 `TransferSrc | TransferDst | ShaderResource`。
- texture view 覆盖完整 mip range。
- sampler 在多 mip texture 上使用 linear mip filtering。
- `DeviceContext::generateTextureMips(Texture&)` 已落地。
- `VulkanCommandContext::generateTextureMips()` 已实现 2D RGBA8 / RGBA8 sRGB GPU blit mip generation。
- Vulkan mip generation 检查 active command recording、dynamic rendering scope、usage、format blit src/dst 和 linear filter capability。
- `TextureResource::upload()` 已在 mip0 upload 后按需调用 mip generation。
- upload / mip generation / staging deferred release 仍发生在 dynamic rendering scope 外。
- `ark_model_resource_smoke` 已覆盖 mip count、full mip texture/view/sampler 描述和 mip generation 调用次数。

## 4. 关键代码阅读顺序

建议按以下顺序审核当前 Phase 0.14 闭环：

1. `docs/phase/phase14.md`
   - 确认 mipmap generation 范围、非目标、剩余限制和验证记录。
2. `src/rhi/Texture.h`
   - 看 `calculateMipLevelCount()` 和 `TextureDesc::mipLevels`。
3. `src/rhi/DeviceContext.h`
   - 看 `uploadTextureData()` 与 `generateTextureMips()` 的分工。
4. `src/rhi/vulkan/VulkanCommandContext.cpp`
   - 看 upload mip0、多 mip layout 保持 CopyDst、`generateTextureMips()` per-mip barrier + blit。
5. `src/renderer/TextureResource.h/.cpp`
   - 看 `generateMips`、full mip view、sampler mip filter、upload 后生成 mip、staging deferred release。
6. `src/renderer/TextureCache.h/.cpp`
   - 确认 cache key 仍是 path + colorSpace，没有提前纳入 sampler/mip 策略。
7. `src/renderer/material/MaterialResource.h/.cpp`
   - 确认 material 仍只引用 `TextureResource*` 并写 sampled image / sampler descriptor。
8. `src/renderer/passes/ForwardPass.h/.cpp`
   - 确认 `prepare()` 仍触发 upload，`execute()` 只 draw。
9. `tests/model_resource_smoke.cpp`
   - 看 mip count、mip generation 统计、texture cache / model resource 不回退。
10. `src/renderer/FrameRenderer.cpp`
    - 确认 `prepare()` 仍在 `beginRendering()` 前，upload/mip generation/release 不进入 dynamic rendering scope。

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
docs/phase/phase11.md
docs/phase/phase12.md
docs/phase/phase13.md
docs/phase/phase14.md
```

硬性边界：

- 只有 `src/rhi/vulkan/` 可以包含 Vulkan 头文件和 `Vk*` 类型。
- `asset/` 只解析外部文件并输出 CPU 数据，不创建 RHI/GPU 资源。
- `TextureLoader` 只输出 CPU `ImageData`，不决定 GPU sRGB/linear 采样语义，也不参与 mip generation。
- `renderer/` 可以创建和持有 RHI 资源，但不能接触 Vulkan 类型。
- `RenderScene` 保存 scene 语义，不创建 GPU 资源。
- `RenderQueue` 是 draw list，不拥有底层 GPU 资源。
- `ModelResource` 是 renderer 层 GPU resource owner，通过 RHI 创建资源。
- local texture cache 可由 `ModelResource` 管理；external texture cache 必须由外部拥有者管理。
- `MaterialResource` 只保存 material 语义和 texture 引用，并负责 descriptor 写入。
- `ForwardPass` 只负责 pipeline、descriptor、per-frame/per-draw binding 和 draw，不负责 asset loading、cache 或 resource lifetime。
- upload / mip generation / deferred release 命令必须记录在 dynamic rendering scope 外。
- 当前继续使用 Vulkan Dynamic Rendering，不引入传统 `VkRenderPass` / `VkFramebuffer`。
- 日志输出使用英文。
- 必要注释使用简洁中文。

## 6. 已知风险和 TODO

P0 / 下一阶段优先：

- glTF material 数据仍只有 baseColor texture path；缺少 baseColorFactor、metallicFactor、roughnessFactor。
- normal / metallicRoughness / occlusion / emissive texture 仍未接入。
- 当前 shader 仍是 baseColor-only，没有基础光照或 PBR。
- `Renderer` 内部默认 scene 是 sandbox 过渡方案；真正 renderer 级资源/场景加载入口仍未设计。
- `TextureCache` 已可复用同一路径同一 colorSpace 的 texture，但还没有引用计数、统计、LRU 或全局 ResourceManager handle。
- external texture cache 的真实 unload 时机仍由外部拥有者控制。
- descriptor set / descriptor layout / pipeline / shader module 的 deferred destruction 仍未纳入。

P1：

- texture upload 仍只覆盖 RGBA8 / RGBA8 sRGB、mip0 upload、array layer 0、tightly packed rows。
- mip generation 只支持 2D color texture、single array layer、GPU blit；不支持离线 mip、array/cubemap、HDR 或压缩纹理。
- non-color texture 的 linear/Unorm 语义还没有材质数据结构支撑。
- glTF sampler 参数、mip bias、anisotropy 和 texture transform 未接入。
- `GltfLoader` 仍不支持 skin、animation、morph target、glTF extensions、embedded image、data URI image。
- shader binding 与 descriptor layout 仍靠人工一致，后续可考虑 reflection 或测试校验。
- `CubePass` 仍保留为阶段性对照/debug pass；后续应决定保留、隐藏还是清理。
- `VulkanDescriptorManager` 有 growable pools，但尚无 free/reset 策略和容量统计。
- `RenderView` 仍是最小 camera 数据容器，还没有 camera controller、scene camera 或 app 级 camera API。
- per-draw descriptor/object uniform 策略简单直接，draw 数量上升时应评估 push constants、dynamic uniform buffer 或 storage buffer。

P2：

- Vulkan debug object names。
- RenderDoc capture / screenshot / pixel smoke。
- ResourceManager handle 系统。
- RenderGraph 第一版 pass/resource 声明。
- PBR、IBL、tone mapping。

## 7. 最近验证记录

Phase 0.14 0.14.2 ~ 0.14.6 已运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

结果：

```text
build passed
CTest: 8/8 passed
```

sandbox smoke 通过。日志确认默认 fixture 仍正常加载：

```text
Loaded glTF model: assets/models/forward_multinode_fixture.gltf (primitives=1, materials=1, instances=2)
Renderer initialized
```

## 8. 推荐下一步

建议下一阶段不要直接进入完整 RenderGraph / bindless。优先考虑：

1. glTF material 数据扩展：baseColorFactor、metallicFactor、roughnessFactor。
2. glTF normal / metallicRoughness / emissive texture 支持，并明确 color / non-color texture 语义。
3. 最小基础光照或 PBR shader。
4. 使用 `assets/models/DamagedHelmet/` 做真实 glTF 2.0 资产验证。
5. 真正的 renderer 资源/场景加载入口，替代内部默认 scene 过渡方案。
6. pipeline / shader / descriptor layout 的 deferred destruction。
7. RenderGraph 第一版 pass/resource 声明。

`assets/models/DamagedHelmet/` 可作为后续真实 glTF 2.0 资产验证对象，但当前 loader/material/shader 还不支持其完整 PBR 纹理语义，不应直接把它作为默认路径。

## 9. 下一次 Codex 启动提示

```text
请先阅读 docs/codex_handoff.md，理解 ARKRenderer 当前 Phase 0.14 完成状态、默认 scene / queue / ForwardPass 渲染路径、glTF 2.0 scene/node transform 最小加载范围、RenderView camera、TextureResource / TextureCache / sRGB / mipmap generation / deferred reset 边界、已知风险和后续建议。

然后阅读：
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase07.md
docs/phase/phase08.md
docs/phase/phase09.md
docs/phase/phase10.md
docs/phase/phase11.md
docs/phase/phase12.md
docs/phase/phase13.md
docs/phase/phase14.md

当前默认渲染路径已经是 Vulkan Dynamic Rendering + ClearPass + ForwardPass + RenderScene + RenderQueue + ModelResource + MeshResource + MaterialResource + TextureResource + TextureCache + glTF scene/node primitive instances + RenderView camera uniform + per-draw object uniform + sampled image + sampler + GPU mipmap generation + indexed textured multi draw + depth attachment。

不要重复 Phase 0.5 / 0.6 / 0.7 / 0.8 / 0.9 / 0.10 / 0.11 / 0.12 / 0.13 / 0.14 已完成工作。

下一步优先考虑 glTF material 数据扩展、glTF normal / metallicRoughness / emissive texture、最小基础光照/PBR 和真正的资源/场景加载入口。不要提前引入完整 RenderGraph、bindless 或复杂 glTF 扩展。

如果实现方向与既有设计文档冲突，先说明并更新设计文档，再修改代码。新增代码保持现有风格：左大括号不换行，namespace 内缩进，日志输出用英文，必要注释用简洁中文。不确定的地方写 TODO 或记录到文档，不要假装完成。

先执行并查看：
git status -sb
git diff --stat
git log --oneline -n 5
```
