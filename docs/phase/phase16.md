# Phase 0.16 glTF PBR Texture Slots 最小闭环

## 阶段判断

Phase 0.15 已经完成 glTF 2.0 core material factors 的最小闭环：

```text
glTF pbrMetallicRoughness
    -> asset::MaterialData
        -> baseColorFactor
        -> metallicFactor
        -> roughnessFactor
    -> MaterialResource::MaterialFactors
    -> ForwardPass material uniform
    -> mesh.frag.hlsl baseColorFactor 调制
```

当前默认渲染路径已经可以稳定绘制 glTF fixture，但材质 texture 仍只有 baseColor：

- `MaterialData` 只有 `baseColorTexturePath`。
- `ModelResource` 只按 sRGB 创建 baseColor texture。
- `MaterialResource` 只保存一个 `TextureResource*`。
- `ForwardPass` 只写一组 sampled image / sampler descriptor。
- `mesh.frag.hlsl` 只采样 baseColor texture。

这会阻塞真实 glTF 2.0 资产验证。以 `DamagedHelmet` 为例，模型通常依赖 normal、metallicRoughness、occlusion、emissive 等 texture；如果只加载 baseColor，即使几何和 baseColor 正确，材质语义仍然严重缺失。

因此 Phase 0.16 不建议直接进入完整 PBR，也不建议直接进入 RenderGraph / bindless。更合理的下一步是：先把 glTF PBR 常见 texture slots 和 color / non-color texture 语义打通，让真实资产的输入数据链路完整可审。

## 目标

Phase 0.16 的目标是在不实现完整 PBR BRDF 的前提下完成以下能力：

- `asset::MaterialData` 表达 glTF 2.0 常见 PBR texture slots：
  - `baseColorTexture`
  - `normalTexture`
  - `metallicRoughnessTexture`
  - `occlusionTexture`
  - `emissiveTexture`
- `MaterialData` 补充 slot 相关 scalar/vector：
  - `emissiveFactor = (0, 0, 0)`
  - `normalScale = 1`
  - `occlusionStrength = 1`
- `GltfLoader` 读取上述 texture slot 的 external image URI。
- `ModelResource` 按 texture 语义选择 `TextureColorSpace`：
  - baseColor：sRGB
  - emissive：sRGB
  - normal：linear / non-color
  - metallicRoughness：linear / non-color
  - occlusion：linear / non-color
- optional texture 缺失时使用明确 fallback texture，保证所有 shader-visible descriptor 都有效。
- `MaterialResource` 保存多个 texture 引用，但仍不拥有 texture 生命周期。
- `ForwardPass` 用固定 descriptor binding 接入多 texture slot，不引入 bindless。
- shader 最小接入多 texture 数据链路，但不假装已经完成完整 PBR。
- smoke tests 覆盖 glTF 多 texture slot 解析、color space 选择、fallback texture、descriptor/shader 不回退。

## 非目标

Phase 0.16 暂不做以下内容：

- 不做完整 PBR BRDF。
- 不做 IBL、environment map、tone mapping、exposure。
- 不做 tangent generation。
- 不正确应用 tangent-space normal map 到 lighting。
- 不解析 glTF sampler 参数。
- 不实现 glTF texture transform。
- 不支持 embedded image 或 data URI image。
- 不支持 KHR 扩展，例如 `KHR_materials_emissive_strength`、clearcoat、transmission、specular 等。
- 不支持 HDR、KTX、BasisU、DDS 或压缩纹理。
- 不支持 texture array、cubemap、array layer、多 mip upload。
- 不引入 bindless、descriptor indexing 或 global texture array。
- 不引入完整 RenderGraph。
- 不把 `assets/models/DamagedHelmet/` 直接切成默认 sandbox 资源，除非本阶段后半明确验证通过。

## 模块边界

Phase 0.16 继续遵守：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase11.md
docs/phase/phase12.md
docs/phase/phase13.md
docs/phase/phase14.md
docs/phase/phase15.md
```

硬性边界：

- `asset/` 只解析 glTF 并输出 CPU material 数据，不创建 RHI/GPU 资源。
- `TextureLoader` 仍只输出 CPU `ImageData`，不决定 GPU color space。
- `renderer/` 可以按 material 语义创建和持有 RHI resource，但不能接触 Vulkan 类型。
- `TextureCache` 负责 texture resource 复用；color / non-color 语义必须进入 cache key。
- `MaterialResource` 只保存 material 参数和 texture 引用，不拥有 texture 生命周期。
- `ForwardPass` 负责 descriptor layout、descriptor update、uniform update 和 draw，不负责 glTF 加载。
- upload、mip generation、deferred release 必须在 dynamic rendering scope 外。
- `rhi/vulkan/` 只处理 Vulkan 映射，不依赖 renderer/asset。

## 推荐数据流

Phase 0.16 目标数据流：

```text
glTF material
    -> asset::MaterialData
        -> baseColorTexturePath
        -> normalTexturePath
        -> metallicRoughnessTexturePath
        -> occlusionTexturePath
        -> emissiveTexturePath
        -> baseColorFactor
        -> metallicFactor
        -> roughnessFactor
        -> emissiveFactor
        -> normalScale
        -> occlusionStrength

ModelResource::create(device, textureCache, model)
    -> acquire texture slots
        -> baseColor: sRGB
        -> emissive: sRGB
        -> normal: linear
        -> metallicRoughness: linear
        -> occlusion: linear
        -> missing optional slot: fallback texture
    -> MaterialResource::create(materialData, materialTextures)
        -> stores factors
        -> stores TextureResource* per slot

ForwardPass::prepare()
    -> MaterialResource::upload()
        -> upload each referenced TextureResource
    -> update descriptor set
        -> camera uniform
        -> object uniform
        -> material uniform
        -> baseColor sampled image + sampler
        -> normal sampled image + sampler
        -> metallicRoughness sampled image + sampler
        -> occlusion sampled image + sampler
        -> emissive sampled image + sampler

ForwardPass::execute()
    -> update object/material uniform
    -> bind descriptor
    -> draw indexed

mesh.frag.hlsl
    -> samples all declared material texture slots
    -> uses baseColor as current output baseline
    -> keeps normal / metallicRoughness / occlusion / emissive data available for Phase 0.17 lighting/PBR
```

## 设计建议

### MaterialData

建议扩展 `asset::MaterialData`：

```cpp
struct MaterialData {
    Path baseColorTexturePath;
    Path normalTexturePath;
    Path metallicRoughnessTexturePath;
    Path occlusionTexturePath;
    Path emissiveTexturePath;

    float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float emissiveFactor[3] = {0.0f, 0.0f, 0.0f};
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;

    std::string debugName;
};
```

注意：

- asset 层继续不引入 `glm`。
- asset 层不判断 sRGB / linear。
- baseColorTexture 当前仍保持必需，避免同时引入 pure-factor material fallback。
- normal / metallicRoughness / occlusion / emissive 是 optional slot。

### GltfLoader

建议新增通用 helper：

```text
resolveTexturePath(gltfPath, model, textureInfo.index)
```

需要读取：

```text
material.pbrMetallicRoughness.baseColorTexture
material.pbrMetallicRoughness.metallicRoughnessTexture
material.normalTexture
material.occlusionTexture
material.emissiveTexture
material.emissiveFactor
material.normalTexture.scale
material.occlusionTexture.strength
```

审核检查点：

- external URI 按 glTF 文件目录解析。
- empty URI / data URI 仍不支持。
- optional texture 缺失不导致 model load 失败。
- baseColorTexture 缺失仍失败，除非先明确设计 pure-factor fallback。
- 不解析 glTF sampler 和 texture transform。

### Texture Slot Color Space

建议固定映射：

```text
BaseColor          -> TextureColorSpace::Srgb
Emissive           -> TextureColorSpace::Srgb
Normal             -> TextureColorSpace::Linear
MetallicRoughness  -> TextureColorSpace::Linear
Occlusion          -> TextureColorSpace::Linear
```

原因：

- baseColor / emissive 表达颜色。
- normal / metallicRoughness / occlusion 表达数据，不能走 sRGB decode。
- `TextureCache` key 必须包含 color space；同一路径如果被不同语义引用，不能错误复用同一个 GPU format。

### Fallback Texture

固定 descriptor layout 下，shader-visible texture binding 必须有有效 descriptor。Phase 0.16 应提供 fallback texture，而不是让 optional texture binding 空着。

建议 fallback：

```text
BaseColor fallback:          white RGBA8 sRGB      (255, 255, 255, 255)
Normal fallback:             flat normal linear    (128, 128, 255, 255)
MetallicRoughness fallback:  rough=1 metal=1       (255, 255, 255, 255), G/B 使用 glTF 语义
Occlusion fallback:          full visibility       (255, 255, 255, 255), R 使用 glTF 语义
Emissive fallback:           black sRGB            (0, 0, 0, 255)
```

实现建议：

- 在 renderer 层新增最小 fallback texture 生成逻辑，不放到 `asset/TextureLoader`。
- fallback 可以由 `TextureCache` 统一持有和复用。
- `TextureCache` key 不应继续只有 path + colorSpace；需要区分 file texture 和 fallback texture。

建议 key 语义：

```cpp
enum class TextureCacheSource {
    File,
    Fallback,
};

enum class FallbackTextureKind {
    White,
    FlatNormal,
    MetallicRoughnessDefault,
    OcclusionDefault,
    Black,
};
```

如果不想在 Phase 0.16 引入过多类型，也可以先用私有 key string 表达 fallback，但文档上应明确这是 cache key 语义，不是文件路径。

### MaterialResource

建议新增 texture slot 聚合结构：

```cpp
struct MaterialTextureSet {
    TextureResource* baseColor = nullptr;
    TextureResource* normal = nullptr;
    TextureResource* metallicRoughness = nullptr;
    TextureResource* occlusion = nullptr;
    TextureResource* emissive = nullptr;
};
```

`MaterialResource` 职责：

- 保存 `MaterialFactors`。
- 保存 `MaterialTextureSet`。
- `upload()` 遍历并上传所有非空 texture。
- `isReady()` 要求 shader-visible slots 都 ready。
- `updateDescriptorSet()` 写所有 texture slot 的 sampled image / sampler descriptor。

不建议：

- 不让 `MaterialResource` 创建 texture。
- 不让 `MaterialResource` 创建 uniform buffer。
- 不让 `MaterialResource` 拥有 `TextureResource` 生命周期。

### ForwardPass Descriptor Layout

当前 binding：

```text
set 0 binding 0: CameraUniformBuffer
set 0 binding 1: BaseColorSampledImage
set 0 binding 2: BaseColorSampler
set 0 binding 3: ObjectUniformBuffer
set 0 binding 4: MaterialUniformBuffer
```

建议新增固定 binding：

```text
set 0 binding 5:  NormalSampledImage
set 0 binding 6:  NormalSampler
set 0 binding 7:  MetallicRoughnessSampledImage
set 0 binding 8:  MetallicRoughnessSampler
set 0 binding 9:  OcclusionSampledImage
set 0 binding 10: OcclusionSampler
set 0 binding 11: EmissiveSampledImage
set 0 binding 12: EmissiveSampler
```

说明：

- 继续使用 separate sampled image / sampler，和当前 RHI 保持一致。
- 不引入 descriptor array。
- 不引入 bindless。
- optional texture 使用 fallback，保证 binding 始终可写。
- descriptor 数量上升后继续依赖 Phase 0.7 已实现的 growable descriptor pool。

### MaterialUniform

Phase 0.16 可以扩展 material uniform：

```cpp
struct alignas(16) MaterialUniform {
    glm::vec4 baseColorFactor;
    glm::vec4 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
};
```

HLSL 对应：

```hlsl
struct MaterialUniform {
    float4 baseColorFactor;
    float4 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
};
```

说明：

- `emissiveFactor.a` 可暂时保留为 0 或 1，不表达扩展语义。
- 不建议本阶段加入大量 bool flag；fallback texture 已能让 shader 无条件采样。
- 如果确实需要 debug flag，可用单独 `uint textureFlags`，但要重新检查 C++ / HLSL cbuffer 对齐。

### Shader 最小接入

Phase 0.16 shader 不应假装完整 PBR。

建议最小路径：

```text
baseColor = baseColorTexture.Sample(...) * baseColorFactor
metallicRoughness = metallicRoughnessTexture.Sample(...)
occlusion = occlusionTexture.Sample(...)
emissive = emissiveTexture.Sample(...) * emissiveFactor
normalSample = normalTexture.Sample(...)
```

输出策略建议二选一：

1. 保持当前 baseColor 输出，只让其余 texture slot 完成 descriptor / sampling 链路。
2. 输出 `baseColor + emissive` 的最小结果，仍不做 lighting。

不建议在没有 tangent / lighting 的情况下强行应用 normal map。normal texture 可以采样并保留变量，或通过临时 debug shader 验证，但默认渲染路径不要把它解释为正确光照。

## 实施顺序

### 0.16.0 文档与范围确认

目标：

- 新增 `docs/phase/phase16.md`。
- 明确主线是 glTF PBR texture slots 和 color-space 语义。
- 明确不进入完整 PBR、RenderGraph、bindless、sampler 参数或 texture transform。

审核检查点：

- 目标不和 Phase 0.15 重复。
- optional texture fallback 是本阶段必需项。
- `DamagedHelmet` 只作为真实资产验证对象，不默认承诺完整 PBR 观感。

### 0.16.1 MaterialData texture slots

目标：

- `MaterialData` 新增 normal / metallicRoughness / occlusion / emissive texture path。
- 新增 emissiveFactor / normalScale / occlusionStrength。
- 保持 asset 层纯数据。

审核检查点：

- asset 层不依赖 renderer/RHI/Vulkan。
- 默认值符合 glTF 2.0。
- optional slots 缺失不会破坏旧 fixture。

当前状态：

- 已完成。`MaterialData` 已补齐 baseColor / normal / metallicRoughness / occlusion / emissive texture path，以及 emissiveFactor / normalScale / occlusionStrength。
- 已补充 `has*Texture()` 查询函数，便于 renderer 层后续按 slot 选择真实 texture 或 fallback。

### 0.16.2 GltfLoader 多 texture 解析

目标：

- 解析 glTF 2.0 material texture slots。
- 解析 slot 相关 scalar/vector。
- 保留 external image URI 限制。

审核检查点：

- baseColorTexture 仍按当前策略要求存在。
- optional texture 缺失不失败。
- data URI / embedded image 仍明确不支持。
- 不解析 sampler / texture transform。

当前状态：

- 已完成。`GltfLoader` 现在读取 glTF 2.0 常见 material texture slots 和 emissive / normal / occlusion 相关参数。
- optional slot 缺失时保留空路径；如果 glTF 显式引用了不支持或非法 texture slot，会输出 warning。
- `forward_fixture.gltf` 已扩展为多 texture slot smoke fixture，仍复用现有 `xiaowei.png`，不新增资产依赖。

### 0.16.3 Fallback texture 最小实现

目标：

- 新增 renderer 层 fallback texture 生成。
- 保证所有 shader-visible texture slot 都有有效 `TextureResource`。
- fallback 走 `TextureCache` 统一复用和 deferred release 语义。

审核检查点：

- fallback 不是 fake file path。
- fallback color space 正确。
- fallback ImageData 仍是 RGBA8。
- `TextureLoader` 不参与 fallback 生成。

当前状态：

- 已完成最小基础设施。`TextureCache` 已支持 White / FlatNormal / MetallicRoughnessDefault / OcclusionDefault / Black 五类 1x1 fallback texture。
- fallback cache key 已区分 file texture 与 fallback texture，并保留 color-space 语义。
- 当前阶段只完成 fallback resource/cache 能力；让所有 shader-visible slots 自动绑定 fallback 会在 0.16.4 到 0.16.6 接入 `ModelResource`、`MaterialResource` 和 `ForwardPass`。

### 0.16.4 ModelResource / TextureCache color-space 接入

目标：

- `ModelResource::create()` 为每个 texture slot 获取正确 color-space 的 `TextureResource`。
- `TextureCache` key 支持 file texture 和 fallback texture。
- `MaterialResource::create()` 接收完整 texture set。

审核检查点：

- baseColor / emissive 使用 sRGB。
- normal / metallicRoughness / occlusion 使用 linear。
- 同一路径不同 colorSpace 不错误复用。
- external texture cache 的 ownership 语义不变。

当前状态：

- 已完成。`ModelResource::create()` 现在为 baseColor / normal / metallicRoughness / occlusion / emissive 获取完整 texture set。
- baseColor / emissive 走 sRGB；normal / metallicRoughness / occlusion 走 linear。
- optional slot 缺失时通过 `TextureCache::getOrCreateFallback()` 获取明确 fallback；baseColorTexture 仍按当前策略要求存在。
- external texture cache ownership 未改变，`ModelResource::resetDeferred()` 不释放外部 cache。

### 0.16.5 MaterialResource 多 texture 引用

目标：

- `MaterialResource` 保存 `MaterialTextureSet`。
- `upload()` 上传所有 referenced textures。
- `isReady()` 检查所有 shader-visible textures。
- descriptor update 写多 texture slot。

审核检查点：

- MaterialResource 不拥有 TextureResource 生命周期。
- MaterialResource 不创建 fallback texture。
- MaterialResource 不创建 uniform buffer。

当前状态：

- 已完成。`MaterialResource` 已保存 `MaterialTextureSet`，并继续只借用 `TextureResource*`。
- `upload()` / `isReady()` / `updateDescriptorSet()` 已覆盖全部 shader-visible texture slots。
- `MaterialFactors` 已补齐 emissiveFactor / normalScale / occlusionStrength。

### 0.16.6 ForwardPass descriptor 扩展

目标：

- descriptor layout 新增 normal / metallicRoughness / occlusion / emissive binding。
- update descriptor set 时写全部 sampled image / sampler。
- 保持 per-draw descriptor resources 策略。

审核检查点：

- shader binding 与 descriptor layout 一致。
- 所有 binding 都有有效 descriptor。
- descriptor pool 增长路径不回退。

当前状态：

- 已完成。`ForwardPass` descriptor layout 已新增 binding 5-12。
- per-draw descriptor set 现在写入 baseColor、normal、metallicRoughness、occlusion、emissive 的 sampled image / sampler。
- material uniform buffer 已扩展为 baseColorFactor、emissiveFactor、metallicFactor、roughnessFactor、normalScale、occlusionStrength。

### 0.16.7 Shader 最小接入

目标：

- `mesh.frag.hlsl` 声明全部 texture slots。
- 最少采样 baseColor / emissive / metallicRoughness / occlusion / normal。
- 默认输出仍保持可解释，不冒充完整 PBR。

审核检查点：

- normal map 不被错误解释为已正确参与 lighting。
- metallic/roughness texture 数据链路可审。
- emissiveFactor 默认不改变旧 fixture 输出。

当前状态：

- 已完成。`mesh.frag.hlsl` 已声明并采样全部 material texture slots。
- 默认输出仍以 baseColor 为基线，最小接入 occlusion 与 emissive；normal 和 metallicRoughness 只保持可审的数据链路，不声明完整 PBR/lighting 已完成。

### 0.16.8 Tests 与 sandbox smoke

目标：

- glTF loader smoke 覆盖多 texture slot path 和默认值。
- model/material resource smoke 覆盖 color space、fallback、cache key 和 descriptor 写入。
- shader assets smoke 通过。
- sandbox smoke 通过。
- 可选验证 `assets/models/DamagedHelmet/` 可加载到当前最小材质链路。

建议运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

涉及 shader / descriptor / 默认路径时继续运行 sandbox smoke。

当前状态：

- 已完成。`model_resource_smoke` 覆盖多 texture slot factors、fallback、sRGB/linear cache key 分离和 descriptor 写入。
- 已完成验证：
  - `cmake --build --preset msvc-vcpkg-local-debug`
  - `ctest --preset msvc-vcpkg-local-debug`
  - `ark_sandbox.exe` 默认 Vulkan 路径 smoke。

### 0.16.9 Phase 0.16 收尾

目标：

- 更新本文档当前实现状态。
- 按需同步 `docs/codex_handoff.md`。
- 记录后续 TODO：tangent、lighting/PBR、glTF sampler、texture transform、HDR/压缩纹理、RenderGraph。

当前状态：

- 已完成。Phase 0.16 已打通 glTF PBR 常见 texture slots 的 CPU 数据、texture cache、fallback、material resource、descriptor layout 和 shader 最小采样链路。
- 本次未同步 `docs/codex_handoff.md`；该文档等明确要求时再更新。
- 后续重点仍是 tangent / normal mapping、基础 lighting/PBR、glTF sampler 与 texture transform、HDR/压缩纹理，以及 RenderGraph 资源声明。

## 审核检查点

- `asset/` 只输出 CPU material 数据。
- `renderer/` 不接触 Vulkan 类型。
- `TextureLoader` 不决定 texture color space。
- `TextureCache` 不把 color texture 和 data texture 错误复用。
- optional texture 缺失时 descriptor 仍有效。
- `MaterialResource` 不接管 texture ownership。
- `ForwardPass` 不接管 asset loading / texture cache。
- descriptor layout 与 shader binding 一致。
- `normalTexture` 接入不等于已经完成 normal mapping。
- `metallicRoughnessTexture` 接入不等于已经完成 PBR。
- upload / mip generation / deferred release 不进入 dynamic rendering scope。
- 现有 Phase 0.15 material factors 行为不回退。

## 验证计划

实现后运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

涉及 shader 或默认 sandbox 渲染路径时，继续运行 sandbox smoke。

建议新增或扩展测试：

- `gltf_loader_smoke`
  - 多 texture slot path 解析。
  - optional texture 缺失默认值。
  - emissiveFactor / normalScale / occlusionStrength 默认值。
- `model_resource_smoke`
  - baseColor / emissive 使用 sRGB。
  - normal / metallicRoughness / occlusion 使用 linear。
  - fallback texture 创建和复用。
  - missing optional slot 不导致 material resource 失败。
- `shader_assets_smoke`
  - `mesh.frag.spv` 正常生成并可加载。

## 完成标准

Phase 0.16 完成时应满足：

- `asset::MaterialData` 表达 glTF 常见 PBR texture slots。
- `GltfLoader` 正确读取 glTF 2.0 texture slot URI 和相关 factors。
- `TextureCache` 能区分 file texture / fallback texture / color space。
- optional texture 缺失时会使用明确 fallback。
- `MaterialResource` 保存多 texture references 并继续只借用 `TextureResource`。
- `ForwardPass` descriptor layout 和 shader bindings 支持多 texture slots。
- shader 至少完成多 texture slot sampling 链路。
- build、ctest 和必要 sandbox smoke 通过。
- 文档明确完整 PBR、normal mapping、sampler、texture transform 和 RenderGraph 留给后续 phase。

## 后续 Phase 建议

Phase 0.16 完成后，建议继续：

1. Phase 0.17：最小基础光照 / PBR shader，开始使用 normal、metallic、roughness、occlusion、emissive。
2. Tangent attribute / tangent generation，支持正确 tangent-space normal map。
3. 使用 `assets/models/DamagedHelmet/` 做真实 glTF 2.0 默认或可选 sandbox 验证。
4. glTF sampler 参数和 texture transform。
5. pipeline / shader / descriptor layout deferred destruction。
6. renderer 级资源 / scene 加载入口。
7. RenderGraph 第一版 pass/resource 声明。
