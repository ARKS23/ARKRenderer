# Codex Handoff Summary

更新时间：2026-06-10

## 1. 当前状态

ARKRenderer 当前已经完成 Phase 0.18。默认渲染主线保持：

```text
Vulkan Dynamic Rendering
    -> Renderer
        -> RenderScene
        -> RenderQueue
    -> FrameRenderer
        -> prepare() upload stage
            -> MeshResource GPU-only vertex/index upload
            -> TextureResource RGBA8/sRGB upload + GPU mipmap generation
            -> TextureCache path + colorSpace reuse
            -> MaterialResource texture references + material factors
        -> beginRendering(color + depth)
        -> ClearPass
        -> ForwardPass
            -> RenderView camera uniform
            -> per-draw object uniform + normal matrix
            -> per-draw material uniform
            -> lighting uniform
            -> sampled images + samplers
            -> mesh pipeline
            -> indexed textured mesh draw(s)
        -> endRendering()
        -> Present
```

当前默认 sandbox 仍加载：

```text
assets/models/forward_multinode_fixture.gltf
assets/textures/xiaowei.png
shaders/mesh.vert.hlsl
shaders/mesh.frag.hlsl
```

Phase 0.18 新增了真实模型验证入口：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

`assets/models/DamagedHelmet/` 是本地真实模型资产目录，已加入 `.gitignore`，不作为默认资源，也不应在未明确要求时提交。

## 2. 最近提交与工作区

最近已推送提交：

```text
0c49e98 完成 Phase17 shader 光照解释收尾
250f24f 完成 Phase17 tangent 与 lighting uniform 基础
5aa9bda 完成 Phase16 多纹理材质闭环
d392e8c 完成 Phase16 texture slots 基础设施
b7fa9f8 完成 Phase15 glTF material factors 闭环
```

当前待提交工作是 Phase 0.18：

```text
.gitignore
docs/phase/phase18.md
docs/codex_handoff.md
apps/sandbox/main.cpp
shaders/mesh.vert.hlsl
src/app/Application.cpp
src/app/Application.h
src/asset/GltfLoader.cpp
src/asset/MeshData.cpp
src/asset/MeshData.h
src/renderer/Renderer.cpp
src/renderer/Renderer.h
src/renderer/passes/ForwardPass.cpp
tests/framework_headers_smoke.cpp
tests/gltf_loader_smoke.cpp
tests/mesh_data_smoke.cpp
tests/shader_assets_smoke.cpp
```

接手时先执行：

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

- 建立 `MeshVertex`、`MeshPrimitiveData`、`MaterialData`、`ModelData`。
- `MeshResource` 负责 CPU mesh 到 GPU-only vertex/index buffer。
- `MaterialResource` 最初完成 textured mesh indexed draw 所需 descriptor 写入。
- `ForwardPass` 完成 textured mesh indexed draw 闭环。
- `GltfLoader` 建立 glTF 2.0 最小加载路径，图片像素交给 `TextureLoader`。

### Phase 0.9

- `RenderScene` 支持 model 级 `SceneModel` 和 primitive 级 `SceneObject`。
- `RenderQueue` 从 scene 生成 flat `DrawItem` list，并展开 `ModelResource` primitives。
- `ModelResource` 从 `asset::ModelData` 创建多个 `MeshResource` 和 `MaterialResource`。
- `ForwardPass` 消费 `RenderQueue`，`prepare()` 上传 mesh/material 并准备 per-draw descriptor resources。
- `CameraUniform` 和 `ObjectUniform` 拆分，per-draw object uniform 独立分配。

### Phase 0.10

- `ModelData::instances` 表达 glTF node 对 primitive 的实例化。
- `TransformData` 使用 column-major `float[16]`，asset 层不依赖 renderer/RHI/Vulkan。
- `GltfLoader` 支持 glTF 2.0 default scene、scene root node 递归遍历、node `matrix` 和 TRS。
- `RenderQueue::build()` 展开 `ModelResource::instances()`。
- `DrawItem::modelMatrix` 使用 `sceneModel.transform * instance.localTransform`。
- `RenderView` 提供 view/projection matrix，`ForwardPass` 从 `FrameContext::view` 读取 camera uniform。
- 默认 sandbox model 切换为 `assets/models/forward_multinode_fixture.gltf`。

### Phase 0.11

- `rhi::Format` 新增 `RGBA8Srgb`，Vulkan format mapping 已补齐。
- `TextureResource` 接管 texture/view/sampler/staging buffer 和首次 upload 状态。
- `TextureCache` 使用规范化 path + `TextureColorSpace` 作为 key。
- glTF baseColor texture 默认通过 `TextureColorSpace::Srgb` 创建 `RGBA8Srgb` RHI texture。
- `MaterialResource` 不再直接调用 `TextureLoader`，只保存 `TextureResource*` 引用并负责 descriptor 写入。
- `texture_cache_fixture.gltf` 验证共享 texture 只创建一次。

### Phase 0.12

- `DeviceContext` 新增 texture/view/sampler deferred release 接口。
- `VulkanDeletionQueue` 扩展 buffer / texture view / sampler / texture 队列。
- `TextureResource` 新增 `releaseDeferred(context)` 和 `resetImmediate()`。
- `TextureCache` 新增 `clearDeferred(context)`，`clear()` 保留为 shutdown / GPU idle immediate path。
- smoke tests 覆盖 texture resource deferred release 和 texture cache deferred clear。

### Phase 0.13

- `MeshResource` 新增 `releaseDeferred(context)` 和 `resetImmediate()`。
- `ModelResource` 新增 `resetDeferred(context)`，用于运行期 model unload / replacement。
- `ModelResource` 区分 local texture cache 和 external texture cache。
- local cache 会在 model deferred reset 中 `clearDeferred(context)`；external cache 由外部拥有者管理。
- `ModelResource::reset()` 保留为 shutdown / GPU idle immediate path。

### Phase 0.14

- `rhi::calculateMipLevelCount(extent)` 支持非 2 次幂尺寸。
- `TextureResourceDesc::generateMips` 默认开启 mip chain。
- `TextureResource` 创建 texture 时计算 mipLevels。
- 多 mip texture usage 包含 `TransferSrc | TransferDst | ShaderResource`。
- texture view 覆盖完整 mip range。
- sampler 在多 mip texture 上使用 linear mip filtering。
- `DeviceContext::generateTextureMips(Texture&)` 和 Vulkan GPU blit mip generation 已落地。
- upload / mip generation / staging deferred release 仍发生在 dynamic rendering scope 外。

### Phase 0.15

- `MaterialData` 新增 baseColor / metallic / roughness factors，默认值遵循 glTF 2.0。
- `GltfLoader::loadMaterial()` 读取 `pbrMetallicRoughness` core factors。
- renderer 层新增 `MaterialFactors`。
- `MaterialResource` 保存 factors，`ForwardPass` 创建 per-draw material uniform buffer。
- descriptor layout 新增 `set 0 binding 4: MaterialUniformBuffer`。
- `mesh.frag.hlsl` 使用 `baseColorTexture * baseColorFactor`。
- metallic / roughness factors 已进入 uniform。

### Phase 0.16

- `MaterialData` 新增 normal、metallicRoughness、occlusion、emissive texture slots。
- `GltfLoader` 读取 glTF core material texture slots。
- `TextureCache` 支持 color / non-color texture 语义。
- fallback textures 最小实现已落地。
- `MaterialResource` 与 `ForwardPass` 接入多 texture descriptors。
- shader 最小接入 baseColor、normal、metallicRoughness、occlusion、emissive 输入。

### Phase 0.17

- `MeshVertex` 增加 tangent。
- `GltfLoader` 支持读取 glTF `TANGENT` attribute。
- `ForwardPass` 增加 `LightingUniform`。
- shader 最小 direct-light-only PBR 输入解释已落地。
- normal map 通过 TBN 进入 world normal。
- 当前仍不是完整 PBR，没有 IBL/HDR/tone mapping。

### Phase 0.18

- 新增 `docs/phase/phase18.md`。
- `asset::generateTangents(MeshPrimitiveData&)` 实现 indexed triangle CPU tangent generation。
- glTF 显式 `TANGENT` 保持优先；缺失 `TANGENT` 时在 primitive 索引读取完成后自动生成。
- 退化 UV / 退化 triangle 会跳过；无有效累计 tangent 的 vertex 使用与 normal 正交的 fallback tangent。
- `ApplicationDesc` / `RendererDesc` 新增 `defaultModelPath`。
- `apps/sandbox/main.cpp` 支持 `ark_sandbox.exe [optional_model_path]`。
- 默认 sandbox fixture 保持 `assets/models/forward_multinode_fixture.gltf`，不绑定 DamagedHelmet。
- `ObjectUniform` 增加 `normalMatrix`。
- `mesh.vert.hlsl` 使用 normal matrix 变换 normal，tangent 使用 model 线性部分变换后相对 world normal 正交化。
- tests 覆盖 generated tangent、explicit tangent、degenerate fallback、shader normal matrix source smoke、DamagedHelmet optional load。
- `.gitignore` 已忽略 `assets/models/DamagedHelmet/`。

## 4. 关键代码阅读顺序

建议按以下顺序审核当前 Phase 0.18 闭环：

1. `docs/phase/phase18.md`
   - 确认 Phase 0.18 范围、非目标、验证记录和质量限制。
2. `src/asset/MeshData.h/.cpp`
   - 看 `MeshVertex::tangent` 和 `generateTangents()` 的 CPU helper。
3. `src/asset/GltfLoader.cpp`
   - 看显式 `TANGENT` 读取和缺失 tangent 自动生成路径。
4. `tests/mesh_data_smoke.cpp` / `tests/gltf_loader_smoke.cpp`
   - 看 generated tangent、degenerate fallback、explicit tangent 和 DamagedHelmet optional 验证。
5. `src/app/Application.h/.cpp`、`src/renderer/Renderer.h/.cpp`、`apps/sandbox/main.cpp`
   - 看 sandbox model path override 如何从 app 传到 renderer 默认 scene。
6. `src/renderer/passes/ForwardPass.cpp`
   - 看 `ObjectUniform` 的 `normalMatrix`，以及 per-draw object uniform update。
7. `shaders/mesh.vert.hlsl` / `shaders/mesh.frag.hlsl`
   - 确认 vertex normal/tangent world-space 变换和 fragment TBN normal map。
8. `src/renderer/FrameRenderer.cpp`
   - 确认 `prepare()` 仍在 `beginRendering()` 前，upload/mip generation/deferred release 不进入 dynamic rendering scope。
9. `src/rhi/vulkan/VulkanCommandContext.cpp`
   - 看 upload/mip generation 的 dynamic rendering scope 检查、barrier 和 blit 限制。

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
docs/phase/phase15.md
docs/phase/phase16.md
docs/phase/phase17.md
docs/phase/phase18.md
```

硬性边界：

- 只有 `src/rhi/vulkan/` 可以包含 Vulkan 头文件和 `Vk*` 类型。
- `asset/` 只解析外部文件并输出 CPU 数据，不创建 RHI/GPU 资源。
- tangent generation 属于 asset CPU 后处理，不进入 renderer/RHI/Vulkan。
- `TextureLoader` 只输出 CPU `ImageData`，不决定 GPU sRGB/linear 采样语义，也不参与 mip generation。
- `renderer/` 可以创建和持有 RHI 资源，但不能接触 Vulkan 类型。
- `RenderScene` 保存 scene 语义，不创建 GPU 资源。
- `RenderQueue` 是 draw list，不拥有底层 GPU 资源。
- `ModelResource` 是 renderer 层 GPU resource owner，通过 RHI 创建资源。
- local texture cache 可由 `ModelResource` 管理；external texture cache 必须由外部拥有者管理。
- `MaterialResource` 保存 material 语义和 texture 引用，并负责 descriptor 写入。
- `ForwardPass` 负责 pipeline、descriptor、per-frame/per-draw binding 和 draw，不负责 asset loading、cache 或 resource lifetime。
- upload / mip generation / deferred release 命令必须记录在 dynamic rendering scope 外。
- 当前继续使用 Vulkan Dynamic Rendering，不引入传统 `VkRenderPass` / `VkFramebuffer`。
- 日志输出使用英文。
- 必要注释使用简洁中文。

## 6. 已知风险和 TODO

P0 / 下一阶段优先：

- tangent generation 不是 MikkTSpace；与 DCC/baker 的 tangent basis 可能不完全一致。
- 算法依赖 glTF 已按 UV seam / normal seam 拆分顶点，不在当前阶段主动重建 vertex split。
- 当前 direct lighting 仍是最小实现，没有 IBL/HDR/tone mapping。
- glTF sampler 参数、texture transform、anisotropy 尚未接入。
- `Renderer` 内部默认 scene 仍是 sandbox 过渡方案；真正 renderer 级资源/场景加载入口尚未设计。
- `assets/models/DamagedHelmet/` 可作为真实 glTF 2.0 验证对象，但不应作为默认路径或提交依赖。
- descriptor set / descriptor layout / pipeline / shader module 的 deferred destruction 仍未纳入。

P1：

- texture upload 仍只覆盖 RGBA8 / RGBA8 sRGB、mip0 upload、array layer 0、tightly packed rows。
- mip generation 只支持 2D color texture、single array layer、GPU blit；不支持离线 mip、array/cubemap、HDR 或压缩纹理。
- glTF skin、animation、morph target、extensions、embedded image、data URI image 尚未支持。
- shader binding 与 descriptor layout 仍靠人工一致，后续可考虑 reflection 或测试校验。
- `CubePass` 仍保留为阶段性 debug pass，后续应决定保留、隐藏还是清理。
- `VulkanDescriptorManager` 有 growable pools，但尚无 free/reset 策略和容量统计。
- per-draw descriptor/object/material uniform 策略简单直接，draw 数量上升时应评估 push constants、dynamic uniform buffer 或 storage buffer。

P2：

- Vulkan debug object names。
- RenderDoc capture / screenshot / pixel smoke。
- ResourceManager handle 系统。
- RenderGraph 第一版 pass/resource 声明。
- 完整 PBR、IBL、tone mapping。

## 7. 最近验证记录

Phase 0.18 已运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：

```text
build passed
CTest: 8/8 passed
default sandbox smoke passed
DamagedHelmet optional smoke passed
```

DamagedHelmet smoke 日志确认：

```text
Using sandbox model: assets\models\DamagedHelmet\DamagedHelmet.gltf
Tangent generation skipped degenerate triangles: mesh=GltfPrimitive, count=66
Loaded glTF model: assets\models\DamagedHelmet\DamagedHelmet.gltf (primitives=1, materials=1, instances=1)
Renderer initialized
```

该 warning 是当前 tangent generation fallback 路径的输入质量提示，不阻断渲染。

## 8. 推荐下一步

建议下一阶段不要直接进入完整 RenderGraph / bindless。优先考虑：

1. glTF sampler 参数与 texture transform。
2. 更完整的 direct lighting BRDF，进一步明确 metallic / roughness / normal / AO / emissive 的组合语义。
3. HDR framebuffer 与 tone mapping。
4. IBL / environment map / BRDF LUT。
5. 可配置 scene light / camera。
6. 真正的 renderer 资源/场景加载入口，替代内部默认 scene 过渡方案。
7. pipeline / shader / descriptor layout 的 deferred destruction。

## 9. 下一次 Codex 启动提示

```text
请先阅读 docs/codex_handoff.md，理解 ARKRenderer 当前 Phase 0.18 完成状态、默认 scene / queue / ForwardPass 渲染路径、glTF 2.0 scene/node transform、RenderView camera、TextureResource / TextureCache / sRGB / mipmap generation / deferred reset 边界、material factors / texture slots / direct lighting / tangent generation / sandbox model override 数据链路、已知风险和后续建议。

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
docs/phase/phase15.md
docs/phase/phase16.md
docs/phase/phase17.md
docs/phase/phase18.md

当前默认渲染路径已经是 Vulkan Dynamic Rendering + ClearPass + ForwardPass + RenderScene + RenderQueue + ModelResource + MeshResource + MaterialResource + TextureResource + TextureCache + glTF scene/node primitive instances + RenderView camera uniform + per-draw object/material/lighting uniform + normal matrix + sampled images + samplers + GPU mipmap generation + direct-light-only PBR 输入解释 + generated/explicit tangent + indexed textured multi draw + depth attachment。

不要重复 Phase 0.5 ~ 0.18 已完成工作。
下一步优先考虑 glTF sampler 参数与 texture transform、更完整的 direct lighting BRDF、HDR/tone mapping、IBL/environment map、可配置 scene light/camera 或真正的 renderer 资源/场景加载入口。不要提前引入完整 RenderGraph、bindless 或复杂 glTF 扩展。

如果实现方向与既有设计文档冲突，先说明并更新设计文档，再修改代码。新增代码保持现有风格：左大括号不换行，namespace 内缩进，日志输出用英文，必要注释用简洁中文。不确定的地方写 TODO 或记录到文档，不要假装完成。

先执行并查看：
git status -sb
git diff --stat
git log --oneline -n 5
```
