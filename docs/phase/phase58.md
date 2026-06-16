# Phase 0.58 Directional Shadow Map Foundation

## 实施状态

已完成 0.58 前置 Texture Load Failure Fallback 到 0.58.6 Tests 的开发工作，并补上默认 sandbox 组合验证场景。当前落地范围包括：KTX/不可解码贴图按 texture slot fallback、Sponza scene preset、shadow-validation sandbox preset、默认放大 Sponza + DamagedHelmet 组合场景、默认开启更明显的 Shadow / Bloom / ACES ToneMapping、`SceneResource` 主模型 transform / 附加模型加载 / preset lighting、`ShadowSettings` / CLI 参数、`ShadowPass` depth-only shadow map、ForwardPass shadow map descriptor 与 direct lighting shadow factor，以及对应 smoke tests。Sponza 当前仍是 fallback material path，不等同于 KTX/KTX2 原生支持。

最终验证：targeted build/CTest 通过，full build 通过，full CTest 29/29 通过，`git diff --check` 仅有行尾提示，sandbox hidden-window smoke 覆盖 default、sponza、shadow-validation、bloom-validation + Bloom + ACES。

## 阶段判断

Phase 0.57 已补齐 Blend bucket back-to-front 排序，Forward 渲染路径的基础正确性又向前走了一步。当前渲染器已经具备：
- Vulkan RHI 与 dynamic rendering。
- glTF PBR 材质、纹理、alpha mode、UV set、texture transform。
- ForwardPass PBR 直接光、diffuse IBL、specular IBL。
- HDR scene color、Physically Based Bloom、ToneMapping。
- frame validation / golden image / postprocess statistical diff。
- sandbox 默认 Sponza + DamagedHelmet 组合场景、scene preset、quality preset 和 orbit camera。

下一块最影响“渲染器完成度”和 sandbox 直观观感的是 **阴影**。没有阴影时，PBR/IBL 物体虽然有材质和环境反射，但缺少接触关系和空间落点。Phase 0.58 建议先做最小可用的 **directional shadow map foundation**，为后续 cascaded shadow maps、shadow validation fixture、engine integration 提供基础。

另外，当前已引入 `assets/models/sponza/sponza.gltf`，该资源的几何和 glTF 结构适合作为后续阴影/大场景验证基础，但贴图全部是 `.ktx`。项目当前 `TextureLoader` 仍是 stb_image 路线，不支持 KTX 解码。为了尽快验证 Sponza 几何、相机、draw queue 和后续 DamagedHelmet 场景组合，本阶段前置加入 **texture load failure fallback**：贴图文件加载失败时不让整个 `ModelResource` 创建失败，而是按 texture slot 回退到已有默认纹理。

## 目标

- 建立 directional light shadow map 的最小闭环。
- 新增或补全 `ShadowPass`，能把 `RenderQueue` 渲染到 depth-only shadow map。
- 新增 shadow map resource 和 light view-projection 数据。
- `ForwardPass` 能读取 shadow map，并对主方向光 direct lighting 施加 shadow factor。
- 第一版只支持 `SceneLighting::mainLight`。
- 第一版使用固定 shadow extent 和固定 orthographic bounds。
- 保持默认 golden baseline 不被无意修改。
- 优先通过 sandbox / validation preset 观察阴影效果。
- 增加 smoke tests 覆盖 ShadowPass 资源、pipeline 和 ForwardPass descriptor / shader 接入。
- 前置补齐 texture load failure fallback，让 Sponza 这类 KTX 贴图资源能先以 fallback material path 加载几何。
- 默认把 DamagedHelmet 作为第二个模型放入 Sponza 中庭附近，形成阴影、Bloom、ToneMapping、IBL 和复杂场景组合的手动验证入口。

## 非目标

- 不做 Cascaded Shadow Maps。
- 不做 point light / spot light shadow。
- 不做 PCSS / VSM / ESM / EVSM。
- 不做 contact shadow / screen-space shadow。
- 不做 dynamic scene bounds fitting。
- 不做 editor UI / runtime shadow debug panel。
- 不做 per-material shadow receiving / casting policy。
- 不改变 Bloom / ToneMapping / IBL 管线。
- 不默认更新现有 golden baseline。
- 不在本阶段实现 KTX / KTX2 原生解码。
- 不在本阶段恢复 Sponza 的完整材质质量；fallback path 只用于几何、阴影和场景组合验证。

## 现有基础

相关代码：

```text
src/renderer/passes/ShadowPass.h
src/renderer/passes/ForwardPass.h
src/renderer/passes/ForwardPass.cpp
src/renderer/FrameContext.h
src/renderer/FrameRenderer.cpp
src/renderer/RenderScene.h
src/renderer/RenderQueue.h
src/renderer/ModelResource.cpp
src/renderer/TextureCache.cpp
shaders/mesh.vert.hlsl
shaders/mesh.frag.hlsl
assets/models/sponza/sponza.gltf
```

当前状态：
- `ShadowPass.h` 已存在，但基本只是占位。
- `FrameRenderer` 当前 pass 顺序为：

```text
ClearPass / SkyboxPass / ForwardPass -> optional BloomPass -> ToneMappingPass
```

- `ForwardPass` 已有 camera / lighting / object / material uniform。
- `LightingUniform` 当前大小为 96 bytes：

```cpp
struct LightingUniform {
    glm::vec4 lightDirection;
    glm::vec4 lightColor;
    glm::vec4 ambientColor;
    glm::vec4 cameraPosition;
    glm::vec4 environment;
    glm::vec4 environmentSpecular;
};
```

- `mesh.frag.hlsl` 当前 direct lighting 没有 shadow factor。
- `FrameContext` 当前没有 shadow map / light matrix 字段。
- Sponza glTF 引用的 image / buffer 文件齐全，但 49 张贴图均为 `.ktx`。
- `GltfLoader` 会跳过图片解码并保留 texture path，因此 Sponza mesh/material metadata 可被解析。
- `TextureCache::getOrCreate()` 当前调用 `asset::loadImageRgba8()`，KTX 加载失败会返回 `nullptr`。
- `ModelResource::acquireTexture()` 当前在贴图加载失败时会把失败传播出去，导致整个 model resource 创建失败。

## 推荐整体方案

### 0.58 前置：Texture Load Failure Fallback

第一步建议先让贴图加载失败不再阻塞模型创建。目标不是支持 KTX，而是让 Sponza 这类资源先能进入 renderer 做大场景验证。

当前已有 fallback texture 类型：

```cpp
FallbackTextureKind::White
FallbackTextureKind::FlatNormal
FallbackTextureKind::MetallicRoughnessDefault
FallbackTextureKind::OcclusionDefault
FallbackTextureKind::Black
```

建议在 `ModelResource::acquireTexture()` 中处理文件贴图失败：

```cpp
TextureResource* texture = textureCache.getOrCreate(device, textureDesc);
if (!texture) {
    ARK_WARN("Texture load failed, using fallback: {}", path.string());
    return textureCache.getOrCreateFallback(device, fallbackKind);
}
return texture;
```

slot fallback 策略：
- baseColor：`White`
- normal：`FlatNormal`
- metallicRoughness：`MetallicRoughnessDefault`
- occlusion：`OcclusionDefault`
- emissive：`Black`

注意：
- 对 baseColor 来说，真实贴图加载失败时使用中性暖灰 `MissingBaseColor`，避免 KTX Sponza 在 HDR 背景下退成纯白不可读。
- 对 metallicRoughness 来说，真实贴图加载失败时使用非金属高粗糙 `MissingMetallicRoughness`，避免大场景因为 MR 白图变成高金属反射天空。
- 对 normal / MR / AO / emissive 来说，已有 fallback 语义正好适合作为缺失贴图默认值。
- fallback 应记录 warning，方便区分“加载成功”和“材质降级”。
- 后续真正做 KTX/KTX2 loader 时，应让 Sponza 走真实材质路径，而不是长期依赖 fallback。

### Shadow Resource

第一版建议由 `FrameRenderer` 或 `ShadowPass` 持有 shadow map target：

```text
format: D32Float
extent: 1024x1024
usage: DepthStencil | ShaderResource
state: DepthStencilWrite -> DepthStencilRead/ShaderResource
```

如果现有 RHI 对 depth texture view 采样能力有限，需要优先确认：
- `TextureUsage::DepthStencil | TextureUsage::ShaderResource` 是否能创建。
- Vulkan image aspect 是否能正确生成 depth view。
- descriptor sampled image 是否能绑定 depth texture view。

如果采样 depth view 暂时复杂，可第一阶段先只完成 ShadowPass depth render + smoke test，把 ForwardPass sampling 放到后续小阶段。但建议 Phase 0.58 尽量做到 ForwardPass shadow factor，形成可见闭环。

### Light View-Projection

第一版使用固定 ortho bounds：

```text
shadow extent: 1024x1024
ortho half extent: 8.0
near/far: 0.1 / 32.0 或 -16 / 32 视实现而定
```

根据 `SceneLighting::mainLight.direction` 构造 light view：

```cpp
glm::vec3 lightDir = normalize(mainLight.direction);
glm::vec3 lightPosition = -lightDir * distance;
glm::mat4 lightView = glm::lookAt(lightPosition, glm::vec3{0.0f}, up);
glm::mat4 lightProjection = glm::orthoRH_ZO(-extent, extent, -extent, extent, nearPlane, farPlane);
glm::mat4 lightViewProjection = lightProjection * lightView;
```

注意：
- Vulkan clip space 仍使用 Z 0..1。
- 如果 projection Y 需要和现有 camera path 一样翻转，必须通过 visual / smoke 确认。
- light dir 接近 up vector 时要选择备用 up，避免 lookAt 奇异。

### FrameContext 扩展

建议新增：

```cpp
rhi::TextureView* shadowMapView = nullptr;
rhi::Sampler* shadowSampler = nullptr;
glm::mat4 lightViewProjection{1.0f};
float shadowStrength = 0.0f;
float shadowBias = 0.0015f;
```

也可以封装为：

```cpp
struct FrameShadowData {
    rhi::TextureView* shadowMapView = nullptr;
    rhi::Sampler* sampler = nullptr;
    glm::mat4 lightViewProjection{1.0f};
    float strength = 0.0f;
    float bias = 0.0015f;
};
```

第一版如果想减少 header churn，也可以先把 shadow view / sampler / matrix 放在 `FrameContext` 中，后续再抽结构。

### ShadowPass

ShadowPass 负责：
- 创建或使用外部 shadow map target。
- 创建 shadow-only vertex shader / pipeline。
- 复用 `RenderQueue` 遍历 draw items。
- 写入 object matrix 和 light view-projection。
- depth-only render，不写 color。
- 过滤不 drawable item。

第一版 shader 可以新增：

```text
shaders/shadow.vert.hlsl
```

不需要 fragment shader，或使用空 fragment shader，取决于当前 pipeline abstraction 是否允许无 fragment stage。若 RHI 当前必须有 fragment shader，则新增：

```text
shaders/shadow.frag.hlsl
```

内容可以为空输出。

### ForwardPass 接入

ForwardPass 需要：
- descriptor layout 增加 shadow map sampled image / sampler。
- lighting uniform 或新 shadow uniform 增加 `lightViewProjection`、bias、strength。
- `mesh.frag.hlsl` 计算 shadow coordinate：

```hlsl
float4 shadowClip = mul(float4(input.worldPosition, 1.0f), g_Lighting.lightViewProjection);
float3 shadowUvDepth = shadowClip.xyz / shadowClip.w;
```

实际矩阵乘法方向需和现有 HLSL / C++ matrix layout 保持一致。

第一版 shadow test：
- 手动 compare depth。
- 使用 3x3 PCF 或 1-tap compare。
- 阴影只影响 direct light，不影响 ambient / IBL / emissive。

推荐：

```text
directLighting *= lerp(1.0, shadowFactor, shadowStrength)
```

其中 shadowFactor 为 0..1，1 表示全亮，0 表示全阴影。

### FrameRenderer Pass 顺序

推荐最终顺序：

```text
ShadowPass
ClearPass
SkyboxPass
ForwardPass
BloomPass
ToneMappingPass
```

ShadowPass 应在 scene color rendering 前执行，并在结束后把 shadow map transition 到 shader read 状态。

### 默认开关策略

为了避免直接改变现有 golden baseline，建议第一版采用以下之一：

方案 A：默认关闭，sandbox/preset opt-in。
- `RendererDesc` / `RendererQualityDesc` 增加 shadow enable。
- 默认保持 false。
- sandbox 增加 `--shadow` 或 `--shadows`。

方案 B：只在新增 `shadow-validation` preset 打开。
- 默认 sandbox 不变。
- 新增 fixture / preset 负责视觉验证。

方案 C：默认开启并更新 golden。
- 视觉收益最大，但风险最大。
- 当前不推荐，除非明确要改默认视觉基线。

推荐 Phase 0.58 使用 **方案 A 或 B**。如果想让用户马上看到 sandbox 效果，可以新增 `--shadows`，并在 README / docs 中给出命令。

## Sandbox / Fixture 建议

### Sponza + DamagedHelmet 验证方向

Sponza fallback material path 打通后，可以先通过：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe assets\models\sponza\sponza.gltf
```

验证：
- Sponza 几何能进入场景。
- 大场景 queue / model resource 不崩溃。
- 相机 orbit / pan / zoom 能找到合适观察位置。
- 后续 shadow map fixed bounds 是否足够覆盖中庭区域。

当前默认 sandbox 已通过代码侧 multi-model scene path 把 DamagedHelmet 作为第二个模型放入 Sponza 中庭附近；默认相机也拉近到中庭验证视角，便于直接检查 Sponza + Helmet 的组合画面。纯 Sponza 仍可通过 `--preset sponza` 查看，显式传入模型路径时不会自动追加默认 Helmet。后续如果需要更稳定的截图/golden 基准，仍建议新增专门 fixture，而不是直接手改原始 Sponza：

```text
assets/models/sponza_helmet_validation_fixture.gltf
```

建议内容：
- 引用或合并 Sponza 几何。
- 在中庭地面附近放置 DamagedHelmet。
- 给一个验证相机。
- 用于 IBL、shadow、Bloom/ToneMapping 和后续材质验证。

短期已采用 renderer scene API / `SceneResource::additionalModels` 在代码侧同时加载 Sponza 与 DamagedHelmet，作为默认 sandbox 组合验证路径。

如果默认模型阴影不明显，建议新增：

```text
assets/models/shadow_validation_fixture.gltf
```

场景结构：
- 大地面。
- 一个球或立方体悬在地面上方。
- 一个倾斜方向光。
- 相机看向接触区域。

Sandbox 命令：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset shadow-validation --shadows
```

若本阶段不想新增 fixture，可先用 `material-ball` 或默认模型配合 `--shadows` 手动观察。

## 分阶段任务

### 0.58.0 文档与范围确认

- 新增 `docs/phase/phase58.md`。
- 明确第一版只做 directional shadow map foundation。
- 明确不做 CSM / OIT / 默认 golden 更新。
- 明确 Sponza KTX 贴图当前走 texture load failure fallback，不等同于 KTX 支持。

### 0.58.1 Texture Load Failure Fallback

修改：

```text
src/renderer/ModelResource.cpp
tests/model_resource_smoke.cpp
tests/scene_resource_smoke.cpp
```

目标：
- `ModelResource::acquireTexture()` 在文件贴图加载失败时回退到 slot 对应 fallback texture。
- warning 记录失败 path 和 fallback kind。
- 现有真实 PNG/JPG/HDR 路径行为不变。
- 新增或扩展 smoke test，覆盖不可解码贴图 path 不会导致 model resource 创建失败。
- 增加 Sponza 轻量加载验证：`assets/models/sponza/sponza.gltf` 能完成 `SceneResource` / `ModelResource` 创建，允许 KTX fallback。

建议验证：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe assets\models\sponza\sponza.gltf
```

说明：
- 该验证只证明 Sponza 能进入 renderer。
- 视觉上会是 fallback material，不代表 KTX 材质正确。

### 0.58.2 Shadow Data / Resource Foundation

修改：

```text
src/renderer/FrameContext.h
src/renderer/FrameRenderer.cpp
```

目标：
- 增加 shadow map view / sampler / light view-projection 数据传递。
- 建立 shadow map texture target。
- 建立 shadow sampler。
- 确认 depth texture 可 transition 到 shader read。

### 0.58.3 ShadowPass Depth Render

修改：

```text
src/renderer/passes/ShadowPass.h
src/renderer/passes/ShadowPass.cpp
shaders/shadow.vert.hlsl
```

目标：
- ShadowPass setup/prepare/execute 完整化。
- 使用 `RenderQueue` 渲染 depth-only shadow map。
- 支持 object transform + light view-projection uniform。
- 输出 shadow map。

### 0.58.4 ForwardPass Shadow Sampling

修改：

```text
src/renderer/passes/ForwardPass.cpp
shaders/mesh.frag.hlsl
```

目标：
- ForwardPass descriptor 增加 shadow map / sampler。
- shader 计算 shadow factor。
- direct lighting 乘 shadow visibility。
- shadow 不影响 emissive / ambient / IBL。

### 0.58.5 Sandbox / Validation Path

可选修改：

```text
src/app/SandboxLaunchOptions.h
src/app/SandboxLaunchOptions.cpp
src/renderer/RendererPreset.h
src/renderer/RendererPreset.cpp
assets/models/shadow_validation_fixture.gltf
README.md
```

目标：
- 提供 `--shadows` 或 `shadow-validation` preset。
- sandbox 能直接看到阴影。
- 默认路径不改变，除非明确决定更新 golden。
- Sponza fallback path 可作为大场景手动 smoke；默认 sandbox 已加载 Sponza + DamagedHelmet 组合场景，正式 golden fixture 可放到后续阶段。

### 0.58.6 Tests

建议新增或扩展：

```text
tests/shadow_pass_smoke.cpp
tests/forward_pass_pipeline_smoke.cpp
tests/shader_assets_smoke.cpp
tests/framework_headers_smoke.cpp
tests/model_resource_smoke.cpp
tests/scene_resource_smoke.cpp
```

建议覆盖：
- texture load failure fallback。
- Sponza glTF 在 KTX 贴图不可解码时仍能通过 fallback 创建 scene/model resource。
- ShadowPass 创建 depth target / pipeline / descriptor。
- shadow shader asset 存在。
- ForwardPass descriptor layout 包含 shadow bindings。
- shadow disabled 时 fallback path 不影响旧材质。
- `ark_frame_validation_smoke` 默认 golden 不变。

建议执行：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_model_resource_smoke ark_scene_resource_smoke ark_shadow_pass_smoke ark_forward_pass_pipeline_smoke ark_shader_assets_smoke ark_frame_validation_smoke ark_sandbox
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(model_resource|scene_resource|shadow_pass|forward_pass_pipeline|shader_assets|frame_validation)_smoke" --output-on-failure
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

Sandbox smoke：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
build\msvc-vcpkg\Debug\ark_sandbox.exe assets\models\sponza\sponza.gltf
build\msvc-vcpkg\Debug\ark_sandbox.exe --shadows
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset shadow-validation --shadows
```

### 0.58.7 验证与收尾

- 更新 `docs/phase/phase58.md` 完成状态。
- 更新 `docs/codex_handoff.md`。
- 确认默认 golden 是否保持不变。
- 记录 sandbox 可见验证入口。
- 记录 Sponza 当前为 fallback material path，后续再做 KTX/KTX2 原生支持或纹理转换。
- 提交并推送。

## 完成标准

- ShadowPass 能渲染 directional shadow map。
- ForwardPass 能读取 shadow map 并影响 direct lighting。
- `.ktx` 等当前不可解码贴图加载失败时，ModelResource 能按 slot fallback，不导致整个模型加载失败。
- Sponza 能作为可视 fallback material 大场景进入 sandbox smoke，默认 sandbox 能加载 Sponza + DamagedHelmet 组合场景，并通过 `loadedModels=2` 日志确认。
- shadow 默认策略明确，不意外改变默认 golden。
- sandbox 有明确命令可以观察阴影。
- 相关 smoke tests 通过。
- full build 和 full CTest 通过。

## 风险与注意事项

- RHI depth texture sampling 可能暴露 Vulkan image aspect / layout / view 的细节问题。
- matrix layout 和 HLSL `mul` 方向容易出错，需要用 visual fixture 或 pixel/stat test 验证。
- shadow acne / peter-panning 需要 bias；第一版 bias 固定即可，不做自动调参。
- fixed ortho bounds 可能只适合默认场景，后续必须做 scene bounds 或 CSM。
- 如果默认开启 shadow，现有 frame golden 很可能变化；建议先 opt-in。
- 透明物体投影/接收阴影策略复杂，本阶段可以先让 alpha Blend 仍参与普通 depth shadow 或直接跳过，后续再细化。
- texture load failure fallback 会掩盖材质资源缺失问题，因此必须保留 warning；测试也应明确这是 fallback path。
- Sponza 的 KTX 贴图 fallback 后材质观感不代表最终质量，不能用它判断纹理/材质正确性。

## 后续方向

Phase 0.58 完成后建议：

1. Phase 0.59：Shadow Validation Fixture / Golden or Statistical Validation。
2. Phase 0.60：Screenshot/golden 基准可围绕默认 Sponza + DamagedHelmet 组合场景或专用 fixture 建立。
3. Phase 0.61：KTX/KTX2 texture loader 或 Sponza texture conversion pipeline。
4. Phase 0.62：`KHR_materials_emissive_strength`。
5. Phase 0.63：Renderer Public API / Engine Integration Boundary。
6. 后续阴影专项：CSM、scene bounds fitting、PCF quality presets、shadow atlas、多灯阴影。
