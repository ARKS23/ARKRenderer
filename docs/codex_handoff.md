# Codex Handoff Summary

更新时间：2026-06-10

## 1. 当前状态

ARKRenderer 当前代码实现已完成 Phase 0.24：`KHR_texture_transform` 最小闭环已经从 asset/glTF loader 一直打通到 `MaterialResource`、`ForwardPass` material uniform、mesh fragment shader、fixture 和 smoke tests；`RenderQueue` 已完成最小 alpha bucket ordering，保证 Opaque / Mask draw items 在 Blend draw items 前绘制；`ForwardPass` 也已按 glTF `doubleSided` 精确设置 raster culling。

当前默认渲染主线：

```text
Vulkan Dynamic Rendering
    -> Renderer
        -> RenderScene / 默认 sandbox scene
        -> RenderQueue
            -> Opaque bucket
            -> Mask bucket
            -> Blend bucket
    -> FrameRenderer
        -> prepare() upload stage
            -> MeshResource GPU-only vertex/index upload
            -> TextureResource RGBA8/sRGB upload + GPU mipmap generation
            -> TextureCache path + colorSpace + fallback + sampler key reuse
            -> MaterialResource texture references + factors + render state + texCoord selectors
        -> beginRendering(color + depth)
        -> ClearPass
        -> ForwardPass
            -> RenderView camera uniform
            -> per-draw object uniform + normal matrix
            -> per-draw material uniform
               factors + alpha state + per-slot texCoord selectors + per-slot texture transforms
            -> lighting uniform
            -> baseColor / normal / metallicRoughness / occlusion / emissive sampled images + samplers
            -> per-slot selectUv() + transformUv() before sampling
            -> alphaMode / doubleSided pipeline variant key
            -> doubleSided culling: None for double-sided, Back for single-sided
            -> indexed textured mesh draw(s) in RenderQueue order
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

真实模型验证入口仍是：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

`assets/models/DamagedHelmet/` 是本地真实模型资产目录，已加入 `.gitignore`，不作为默认资源，也不应在未明确要求时提交。

Phase 0.22 已完成的主要改动：

```text
docs/phase/phase22.md
src/asset/MeshData.h
src/asset/GltfLoader.cpp
src/renderer/material/MaterialResource.h/.cpp
src/renderer/passes/ForwardPass.cpp
shaders/mesh.frag.hlsl
assets/models/texture_transform_fixture.gltf
tests/gltf_loader_smoke.cpp
tests/model_resource_smoke.cpp
tests/shader_assets_smoke.cpp
tests/framework_headers_smoke.cpp
```

Phase 0.23 已完成的主要改动：

```text
docs/phase/phase23.md
src/renderer/RenderQueue.cpp
tests/render_scene_queue_smoke.cpp
tests/model_resource_smoke.cpp
docs/codex_handoff.md
```

Phase 0.24 已完成的主要改动：

```text
docs/phase/phase24.md
src/renderer/passes/ForwardPass.cpp
tests/forward_pass_pipeline_smoke.cpp
CMakeLists.txt
docs/codex_handoff.md
```

当前支持范围：

- 读取 textureInfo 上的 `KHR_texture_transform`。
- 支持 `offset`、`scale`、`rotation`。
- 支持 extension 内 `texCoord` override。
- 最终 `texCoord` 仍只支持 0/1；超过范围 warning 后 fallback 到 0。
- transform 是 material texture slot 语义，不进入 `TextureResource` 或 `TextureCache` key。
- shader 对 baseColor、normal、metallicRoughness、occlusion、emissive 分别应用自己的 transform。
- 尚不支持 texture transform animation、`TEXCOORD_2+`、完整 glTF extension 系统或 per-UV-set tangent basis。
- `RenderQueue::build()` 会按 material alpha mode 生成稳定 draw order：Opaque bucket、Mask bucket、Blend bucket。
- 每个 bucket 内保持原 scene/model traversal order。
- `ForwardPass` 不理解 bucket，仍只按 `RenderQueue::drawItems()` 顺序绘制。
- 当前不是完整 transparent sorting；Blend bucket 内不做 camera-distance back-to-front 排序。
- `ForwardPass` 根据 `MaterialRenderState::doubleSided` 设置 cull mode：
  - `doubleSided=false` -> `CullMode::Back`
  - `doubleSided=true` -> `CullMode::None`
- Forward mesh pipeline 显式使用 `FrontFace::CounterClockwise`。
- 当前不做 two-sided lighting；双面材质只是关闭背面剔除，shader 没有基于 `gl_FrontFacing` 翻转 normal。

## 2. 最近提交与工作区

最近提交（本次 Phase 0.24 提交前）：

```text
bea5c01 完成 Phase23 RenderQueue alpha 分桶
2d4b4f4 完成 Phase22 texture transform 渲染闭环
47b7af2 完成 Phase22 texture transform 数据流基础
98aeea9 更新 Codex handoff 至 Phase21
40b5082 pahse22文档
a458a5c 完成 Phase21 TEXCOORD1 采样闭环
7b5307e 完成 Phase20 glTF alpha render states
039c16f 完成 Phase19 glTF sampler 闭环
54f4f48 完成 Phase18 tangent 生成与模型验证入口
0c49e98 完成 Phase17 shader 光照解释收尾
```

本次 Phase 0.24 提交推送后，预期工作区状态：

```text
## main...origin/main
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

### Phase 0.19

- `MaterialTextureSlotData` 表达 path、`texCoord`、sampler 和 sampler 是否显式存在。
- `GltfLoader` 已读取 glTF `textureInfo.index` / `texCoord` 和 `samplers` 的 filter / wrap。
- asset 层有自己的 texture filter / address mode 枚举，不依赖 RHI。
- RHI 已补齐 `AddressMode::MirroredRepeat`，Vulkan sampler 已映射。
- renderer 层把 asset sampler 转换为 `rhi::SamplerDesc`。
- `TextureCache` key 已包含 sampler override，避免同一路径不同 sampler 错误复用。
- 新增 `sampler_fixture.gltf` 覆盖 default sampler、explicit sampler、同 image 不同 sampler 和 `texCoord=1` 路径。

### Phase 0.20

- `MaterialData` 新增 `AlphaMode`、`alphaCutoff`、`doubleSided`，默认值对齐 glTF 2.0。
- `GltfLoader` 读取 `alphaMode`、`alphaCutoff`、`doubleSided`，未知 alphaMode warning 并 fallback 到 Opaque。
- RHI 补齐最小 `BlendFactor` / `BlendOp`，Vulkan pipeline creation 已映射。
- `MaterialResource` 缓存 material render state。
- `ForwardPass` 按 color/depth format、alpha mode、doubleSided 建立 pipeline variant。
- Blend material 使用标准 alpha blending，并关闭 depth write；Opaque / Mask 保持 depth write。
- `mesh.frag.hlsl` 支持 Mask discard，并让 Blend 输出 base color alpha。
- 新增 `alpha_modes_fixture.gltf`，覆盖 Opaque / Mask / Blend / doubleSided loader 与 material resource 路径。

### Phase 0.21

- `MeshVertex` 新增 `uv1`。
- `GltfLoader` 读取可选 `TEXCOORD_1`，缺失时复制 `uv0` 到 `uv1`。
- `MaterialResource` 缓存 baseColor、normal、metallicRoughness、occlusion、emissive 的 per-slot texCoord。
- `ForwardPass` vertex layout 增加 `uv1`，tangent location 调整到 4。
- `MaterialUniform` 增加 per-slot texCoord selector。
- `mesh.vert.hlsl` 传递 `uv1`。
- `mesh.frag.hlsl` 按每个 texture slot 选择 `uv0` / `uv1`。
- 新增 `texcoord1_fixture.gltf`，覆盖 `TEXCOORD_1` 和多 texture slot texCoord。
- `gltf_loader_smoke`、`model_resource_smoke`、`shader_assets_smoke`、`framework_headers_smoke`、`mesh_data_smoke` 覆盖 Phase 0.21 关键路径。

### Phase 0.22

- `asset::TextureTransformData` 表达 glTF texture transform，默认 identity。
- `MaterialTextureSlotData` 持有 per-slot transform。
- `GltfLoader` 读取 textureInfo 上的 `KHR_texture_transform`，支持 `offset`、`scale`、`rotation` 和 extension 内 `texCoord` override。
- malformed optional transform field warning 后保留默认值，不中断 glTF 加载。
- extension 内 `texCoord` override 后仍沿用 0/1/fallback 规则。
- `MaterialResource` 缓存 baseColor、normal、metallicRoughness、occlusion、emissive 的 per-slot transform，并暴露 `textureTransforms()`。
- `ForwardPass::MaterialUniform` 携带 per-slot texture transform，binding/layout 保持不变。
- `mesh.frag.hlsl` 对每个 texture slot 执行 `selectUv()` 后再执行 `transformUv()`。
- baseColor alpha mask 使用 transformed baseColor UV。
- 新增 `texture_transform_fixture.gltf` 覆盖 `TEXCOORD_0/1`、extension texCoord override 和五个 texture slot 的 transform。
- `gltf_loader_smoke`、`model_resource_smoke`、`shader_assets_smoke`、`framework_headers_smoke` 覆盖 Phase 0.22 关键路径。

### Phase 0.23

- 新增 `docs/phase/phase23.md`，明确本阶段只做 RenderQueue alpha 分桶，不做完整透明排序、OIT、HDR、IBL 或 RenderGraph。
- `RenderQueue::build()` 按 `MaterialResource::renderState().alphaMode` 将 draw item 分入 Opaque / Mask / Blend bucket。
- 最终 `drawItems()` 输出顺序为 Opaque、Mask、Blend。
- bucket 内保持原 scene/model traversal order；Blend bucket 内仍不做 back-to-front sorting。
- `ForwardPass`、descriptor layout、pipeline layout、shader 均未修改，继续按 queue 顺序绘制。
- `render_scene_queue_smoke` 覆盖 scene object 的 alpha bucket ordering 和 bucket 内稳定顺序。
- `model_resource_smoke` 覆盖 model primitive instance 展开后的 alpha bucket ordering 和 bucket 内稳定顺序。

### Phase 0.24

- 新增 `docs/phase/phase24.md`，明确本阶段只做 `doubleSided` culling 精确化，不做 two-sided lighting、透明排序、HDR、IBL 或 RenderGraph。
- `ForwardPass` 已把 `MaterialRenderState::doubleSided` 映射到 raster cull mode。
- `doubleSided=false` material 使用 `rhi::CullMode::Back`。
- `doubleSided=true` material 使用 `rhi::CullMode::None`。
- Forward mesh pipeline 显式设置 `rhi::FrontFace::CounterClockwise`。
- `ForwardPipelineKey::doubleSided` 与实际 raster state 已对齐。
- 新增 `ark_forward_pass_pipeline_smoke`，使用 fake RHI 捕获 `GraphicsPipelineDesc`，覆盖 single-sided opaque、double-sided mask、single-sided blend 的 cull/depth/blend state。

## 4. 关键代码阅读顺序

建议按以下顺序审核当前 Phase 0.24 闭环：

1. `docs/phase/phase21.md`
   - 回看 `TEXCOORD_1` / per-slot UV selection 的前置范围和限制。
2. `docs/phase/phase22.md`
   - 确认 `KHR_texture_transform` 最小闭环、当前限制和验证记录。
3. `docs/phase/phase23.md`
   - 确认 RenderQueue alpha bucket 范围、非目标、验证记录和仍非完整 transparent sorting 的限制。
4. `docs/phase/phase24.md`
   - 确认 doubleSided culling 范围、front-face 约定、测试策略和仍不做 two-sided lighting 的限制。
5. `src/asset/MeshData.h/.cpp`
   - 看 `MeshVertex::uv1`、tangent 字段、`TextureTransformData`、`MaterialTextureSlotData` 和 `generateTangents()`。
6. `src/asset/GltfLoader.cpp`
   - 看 sampler、alpha render state、`TEXCOORD_1`、`KHR_texture_transform`、显式/生成 tangent、scene/node instance 的读取路径。
7. `src/renderer/ModelResource.cpp`
   - 看 asset sampler 到 RHI sampler 的转换、texture cache 获取和 fallback texture。
8. `src/renderer/material/MaterialResource.h/.cpp`
   - 看 material factors、render state、texture references、per-slot texCoord set、per-slot transform set 和 descriptor 写入。
9. `src/renderer/RenderQueue.cpp`
   - 看 scene/model draw item 展开、Opaque / Mask / Blend 分桶和 bucket 合并顺序。
10. `src/renderer/passes/ForwardPass.cpp`
   - 看 descriptor layout、pipeline variant key、vertex layout、doubleSided cull mode、camera/object/material/lighting uniform、per-slot transform 写入和 draw loop。
11. `shaders/mesh.vert.hlsl` / `shaders/mesh.frag.hlsl`
   - 确认 normal matrix、uv1 传递、per-slot `selectUv()` + `transformUv()`、alpha mask/blend 和 lighting 路径。
12. `src/renderer/FrameRenderer.cpp`
   - 确认 `prepare()` 仍在 `beginRendering()` 前，upload/mip generation/deferred release 不进入 dynamic rendering scope。
13. `src/rhi/vulkan/VulkanCommandContext.cpp` / `VulkanPipelineState.cpp` / `VulkanSampler.cpp`
    - 看 upload/mip generation scope 检查、blend/cull/depth state、sampler address/filter 映射。
14. `tests/forward_pass_pipeline_smoke.cpp` / `tests/render_scene_queue_smoke.cpp` / `tests/model_resource_smoke.cpp` / `tests/gltf_loader_smoke.cpp` / `tests/shader_assets_smoke.cpp`
    - 看当前 smoke tests 对 ForwardPass cull state、queue alpha bucket、sampler、alpha、uv1、texture transform、shader source 的约束。

## 5. 必须继续遵守的架构边界

后续开发前继续阅读：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase18.md
docs/phase/phase19.md
docs/phase/phase20.md
docs/phase/phase21.md
docs/phase/phase22.md
docs/phase/phase23.md
docs/phase/phase24.md
```

硬性边界：

- 只有 `src/rhi/vulkan/` 可以包含 Vulkan 头文件和 `Vk*` 类型。
- `asset/` 只解析外部文件并输出 CPU 数据，不创建 RHI/GPU 资源。
- `asset/` 不依赖 renderer/RHI/Vulkan；需要新增语义时先放 asset 自有数据结构。
- tangent generation 属于 asset CPU 后处理，不进入 renderer/RHI/Vulkan。
- `TextureLoader` 只输出 CPU `ImageData`，不决定 GPU sRGB/linear 采样语义，也不参与 mip generation。
- texture transform 属于 material slot 语义；不要放进 `TextureResource` 或 `TextureCache` key。
- `renderer/` 可以创建和持有 RHI 资源，但不能接触 Vulkan 类型。
- `RenderScene` 保存 scene 语义，不创建 GPU 资源。
- `RenderQueue` 是 draw list，不拥有底层 GPU 资源；当前只做 Opaque / Mask / Blend alpha bucket ordering，不做完整 transparent sorting。
- `ModelResource` 是 renderer 层 GPU resource owner，通过 RHI 创建资源。
- local texture cache 可由 `ModelResource` 管理；external texture cache 必须由外部拥有者管理。
- `MaterialResource` 保存 material 语义和 texture 引用，并负责 descriptor 写入。
- `ForwardPass` 负责 pipeline、descriptor、per-frame/per-draw binding、doubleSided culling 和 draw，不负责 asset loading、cache 或 resource lifetime。
- upload / mip generation / deferred release 命令必须记录在 dynamic rendering scope 外。
- 当前继续使用 Vulkan Dynamic Rendering，不引入传统 `VkRenderPass` / `VkFramebuffer`。
- 日志输出使用英文。
- 必要注释使用简洁中文。

## 6. 已知风险和 TODO

P0 / 下一阶段优先：

- Blend draw 已通过 RenderQueue alpha bucket 保证位于 Opaque / Mask 之后，但仍未做 back-to-front sorting；当前不声明透明排序完全正确。
- tangent generation 不是 MikkTSpace；与 DCC/baker 的 tangent basis 可能不完全一致。
- tangent generation 仍基于 `uv0`；如果 normal texture 使用 `texCoord=1`，严格 tangent basis 仍是后续改进项。
- `KHR_texture_transform` 已支持 textureInfo 上的 offset / scale / rotation / texCoord override，但不支持 animation、`TEXCOORD_2+` 或 per-UV-set tangent basis。
- `doubleSided=true` 当前只关闭背面剔除，不做 two-sided lighting，也不基于 `gl_FrontFacing` 翻转 normal。
- 当前 direct lighting 仍是最小实现，没有 IBL/HDR/tone mapping。
- `Renderer` 内部默认 scene 仍是 sandbox 过渡方案；真正 renderer 级资源/场景加载入口尚未设计。
- `assets/models/DamagedHelmet/` 可作为真实 glTF 2.0 验证对象，但不应作为默认路径或提交依赖。
- descriptor set / descriptor layout / pipeline / shader module 的 deferred destruction 仍未纳入。

P1：

- texture upload 仍只覆盖 RGBA8 / RGBA8 sRGB、mip0 upload、array layer 0、tightly packed rows。
- mip generation 只支持 2D color texture、single array layer、GPU blit；不支持离线 mip、array/cubemap、HDR 或压缩纹理。
- glTF skin、animation、morph target、embedded image、data URI image、`TEXCOORD_2+` 尚未支持。
- glTF extensions 目前只完成 `KHR_texture_transform` 最小支持；`KHR_materials_*` 等仍未支持。
- sampler cache 尚未独立拆分；同 image 不同 sampler 当前会重复创建 texture/view/sampler。
- anisotropy、compare sampler、shadow sampler 尚未接入。
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

Phase 0.24 收尾在 Windows/MSVC vcpkg debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_forward_pass_pipeline_smoke
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：

```text
targeted build passed
ark_forward_pass_pipeline_smoke passed
build passed
CTest: 9/9 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
```

本轮 sandbox smoke 使用隐藏窗口启动 3 秒后自动停止，用于确认默认场景和 DamagedHelmet 路径不会启动即退出。

## 8. 推荐下一步

Phase 0.24 后建议继续保持小步闭环，不要直接进入完整 RenderGraph / bindless。

优先顺序：

1. 可配置 scene light / camera。
2. 更完整的 direct lighting BRDF。
3. HDR framebuffer 与 tone mapping。
4. IBL / environment map / BRDF LUT。
5. 真正的 renderer 资源/场景加载入口，替代内部默认 scene 过渡方案。
6. 基于 camera 和 bounds 的 Blend bucket back-to-front sorting。
7. pipeline / shader / descriptor layout 的 deferred destruction。

## 9. 下一次 Codex 启动提示

```text
请先阅读 docs/codex_handoff.md，理解 ARKRenderer 当前已完成 Phase 0.24：KHR_texture_transform 最小闭环已经打通到 asset、GltfLoader、MaterialResource、ForwardPass uniform、mesh.frag.hlsl、fixture 和 smoke tests；RenderQueue alpha bucket ordering 也已完成，Opaque / Mask draw items 会稳定排在 Blend draw items 前；ForwardPass 已按 glTF doubleSided 精确设置 raster culling。

重点理解当前默认渲染路径：
Vulkan Dynamic Rendering + Renderer + RenderScene/RenderQueue alpha buckets + FrameRenderer + ClearPass + ForwardPass doubleSided culling + ModelResource + MeshResource + MaterialResource + TextureResource + TextureCache + glTF scene/node primitive instances + RenderView camera uniform + per-draw object/material/lighting uniform + normal matrix + sampled images/samplers + GPU mipmap generation + direct-light-only PBR 输入解释 + generated/explicit tangent + glTF sampler + alpha render states + TEXCOORD_1 / per-slot UV selection + KHR_texture_transform per-slot transform + indexed textured multi draw + depth attachment。

然后阅读：
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase18.md
docs/phase/phase19.md
docs/phase/phase20.md
docs/phase/phase21.md
docs/phase/phase22.md
docs/phase/phase23.md
docs/phase/phase24.md

不要重复 Phase 0.5 ~ 0.24 已完成工作。
不要重复 Phase 0.22 已完成的 KHR_texture_transform 最小闭环，不要重复 Phase 0.23 已完成的 RenderQueue alpha bucket，也不要重复 Phase 0.24 已完成的 doubleSided culling。下一步建议从可配置 scene light / camera、更完整 direct lighting BRDF 等小步继续。不要提前引入完整 RenderGraph、bindless、复杂 glTF extensions、HDR/IBL，除非用户明确改变目标。

如果实现方向与既有设计文档冲突，先说明并更新设计文档，再修改代码。新增代码保持现有风格：左大括号不换行，namespace 内缩进，日志输出用英文，必要注释用简洁中文。不确定的地方写 TODO 或记录到文档，不要假装完成。

先执行并查看：
git status -sb
git diff --stat
git log --oneline -n 5
```
