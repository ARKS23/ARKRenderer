# Phase 0.15 glTF PBR Material 参数最小闭环

## 阶段判断

Phase 0.14 已经完成 texture mipmap upload / generation 的最小闭环：

```text
TextureResource
    -> mip count
    -> full mip texture/view
    -> upload mip0
    -> DeviceContext::generateTextureMips()
    -> Vulkan per-mip barrier + vkCmdBlitImage
    -> ShaderResource sampling
```

这说明 base color texture 的基础采样质量已经稳定，可以继续推进 glTF material 语义。但当前材质路径仍停留在 baseColor texture-only：

- `asset::MaterialData` 只有 `baseColorTexturePath`。
- `GltfLoader` 只读取 `pbrMetallicRoughness.baseColorTexture`。
- `MaterialResource` 只保存 `TextureResource*`，没有保存材质参数。
- `ForwardPass` descriptor set 没有 material uniform。
- `mesh.frag.hlsl` 只返回 base color texture sample，没有使用 glTF factors。

因此 Phase 0.15 不应直接进入 normal / metallicRoughness / emissive 多 texture，也不应直接实现完整 PBR。当前更合理的主线是：先把 glTF 2.0 core PBR 的最小 scalar/vector 参数打通，让 asset 层、renderer 层、descriptor/uniform 和 shader 之间形成可审核的数据流。

## 目标

Phase 0.15 的目标是在不扩大 texture 数量和不引入完整 PBR 光照的前提下完成以下能力：

- `asset::MaterialData` 支持 `baseColorFactor`、`metallicFactor`、`roughnessFactor`。
- `GltfLoader` 读取 glTF 2.0 `pbrMetallicRoughness` 的 core material factors。
- 缺省值遵循 glTF 2.0 语义：
  - `baseColorFactor = (1, 1, 1, 1)`
  - `metallicFactor = 1`
  - `roughnessFactor = 1`
- `MaterialResource` 保存材质参数，并继续只引用 baseColor `TextureResource`。
- `ForwardPass` 增加最小 material uniform 绑定，让每个 draw 可以上传对应 material factors。
- `mesh.frag.hlsl` 至少使用 `baseColorFactor` 调制 base color texture。
- `metallicFactor` / `roughnessFactor` 先进入数据链路和 uniform，供后续基础光照 / PBR 使用。
- smoke tests 覆盖 glTF factor 读取、默认值、MaterialResource 参数保存和默认渲染路径不回退。
- 文档记录多 texture、normal map、metallicRoughness map、emissive、occlusion 和完整 PBR 留给后续 phase。

## 非目标

Phase 0.15 暂不做以下内容：

- 不做完整 PBR BRDF。
- 不做环境光、IBL、tone mapping 或 exposure。
- 不接入 normal texture。
- 不接入 metallicRoughness texture。
- 不接入 occlusion texture。
- 不接入 emissive texture。
- 不解析 glTF sampler 参数。
- 不实现 glTF texture transform。
- 不支持 embedded image 或 data URI image。
- 不引入 bindless、descriptor indexing 或 global texture array。
- 不引入 RenderGraph。
- 不把 `assets/models/DamagedHelmet/` 直接切成默认 sandbox 资源。

`DamagedHelmet` 可以作为后续真实资产验证对象，但 Phase 0.15 只负责 material factors，不保证该模型的完整 PBR 观感正确。

## 模块边界

Phase 0.15 继续遵守：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase11.md
docs/phase/phase12.md
docs/phase/phase13.md
docs/phase/phase14.md
```

硬性边界：

- `asset/` 只解析 glTF 并输出 CPU material 数据，不创建 RHI/GPU 资源。
- `TextureLoader` 仍只输出 CPU image，不参与 material factor 或 shader uniform。
- `renderer/` 可以保存 material 参数并创建 uniform buffer，但不接触 Vulkan 类型。
- `rhi/` 只表达 uniform buffer、descriptor、pipeline 等 API 无关语义。
- `rhi/vulkan/` 只处理 descriptor / pipeline / command 的 Vulkan 映射。
- `MaterialResource` 保存 material 语义和 texture 引用，负责 descriptor 写入所需数据准备。
- `ForwardPass` 负责 per-frame / per-draw descriptor resources 和 draw，不负责 glTF 加载或 texture cache ownership。
- upload、mip generation、deferred release 仍必须记录在 dynamic rendering scope 外。
- 当前继续使用 Vulkan Dynamic Rendering，不引入传统 `VkRenderPass` / `VkFramebuffer`。

## 推荐数据流

Phase 0.15 目标数据流：

```text
glTF material.pbrMetallicRoughness
    -> asset::MaterialData
        -> baseColorTexturePath
        -> baseColorFactor
        -> metallicFactor
        -> roughnessFactor

ModelResource::create(device, textureCache, model)
    -> TextureCache::getOrCreate(baseColor texture, sRGB)
    -> MaterialResource::create(materialData, baseColorTexture)
        -> stores factors
        -> stores TextureResource*

ForwardPass::prepare()
    -> MaterialResource::upload()
        -> TextureResource::upload()
    -> update draw descriptor set
        -> camera uniform
        -> sampled image + sampler
        -> object uniform
        -> material uniform

ForwardPass::execute()
    -> bind descriptor
    -> draw indexed

mesh.frag.hlsl
    -> baseColor = texture.Sample(...) * baseColorFactor
```

## 设计建议

### asset::MaterialData

建议扩展：

```cpp
struct MaterialData {
    Path baseColorTexturePath;
    float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    std::string debugName;
};
```

注意：
- 保持 asset 层纯数据。
- 不引入 glm，避免 asset 层依赖 renderer/math 类型。
- 不在 asset 层判断 sRGB / linear GPU format。
- 不在本阶段强制支持无 baseColorTexture 的 material；是否支持纯 factor material 可作为后续扩展记录。

### GltfLoader

建议在 `loadMaterial()` 中读取：

```text
gltfMaterial.pbrMetallicRoughness.baseColorFactor
gltfMaterial.pbrMetallicRoughness.metallicFactor
gltfMaterial.pbrMetallicRoughness.roughnessFactor
```

审核检查点：
- `baseColorFactor` 只有 size 为 4 时才逐项读取，否则保留默认值。
- `metallicFactor` / `roughnessFactor` 使用 tinygltf 提供的值；缺省时 tinygltf 通常已填默认值，但实现上仍应保持默认安全。
- 不要读取 normal / metallicRoughness texture。
- 不要改变当前 external baseColorTexture 要求，避免本阶段范围扩大到 fallback texture。

### MaterialResource

建议新增 renderer 层 material 参数结构：

```cpp
struct MaterialFactors {
    float baseColorFactor[4];
    float metallicFactor;
    float roughnessFactor;
};
```

`MaterialResource` 职责：
- 保存 `MaterialFactors`。
- 保存 baseColor `TextureResource*`。
- `upload()` 仍只触发 texture upload。
- 新增只读 accessor，让 ForwardPass 创建 material uniform。

不建议让 `MaterialResource` 自己创建 uniform buffer。原因是当前 descriptor set 是 ForwardPass per draw 管理；uniform buffer 生命周期和 frame slot 更适合继续留在 pass 内。

### ForwardPass descriptor layout

当前 binding：

```text
set 0 binding 0: CameraUniformBuffer
set 0 binding 1: SampledImage
set 0 binding 2: Sampler
set 0 binding 3: ObjectUniformBuffer
```

建议新增：

```text
set 0 binding 4: MaterialUniformBuffer
```

`MaterialUniform` 建议满足 16-byte 对齐：

```cpp
struct alignas(16) MaterialUniform {
    glm::vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float padding[2];
};
```

注意：
- 当前 per-draw descriptor resources 已经存在 object uniform buffer，可以顺势增加 material uniform buffer。
- 每个 draw 每个 frame slot 拥有独立 material uniform buffer，避免覆盖 in-flight draw 数据。
- 本阶段先保持简单直接；后续 draw 数增多后再评估 material-level descriptor、dynamic uniform 或 storage buffer。

### Shader

建议最小修改 `mesh.frag.hlsl`：

```hlsl
struct MaterialUniform {
    float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float2 padding;
};

[[vk::binding(4, 0)]]
ConstantBuffer<MaterialUniform> g_Material;

float4 main(PSInput input) : SV_Target0 {
    return g_BaseColorTexture.Sample(g_BaseColorSampler, input.uv0) * g_Material.baseColorFactor;
}
```

`metallicFactor` / `roughnessFactor` 暂时不参与输出，原因是当前没有 lighting/PBR。为了避免编译器警告或后续误解，可以在文档中明确它们是后续 PBR 输入。

### TextureCache

Phase 0.15 不需要修改 `TextureCache` key。当前仍然：

```text
normalized path + TextureColorSpace
```

原因：
- material factors 不改变 texture resource。
- sampler 参数仍未接入。
- mip 策略当前统一默认生成 mip。

## 实施顺序

### 0.15.0 文档与范围确认

目标：
- 新增 `docs/phase/phase15.md`。
- 明确主线是 glTF material factors 最小闭环。
- 明确不进入多 texture、完整 PBR 或 RenderGraph。

当前实现状态：

- 文档已创建，范围已确认；本轮推进 0.15.1 ~ 0.15.5。

### 0.15.1 MaterialData factors

目标：
- `asset::MaterialData` 新增 baseColor / metallic / roughness factors。
- 补充必要注释。
- 更新 framework/header smoke。

审核检查点：
- asset 层不依赖 renderer/RHI/Vulkan。
- 默认值符合 glTF 2.0。
- 不破坏现有只使用 baseColorTexturePath 的 fixture。

当前实现状态：

- 已完成：`MaterialData` 新增 `baseColorFactor`、`metallicFactor`、`roughnessFactor`，默认值遵循 glTF 2.0。
- 已完成：字段保持 POD/CPU 数据，不引入 renderer/RHI/Vulkan 依赖。

### 0.15.2 GltfLoader material factor parsing

目标：
- 读取 glTF 2.0 `pbrMetallicRoughness` factors。
- 保留 external baseColorTexture 路径要求。
- 扩展或新增 glTF fixture 覆盖 factor。

审核检查点：
- 缺省值正确。
- `baseColorFactor` 长度异常时不越界。
- 不接入多 texture。

当前实现状态：

- 已完成：`GltfLoader::loadMaterial()` 读取 `pbrMetallicRoughness` factors。
- 已完成：`forward_fixture.gltf` 覆盖非默认 factors；`forward_multinode_fixture.gltf` 继续覆盖默认 factors。

### 0.15.3 MaterialResource 参数保存

目标：
- `MaterialResource` 保存 material factors。
- 提供只读 accessor 给 ForwardPass。
- `upload()` 行为仍只上传 texture。

审核检查点：
- MaterialResource 不创建 uniform buffer。
- MaterialResource 不拥有 TextureResource 生命周期。
- reset/deferred release 语义不受影响。

当前实现状态：

- 已完成：新增 `MaterialFactors`，`MaterialResource` 在 `create()` 时保存 factors。
- 已完成：新增只读 accessor 给 ForwardPass 使用；`upload()` 仍只触发 base color texture upload。

### 0.15.4 ForwardPass material uniform

目标：
- descriptor layout 新增 binding 4。
- per-draw descriptor resources 增加 material uniform buffer。
- prepare/update descriptor 时写 material uniform binding。
- execute/draw 前更新 material uniform 数据。

审核检查点：
- shader binding 与 descriptor layout 一致。
- per-frame/per-draw buffer 不覆盖 in-flight 数据。
- descriptor set 增长后 descriptor pool 行为不回退。

当前实现状态：

- 已完成：ForwardPass descriptor layout 新增 set 0 binding 4，类型为 fragment uniform buffer。
- 已完成：per-frame/per-draw descriptor resources 新增 material uniform buffer。
- 已完成：descriptor update 写入 binding 4，draw 前更新当前 material factors。

### 0.15.5 Shader 最小接入

目标：
- `mesh.frag.hlsl` 增加 material uniform。
- base color sample 乘以 `baseColorFactor`。
- 编译 shader assets。

审核检查点：
- `baseColorFactor.a` 语义保留。
- 不假装实现 PBR。
- metallic/roughness 进入 uniform 但暂不影响输出，需要文档记录。

当前实现状态：

- 已完成：`mesh.frag.hlsl` 新增 `MaterialUniform`，binding 与 ForwardPass descriptor layout 对齐。
- 已完成：fragment 输出使用 `baseColorTexture * baseColorFactor`。
- 说明：`metallicFactor` / `roughnessFactor` 只进入 uniform，当前 shader 不做 lighting/PBR；`baseColorFactor.a` 只写入输出 alpha，pipeline blending 不是本阶段范围。

### 0.15.6 Tests 与 sandbox smoke

目标：
- glTF loader smoke 覆盖 material factors。
- model/material resource smoke 覆盖 factor 保存和 material uniform 更新。
- build、ctest、sandbox smoke 通过。

建议运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

涉及 shader 或默认渲染路径时运行 sandbox smoke。

当前实现状态：

- 已完成：`gltf_loader_smoke` 覆盖显式 material factors 和 glTF 默认 factors。
- 已完成：`model_resource_smoke` 覆盖 `MaterialResource` 保存 factors。
- 已完成：`cmake --build --preset msvc-vcpkg-local-debug` 通过。
- 已完成：`ctest --preset msvc-vcpkg-local-debug` 通过，8/8 tests passed。
- 已完成：`ark_sandbox` Vulkan smoke 通过，默认 `forward_multinode_fixture.gltf` 正常加载并完成 renderer 初始化。

### 0.15.7 Phase 0.15 收尾

目标：
- 更新本文档当前实现状态。
- 按需同步 `docs/codex_handoff.md`。
- 记录后续 TODO：多 texture、normal map、metallicRoughness map、emissive、occlusion、基础光照/PBR。

当前实现状态：

- 已完成：Phase 0.15 文档记录 0.15.1 ~ 0.15.6 的落地状态和验证结果。
- 未同步：`docs/codex_handoff.md` 本轮未更新，等待明确交接同步请求。
- 后续 TODO 保持：normal / metallicRoughness / emissive / occlusion textures、基础光照/PBR、真实 `DamagedHelmet` 资产验证、pipeline/shader/descriptor layout deferred destruction。

## 审核检查点

- `asset/` 只输出 CPU material 数据。
- `renderer/` 不接触 Vulkan 类型。
- `MaterialResource` 不接管 texture ownership。
- `ForwardPass` 不接管 asset loading / texture cache。
- descriptor layout 与 shader binding 一致。
- material uniform buffer 具备 16-byte 对齐。
- baseColorFactor 默认值不改变旧 fixture 输出。
- metallic/roughness 不被错误解释为已经完成 PBR。
- 现有 texture mipmap generation 行为不回退。
- 现有 model deferred reset 行为不回退。

## 验证计划

实现后运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

涉及 shader 或默认 sandbox 渲染路径时，继续运行 sandbox smoke：

```powershell
$stdout = "build\msvc-vcpkg\ark_sandbox_stdout.log"
$stderr = "build\msvc-vcpkg\ark_sandbox_stderr.log"
Remove-Item -LiteralPath $stdout,$stderr -Force -ErrorAction SilentlyContinue
$process = Start-Process -FilePath "build\msvc-vcpkg\Debug\ark_sandbox.exe" -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr
Start-Sleep -Seconds 3
if ($process.HasExited) {
    Write-Output "ark_sandbox exited early with code $($process.ExitCode)"
    Get-Content -Path $stdout,$stderr -ErrorAction SilentlyContinue
    exit $process.ExitCode
} else {
    Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300
    Write-Output "ark_sandbox started successfully and was stopped after smoke check"
    Get-Content -Path $stdout,$stderr -ErrorAction SilentlyContinue
}
```

## 完成标准

Phase 0.15 完成时应满足：

- `asset::MaterialData` 表达 glTF baseColor / metallic / roughness factors。
- `GltfLoader` 正确读取 glTF 2.0 material factors。
- `MaterialResource` 保存 factors 并继续只引用 TextureResource。
- `ForwardPass` 能上传 per-draw material uniform。
- `mesh.frag.hlsl` 使用 baseColorFactor 调制 base color sample。
- build、ctest 和必要 sandbox smoke 通过。
- 文档明确 metallic/roughness 只是 PBR 输入数据，完整 PBR 留给后续 phase。

## 后续 Phase 建议

Phase 0.15 完成后，建议继续：

1. glTF normal / metallicRoughness / emissive texture 支持，明确 color / non-color texture 语义。
2. 最小基础光照或 PBR shader，使用 normal、baseColor、metallic、roughness。
3. 使用 `assets/models/DamagedHelmet/` 做真实 glTF 2.0 资产验证。
4. renderer 级资源 / scene 加载入口，替代内部默认 scene 过渡方案。
5. pipeline / shader / descriptor layout 的 deferred destruction。
6. RenderGraph 第一版 pass/resource 声明。
