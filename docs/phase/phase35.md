# Phase 0.35 ForwardPass Diffuse Irradiance IBL

## 阶段判断

Phase 0.34 已经完成 diffuse irradiance cubemap generation foundation：

```text
EnvironmentResource
    -> EnvironmentCubeConverter
        -> DefaultSandboxEnvironmentCube
    -> EnvironmentIrradianceGenerator
        -> DefaultSandboxIrradianceCube
```

但当前 `ForwardPass` 仍然没有使用这个 irradiance cubemap。mesh shader 的 ambient lighting 仍然走旧的 equirectangular 2D environment 采样路径：

```hlsl
Texture2D<float4> g_EnvironmentTexture;
SamplerState g_EnvironmentSampler;

float3 sampleEnvironment(float3 direction) {
    return g_EnvironmentTexture.Sample(g_EnvironmentSampler, directionToEquirectUv(direction)).rgb;
}
```

这条路径只是在 normal direction 上做一次环境采样，不是半球积分后的 diffuse irradiance。Phase 0.34 已经把半球积分结果 bake 到低分辨率 cubemap，因此下一阶段最自然的工作是把该 cubemap 接入 `ForwardPass`，让 mesh lighting 真正使用 diffuse irradiance IBL。

Phase 0.35 仍然不是完整 IBL。它只做 diffuse ambient IBL，不做 specular prefilter、BRDF LUT、roughness mip sampling 或 image-based specular。

## 目标

Phase 0.35 目标：

- 在 `FrameContext` 中新增非拥有 `EnvironmentCubeResource* irradianceCube`。
- 默认 renderer 在 `DefaultSandboxIrradianceCube` 生成成功后，把 irradiance cubemap 传给 frame context。
- `ForwardPass` 新增 irradiance cubemap sampled image / sampler binding。
- `ForwardPass` 创建 fallback irradiance cubemap，保证 descriptor set 始终完整。
- `mesh.frag.hlsl` 新增 `TextureCube<float4>` irradiance sampling path。
- `evaluateAmbientLighting()` 优先使用 irradiance cubemap 作为 diffuse ambient IBL。
- 旧 equirectangular 2D environment path 保留为 fallback / 过渡路径。
- smoke tests 覆盖 descriptor layout、fallback、scene irradiance binding、lighting uniform、shader token 和 runtime smoke。
- 文档明确本阶段仍不做 specular IBL、prefilter、BRDF LUT、roughness mip sampling。

## 非目标

Phase 0.35 暂不做：

- 不做 prefiltered specular environment map。
- 不做 BRDF LUT。
- 不做 specular IBL。
- 不做 roughness-based mip sampling。
- 不做 irradiance cubemap 动态切换 API。
- 不把 `RenderScene::SceneEnvironment` 扩展成完整 environment asset object。
- 不做 cubemap face orientation pixel/readback test。
- 不做 camera controller。
- 不做 bloom / auto exposure / ACES。
- 不做 RenderGraph 重构。
- 不做 bindless descriptor。
- 不做完整 renderer resource manager。

## 当前基线

### FrameContext

当前 `FrameContext` 已有：

```cpp
EnvironmentCubeResource* environmentCube = nullptr;
```

该指针只服务于 `SkyboxPass`，表示当前 frame 可见的 source environment cubemap。Phase 0.35 建议新增：

```cpp
EnvironmentCubeResource* irradianceCube = nullptr;
```

语义：

- `environmentCube`：清晰 source cubemap，用于 skybox background。
- `irradianceCube`：低频 diffuse irradiance cubemap，用于 mesh ambient IBL。
- 两者都是非拥有指针。
- 两者可以同时为空。
- Phase 0.35 只由默认 renderer 填充默认 irradiance cubemap，不扩展通用 scene API。

### Renderer 默认链路

当前默认 renderer 已有：

```cpp
EnvironmentCubeResource m_DefaultEnvironmentCube;
EnvironmentCubeResource m_DefaultIrradianceCube;
bool m_DefaultEnvironmentCubeConverted = false;
bool m_DefaultIrradianceCubeGenerated = false;
```

Phase 0.35 建议新增：

```cpp
EnvironmentCubeResource* resolveFrameIrradianceCube(RenderScene& renderScene);
```

返回条件：

- `m_DefaultIrradianceCubeGenerated == true`
- 当前 `RenderScene::environment().environment == &m_DefaultEnvironment`
- `m_DefaultIrradianceCube.isValid()`

然后在 `DefaultRenderer::render()` 中设置：

```cpp
frameContext.environmentCube = resolveFrameEnvironmentCube(renderScene);
frameContext.irradianceCube = resolveFrameIrradianceCube(renderScene);
```

### ForwardPass

当前 `ForwardPass` descriptor layout 中，environment 2D texture 使用：

```text
binding 14: sampled image
binding 15: sampler
```

建议新增：

```text
binding 16: irradiance cubemap sampled image
binding 17: irradiance cubemap sampler
```

不建议直接复用 14/15，因为 `Texture2D` 和 `TextureCube` shader resource 类型不同，且保留 14/15 可以在没有 irradiance cube 时保持旧 fallback 行为。

### Mesh Shader

当前 `mesh.frag.hlsl` ambient：

```hlsl
float3 evaluateAmbientLighting(float3 normal, float3 albedo) {
    if (g_Lighting.environment.y > 0.5f) {
        const float3 environmentRadiance = sampleEnvironment(normal) * g_Lighting.environment.x;
        return environmentRadiance * albedo;
    }

    return g_Lighting.ambientColor.rgb * albedo;
}
```

Phase 0.35 建议改成：

```hlsl
TextureCube<float4> g_IrradianceCube;
SamplerState g_IrradianceSampler;

float3 sampleIrradiance(float3 normal) {
    return g_IrradianceCube.Sample(g_IrradianceSampler, normalize(normal)).rgb;
}

float3 evaluateAmbientLighting(float3 normal, float3 albedo) {
    if (g_Lighting.environment.y > 0.5f) {
        const float3 irradiance = sampleIrradiance(normal) * g_Lighting.environment.x;
        return irradiance * albedo;
    }

    return g_Lighting.ambientColor.rgb * albedo;
}
```

更严格的 PBR 会把 metallic / Fresnel / kd 也带入 ambient diffuse 分量：

```hlsl
diffuseIBL = kd * albedo * irradiance;
```

但 Phase 0.35 第一版建议控制范围，先替换环境采样来源，保留当前 direct BRDF 结构，避免同时重构 `evaluateDirectLighting()` 的参数流。后续 Phase 可以把 ambient diffuse 与 direct BRDF 的 `kd` 统一整理。

## 建议设计

### ForwardPass Fallback Irradiance

需要新增 fallback cubemap，原因是 descriptor set 必须始终完整，且 shader binding 16/17 不能悬空。

推荐新增成员：

```cpp
EnvironmentCubeResource m_FallbackIrradianceCube;
```

fallback cubemap 建议：

```text
faceExtent = 1x1
format = RGBA16Float 或 RGBA32Float
mipLevels = 1
color = black 或 very low ambient gray
```

注意当前 `EnvironmentCubeResource` 只能创建空 cubemap，不能直接 CPU 上传 cubemap faces。Phase 0.35 有两个可选方案：

1. **推荐方案：复用 generated default irradiance path，缺失时保留旧 equirectangular ambient。**
   - descriptor 仍需要 fallback cube。
   - fallback cube 内容不重要，只在 shader disabled path 或 old fallback path 下不影响结果。
   - 若没有真实 irradiance cube，则 `g_Lighting.environment.z` 或类似 flag 表示 irradiance disabled。

2. **扩展 RHI / EnvironmentCubeResource 支持 CPU upload cubemap faces。**
   - 范围偏大，不建议 Phase 0.35 做。

因此建议扩展 `LightingUniform.environment`：

```cpp
// x = intensity
// y = environment enabled
// z = irradiance enabled
// w = reserved
glm::vec4 environment;
```

shader 行为：

```hlsl
if (g_Lighting.environment.z > 0.5f) {
    return sampleIrradiance(normal) * intensity * albedo;
}

if (g_Lighting.environment.y > 0.5f) {
    return sampleEnvironment(normal) * intensity * albedo;
}

return ambientColor * albedo;
```

这样即使 fallback irradiance cubemap 是未初始化 GPU texture，也不会在 disabled path 采样它。

### Descriptor Update

`ForwardPass::updateDrawDescriptorSet()` 当前绑定 material textures、camera/object/material/lighting uniform 和 equirectangular environment。

Phase 0.35 建议新增：

```cpp
EnvironmentCubeResource* irradiance = resolveIrradianceResource(frameContext);

descriptors.descriptorSet->updateSampledImage(16, irradiance->textureView());
descriptors.descriptorSet->updateSampler(17, irradiance->sampler());
```

同时新增：

```cpp
EnvironmentCubeResource* resolveIrradianceResource(FrameContext& frameContext);
bool ensureFallbackIrradianceCube();
```

`resolveIrradianceResource()` 只负责 descriptor 完整性；是否真正使用 irradiance 由 lighting uniform flag 决定。

### Lighting Uniform

当前 `LightingUniform` size 是 80 bytes：

```cpp
struct alignas(16) LightingUniform {
    glm::vec4 lightDirection;
    glm::vec4 lightColor;
    glm::vec4 ambientColor;
    glm::vec4 cameraPosition;
    glm::vec4 environment;
};
```

无需改 size，只复用 `environment.z`：

```text
environment.x = intensity
environment.y = environment enabled
environment.z = irradiance enabled
environment.w = reserved
```

`makeLightingUniform()`：

- 当 scene environment ready：`y = 1`
- 当 `frameContext.irradianceCube` valid：`z = 1`
- intensity 仍来自 `SceneEnvironment::intensity`

注意：如果 scene environment disabled，即使有 fallback irradiance cube，也不应启用 irradiance lighting。

### Shader Binding

`mesh.frag.hlsl` 新增：

```hlsl
[[vk::binding(16, 0)]]
TextureCube<float4> g_IrradianceCube;

[[vk::binding(17, 0)]]
SamplerState g_IrradianceSampler;
```

`shader_assets_smoke` 需要检查：

- `TextureCube<float4> g_IrradianceCube`
- `SamplerState g_IrradianceSampler`
- `sampleIrradiance`
- `g_Lighting.environment.z`
- 仍保留 `Texture2D<float4> g_EnvironmentTexture`
- 仍保留 `directionToEquirectUv` fallback

## 摄像机交互判断

摄像机交互仍然重要，但建议不放进 Phase 0.35。

原因：

- Phase 0.35 的核心是把 Phase 0.34 生成的 lighting resource 接入 mesh shader，形成 PBR/IBL 主线闭环。
- 摄像机交互属于 sandbox/debug experience，应该单独做，避免把 input/window API 和 renderer lighting 改动混在一起。
- diffuse irradiance IBL 接入后，camera controller 的价值会更高，因为可以从多角度观察环境光对模型的影响。

推荐后续：

```text
Phase 0.36 Sandbox Orbit Camera Controller
```

第一版 camera controller 放在 app/sandbox 层，让 `RenderView` 继续只做 view/projection 数据载体，renderer 不直接依赖 GLFW/input。

## 测试策略

### ForwardPass Pipeline Smoke

扩展 `tests/forward_pass_pipeline_smoke.cpp`：

- descriptor layout 包含 binding 16/17。
- fallback path 下：
  - equirectangular environment descriptor 仍完整。
  - irradiance descriptor 也完整。
  - lighting uniform `environment.z == 0`。
- scene irradiance path 下：
  - `frameContext.irradianceCube` 指向 valid cube。
  - descriptor binding 16 使用 irradiance cube texture view。
  - descriptor binding 17 使用 irradiance cube sampler。
  - lighting uniform `environment.z == 1`。
- existing alpha / doubleSided / frame format pipeline tests 不回退。

### Shader Asset Smoke

扩展 `tests/shader_assets_smoke.cpp`：

- mesh fragment shader token：
  - `TextureCube<float4> g_IrradianceCube`
  - `SamplerState g_IrradianceSampler`
  - `sampleIrradiance`
  - `g_Lighting.environment.z`
  - `Texture2D<float4> g_EnvironmentTexture`
  - `directionToEquirectUv`
  - `evaluateAmbientLighting`

### Framework Headers Smoke

扩展 `tests/framework_headers_smoke.cpp`：

- touch `FrameContext::irradianceCube`。
- touch `EnvironmentCubeResource::textureView()` / `sampler()` 作为 ForwardPass irradiance input。

### Runtime Smoke

建议验证：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug --target ark_forward_pass_pipeline_smoke ark_shader_assets_smoke ark_framework_headers_smoke ark_environment_irradiance_smoke ark_skybox_pass_smoke
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
build/msvc-vcpkg/Debug/ark_environment_irradiance_smoke.exe
build/msvc-vcpkg/Debug/ark_skybox_pass_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

runtime smoke 仍只验证不启动即退出，不等于视觉正确性测试。若条件允许，建议手动打开 DamagedHelmet 对比环境光变化。

## 实施顺序

### 0.35.0 文档与范围确认

目标：

- 新增 `docs/phase/phase35.md`。
- 明确本阶段只做 diffuse irradiance cubemap 接入 `ForwardPass`。
- 明确暂不做 specular IBL / prefilter / BRDF LUT / camera controller。

审核点：

- 不扩大到完整 IBL。
- 不引入 RenderGraph / bindless。
- 不把摄像机交互混入本阶段。

### 0.35.1 FrameContext Irradiance Input

目标：

- `FrameContext` 新增 `EnvironmentCubeResource* irradianceCube`。
- 默认 renderer 新增 `resolveFrameIrradianceCube()`。
- `DefaultRenderer::render()` 填充 `frameContext.irradianceCube`。

审核点：

- 非拥有指针。
- 不扩展通用 scene API。
- 缺失 irradiance 时保持 nullptr。

### 0.35.2 ForwardPass Descriptor Path

目标：

- `ForwardPass` descriptor layout 新增 binding 16/17。
- 新增 fallback irradiance descriptor path。
- `updateDrawDescriptorSet()` 绑定 irradiance cubemap sampled image / sampler。
- `makeLightingUniform()` 写入 `environment.z` irradiance enabled flag。

审核点：

- 14/15 equirectangular environment path 不回退。
- descriptor 始终完整。
- 无 irradiance 时 shader 不采样 fallback cube。

### 0.35.3 Mesh Shader Diffuse IBL

目标：

- `mesh.frag.hlsl` 新增 `TextureCube<float4> g_IrradianceCube`。
- 新增 `sampleIrradiance()`。
- `evaluateAmbientLighting()` 优先使用 irradiance cube。
- 保留 equirectangular fallback。

审核点：

- 输出仍为 linear HDR。
- 不做 tone mapping / gamma。
- 不做 specular IBL。
- 不破坏 alpha discard / blend path。

### 0.35.4 Tests

目标：

- 更新 `ark_forward_pass_pipeline_smoke`。
- 更新 `ark_shader_assets_smoke`。
- 更新 `ark_framework_headers_smoke`。
- 保持 `ark_environment_irradiance_smoke`、`ark_skybox_pass_smoke` 通过。

审核点：

- fake RHI test 不依赖真实 Vulkan。
- fallback path 和 valid irradiance path 都覆盖。
- shader token 覆盖新 binding 和 fallback 保留。

### 0.35.5 验证与收尾

目标：

- 完整 build。
- CTest 全量通过。
- runtime smoke 通过。
- 更新 `docs/codex_handoff.md`。
- 文档记录 Phase 0.35 后仍未支持：
  - specular IBL
  - prefiltered specular
  - BRDF LUT
  - roughness mip sampling
  - camera controller
  - cubemap orientation pixel validation

审核点：

- 不把 diffuse IBL 写成完整 IBL。
- 不把 runtime smoke 写成视觉正确性自动验证。
- 下一步建议明确 camera controller 或 specular IBL 前置。

## 实施结果

Phase 0.35 已完成 0.35.0 ~ 0.35.5：

- `FrameContext` 已新增非拥有 `EnvironmentCubeResource* irradianceCube`。
- 默认 renderer 在 `DefaultSandboxIrradianceCube` 生成成功后通过 `resolveFrameIrradianceCube()` 传入 frame context。
- `ForwardPass` descriptor layout 已新增 binding 16/17，用于 irradiance cubemap sampled image / sampler。
- `ForwardPass` 已新增 1x1 `RGBA16Float` fallback irradiance cubemap，保证 descriptor set 完整；无真实 irradiance 时 `environment.z = 0`，shader 不采样 fallback cube。
- `LightingUniform.environment` 语义更新为 `x = intensity`、`y = environment enabled`、`z = irradiance enabled`、`w = reserved`。
- `mesh.frag.hlsl` 已新增 `TextureCube<float4> g_IrradianceCube`、`SamplerState g_IrradianceSampler` 和 `sampleIrradiance()`。
- `evaluateAmbientLighting()` 现在优先使用 irradiance cubemap 做 diffuse ambient IBL；没有 irradiance cubemap 时保留 equirectangular environment fallback；没有 environment 时保留 scene ambient fallback。
- 本阶段仍不做 prefiltered specular、BRDF LUT、specular IBL、roughness mip sampling、camera controller、bloom 或 auto exposure。

## 验证记录

Windows/MSVC/vcpkg/DXC debug preset 下已完成：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_forward_pass_pipeline_smoke ark_shader_assets_smoke ark_framework_headers_smoke ark_environment_irradiance_smoke ark_skybox_pass_smoke
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
build/msvc-vcpkg/Debug/ark_environment_irradiance_smoke.exe
build/msvc-vcpkg/Debug/ark_skybox_pass_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
targeted smoke build passed
ark_forward_pass_pipeline_smoke passed
ark_shader_assets_smoke passed
ark_framework_headers_smoke passed
ark_environment_irradiance_smoke passed
ark_skybox_pass_smoke passed
full build passed
CTest: 15/15 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

`ark_environment_irradiance_smoke` 中的 `InvalidSameCubeIrradiance` error log 是测试刻意触发非法输入路径，用于验证 `source == target` 会被拒绝，进程退出码为 0。runtime smoke 仍只验证启动链路和初始化稳定性，不等同于自动视觉正确性验证。

## 完成标准

Phase 0.35 完成时应满足：

- 默认 renderer 能把 generated irradiance cubemap 传入 frame context。
- `ForwardPass` descriptor layout 支持 irradiance cubemap binding 16/17。
- mesh shader 使用 `TextureCube` irradiance path 做 diffuse ambient IBL。
- 没有 irradiance cubemap 时仍能 fallback。
- equirectangular environment path 不回退。
- skybox path 不回退。
- build / CTest / runtime smoke 通过。
- handoff 明确当前只完成 diffuse IBL，不是完整 IBL。

## 后续 Phase 建议

Phase 0.35 后建议：

1. **Sandbox Orbit Camera Controller**
   - 优先提升调试体验，方便观察 IBL、skybox orientation 和材质响应。
2. **Cubemap Face Orientation Fixture / Pixel Validation**
   - 用小型 debug HDR 或 readback/pixel test 严格验证 face order / flip。
3. **Prefiltered Specular Environment + BRDF LUT**
   - 进入 specular IBL 前置资源。
4. **ForwardPass Specular IBL**
   - 接入 roughness mip sampling 和 split-sum BRDF。
5. **Bloom / Auto Exposure / ACES**
   - 后处理和色调映射质量提升。
