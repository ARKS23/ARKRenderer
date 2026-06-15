# Phase 0.43 ForwardPass Specular IBL

## 阶段判断

Phase 0.42 已经完成 renderer default specular bake path。默认 renderer 现在能在 environment cubemap conversion 之后生成：

```text
DefaultSandboxSpecularCube
    256x256 RGBA16Float full mip chain

DefaultSandboxBrdfLut
    256x256 RGBA16Float 2D LUT
```

并通过 `FrameContext` 传递：

```cpp
EnvironmentCubeResource* prefilteredSpecularCube = nullptr;
EnvironmentBrdfLutResource* brdfLut = nullptr;
```

但是 `ForwardPass` 仍只消费：

```text
binding 14: equirectangular environment sampled image
binding 15: equirectangular environment sampler
binding 16: diffuse irradiance cubemap sampled image
binding 17: diffuse irradiance cubemap sampler
```

`mesh.frag.hlsl` 当前 ambient 仍是 diffuse-only：

```text
if irradiance cube exists:
    diffuse ambient = irradiance * albedo
else if equirectangular environment exists:
    fallback ambient = environment(normal) * albedo
else:
    fallback ambient = scene ambient * albedo
```

因此 Phase 0.43 的核心目标是让 `ForwardPass` 真正消费 prefiltered specular cubemap 和 BRDF LUT，把 split-sum specular IBL 接入 mesh shader。

## 目标

Phase 0.43 目标：

- `ForwardPass` descriptor layout 增加 prefiltered specular cubemap bindings。
- `ForwardPass` descriptor layout 增加 BRDF LUT bindings。
- `ForwardPass` 支持 specular IBL fallback resources，保证 descriptors 始终完整。
- `ForwardPass` 从 `FrameContext::prefilteredSpecularCube` 和 `FrameContext::brdfLut` 解析 scene/default resources。
- `LightingUniform` 传递 specular IBL enabled flag 和 specular mip count。
- `mesh.frag.hlsl` 增加 split-sum specular IBL 路径。
- `mesh.frag.hlsl` 使用 `roughness` 选择 prefiltered specular mip。
- diffuse IBL 使用 `kd = (1 - F) * (1 - metallic)`，避免金属吃过多 diffuse ambient。
- specular IBL 与 diffuse IBL 组合为 indirect lighting。
- 保持 direct Cook-Torrance BRDF 现有路径。
- 更新 fake `forward_pass_pipeline_smoke` 覆盖 descriptor layout、fallback binding、FrameContext resource binding 和 lighting uniform。
- 更新 `shader_assets_smoke` 覆盖 specular IBL shader token。
- 保证 default sandbox / debug orientation / DamagedHelmet + HDR runtime smoke 继续通过。

## 非目标

Phase 0.43 暂不做：

- 不改 Phase 0.42 的 renderer default bake resource lifetime。
- 不新增或重写 `EnvironmentSpecularPrefilterGenerator`。
- 不新增或重写 `EnvironmentBrdfLutGenerator`。
- 不改变 `FrameRenderer` pass 顺序。
- 不引入 RenderGraph、bindless、descriptor indexing 或 shader reflection。
- 不做 screenshot/golden image infrastructure。
- 不做自动曝光、ACES、bloom、color grading 或 post-process stack。
- 不做 glTF camera、scene camera selection 或 editor camera。
- 不做多光源、shadow、`KHR_lights_punctual`。
- 不做完整材质扩展，如 `KHR_materials_specular`、`KHR_materials_ior`、`KHR_materials_transmission`。
- 不提交新的大型 HDRI 或模型资源。

## 当前基线

### ForwardPass Descriptor Layout

当前 `ForwardPass` 已使用：

```text
binding 0: camera uniform
binding 1: baseColor sampled image
binding 2: baseColor sampler
binding 3: object uniform
binding 4: material uniform
binding 5: normal sampled image
binding 6: normal sampler
binding 7: metallicRoughness sampled image
binding 8: metallicRoughness sampler
binding 9: occlusion sampled image
binding 10: occlusion sampler
binding 11: emissive sampled image
binding 12: emissive sampler
binding 13: lighting uniform
binding 14: equirectangular environment sampled image
binding 15: equirectangular environment sampler
binding 16: irradiance cubemap sampled image
binding 17: irradiance cubemap sampler
```

Phase 0.43 建议顺延：

```text
binding 18: prefiltered specular cubemap sampled image
binding 19: prefiltered specular cubemap sampler
binding 20: BRDF LUT sampled image
binding 21: BRDF LUT sampler
```

这保持 binding 编号连续，减少 shader / fake RHI 测试理解成本。

### FrameContext Resource Inputs

Phase 0.42 已新增：

```cpp
frameContext.prefilteredSpecularCube = resolveFramePrefilteredSpecularCube(renderScene);
frameContext.brdfLut = resolveFrameBrdfLut(renderScene);
```

Phase 0.43 中 `ForwardPass` 应提供 resolver：

```cpp
EnvironmentCubeResource* resolvePrefilteredSpecularResource(FrameContext& frameContext);
EnvironmentBrdfLutResource* resolveBrdfLutResource(FrameContext& frameContext);
```

如果 `FrameContext` resource 缺失或无效，应回退到 pass 内 fallback resource。

### LightingUniform

当前 `LightingUniform::environment` 已有约定：

```text
x: environment intensity
y: equirectangular environment enabled
z: irradiance cube enabled
w: unused
```

Phase 0.43 建议复用 `w`：

```text
w: specular IBL enabled
```

另外还需要传递 max specular mip。为避免破坏既有 `environment` 语义，建议新增一个 vec4：

```cpp
glm::vec4 environmentSpecular;
```

建议布局：

```text
environmentSpecular.x: max prefiltered specular mip
environmentSpecular.y: reserved
environmentSpecular.z: reserved
environmentSpecular.w: reserved
```

这样 `LightingUniform` 从 80 bytes 变为 96 bytes，仍保持 16-byte 对齐。

可选方案是把 max mip 放在 `environment.w`，把 enabled 由 `maxMip > 0` 推断，但不建议这么做：fallback 1x1 specular cube 的 max mip 为 0，仍可能需要显式区分“资源存在但不启用”和“只采 mip0”。

## Shader 方案

### New Bindings

`mesh.frag.hlsl` 新增：

```hlsl
[[vk::binding(18, 0)]]
TextureCube<float4> g_PrefilteredSpecularCube;

[[vk::binding(19, 0)]]
SamplerState g_PrefilteredSpecularSampler;

[[vk::binding(20, 0)]]
Texture2D<float4> g_BrdfLut;

[[vk::binding(21, 0)]]
SamplerState g_BrdfLutSampler;
```

### Fresnel For IBL

Direct lighting 现有 `fresnelSchlick()` 使用 `VdotH`。Specular IBL 常用 view/normal angle：

```hlsl
float3 fresnelSchlickRoughness(float cosTheta, float3 f0, float roughness) {
    return f0 + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), f0) - f0) *
                pow(1.0f - saturate(cosTheta), 5.0f);
}
```

本阶段可以先使用该常见近似，让 rough surface 的 grazing 反射更稳定。

### Specular IBL Function

建议新增：

```hlsl
float3 samplePrefilteredSpecular(float3 reflectionDirection, float roughness) {
    const float maxMip = g_Lighting.environmentSpecular.x;
    const float mipLevel = roughness * maxMip;
    return g_PrefilteredSpecularCube.SampleLevel(
        g_PrefilteredSpecularSampler,
        normalize(reflectionDirection),
        mipLevel
    ).rgb;
}

float2 sampleBrdfLut(float nDotV, float roughness) {
    return g_BrdfLut.Sample(g_BrdfLutSampler, float2(saturate(nDotV), saturate(roughness))).rg;
}
```

BRDF LUT 坐标必须与 Phase 0.41 约定一致：

```text
u = NdotV
v = roughness
```

### Indirect Lighting Function

当前 `evaluateAmbientLighting(float3 normal, float3 albedo)` 不知道 view vector、roughness、metallic 或 f0。Phase 0.43 建议替换为 PBR-aware indirect function：

```hlsl
float3 evaluateIndirectLighting(
    float3 n,
    float3 v,
    float3 albedo,
    float metallic,
    float roughness,
    float3 f0
);
```

其中：

```hlsl
const float nDotV = max(saturate(dot(n, v)), 0.0001f);
const float3 fresnel = fresnelSchlickRoughness(nDotV, f0, roughness);
const float3 kd = (1.0f - fresnel) * (1.0f - metallic);

float3 diffuseIBL = ...;
float3 specularIBL = ...;

return diffuseIBL * kd + specularIBL;
```

注意当前 diffuse irradiance path 已经返回 `irradiance * albedo`，所以 `kd` 应在新 indirect function 中统一乘，不要在 `sampleIrradiance()` 中混入 metallic 语义。

### Fallback Behavior

如果 `g_Lighting.environment.w <= 0.5f`，shader 不应使用 specular IBL：

```hlsl
if (g_Lighting.environment.w > 0.5f) {
    const float3 r = reflect(-v, n);
    const float3 prefiltered = samplePrefilteredSpecular(r, roughness) * g_Lighting.environment.x;
    const float2 brdf = sampleBrdfLut(nDotV, roughness);
    specularIBL = prefiltered * (f0 * brdf.x + brdf.y);
}
```

如果只有 fallback resources 绑定，但 scene environment disabled，则 `environment.w` 应保持 0，shader 输出应与现有 fallback 行为一致。

## ForwardPass 方案

### Fallback Resources

`ForwardPass` 当前已有：

```cpp
EnvironmentResource m_FallbackEnvironment;
EnvironmentCubeResource m_FallbackIrradianceCube;
```

Phase 0.43 建议新增：

```cpp
EnvironmentCubeResource m_FallbackSpecularCube;
EnvironmentBrdfLutResource m_FallbackBrdfLut;
```

fallback specular cubemap 建议：

```text
debugName: ForwardFallbackSpecularCube
faceExtent: 1x1
format: RGBA16Float
mipLevels: 1
```

fallback BRDF LUT 建议：

```text
debugName: ForwardFallbackBrdfLut
extent: 1x1
format: RGBA16Float
```

注意：`EnvironmentBrdfLutResource` 当前创建 render target + sampled view，但没有 CPU upload。fallback BRDF LUT 如果只用于 descriptor completeness 且 `environment.w == 0`，可以不生成实际内容。若 fake / validation 要求 texture state 为 `ShaderResource`，需要用 barrier transition。

### Resource Readiness

新增 helper：

```cpp
bool isPrefilteredSpecularCubeReady(const EnvironmentCubeResource* specularCube);
bool isBrdfLutReady(const EnvironmentBrdfLutResource* brdfLut);
```

readiness 要求：

```text
resource != nullptr
resource->isValid()
resource->textureView() != nullptr
resource->sampler() != nullptr
resource->texture() in ShaderResource state when needed
```

### Upload / Transition

`uploadEnvironmentResources()` 当前确保 fallback environment upload、fallback irradiance transition、scene environment upload。

Phase 0.43 应扩展：

- ensure fallback specular cubemap。
- ensure fallback BRDF LUT。
- transition fallback specular cubemap to `ShaderResource`。
- transition fallback BRDF LUT to `ShaderResource`。

FrameContext provided resources 应该由 Phase 0.42 bake path 转到 `ShaderResource`，`ForwardPass` 只验证并绑定。

### Descriptor Update

`updateDrawDescriptorSet()` 中在 binding 16/17 后新增：

```cpp
EnvironmentCubeResource* specular = resolvePrefilteredSpecularResource(frameContext);
updateSampledImage(18, specular->textureView());
updateSampler(19, specular->sampler());

EnvironmentBrdfLutResource* brdfLut = resolveBrdfLutResource(frameContext);
updateSampledImage(20, brdfLut->textureView());
updateSampler(21, brdfLut->sampler());
```

所有 descriptors 都必须完整绑定，即使 `environment.w == 0`。

### Max Mip

`makeLightingUniform()` 应计算：

```cpp
const bool specularReady = isPrefilteredSpecularCubeReady(frameContext.prefilteredSpecularCube) &&
                           isBrdfLutReady(frameContext.brdfLut);

uniform.environment.w = specularReady ? 1.0f : 0.0f;
uniform.environmentSpecular.x = specularReady
    ? static_cast<float>(frameContext.prefilteredSpecularCube->mipLevels() - 1)
    : 0.0f;
```

如果 scene environment disabled，即使 resources 存在，也建议 specular disabled：

```text
environment enabled && specular resources ready -> specular enabled
otherwise -> specular disabled
```

原因：specular IBL intensity 仍跟 scene environment intensity 绑定；没有 environment 语义时不应突然出现 specular light。

## Tests

### forward_pass_pipeline_smoke

建议扩展 capture：

- descriptor layout contains bindings 18 / 19 / 20 / 21。
- fallback specular image/sampler bound。
- fallback BRDF LUT image/sampler bound。
- fallback specular cube desc：
  - `TextureType::Cube`
  - `RGBA16Float`
  - `1x1`
  - `arrayLayers == 6`
  - `mipLevels == 1`
- fallback BRDF LUT desc：
  - `TextureType::Texture2D`
  - `RGBA16Float`
  - `1x1`
- scene `FrameContext::prefilteredSpecularCube` overrides fallback。
- scene `FrameContext::brdfLut` overrides fallback。
- lighting uniform writes:
  - `environment.w == 0` when no scene environment or no specular resources
  - `environment.w == 1` when scene environment and specular resources are ready
  - `environmentSpecular.x == mipLevels - 1`

如果 fake RHI capture 还没有捕获 binding 18-21，需要扩展对应 fake descriptor set 记录。

### shader_assets_smoke

建议检查 `mesh.frag.hlsl` token：

```text
g_PrefilteredSpecularCube
g_PrefilteredSpecularSampler
g_BrdfLut
g_BrdfLutSampler
SampleLevel
reflect
fresnelSchlickRoughness
environmentSpecular
```

同时保留现有 checks：

- Cook-Torrance direct BRDF token。
- alpha / UV transform token。
- irradiance fallback token。

### Runtime Smoke

继续运行：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

预期：

- 不启动即退出。
- DamagedHelmet 金属区域应开始体现 HDR environment specular contribution。
- `--debug-orientation` 下高光方向可能比较人工化，但不应黑屏或 shader 编译失败。

runtime smoke 不等同于视觉正确性测试。若要确认 roughness mip 是否正确，后续仍需要 roughness / metallic fixture 或 screenshot/readback 验证。

## 实施计划

### 0.43.0 文档与范围确认

- 明确本阶段只做 `ForwardPass` specular IBL。
- 确认 binding 18-21。
- 确认 `LightingUniform` 扩展策略。
- 确认 fallback descriptor completeness 策略。

### 0.43.1 ForwardPass Descriptor And Fallback Resources

- `ForwardPass.h` include / forward declare `EnvironmentBrdfLutResource`。
- 新增 fallback specular cube / fallback BRDF LUT。
- 新增 readiness helpers。
- 新增 resolver helpers。
- descriptor layout 加 binding 18-21。
- upload/transition fallback specular resources。

### 0.43.2 LightingUniform Specular Flags

- 扩展 `LightingUniform`。
- 更新 `static_assert`。
- `makeLightingUniform()` 写入 specular enabled 和 max mip。
- 保持 scene environment disabled 时 specular disabled。

### 0.43.3 Descriptor Binding Path

- `updateDrawDescriptorSet()` 绑定 specular cubemap image/sampler。
- `updateDrawDescriptorSet()` 绑定 BRDF LUT image/sampler。
- fake capture 记录 binding 18-21。
- `forward_pass_pipeline_smoke` 覆盖 fallback 和 FrameContext override。

### 0.43.4 Mesh Shader Specular IBL

- 新增 HLSL bindings 18-21。
- 新增 `fresnelSchlickRoughness()`。
- 新增 BRDF LUT sampling。
- 新增 prefiltered specular `SampleLevel()`。
- 替换 diffuse-only ambient 为 PBR-aware indirect lighting。
- 保持 direct BRDF 路径和 alpha/UV/material path 不变。

### 0.43.5 Tests

- 更新 `shader_assets_smoke`。
- full build。
- CTest。
- default sandbox smoke。
- debug orientation sandbox smoke。
- DamagedHelmet + HDR runtime smoke。

### 0.43.6 验证与收尾

- 自审 descriptor layout 与 HLSL binding 一致性。
- 自审 fallback resource state。
- 自审 `LightingUniform` CPU/HLSL layout 一致性。
- 更新 handoff。
- 记录测试结果。
- 提交和推送。

## 完成标准

Phase 0.43 完成时应满足：

- `ForwardPass` descriptor layout 包含 binding 18-21。
- `ForwardPass` 始终绑定 specular cubemap / BRDF LUT descriptors。
- fallback specular cubemap 和 fallback BRDF LUT 可用。
- `FrameContext::prefilteredSpecularCube` 和 `FrameContext::brdfLut` 可覆盖 fallback。
- `LightingUniform` 正确传递 specular enabled 和 max mip。
- `mesh.frag.hlsl` 消费 prefiltered specular cubemap 和 BRDF LUT。
- shader 使用 roughness mip sampling。
- shader 使用 split-sum BRDF LUT 公式。
- direct lighting、diffuse irradiance、equirectangular fallback、alpha、UV transform 继续工作。
- fake `forward_pass_pipeline_smoke` 覆盖 descriptor 和 uniform 数据流。
- `shader_assets_smoke` 覆盖 shader token。
- CTest 通过。
- runtime smoke 通过。
- handoff 明确后续仍需视觉验证、quality policy 和 screenshot/readback validation。

## 实施结果

Phase 0.43 已完成 0.43.0 ~ 0.43.6：

- `ForwardPass` descriptor layout 新增 binding 18/19，用于 prefiltered specular cubemap image/sampler。
- `ForwardPass` descriptor layout 新增 binding 20/21，用于 BRDF LUT image/sampler。
- `ForwardPass` 新增 fallback specular cubemap 和 fallback BRDF LUT，保证 descriptors 始终完整。
- `ForwardPass` 新增 specular/BRDF LUT readiness helpers 和 resolver helpers。
- `LightingUniform` 扩展 `environmentSpecular`，CPU/HLSL 布局从 80 bytes 扩展到 96 bytes。
- `LightingUniform::environment.w` 现在表示 specular IBL enabled。
- `LightingUniform::environmentSpecular.x` 传递 prefiltered specular max mip。
- `mesh.frag.hlsl` 新增 prefiltered specular cubemap 和 BRDF LUT bindings。
- `mesh.frag.hlsl` 新增 `fresnelSchlickRoughness()`、`samplePrefilteredSpecular()`、`sampleBrdfLut()` 和 PBR-aware `evaluateIndirectLighting()`。
- shader 使用 `SampleLevel()` 按 roughness 采样 prefiltered specular mip。
- shader 使用 split-sum 公式 `prefilteredSpecular * (f0 * brdf.x + brdf.y)`。
- diffuse IBL 现在通过 `kd = (1 - F) * (1 - metallic)` 参与 indirect lighting。
- direct Cook-Torrance BRDF、alpha、UV transform、texture slot path 保持原有职责。
- `forward_pass_pipeline_smoke` 已覆盖 binding 18-21、fallback descriptors、FrameContext specular resources override 和 lighting uniform specular data。
- `shader_assets_smoke` 已覆盖 specular IBL shader tokens。

最终验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_forward_pass_pipeline_smoke ark_shader_assets_smoke
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
targeted forward/shader smoke build passed
ark_forward_pass_pipeline_smoke passed
ark_shader_assets_smoke passed
full build passed
CTest: 21/21 passed
default sandbox smoke passed
debug orientation sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

当前边界：Phase 0.43 完成了 ForwardPass split-sum specular IBL 接入，但仍没有 screenshot/golden image、roughness/metallic fixture、可配置 quality policy、bloom、auto exposure、ACES、shadow 或多光源。

## 风险与注意事项

- `LightingUniform` CPU/HLSL 布局必须保持 16-byte 对齐；修改后要同步 fake test capture struct。
- Descriptor binding 18-21 必须和 HLSL `[[vk::binding]]` 完全一致。
- fallback BRDF LUT 如果不生成内容，只能在 specular disabled 时安全；不要在 shader 中误启用 fallback specular。
- `SampleLevel()` 需要 target cubemap mip chain 有效；默认 Phase 0.42 specular cube 已经 full mip chain，但 fallback 是 mip0。
- diffuse IBL 需要乘 `kd`，否则金属材质会有错误的 diffuse ambient。
- 直接把 ambient 乘 occlusion 会同时压暗 specular IBL；这符合当前 occlusion 简化路径，但不是完整 glTF AO policy。
- 当前没有 screenshot/golden image，runtime smoke 只能验证不崩溃，不证明高光视觉完全正确。

## 后续 Phase 建议

Phase 0.44 建议做 specular IBL validation / quality pass：

- 增加 roughness / metallic fixture 或材质球 grid。
- 使用本地 HDR 和默认 DamagedHelmet 做人工对比验证。
- 如条件允许，扩展 screenshot/readback 到 frame color validation。
- 暴露 specular intensity、specular cube size、BRDF LUT size 和 sample count policy。

之后可以考虑：

- screenshot / pixel test infrastructure。
- glTF camera / scene camera selection。
- bloom / auto exposure / ACES。
- transparent sorting。
- renderer resource / scene loading API。
