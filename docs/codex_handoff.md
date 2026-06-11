# Codex Handoff Summary

更新时间：2026-06-11

## 1. 当前状态

ARKRenderer 当前代码实现已完成 Phase 0.29。`KHR_texture_transform` 最小闭环已经从 asset/glTF loader 一直打通到 `MaterialResource`、`ForwardPass` material uniform、mesh fragment shader、fixture 和 smoke tests；`RenderQueue` 已完成最小 alpha bucket ordering，保证 Opaque / Mask draw items 在 Blend draw items 前绘制；`ForwardPass` 已按 glTF `doubleSided` 精确设置 raster culling；`RenderScene` / `RenderView` 已提供可配置 scene lighting 和 camera position；mesh fragment shader 已把 direct lighting 从旧 specular power 路径升级到 Cook-Torrance direct BRDF；`FrameRenderer` 现在通过 `RGBA16Float` HDR scene color 和 `ToneMappingPass` 输出到 swapchain backbuffer；Phase 0.29 新增了 HDR environment texture 前置链路。

Phase 0.28 已把 `ToneMappingPass` 从 hardcoded exposure 改为 `RenderView` 持有 `ark::ToneMappingSettings` -> per-frame uniform buffer -> `tonemap.frag.hlsl` constant buffer 的数据流，并新增 fake RHI `ark_tone_mapping_pass_smoke` 覆盖 uniform 数据流、descriptor layout、per-frame resources、pipeline state 和 fullscreen triangle draw。Phase 0.29 已新增 `loadImageHdrRgba32F()`、`rhi::Format::RGBA32Float`、Vulkan `RGBA32Float` upload、`EnvironmentResource` 和 `RenderScene` environment API。Windows/MSVC/vcpkg/DXC debug preset 下 full build、CTest 11/11、default sandbox smoke 和 DamagedHelmet smoke 均已通过。

当前默认渲染主线：

```text
Vulkan Dynamic Rendering
    -> Renderer
        -> RenderScene / 默认 sandbox scene
            -> SceneLighting main directional light + ambient
            -> SceneEnvironment slot (resource pointer + intensity, not consumed by ForwardPass yet)
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
        -> beginRendering(RGBA16Float scene color + depth)
        -> ClearPass
        -> ForwardPass
            -> RenderView camera uniform + camera position
            -> per-draw object uniform + normal matrix
            -> per-draw material uniform
               factors + alpha state + per-slot texCoord selectors + per-slot texture transforms
            -> lighting uniform from RenderScene lighting + RenderView camera position
            -> baseColor / normal / metallicRoughness / occlusion / emissive sampled images + samplers
            -> per-slot selectUv() + transformUv() before sampling
            -> Cook-Torrance direct BRDF
               GGX distribution + Smith geometry + Schlick Fresnel
            -> alphaMode / doubleSided pipeline variant key
            -> doubleSided culling: None for double-sided, Back for single-sided
            -> indexed textured mesh draw(s) in RenderQueue order
        -> endRendering()
        -> scene color RenderTarget -> ShaderResource
        -> beginRendering(swapchain backbuffer)
        -> ToneMappingPass
            -> fullscreen triangle
            -> sample HDR scene color
            -> RenderView ToneMappingSettings
            -> per-frame ToneMappingUniformBuffer
            -> exposure + Reinhard tone mapping + output gamma encoding
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

Phase 0.25 已完成的主要改动：

```text
docs/phase/phase25.md
src/renderer/RenderScene.h/.cpp
src/renderer/RenderView.h
src/renderer/passes/ForwardPass.cpp
tests/forward_pass_pipeline_smoke.cpp
tests/render_scene_queue_smoke.cpp
tests/framework_headers_smoke.cpp
docs/codex_handoff.md
```

Phase 0.26 已完成的主要改动：

```text
docs/phase/phase26.md
shaders/mesh.frag.hlsl
tests/shader_assets_smoke.cpp
docs/codex_handoff.md
```

Phase 0.27 已完成的主要改动：

```text
docs/phase/phase27.md
src/renderer/FrameContext.h
src/renderer/FrameRenderer.cpp
src/renderer/passes/ForwardPass.cpp
src/renderer/passes/ToneMappingPass.h/.cpp
shaders/tonemap.vert.hlsl
shaders/tonemap.frag.hlsl
CMakeLists.txt
tests/forward_pass_pipeline_smoke.cpp
tests/shader_assets_smoke.cpp
docs/codex_handoff.md
```

Phase 0.28 已完成的主要改动：

```text
docs/phase/phase28.md
src/renderer/RenderView.h
src/renderer/passes/ToneMappingPass.h/.cpp
shaders/tonemap.frag.hlsl
CMakeLists.txt
tests/tone_mapping_pass_smoke.cpp
tests/shader_assets_smoke.cpp
tests/framework_headers_smoke.cpp
docs/codex_handoff.md
```

Phase 0.29 已完成的主要改动：

```text
docs/phase/phase29.md
src/asset/TextureLoader.h/.cpp
src/rhi/RHICommon.h
src/rhi/DeviceContext.h
src/rhi/vulkan/VulkanCommon.cpp
src/rhi/vulkan/VulkanCommandContext.cpp
src/renderer/EnvironmentResource.h/.cpp
src/renderer/RenderScene.h/.cpp
CMakeLists.txt
tests/texture_loader_smoke.cpp
tests/environment_resource_smoke.cpp
tests/render_scene_queue_smoke.cpp
tests/framework_headers_smoke.cpp
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
- `RenderScene` 持有最小 `SceneLighting`：一个 directional light 和 ambient color。
- `RenderScene::lighting()` / `setLighting()` 可配置 scene lighting，默认值与 Phase 0.24 hardcoded light 对齐。
- `RenderView` 持有 camera position；`setDefaultPerspective()` 写入默认 `(0, 0, -4)`。
- `ForwardPass::makeLightingUniform()` 从 `FrameContext::scene` 和 `FrameContext::view` 读取 lighting 与 camera position。
- `LightingUniform` binding / size / descriptor layout 未变化；scene lighting / camera position 只改变 uniform 数据来源。
- `mesh.frag.hlsl` 的 direct lighting 已升级为 Cook-Torrance direct BRDF：
  - GGX / Trowbridge-Reitz normal distribution
  - Smith geometry term
  - Schlick Fresnel
  - Lambert diffuse
  - metallic workflow F0
- `FrameContext` 提供当前 render scope 的 `colorFormat` / `depthFormat` 和 post pass 采样用的 `sceneColorView`。
- `ForwardPass` pipeline key 优先使用 `FrameContext::colorFormat` / `FrameContext::depthFormat`，不再强依赖 swapchain color format。
- `FrameRenderer` 创建 `RGBA16Float` scene color，usage 为 `RenderTarget | ShaderResource`。
- `FrameRenderer` 当前是两段 dynamic rendering：Forward scene pass 写 HDR scene color，ToneMappingPass 再写 swapchain backbuffer。
- `RenderView` 持有最小 `ToneMappingSettings`：`exposure` 和 `outputGamma`。
- `ToneMappingPass` 使用 per-frame descriptor set 采样 scene color，并用 per-frame uniform buffer 上传 tone mapping settings。
- `tonemap.frag.hlsl` 通过 binding 2 的 `ToneMappingUniform` 读取 `exposure` / `inverseOutputGamma`。
- 默认 `exposure = 1.0`、`outputGamma = 2.2`，保持 Phase 0.27 默认视觉行为。
- `ark_tone_mapping_pass_smoke` 使用 fake RHI 验证 `ToneMappingPass` uniform 数据流、fallback/clamp、descriptor layout、per-frame resources 和 fullscreen triangle draw。
- `TextureLoader` 支持显式 HDR float loader：`loadImageHdrRgba32F()` / `TextureLoader::loadHdrRgba32F()`。
- `loadImageRgba8()` 仍显式拒绝 HDR 输入，不静默量化到 RGBA8。
- `asset::ImageData(Rgba32Float)` 已作为真实 HDR CPU image output 使用，`bytesPerPixel = 16`。
- RHI/Vulkan 已支持最小 `RGBA32Float` sampled texture upload，Vulkan 映射为 `VK_FORMAT_R32G32B32A32_SFLOAT`。
- `VulkanCommandContext::uploadTextureData()` 支持 `RGBA32Float` tightly packed mip0/layer0 upload；HDR mip generation 仍未接入。
- `EnvironmentResource` 负责把 `ImageData(Rgba32Float)` 创建为 `RGBA32Float` 2D sampled texture，默认 `mipLevels = 1`、usage 为 `ShaderResource | TransferDst`。
- `EnvironmentResource` 默认 sampler 为 `U=Repeat`、`V/W=ClampToEdge`，适配 equirectangular environment map。
- `RenderScene` 持有 `SceneEnvironment`：`EnvironmentResource*` 与 `intensity`，但不拥有 GPU resource。
- `RenderScene::clear()` 保留 lighting/environment policy；显式清空 environment 使用 `clearEnvironment()`。
- 当前仍是 direct-light-only；尚未支持 environment shader sampling、IBL、shadow、多光源、skybox、bloom 或 auto exposure。

## 2. 最近提交与工作区

最近提交（接手时仍以 `git log --oneline -n 5` 为准）：

```text
7a20cf5 完成 Phase28 tone mapping 参数化
1b7c3b9 完成 Phase27 HDR tone mapping
af0d530 完成 Phase26 direct lighting BRDF
4aaeba4 完成 Phase25 scene light camera
aefbdd5 完成 Phase24 doubleSided culling
bea5c01 完成 Phase23 RenderQueue alpha 分桶
```

本轮 0.29.0 ~ 0.29.6 收尾主要工作区改动：

```text
## main...origin/main
 M CMakeLists.txt
 M docs/codex_handoff.md
 M src/asset/TextureLoader.cpp
 M src/asset/TextureLoader.h
 M src/renderer/RenderScene.cpp
 M src/renderer/RenderScene.h
 M src/rhi/DeviceContext.h
 M src/rhi/RHICommon.h
 M src/rhi/vulkan/VulkanCommandContext.cpp
 M src/rhi/vulkan/VulkanCommon.cpp
 M tests/framework_headers_smoke.cpp
 M tests/render_scene_queue_smoke.cpp
 M tests/texture_loader_smoke.cpp
?? docs/phase/phase29.md
?? src/renderer/EnvironmentResource.cpp
?? src/renderer/EnvironmentResource.h
?? tests/environment_resource_smoke.cpp
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

### Phase 0.25

- 新增 `docs/phase/phase25.md`，明确本阶段只做可配置 scene light / camera，不做 BRDF、HDR、IBL、RenderGraph、glTF lights/cameras 或透明排序。
- `RenderScene` 新增最小 lighting 语义：
  - `DirectionalLight`
  - `SceneLighting`
  - `RenderScene::lighting()`
  - `RenderScene::setLighting()`
- 默认 lighting 保持 Phase 0.24 hardcoded values：direction `(-0.35, -0.8, -0.45)`，color `(1.0, 0.96, 0.88)`，ambient `(0.08, 0.09, 0.11)`。
- `RenderView` 新增 camera position：
  - `setDefaultPerspective()` 写入默认 camera position `(0, 0, -4)`
  - `setMatrices(view, projection, cameraPosition)` 支持显式 camera position
  - 旧 `setMatrices(view, projection)` 保持可用，并在 `RenderView` 内部反推 camera position
- `ForwardPass::makeLightingUniform()` 已从 `FrameContext::scene` 读取 scene lighting，从 `FrameContext::view` 读取 camera position。
- `LightingUniform` size、binding 13、descriptor layout、pipeline layout 和 mesh shaders 均未修改。
- `ark_forward_pass_pipeline_smoke` 已扩展 fake context 捕获 `ForwardLightingUniformBuffer`，覆盖 scene lighting / view camera position 到 ForwardPass uniform 的数据流，并继续覆盖 Phase 0.24 pipeline state。
- `ark_render_scene_queue_smoke` 覆盖 `RenderScene` lighting 默认值和 set/get。
- `ark_framework_headers_smoke` 覆盖新增 public structs 能编译。

### Phase 0.26

- 新增 `docs/phase/phase26.md`，明确本阶段只做 direct lighting BRDF shader 升级，不做 HDR、tone mapping、IBL、shadow、多光源、RenderGraph 或 glTF material extensions。
- `mesh.frag.hlsl` 新增 direct lighting BRDF helper：
  - `PI`
  - `distributionGGX()`
  - `geometrySchlickGGX()`
  - `geometrySmith()`
  - `fresnelSchlick()`
- `evaluateDirectLighting()` 已从旧的 `specularPower` / `pow(nDotH, specularPower)` 高光模型升级为 Cook-Torrance direct BRDF。
- direct light 公式现在使用 GGX distribution、Smith geometry、Schlick Fresnel、Lambert diffuse 和 metallic workflow F0。
- `readPbrInputs()` 的 texture sampling、UV selection、texture transform、alpha mask/blend 路径未修改。
- `LightingUniform` binding 13、descriptor layout、pipeline layout、RHI/Vulkan 均未修改。
- `shader_assets_smoke` 已扩展 source smoke，覆盖 BRDF helper 和关键变量，并继续验证 mesh SPIR-V 能加载。

### Phase 0.27

- 新增 `docs/phase/phase27.md`，明确本阶段只做 HDR scene color / tone mapping 最小闭环，不做 IBL、bloom、auto exposure、shadow、多光源或 RenderGraph。
- `FrameContext` 新增：
  - `sceneColorView`
  - `colorFormat`
  - `depthFormat`
- `ForwardPass` pipeline format 已从 swapchain 解耦，优先使用 `FrameContext::colorFormat` / `FrameContext::depthFormat`，并保留 swapchain fallback。
- `DefaultFrameRenderer` 新增 `RGBA16Float` scene color texture/view，usage 为 `RenderTarget | ShaderResource`。
- `FrameRenderer` 从单 render scope 改为两段：
  - scene pass：`ClearPass` + `ForwardPass` 写 HDR scene color + swapchain depth。
  - post pass：`ToneMappingPass` 采样 scene color 后写 swapchain backbuffer。
- `ToneMappingPass` 新增完整 C++ 实现：
  - descriptor layout：binding 0 sampled image，binding 1 sampler
  - per-frame descriptor set，避免多帧并行时改写同一 descriptor set
  - clamp-to-edge linear sampler
  - fullscreen triangle draw
  - pipeline 按当前 backbuffer color format 创建
- 新增 `shaders/tonemap.vert.hlsl`，使用 `SV_VertexID` 生成 fullscreen triangle。
- Phase 0.27 当时新增 `shaders/tonemap.frag.hlsl`，采样 HDR scene color，执行固定 exposure + Reinhard tone mapping + linear-to-sRGB；当前 Phase 0.28 工作区已把 exposure / output gamma 参数化。
- CMake 已编译 `tonemap.vert.spv` / `tonemap.frag.spv`。
- `shader_assets_smoke` 已覆盖 tonemap SPIR-V 和关键源码 token。
- `ark_forward_pass_pipeline_smoke` 已覆盖 `FrameContext::colorFormat = RGBA16Float` 时 ForwardPass pipeline 使用 HDR color format。

### Phase 0.28（0.28.0 ~ 0.28.6 已完成并验证）

- 新增 `docs/phase/phase28.md`，明确本阶段只做 tone mapping settings / color pipeline 收口，不做 IBL、bloom、auto exposure、ACES/filmic、sRGB swapchain 切换或完整 post-process stack。
- `RenderView` 新增 `ToneMappingSettings`：
  - `exposure`
  - `outputGamma`
  - `toneMappingSettings()`
  - `setToneMappingSettings()`
- `RenderView::setDefaultPerspective()` 不重置用户设置的 tone mapping settings。
- `ToneMappingPass` 新增 `ToneMappingUniform`，每个 frame slot 创建一个 `ToneMappingUniformBuffer`。
- `ToneMappingPass` descriptor layout 新增 binding 2 `UniformBuffer`，fragment stage 可见。
- `ToneMappingPass::execute()` 从 `frameContext.view` 读取 `ToneMappingSettings`，并在 draw fullscreen triangle 前更新当前 frame slot uniform。
- CPU 侧对 settings 做最小防御：`exposure < 0` clamp 到 `0`，`outputGamma <= 0` fallback 到 `2.2`。
- `shaders/tonemap.frag.hlsl` 删除 hardcoded `Exposure`，改用 binding 2 `ConstantBuffer<ToneMappingUniform>`。
- shader 现在使用 `g_ToneMapping.exposure` 和 `g_ToneMapping.inverseOutputGamma`，output encoding helper 命名为 `linearToOutput()`。
- `shader_assets_smoke` 已更新 source token，覆盖 tone mapping uniform、binding 2、exposure、inverse gamma 和 `linearToOutput()`。
- `framework_headers_smoke` 已覆盖 `ToneMappingSettings` public API 编译路径。
- 新增 `tests/tone_mapping_pass_smoke.cpp`，使用 fake RHI / fake context 捕获 `ToneMappingUniformBuffer` 的实际上传数据。
- `ark_tone_mapping_pass_smoke` 覆盖正常 exposure/gamma、非法参数 clamp/fallback、无 view 默认 settings、descriptor layout、per-frame resources、pipeline state 和 fullscreen triangle draw。
- `CMakeLists.txt` 已在 `ARK_DXC_SUPPORTED` 测试块中接入 `ark_tone_mapping_pass_smoke`，并添加 `ark_shaders` 依赖。

### Phase 0.29（0.29.0 ~ 0.29.6 已完成并验证）

- 新增 `docs/phase/phase29.md`，明确本阶段是 HDR environment texture / IBL prelude，不做完整 IBL、cubemap、irradiance、prefilter、BRDF LUT、skybox、bloom 或 auto exposure。
- `TextureLoader` 新增 `loadImageHdrRgba32F()` 和 `TextureLoader::loadHdrRgba32F()`，使用 stb float path 显式读取 HDR。
- `loadImageRgba8()` 继续拒绝 HDR 输入，避免隐式量化到 RGBA8。
- `ImageData(Rgba32Float)` 现在使用 `bytesPerPixel = 16`，`pixels` 保存 RGBA float bytes。
- `rhi::Format` 新增 `RGBA32Float`，Vulkan 映射到 `VK_FORMAT_R32G32B32A32_SFLOAT`。
- `VulkanCommandContext::uploadTextureData()` 支持 `RGBA32Float` 16 bytes per pixel tightly packed upload。
- 新增 `EnvironmentResource`，从 `ImageData(Rgba32Float)` 创建 `RGBA32Float` 2D sampled environment texture、view、sampler 和 staging buffer。
- `EnvironmentResource` 默认 mipLevels 为 1，usage 为 `ShaderResource | TransferDst`，不调用 `generateTextureMips()`。
- `EnvironmentResource` 支持 `releaseDeferred()` 与 `resetImmediate()`，生命周期风格对齐现有 renderer resource。
- `RenderScene` 新增 `SceneEnvironment`、`environment()`、`setEnvironment()` 和 `clearEnvironment()`；scene 只保存 resource pointer 与 intensity，不拥有 GPU resource。
- `framework_headers_smoke`、`texture_loader_smoke`、`render_scene_queue_smoke` 和新增 `ark_environment_resource_smoke` 已覆盖 public API、loader 边界、resource desc/upload desc、sampler policy、deferred release 和 scene environment policy。

## 4. 关键代码阅读顺序

建议按以下顺序审核当前 Phase 0.29 完整闭环：

1. `docs/phase/phase21.md`
   - 回看 `TEXCOORD_1` / per-slot UV selection 的前置范围和限制。
2. `docs/phase/phase22.md`
   - 确认 `KHR_texture_transform` 最小闭环、当前限制和验证记录。
3. `docs/phase/phase23.md`
   - 确认 RenderQueue alpha bucket 范围、非目标、验证记录和仍非完整 transparent sorting 的限制。
4. `docs/phase/phase24.md`
   - 确认 doubleSided culling 范围、front-face 约定、测试策略和仍不做 two-sided lighting 的限制。
5. `docs/phase/phase25.md`
   - 确认 scene lighting / camera position 的范围、非目标、测试策略和仍不做 BRDF/HDR/IBL 的限制。
6. `docs/phase/phase26.md`
   - 确认 direct lighting BRDF 升级范围、非目标、测试策略和仍不做 HDR/IBL 的限制。
7. `docs/phase/phase27.md`
   - 确认 HDR scene color / tone mapping 范围、两段 FrameRenderer 调度、测试策略和仍不做 IBL/bloom/auto exposure 的限制。
8. `docs/phase/phase28.md`
   - 确认 tone mapping settings / color pipeline 收口范围、0.28.0 ~ 0.28.6 已完成项和验证记录。
9. `docs/phase/phase29.md`
   - 确认 HDR environment texture 前置链路范围、0.29.0 ~ 0.29.6 已完成项、非目标和验证记录。
10. `src/asset/TextureLoader.h/.cpp`
   - 看 `loadImageRgba8()` 对 HDR 的拒绝语义，以及 `loadImageHdrRgba32F()` 的 float RGBA32F 输出路径。
11. `src/rhi/RHICommon.h` / `src/rhi/vulkan/VulkanCommon.cpp`
   - 看 `RGBA32Float` format 枚举、format name 和 Vulkan format mapping。
12. `src/rhi/vulkan/VulkanCommandContext.cpp`
   - 看 RGBA8 / RGBA32Float upload 的 bytes-per-pixel 约束，以及 HDR mip generation 仍未支持的边界。
13. `src/renderer/EnvironmentResource.h/.cpp`
   - 看 HDR environment texture resource 的创建、upload、sampler policy、deferred release 和 reset 行为。
14. `src/renderer/FrameContext.h`
   - 看 `sceneColorView`、`colorFormat`、`depthFormat` 的 pass 间共享语义。
15. `src/renderer/FrameRenderer.cpp`
   - 看 `RGBA16Float` scene color 创建、scene pass / tone mapping pass 两段 dynamic rendering、barrier 和 viewport/scissor。
16. `src/renderer/RenderView.h`
   - 看 camera position、`setDefaultPerspective()`、显式 `setMatrices()`、旧 `setMatrices()` 兼容路径和 `ToneMappingSettings`。
17. `src/renderer/passes/ToneMappingPass.h/.cpp`
   - 看 per-frame descriptor set、scene color sampled image/sampler binding、per-frame tone mapping uniform buffer、fullscreen triangle draw 和 pipeline format。
18. `shaders/tonemap.vert.hlsl` / `shaders/tonemap.frag.hlsl`
   - 看 `SV_VertexID` fullscreen triangle、HDR scene color sampling、uniform exposure、Reinhard tone mapping 和 output gamma encoding。
19. `src/renderer/RenderScene.h/.cpp`
   - 看 `DirectionalLight`、`SceneLighting`、`SceneEnvironment`、`RenderScene::lighting()`、`RenderScene::setLighting()` 和 environment API。
20. `src/asset/MeshData.h/.cpp`
   - 看 `MeshVertex::uv1`、tangent 字段、`TextureTransformData`、`MaterialTextureSlotData` 和 `generateTangents()`。
21. `src/asset/GltfLoader.cpp`
   - 看 sampler、alpha render state、`TEXCOORD_1`、`KHR_texture_transform`、显式/生成 tangent、scene/node instance 的读取路径。
22. `src/renderer/ModelResource.cpp`
   - 看 asset sampler 到 RHI sampler 的转换、texture cache 获取和 fallback texture。
23. `src/renderer/material/MaterialResource.h/.cpp`
   - 看 material factors、render state、texture references、per-slot texCoord set、per-slot transform set 和 descriptor 写入。
24. `src/renderer/RenderQueue.cpp`
   - 看 scene/model draw item 展开、Opaque / Mask / Blend 分桶和 bucket 合并顺序。
25. `src/renderer/passes/ForwardPass.cpp`
   - 看 descriptor layout、pipeline variant key、FrameContext attachment format 解耦、vertex layout、doubleSided cull mode、camera/object/material/lighting uniform、scene lighting / view camera position 读取、per-slot transform 写入和 draw loop。
26. `shaders/mesh.vert.hlsl` / `shaders/mesh.frag.hlsl`
   - 确认 normal matrix、uv1 传递、per-slot `selectUv()` + `transformUv()`、alpha mask/blend 和 Cook-Torrance direct BRDF 路径。
27. `src/rhi/vulkan/VulkanPipelineState.cpp` / `src/rhi/vulkan/VulkanSampler.cpp`
    - 看 blend/cull/depth state 和 sampler address/filter 映射。
28. `tests/environment_resource_smoke.cpp` / `tests/texture_loader_smoke.cpp` / `tests/forward_pass_pipeline_smoke.cpp` / `tests/tone_mapping_pass_smoke.cpp` / `tests/render_scene_queue_smoke.cpp` / `tests/model_resource_smoke.cpp` / `tests/gltf_loader_smoke.cpp` / `tests/shader_assets_smoke.cpp` / `tests/framework_headers_smoke.cpp`
    - 看当前 smoke tests 对 HDR loader、EnvironmentResource、SceneEnvironment、ForwardPass HDR attachment format、cull state、lighting uniform、ToneMappingPass uniform 数据流、queue alpha bucket、sampler、alpha、uv1、texture transform、BRDF shader source、tone mapping shader source 和 public API 的约束。

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
docs/phase/phase25.md
docs/phase/phase26.md
docs/phase/phase27.md
docs/phase/phase28.md
docs/phase/phase29.md
```

硬性边界：

- 只有 `src/rhi/vulkan/` 可以包含 Vulkan 头文件和 `Vk*` 类型。
- `asset/` 只解析外部文件并输出 CPU 数据，不创建 RHI/GPU 资源。
- `asset/` 不依赖 renderer/RHI/Vulkan；需要新增语义时先放 asset 自有数据结构。
- tangent generation 属于 asset CPU 后处理，不进入 renderer/RHI/Vulkan。
- `TextureLoader` 只输出 CPU `ImageData`，不决定 GPU sRGB/linear 采样语义，也不参与 mip generation。
- HDR loader 必须保持显式入口；不要让 `loadImageRgba8()` 静默量化 HDR。
- texture transform 属于 material slot 语义；不要放进 `TextureResource` 或 `TextureCache` key。
- `renderer/` 可以创建和持有 RHI 资源，但不能接触 Vulkan 类型。
- `RenderScene` 保存 scene 语义，不创建 GPU 资源。
- `RenderScene` 的 environment slot 只保存 `EnvironmentResource*` 和 intensity，不拥有或创建 environment resource。
- scene lighting 属于 `RenderScene` 语义，不进入 RHI/Vulkan，也不由 `ForwardPass` 决定默认场景策略。
- camera position 属于 `RenderView` 语义；`ForwardPass` 只读取并写入 uniform。
- tone mapping settings 属于 `RenderView` 语义；`ToneMappingPass` 只通过 `FrameContext::view` 读取并写入自己的 uniform。
- direct lighting BRDF 当前只落在 `shaders/mesh.frag.hlsl`；不为 shader 公式升级改 RHI/Vulkan 或 descriptor layout。
- environment lighting 尚未接入 `ForwardPass` / `mesh.frag.hlsl`，不要假定当前画面会因 `SceneEnvironment` 改变。
- `RenderQueue` 是 draw list，不拥有底层 GPU 资源；当前只做 Opaque / Mask / Blend alpha bucket ordering，不做完整 transparent sorting。
- `ModelResource` 是 renderer 层 GPU resource owner，通过 RHI 创建资源。
- `EnvironmentResource` 是 renderer 层 GPU resource owner，不复用 material `TextureResource` 的 sRGB/linear policy，也暂不进入 `TextureCache`。
- local texture cache 可由 `ModelResource` 管理；external texture cache 必须由外部拥有者管理。
- `MaterialResource` 保存 material 语义和 texture 引用，并负责 descriptor 写入。
- `ForwardPass` 负责 pipeline、descriptor、per-frame/per-draw binding、doubleSided culling 和 draw，不负责 asset loading、cache 或 resource lifetime。
- `ForwardPass` 的 pipeline attachment format 应来自当前 render scope 的 `FrameContext::colorFormat` / `depthFormat`，不要重新绑定到 swapchain format。
- `FrameRenderer` 当前拥有 frame-level HDR scene color，不放入 `RenderScene`、`ForwardPass` 或 `ToneMappingPass`。
- `ToneMappingPass` 只负责采样 scene color 并写 backbuffer，不拥有 scene color 生命周期。
- tone mapping 已支持最小 exposure / output gamma 参数化，但仍不代表完整 post-process stack。
- output encoding 当前只应发生在 tone mapping shader；mesh/lighting shader 继续输出 linear HDR。
- upload / mip generation / deferred release 命令必须记录在 dynamic rendering scope 外。
- `RGBA32Float` environment upload 当前只支持 tightly packed mip0/layer0；不要把它理解为 cubemap、array texture 或 HDR mip generation 已完成。
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
- 当前 direct lighting 已使用 Cook-Torrance BRDF，并通过 `RGBA16Float` scene color + ToneMappingPass 输出；HDR environment texture resource 前置已完成，但仍没有 shader 侧 environment sampling、完整 IBL、shadow 或多光源。
- tone mapping 当前只有手动 `exposure` / `outputGamma` + Reinhard，不支持 auto exposure、ACES 参数化、bloom 或 color grading。
- 当前只支持一个 directional light 和 ambient color；不支持 point / spot / area light、shadow、glTF camera 或 `KHR_lights_punctual`。
- `Renderer` 内部默认 scene 仍是 sandbox 过渡方案；真正 renderer 级资源/场景加载入口尚未设计。
- `assets/models/DamagedHelmet/` 可作为真实 glTF 2.0 验证对象，但不应作为默认路径或提交依赖。
- descriptor set / descriptor layout / pipeline / shader module 的 deferred destruction 仍未纳入。

P1：

- texture upload 仍只覆盖 RGBA8 / RGBA8 sRGB / RGBA32Float、mip0 upload、array layer 0、tightly packed rows。
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
- 完整 PBR、IBL、bloom、auto exposure。

## 7. 最近验证记录

Phase 0.27 收尾在 Windows/MSVC vcpkg debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_shader_assets_smoke
cmake --build --preset msvc-vcpkg-debug --target ark_forward_pass_pipeline_smoke
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
cmake --build --preset msvc-vcpkg-debug --target ark_sandbox -- /t:Rebuild /m:1
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：

```text
targeted shader assets build passed
targeted forward pass pipeline build passed
ark_shader_assets_smoke passed
ark_forward_pass_pipeline_smoke passed
single-process ark_sandbox rebuild passed
full build passed
CTest: 9/9 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
```

本轮 sandbox smoke 使用隐藏窗口启动 3 秒后自动停止，用于确认默认场景和 DamagedHelmet 路径不会启动即退出。期间曾遇到一次并行 MSBuild 写 PDB/tlog 锁和一次脏 `ToneMappingPass.obj` 链接问题；通过顺序构建和单进程 Rebuild 清理中间产物后，完整 build、CTest 和 runtime smoke 均通过。

Phase 0.28 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_tone_mapping_pass_smoke
build/msvc-vcpkg/Debug/ark_tone_mapping_pass_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：

```text
targeted ark_tone_mapping_pass_smoke build passed
ark_tone_mapping_pass_smoke passed
full build passed
CTest: 10/10 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
```

Phase 0.29 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_environment_resource_smoke ark_texture_loader_smoke ark_render_scene_queue_smoke ark_framework_headers_smoke
build/msvc-vcpkg/Debug/ark_environment_resource_smoke.exe
build/msvc-vcpkg/Debug/ark_texture_loader_smoke.exe
build/msvc-vcpkg/Debug/ark_render_scene_queue_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：

```text
targeted smoke build passed
ark_environment_resource_smoke passed
ark_texture_loader_smoke passed
ark_render_scene_queue_smoke passed
ark_framework_headers_smoke passed
full build passed
CTest: 11/11 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
```

本轮 sandbox smoke 使用隐藏窗口启动 3 秒后自动停止，用于确认默认场景和 DamagedHelmet 路径不会启动即退出。`texture_loader_smoke` 和 `environment_resource_smoke` 中出现的 error log 是测试刻意触发非法输入路径，用于验证拒绝行为，进程退出码为 0。

## 8. 推荐下一步

Phase 0.29 已收尾，建议下一阶段从最小 environment lighting 接入开始，不要直接进入完整 RenderGraph / bindless。

优先顺序：

1. 最小 environment lighting 接入：`ForwardPass` descriptor layout 增加 environment texture/sampler/intensity，mesh shader 先用 equirectangular HDR 替换或调制 ambient。
2. Cubemap resource / equirectangular -> cubemap conversion。
3. Diffuse irradiance map、prefiltered specular environment map 和 BRDF LUT。
4. Skybox / environment background pass。
5. bloom、auto exposure、ACES/filmic 或 exposure UI/config 可作为后续独立阶段。
6. 真正的 renderer 资源/场景加载入口，替代内部默认 scene 过渡方案。
7. 基于 camera 和 bounds 的 Blend bucket back-to-front sorting。
8. pipeline / shader / descriptor layout 的 deferred destruction。

## 9. 下一次 Codex 启动提示

```text
请先阅读 docs/codex_handoff.md，理解 ARKRenderer 当前已完成 Phase 0.29：KHR_texture_transform 最小闭环已经打通到 asset、GltfLoader、MaterialResource、ForwardPass uniform、mesh.frag.hlsl、fixture 和 smoke tests；RenderQueue alpha bucket ordering 也已完成，Opaque / Mask draw items 会稳定排在 Blend draw items 前；ForwardPass 已按 glTF doubleSided 精确设置 raster culling；RenderScene / RenderView 已提供可配置 scene lighting 和 camera position；mesh.frag.hlsl 已升级为 Cook-Torrance direct BRDF；FrameRenderer 已接入 RGBA16Float HDR scene color 和 ToneMappingPass，ForwardPass 先写 HDR scene color，ToneMappingPass 再写 swapchain backbuffer；ToneMappingPass 已从 hardcoded exposure 改为 RenderView 持有 ToneMappingSettings -> per-frame ToneMappingUniformBuffer -> tonemap.frag.hlsl binding 2 constant buffer；Phase 0.29 已新增 HDR loader、RGBA32Float upload、EnvironmentResource 和 RenderScene environment API。当前仍没有 environment shader sampling、cubemap、irradiance、prefilter、BRDF LUT、skybox、bloom 或 auto exposure。

重点理解当前默认渲染路径：
Vulkan Dynamic Rendering + Renderer + RenderScene scene lighting + SceneEnvironment slot + RenderView camera matrix/camera position + ToneMappingSettings + RenderQueue alpha buckets + FrameRenderer two-stage rendering + RGBA16Float scene color + ClearPass + ForwardPass doubleSided culling + ModelResource + MeshResource + MaterialResource + TextureResource + TextureCache + EnvironmentResource + glTF scene/node primitive instances + RenderView camera uniform + per-draw object/material/lighting uniform + normal matrix + sampled images/samplers + GPU mipmap generation + Cook-Torrance direct BRDF + generated/explicit tangent + glTF sampler + alpha render states + TEXCOORD_1 / per-slot UV selection + KHR_texture_transform per-slot transform + indexed textured multi draw + depth attachment + ToneMappingPass fullscreen triangle + exposure/Reinhard/output gamma encoding + swapchain backbuffer。

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
docs/phase/phase25.md
docs/phase/phase26.md
docs/phase/phase27.md
docs/phase/phase28.md
docs/phase/phase29.md

不要重复 Phase 0.5 ~ 0.29 已完成工作。
不要重复 Phase 0.22 已完成的 KHR_texture_transform 最小闭环，不要重复 Phase 0.23 已完成的 RenderQueue alpha bucket，不要重复 Phase 0.24 已完成的 doubleSided culling，不要重复 Phase 0.25 已完成的 scene light / camera 数据入口，不要重复 Phase 0.26 已完成的 direct lighting BRDF，不要重复 Phase 0.27 已完成的 HDR scene color / ToneMappingPass 最小闭环，不要重复 Phase 0.28 已完成的 tone mapping settings / color pipeline 收口，也不要重复 Phase 0.29 已完成的 HDR loader / RGBA32Float upload / EnvironmentResource / RenderScene environment API。下一步优先考虑最小 environment lighting 接入、cubemap/IBL 前置、bloom/auto exposure 或 renderer 资源/场景加载入口等小步。不要提前引入完整 RenderGraph、bindless、复杂 glTF extensions 或完整材质扩展，除非用户明确改变目标。

如果实现方向与既有设计文档冲突，先说明并更新设计文档，再修改代码。新增代码保持现有风格：左大括号不换行，namespace 内缩进，日志输出用英文，必要注释用简洁中文。不确定的地方写 TODO 或记录到文档，不要假装完成。

先执行并查看：
git status -sb
git diff --stat
git log --oneline -n 5
```
