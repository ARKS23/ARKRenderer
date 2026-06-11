# Phase 0.26 Direct Lighting BRDF 升级

## 阶段判断

Phase 0.25 已经把 scene light / camera 数据入口整理到 renderer 语义中：

```text
RenderScene::lighting()
    -> FrameContext::scene
    -> ForwardPass::LightingUniform

RenderView::cameraPosition()
    -> FrameContext::view
    -> ForwardPass::LightingUniform
```

这意味着 `ForwardPass` 不再把 light direction / color / ambient / camera position 写死在 pass 内部。下一步最合适的小闭环，是升级 mesh fragment shader 中的 direct lighting 公式，让当前 glTF metallic-roughness 输入进入一个更接近真实 PBR 的 direct BRDF。

当前 `shaders/mesh.frag.hlsl` 已经有完整的 PBR 输入读取路径：

```text
baseColor texture + factor
normal texture + normalScale + TBN
metallicRoughness texture + factors
occlusion texture + strength
emissive texture + factor
scene directional light
camera position
```

但 `evaluateDirectLighting()` 仍然是 Phase 0.17 留下的最小 direct-light-only 解释：

```hlsl
const float3 diffuseColor = inputs.baseColor.rgb * (1.0f - inputs.metallic);
const float3 specularColor = lerp(float3(0.04f, 0.04f, 0.04f), inputs.baseColor.rgb, inputs.metallic);
const float specularPower = lerp(96.0f, 8.0f, inputs.roughness);
const float specularStrength = pow(nDotH, specularPower) * (1.0f - inputs.roughness * 0.5f);
```

这能表现基本高光，但不是 glTF metallic-roughness 常用的 Cook-Torrance microfacet BRDF。Phase 0.26 应该在不改变资源系统、RHI、descriptor layout 和渲染管线结构的前提下，把 direct light shader 公式升级为更标准的：

```text
Lambert diffuse
GGX / Trowbridge-Reitz normal distribution
Smith geometry term
Schlick Fresnel
metallic workflow F0
```

本阶段重点是“shader BRDF 公式升级”，不是进入完整 PBR 环境光链路。

## 目标

Phase 0.26 目标：

- 在 `mesh.frag.hlsl` 中实现最小 Cook-Torrance direct lighting。
- 使用 GGX / Trowbridge-Reitz NDF。
- 使用 Smith geometry term。
- 使用 Schlick Fresnel。
- diffuse 使用 Lambert，并按 metallic 抑制。
- F0 使用 `lerp(float3(0.04), baseColor.rgb, metallic)`。
- 保留当前 metallic / roughness / normal / occlusion / emissive 输入路径。
- 保留当前 `LightingUniform`、descriptor binding、pipeline layout 和 RHI shape。
- 保留 Phase 0.20 alpha mask / blend 行为。
- 保留 Phase 0.21 / 0.22 UV selection 和 texture transform 行为。
- 保留 Phase 0.24 doubleSided culling 行为。
- 保留 Phase 0.25 scene lighting / camera position 数据来源。
- 扩展 shader source smoke test，避免 BRDF helper 被意外删回旧公式。
- 文档明确当前仍是 direct-light-only，不声明完整 glTF PBR。

## 非目标

Phase 0.26 暂不做：

- 不做 HDR framebuffer。
- 不做 tone mapping / exposure。
- 不做 IBL / environment map。
- 不做 irradiance map。
- 不做 prefiltered specular environment map。
- 不做 BRDF LUT。
- 不做 bloom。
- 不做 shadow map。
- 不做 point light / spot light / area light。
- 不做多光源数组。
- 不做 glTF `KHR_lights_punctual`。
- 不做 glTF material extensions，例如 clearcoat、sheen、transmission、ior、specular。
- 不做 energy compensation / multiple scattering compensation。
- 不做 two-sided lighting。
- 不做 transparent sorting / OIT。
- 不做 RenderGraph 重构。
- 不做 descriptor layout / pipeline layout / RHI 改动。
- 不做 CPU-side material 或 resource 数据结构扩展，除非 shader 编译证明必须补齐常量。

## 模块边界

继续遵守现有设计文档和最近 phase：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase24.md
docs/phase/phase25.md
```

边界要求：

- `asset/` 不参与本阶段；glTF material 输入已经足够。
- `renderer/` 不新增 GPU resource owner。
- `RenderScene` 和 `RenderView` 不需要新增字段。
- `ForwardPass` C++ descriptor layout / uniform structs 不需要修改。
- `rhi/` 和 `rhi/vulkan/` 不需要修改。
- 本阶段唯一 production 代码落点应是 `shaders/mesh.frag.hlsl`。
- 测试落点优先是 `tests/shader_assets_smoke.cpp`。
- 如需更新注释，注释应说明当前仍为 direct-light-only。

## 当前行为

当前 `readPbrInputs()` 已经读取：

```hlsl
inputs.baseColor
inputs.worldNormal
inputs.metallic
inputs.roughness
inputs.occlusion
inputs.emissive
```

当前 `evaluateDirectLighting()` 使用：

```hlsl
const float3 n = normalize(inputs.worldNormal);
const float3 l = normalize(-g_Lighting.lightDirection.xyz);
const float3 v = normalize(g_Lighting.cameraPosition.xyz - worldPosition);
const float3 h = normalize(l + v);
const float nDotL = saturate(dot(n, l));
const float nDotH = saturate(dot(n, h));
```

然后用 roughness 映射到一个 specular power：

```hlsl
const float specularPower = lerp(96.0f, 8.0f, inputs.roughness);
const float specularStrength = pow(nDotH, specularPower) * (1.0f - inputs.roughness * 0.5f);
```

主要限制：

- roughness 与高光形状不是 GGX。
- Fresnel 没有使用 view/half angle。
- geometry visibility term 缺失。
- diffuse/specular energy split 比较粗糙。
- 仍是 LDR backbuffer 输出，升级 BRDF 后画面亮度可能变化。

## 建议设计

### Shader Helpers

建议新增 shader helper：

```hlsl
static const float PI = 3.14159265359f;

float distributionGGX(float nDotH, float roughness);
float geometrySchlickGGX(float nDotV, float roughness);
float geometrySmith(float nDotV, float nDotL, float roughness);
float3 fresnelSchlick(float cosTheta, float3 f0);
```

也可以使用 correlated Smith GGX visibility：

```hlsl
float visibilitySmithGGXCorrelated(float nDotV, float nDotL, float roughness);
```

两种方案都可接受。Phase 0.26 推荐更直观的 `geometrySchlickGGX + geometrySmith`，便于 smoke test 和后续阅读。等 HDR/IBL 阶段再统一调整为更完整的 glTF reference shader 风格。

### BRDF 公式

建议 direct lighting 基本公式：

```hlsl
const float3 albedo = inputs.baseColor.rgb;
const float metallic = saturate(inputs.metallic);
const float roughness = clamp(inputs.roughness, 0.04f, 1.0f);

const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
const float3 f = fresnelSchlick(vDotH, f0);
const float d = distributionGGX(nDotH, roughness);
const float g = geometrySmith(nDotV, nDotL, roughness);

const float denominator = max(4.0f * nDotV * nDotL, 0.001f);
const float3 specular = (d * g * f) / denominator;

const float3 kd = (1.0f - f) * (1.0f - metallic);
const float3 diffuse = kd * albedo / PI;

const float3 direct = g_Lighting.lightColor.rgb * nDotL * (diffuse + specular);
```

Ambient 仍保持当前最小策略：

```hlsl
const float3 ambient = g_Lighting.ambientColor.rgb * albedo;
```

最终输出：

```hlsl
return (ambient + direct) * inputs.occlusion + inputs.emissive;
```

注意：

- 本阶段不把 AO 只作用于 ambient；为了最小行为变化，可以继续沿用当前 `(ambient + direct) * occlusion + emissive`。如果改为只影响 ambient，必须在文档中说明画面变化。
- 如果 `l + v` 近似零向量，应避免 `normalize(0)`。可通过 `max` 和 `saturate` 降低风险；必要时给 half vector fallback。
- roughness 下限沿用当前 `0.04`。
- 不做 tone mapping，因此不要把 light intensity 或 exposure 引入本阶段。

### LDR 输出风险

Cook-Torrance BRDF 更接近物理，但当前 backbuffer 仍是 LDR。直接升级 shader 后：

- 金属高光可能更集中。
- 低 roughness 材质可能更亮或更暗。
- DamagedHelmet 的观感可能变化，但不应启动失败或出现 NaN/全黑。

本阶段验收应关注：

- shader 能编译。
- 默认 fixture 和 DamagedHelmet 能运行。
- alpha/mask/blend 不回退。
- 输出没有明显 NaN 闪烁、全黑或全白。

真正的最终观感稳定应留给 Phase 0.27 HDR / tone mapping。

## 测试策略

优先扩展：

```text
tests/shader_assets_smoke.cpp
```

新增 source smoke 关键字：

```text
PI
distributionGGX
geometrySchlickGGX
geometrySmith
fresnelSchlick
nDotV
vDotH
F0 / f0
```

同时保留既有断言：

- `struct PbrInputs`
- `buildWorldNormal`
- `evaluateDirectLighting`
- binding 13
- alpha mask / blend
- UV selection
- texture transform

本阶段不建议新增 fake RHI 测试，因为 BRDF 公式在 shader 内，最直接的守护是 shader source smoke + DXC 编译出的 SPIR-V 验证。

可选增强：

- 新增一个 tiny CPU-side reference test 并不划算，因为 HLSL 与 C++ 公式容易重复维护。
- 如果未来需要数值测试，应考虑 shader unit test 或 golden screenshot，但这不是 Phase 0.26 目标。

## 实施顺序

### 0.26.0 文档与范围确认

目标：

- 新增 `docs/phase/phase26.md`。
- 明确本阶段只做 direct lighting BRDF shader 升级。
- 明确不做 HDR、IBL、tone mapping、RenderGraph、glTF lights/material extensions。

审核点：

- 不重复 Phase 0.25 scene light / camera 数据入口。
- 不修改 RHI/Vulkan。
- 不新增 descriptor binding。

### 0.26.1 Shader BRDF helper

目标：

- 在 `mesh.frag.hlsl` 中新增 GGX / Smith / Fresnel helper。
- 保持 helper 命名清晰，便于 smoke test。
- 避免除零和 `normalize(0)`。

审核点：

- helper 不依赖新增 uniform。
- roughness clamp 保持稳定。
- shader 风格与现有 HLSL 保持一致。

### 0.26.2 evaluateDirectLighting 改造

目标：

- 用 Cook-Torrance direct BRDF 替换旧 specular power 公式。
- 保持 ambient / occlusion / emissive 输出路径清晰。
- 保持 alpha mask / blend 逻辑不动。

审核点：

- 不改 `readPbrInputs()` 的 texture sampling 路径。
- 不改 texture transform / UV selection。
- 不改 `LightingUniform`。

### 0.26.3 Tests

目标：

- 扩展 `shader_assets_smoke`，覆盖 BRDF helper 关键字。
- 保证 mesh fragment shader source smoke 能捕获旧公式回退。
- 通过 DXC 编译验证 HLSL 语法。

审核点：

- 不引入真实 Vulkan 依赖。
- 不用截图作为本阶段必须项。

### 0.26.4 验证与收尾

目标：

- 更新本文档实现状态。
- 按需同步 `docs/codex_handoff.md`。
- 记录当前仍未做 HDR/tone mapping/IBL。

建议运行：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_shader_assets_smoke
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

## 审核检查点

- `mesh.frag.hlsl` 不再使用旧 `specularPower` / `pow(nDotH, specularPower)` 作为主要高光模型。
- shader 中存在 GGX distribution、Smith geometry、Schlick Fresnel。
- `LightingUniform` binding 13 不变。
- descriptor layout / pipeline layout 不变。
- `readPbrInputs()` 路径不回退。
- alpha mask / blend 不回退。
- texture transform / UV selection 不回退。
- scene lighting / camera position 数据来源不回退。
- 文档明确当前仍是 direct-light-only。
- build、CTest、default sandbox、DamagedHelmet optional smoke 通过。

## 验证计划

必须通过：

```powershell
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
```

smoke：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

## 当前实现状态

已完成 0.26.0 ~ 0.26.4：

- 本文档已明确 Phase 0.26 范围、非目标、模块边界、测试策略和验证计划。
- `mesh.frag.hlsl` 已新增 direct lighting BRDF helper：
  - `PI`
  - `distributionGGX()`
  - `geometrySchlickGGX()`
  - `geometrySmith()`
  - `fresnelSchlick()`
- `evaluateDirectLighting()` 已从旧的 `specularPower` / `pow(nDotH, specularPower)` 高光模型升级为 Cook-Torrance direct BRDF：
  - GGX / Trowbridge-Reitz normal distribution
  - Smith geometry term
  - Schlick Fresnel
  - Lambert diffuse
  - metallic workflow F0
- `readPbrInputs()` 的 texture sampling、UV selection、texture transform、alpha mask/blend 路径未修改。
- `LightingUniform` binding 13、descriptor layout、pipeline layout、RHI/Vulkan 均未修改。
- ambient / occlusion / emissive 输出仍保持当前 direct-light-only 最小策略。
- `shader_assets_smoke` 已扩展 source smoke，覆盖 BRDF helper 和关键变量，防止 shader 回退到旧公式。

本轮 Phase 0.26 验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_shader_assets_smoke
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：

```text
targeted build passed
ark_shader_assets_smoke passed
full build passed
CTest: 9/9 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
```

## 完成标准

Phase 0.26 完成时应满足：

- mesh fragment shader 使用 Cook-Torrance direct BRDF。
- shader helper 覆盖 GGX、Smith、Schlick Fresnel。
- glTF metallic-roughness 输入继续驱动 baseColor、metallic、roughness、normal、occlusion、emissive。
- descriptor layout、pipeline layout、RHI/Vulkan 不变。
- tests 覆盖 shader source 中的 direct BRDF 关键路径。
- build、CTest、default sandbox、DamagedHelmet optional smoke 通过。
- 文档明确仍未支持 HDR、tone mapping、IBL、shadow、多光源、完整 glTF PBR extensions。

## 后续 Phase 建议

Phase 0.26 后建议进入：

1. HDR framebuffer / tone mapping。
2. IBL / environment map / BRDF LUT。
3. renderer 级资源 / 场景加载入口整理。
4. 基于 camera 和 bounds 的 Blend bucket back-to-front sorting。
5. pipeline / shader / descriptor layout 的 deferred destruction。
