# Phase 0.30 Minimal Environment Lighting

## 阶段判断

Phase 0.29 已经完成 HDR environment texture 的前置链路：

```text
HDR file
    -> asset::ImageData(Rgba32Float)
    -> renderer EnvironmentResource
    -> RHI 2D sampled texture
    -> RenderScene environment slot
```

但当前 renderer 仍然是 direct-light-only。`RenderScene::environment()` 只保存语义，`ForwardPass` 和 `mesh.frag.hlsl` 还没有消费 environment resource，所以环境贴图不会影响画面。

下一阶段最合理的目标不是直接进入完整 IBL，而是补齐最小消费链路：

```text
RenderScene::SceneEnvironment
    -> ForwardPass descriptor binding
    -> mesh.frag.hlsl equirectangular sampling
    -> ambient lighting contribution
    -> ToneMappingPass 输出
```

这一步的价值是让 Phase 0.29 产出的 HDR environment resource 真正进入 lighting result，同时仍保持工程面小、风险可控。

## 目标

Phase 0.30 目标：

- 让 `ForwardPass` 读取 `RenderScene::environment()`。
- 为 forward mesh shader 增加 environment texture / sampler descriptor binding。
- 在 `mesh.frag.hlsl` 中增加 equirectangular environment sampling。
- 先把 environment 用作 diffuse-ish ambient source，不做 specular IBL。
- 支持 environment intensity。
- environment 未设置或未启用时，保持 Phase 0.29 以前的 ambient/direct lighting 行为。
- 为 descriptor 完整性提供 fallback environment binding，避免 Vulkan draw 时存在未绑定 sampled image。
- 可选增加 sandbox environment path override，便于用本地 `.hdr` 验证画面变化。
- 补充 smoke tests 覆盖 descriptor layout、descriptor writes、lighting uniform、shader token 和 fallback 行为。
- 保持 Phase 0.28 / 0.29 的 HDR scene color 与 tone mapping 链路不回退。

## 非目标

Phase 0.30 暂不做：

- 不做 cubemap texture/view。
- 不做 equirectangular -> cubemap conversion。
- 不做 diffuse irradiance map。
- 不做 prefiltered specular environment map。
- 不做 BRDF LUT。
- 不做 roughness-based mip sampling。
- 不做 specular IBL。
- 不做 skybox / environment background pass。
- 不做 HDR mipmap generation。
- 不做 offline mip / compressed HDR texture。
- 不做 KTX / DDS / EXR loader。
- 不做 glTF environment extension。
- 不做 `KHR_lights_punctual`。
- 不做 bloom。
- 不做 auto exposure / histogram。
- 不做 ACES / filmic tone mapping。
- 不做 RenderGraph 重构。
- 不做 bindless descriptor。
- 不把大型 HDR 资产提交为默认资源。

## 模块边界

继续遵守现有设计边界：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase27.md
docs/phase/phase28.md
docs/phase/phase29.md
```

边界要求：

- `asset/` 仍只负责读取 HDR 文件并输出 CPU `ImageData`。
- `EnvironmentResource` 仍是 renderer 层 GPU resource owner。
- `RenderScene` 只保存 `EnvironmentResource*` 和 intensity，不创建、不拥有 GPU resource。
- `ForwardPass` 可以消费 scene environment，但不负责 asset loading。
- `ForwardPass` 可以持有 fallback environment resource，用于 descriptor 完整性。
- `mesh.frag.hlsl` 可以采样 environment texture，但本阶段只把它用于 ambient contribution。
- `FrameRenderer` 的两段 dynamic rendering 顺序不变。
- `ToneMappingPass` 不参与 environment resource，只继续消费 HDR scene color。
- sandbox environment path override 如果接入，应通过 `ApplicationDesc` / `RendererDesc` 传递，不暴露 RHI/Vulkan 类型。

## 当前基线

### RenderScene Environment

当前已有：

```cpp
struct SceneEnvironment {
    EnvironmentResource* environment = nullptr;
    float intensity = 1.0f;

    bool isEnabled() const {
        return environment != nullptr && intensity > 0.0f;
    }
};
```

并且：

- `RenderScene::setEnvironment()` 会把负 intensity clamp 到 `0`。
- `RenderScene::clear()` 保留 environment policy。
- `RenderScene::clearEnvironment()` 显式清空 environment。

### EnvironmentResource

当前 `EnvironmentResource` 支持：

- `ImageData(Rgba32Float)` -> `rhi::Format::RGBA32Float` 2D texture。
- usage = `ShaderResource | TransferDst`。
- `mipLevels = 1`。
- sampler 默认 `U=Repeat`、`V/W=ClampToEdge`。
- `upload()` / `releaseDeferred()` / `resetImmediate()`。

当前还不支持：

- cubemap。
- mip chain。
- prefilter。
- skybox。

### ForwardPass Descriptor Layout

当前 forward descriptor set 使用 set 0：

```text
0  CameraUniformBuffer              VS
1  BaseColorTexture                 FS
2  BaseColorSampler                 FS
3  ObjectUniformBuffer              VS
4  MaterialUniformBuffer            FS
5  NormalTexture                    FS
6  NormalSampler                    FS
7  MetallicRoughnessTexture         FS
8  MetallicRoughnessSampler         FS
9  OcclusionTexture                 FS
10 OcclusionSampler                 FS
11 EmissiveTexture                  FS
12 EmissiveSampler                  FS
13 LightingUniformBuffer            FS
```

Phase 0.30 建议新增：

```text
14 EnvironmentTexture               FS
15 EnvironmentSampler               FS
```

并扩展 binding 13 的 `LightingUniform`，把 environment intensity / enabled flag 放进已有 scene lighting uniform。这样不需要新增第三个 per-frame uniform buffer，也能保持 environment 与 scene lighting 语义聚合。

### Shader Baseline

当前 `mesh.frag.hlsl` 的 ambient 仍是常量颜色：

```hlsl
const float3 ambient = g_Lighting.ambientColor.rgb * albedo;
```

Phase 0.30 的最小改造点应集中在这里：direct BRDF 路径不动，只替换 ambient source。

## 建议设计

### ForwardPass Descriptor Binding

新增 descriptor binding：

```cpp
addFragmentTextureBindingPair(descriptorSetLayoutDesc, 14, 15);
```

shader 对应：

```hlsl
[[vk::binding(14, 0)]]
Texture2D<float4> g_EnvironmentTexture;

[[vk::binding(15, 0)]]
SamplerState g_EnvironmentSampler;
```

`updateDrawDescriptorSet()` 当前只接收 `MaterialResource& material`。Phase 0.30 建议改为接收 `FrameContext&` 或显式 environment binding：

```cpp
bool updateDrawDescriptorSet(FrameContext& frameContext,
                             u32 frameSlot,
                             usize drawIndex,
                             MaterialResource& material);
```

写入顺序建议：

1. camera uniform。
2. material textures。
3. object uniform。
4. material uniform。
5. lighting uniform。
6. environment sampled image + sampler。

### Lighting Uniform 扩展

当前 CPU / shader 两侧 `LightingUniform` 是：

```cpp
struct alignas(16) LightingUniform {
    glm::vec4 lightDirection;
    glm::vec4 lightColor;
    glm::vec4 ambientColor;
    glm::vec4 cameraPosition;
};
```

建议扩展为：

```cpp
struct alignas(16) LightingUniform {
    glm::vec4 lightDirection;
    glm::vec4 lightColor;
    glm::vec4 ambientColor;
    glm::vec4 cameraPosition;
    glm::vec4 environment; // x = intensity, y = enabled, z/w reserved
};
```

shader 侧保持相同布局：

```hlsl
struct LightingUniform {
    float4 lightDirection;
    float4 lightColor;
    float4 ambientColor;
    float4 cameraPosition;
    float4 environment;
};
```

约束：

- `environment.x` 为 clamp 后 intensity。
- `environment.y` 为 `1.0` 或 `0.0`。
- environment 未设置、未 ready 或 intensity 为 0 时，`environment.y = 0.0`。
- `ambientColor` 继续保留，用于无 environment 时的 fallback ambient。

### Fallback Environment Binding

Vulkan descriptor set layout 中声明了 sampled image 后，应保证每个 draw descriptor set 都写入有效 image/sampler。即使 shader 通过 `environment.y` 跳过采样，也不要依赖未绑定 descriptor。

建议 `ForwardPass` 持有一个 1x1 fallback environment resource：

```text
rgba32f pixel = (0, 0, 0, 1)
format = RGBA32Float
mipLevels = 1
sampler = default environment sampler
```

fallback resource 用途：

- scene 没有 environment 时，binding 14/15 仍写 fallback texture/sampler。
- scene environment 未 uploaded 或 not ready 时，先尝试 upload；失败则返回 false，不静默画错。
- fallback upload 发生在 dynamic rendering scope 外，也就是 `ForwardPass::prepare()` 阶段。

建议 helper：

```cpp
EnvironmentResource* resolveEnvironmentResource(FrameContext& frameContext);
bool ensureFallbackEnvironment(rhi::RenderDevice& device);
bool uploadEnvironmentResources(FrameContext& frameContext);
```

### Environment Upload 时机

`ForwardPass::prepare()` 当前负责 mesh/material upload 和 descriptor update。Phase 0.30 建议在 draw descriptor update 前补充：

```text
ensure fallback environment created
upload fallback environment if needed
if scene environment enabled:
    upload scene environment
```

这样 shader draw 前 descriptor 绑定一定指向 ready texture。

注意：

- 不要在 `execute()` 的 dynamic rendering scope 内做 texture upload。
- 不要由 `RenderScene` 发起 upload。
- 不要让 `EnvironmentResource` 进入 `MaterialResource` 或 `TextureCache`。

### Shader Environment Sampling

Phase 0.30 使用 equirectangular 2D environment，不做 cubemap。建议新增：

```hlsl
float2 directionToEquirectUv(float3 direction) {
    const float3 d = normalize(direction);
    const float u = atan2(d.z, d.x) * (1.0f / (2.0f * PI)) + 0.5f;
    const float v = acos(clamp(d.y, -1.0f, 1.0f)) * (1.0f / PI);
    return float2(u, v);
}

float3 sampleEnvironment(float3 direction) {
    return g_EnvironmentTexture.Sample(g_EnvironmentSampler, directionToEquirectUv(direction)).rgb;
}
```

最小 ambient 策略建议：

```hlsl
float3 evaluateAmbientLighting(float3 normal, float3 albedo) {
    if (g_Lighting.environment.y > 0.5f) {
        const float3 environmentRadiance = sampleEnvironment(normal) * g_Lighting.environment.x;
        return environmentRadiance * albedo;
    }

    return g_Lighting.ambientColor.rgb * albedo;
}
```

然后在 `evaluateDirectLighting()` 中替换现有 ambient：

```hlsl
const float3 ambient = evaluateAmbientLighting(n, albedo);
```

这个策略的意图是：

- environment enabled 时，用 HDR environment 替换旧常量 ambient。
- environment disabled 时，完全保持旧行为。
- direct Cook-Torrance 光照路径不变。
- 不把 environment 当成 specular reflection source。

### Sandbox Environment Path

为了便于人工验证真实 HDR environment，建议新增可选路径：

```cpp
struct ApplicationDesc {
    Path defaultModelPath;
    Path defaultEnvironmentPath;
};

struct RendererDesc {
    rhi::Extent2D extent;
    Path defaultModelPath;
    Path defaultEnvironmentPath;
};
```

`apps/sandbox/main.cpp` 建议支持：

```text
ark_sandbox.exe [optional_model_path] [optional_environment_hdr_path]
```

规则：

- `argc > 1` 仍解析模型路径，保持 Phase 0.18 行为。
- `argc > 2` 解析 environment HDR 路径。
- 没有 environment path 时，默认行为不变。
- 本阶段不要提交大型 HDR asset。
- 如果本地 environment path 不存在，只 warning 并保持 environment disabled。

Renderer 默认 scene 可以持有：

```text
EnvironmentResource m_DefaultEnvironment;
```

加载流程：

```text
defaultEnvironmentPath exists
    -> loadImageHdrRgba32F()
    -> m_DefaultEnvironment.create()
    -> m_DefaultScene.setEnvironment({ &m_DefaultEnvironment, 1.0f })
```

upload 仍由 `ForwardPass::prepare()` 触发。

## 实施顺序

### 0.30.0 文档与范围确认

目标：

- 新增 `docs/phase/phase30.md`。
- 明确本阶段是最小 environment lighting，不是完整 IBL。
- 明确本阶段只做 equirectangular environment ambient contribution。

审核点：

- 不把阶段扩大成 cubemap / irradiance / prefilter / BRDF LUT。
- 不修改 ToneMappingPass。
- 不引入 RenderGraph / bindless。

### 0.30.1 ForwardPass Environment Descriptor

目标：

- `ForwardPass` descriptor layout 新增 binding 14/15。
- `ForwardPass` descriptor update 写入 environment texture/sampler。
- `LightingUniform` 扩展 environment intensity / enabled。
- `makeLightingUniform()` 从 `RenderScene::environment()` 读取环境状态。

审核点：

- environment disabled 时仍绑定 fallback texture/sampler。
- environment not ready 时不进入 draw。
- 旧 material texture bindings 不变。
- pipeline variant key 不需要增加 environment 状态，因为 descriptor layout 固定。

### 0.30.2 ForwardPass Fallback Environment

目标：

- `ForwardPass` 创建 1x1 RGBA32F fallback environment。
- `prepare()` 在 draw descriptor update 前上传 fallback environment。
- environment 未设置时使用 fallback binding。

审核点：

- fallback upload 发生在 dynamic rendering scope 外。
- fallback 不进入 `TextureCache`。
- fallback 不改变无 environment 场景的视觉结果，因为 lighting uniform disabled。

### 0.30.3 Shader Environment Ambient

目标：

- `mesh.frag.hlsl` 新增 environment texture/sampler bindings。
- 新增 equirectangular direction -> UV helper。
- 新增 environment ambient sampling。
- environment enabled 时用 sampled radiance * intensity * albedo 替换旧 ambient。
- environment disabled 时保持旧 `ambientColor * albedo`。

审核点：

- direct Cook-Torrance BRDF 不变。
- alpha mask / blend 行为不变。
- occlusion / emissive 合成顺序不变。
- output 仍是 linear HDR，tone mapping 仍在 `ToneMappingPass`。

### 0.30.4 Sandbox Environment Path

目标：

- `ApplicationDesc` / `RendererDesc` 增加 `defaultEnvironmentPath`。
- `apps/sandbox/main.cpp` 支持第二个参数作为 HDR environment path。
- renderer 默认 scene 可选加载 environment resource 并设置 `SceneEnvironment`。

审核点：

- 默认 sandbox 行为不变。
- 没有本地 HDR asset 时不失败。
- 不提交大型 HDR asset。
- DamagedHelmet 显式模型路径 smoke 仍可用。

### 0.30.5 Tests

目标：

- `shader_assets_smoke` 覆盖：
  - binding 14 / 15。
  - `directionToEquirectUv()`。
  - environment sampled texture。
  - environment intensity / enabled。
- `forward_pass_pipeline_smoke` 覆盖：
  - descriptor layout 新增 environment sampled image/sampler。
  - lighting uniform size 和 environment fields。
  - environment disabled 时 descriptor 写 fallback。
  - environment enabled 时 descriptor 写 scene environment。
- `render_scene_queue_smoke` 覆盖已有 `SceneEnvironment` 行为，如有必要补充 clear/fallback 语义。
- `framework_headers_smoke` 覆盖 `ApplicationDesc` / `RendererDesc` 的 environment path public API。
- 如新增 sandbox 参数，补充 smoke 或 header compile 覆盖。

审核点：

- tests 不依赖大型 HDR asset。
- fake RHI tests 不需要真实 Vulkan。
- default scene visual 不作为自动测试条件。

### 0.30.6 验证与收尾

目标：

- 更新本文档实现状态和验证记录。
- 更新 `docs/codex_handoff.md`。
- 记录仍未支持：
  - cubemap。
  - irradiance map。
  - prefiltered specular。
  - BRDF LUT。
  - skybox。
  - HDR mip generation。
  - bloom / auto exposure。

建议验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_forward_pass_pipeline_smoke ark_shader_assets_smoke ark_framework_headers_smoke ark_render_scene_queue_smoke
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
build/msvc-vcpkg/Debug/ark_render_scene_queue_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

如果本地存在轻量 HDR environment，可额外人工 smoke：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/environments/local_test.hdr
```

## 审核检查点

- `RenderScene` 仍不拥有 GPU resource。
- `ForwardPass` 只消费 environment resource，不加载 asset。
- descriptor set layout 与 shader binding 完全一致。
- binding 14/15 在所有 draw descriptor set 中都有有效写入。
- environment disabled 时画面保持旧 ambient 行为。
- environment enabled 时只影响 ambient contribution，不影响 direct light BRDF。
- output 仍是 linear HDR。
- ToneMappingPass 不变。
- fallback environment 不进入 material texture cache。
- upload / deferred release 不发生在 dynamic rendering scope 内。
- build、CTest、default sandbox、DamagedHelmet smoke 通过。
- 文档明确仍未进入完整 IBL。

## 当前实现状态

Phase 0.30 已完成 0.30.0 ~ 0.30.6 的实现、测试与收尾。

已完成：

- `ForwardPass` descriptor layout 新增 environment texture/sampler binding 14/15。
- `LightingUniform` 新增 `environment` 字段，`x = intensity`，`y = enabled`。
- `ForwardPass::prepare()` 会上传 fallback environment，并在 scene environment 启用时上传 scene environment。
- `ForwardPass` 持有 1x1 RGBA32F fallback environment，保证每个 draw descriptor set 都写入有效 environment image/sampler。
- `mesh.frag.hlsl` 新增 equirectangular direction -> UV、environment sampling 和 `evaluateAmbientLighting()`。
- environment enabled 时用 HDR environment ambient 替换旧常量 ambient；disabled 时保持旧 `ambientColor * albedo`。
- `ApplicationDesc` / `RendererDesc` 新增 `defaultEnvironmentPath`。
- `apps/sandbox/main.cpp` 支持第二个参数作为可选 HDR environment path。
- Renderer 默认 scene 可选加载 `defaultEnvironmentPath`，创建 `EnvironmentResource` 并设置 `SceneEnvironment`。
- `forward_pass_pipeline_smoke` 覆盖 environment descriptor layout、fallback binding、scene environment binding、lighting uniform 和 upload 行为。
- `shader_assets_smoke` 覆盖 mesh shader environment binding / sampling token。
- `framework_headers_smoke` 覆盖 `ApplicationDesc` / `RendererDesc` environment path public API。

仍未支持：

- cubemap texture/view。
- equirectangular -> cubemap conversion。
- diffuse irradiance map。
- prefiltered specular environment map。
- BRDF LUT。
- specular IBL。
- skybox / environment background pass。
- HDR mipmap generation。
- bloom、auto exposure、ACES / filmic tone mapping。

## 验证记录

Phase 0.30 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_forward_pass_pipeline_smoke ark_shader_assets_smoke ark_framework_headers_smoke ark_render_scene_queue_smoke
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
build/msvc-vcpkg/Debug/ark_render_scene_queue_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：

```text
targeted smoke build passed
ark_forward_pass_pipeline_smoke passed
ark_shader_assets_smoke passed
ark_framework_headers_smoke passed
ark_render_scene_queue_smoke passed
full build passed
CTest: 11/11 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
```

runtime smoke 使用隐藏窗口启动 3 秒后自动停止，用于确认默认场景和 DamagedHelmet 路径不会启动即退出。

## 完成标准

Phase 0.30 完成时应满足：

- `RenderScene::environment()` 能被 `ForwardPass` 消费。
- forward descriptor layout 包含 environment texture/sampler。
- environment 未设置时 descriptor 仍完整，画面保持旧 ambient 行为。
- environment 设置且 ready 时 shader 采样 equirectangular HDR environment 并影响 ambient lighting。
- environment intensity 生效。
- direct Cook-Torrance lighting、material texture sampling、alpha mask/blend、tone mapping 不回退。
- tests 覆盖 shader bindings、descriptor writes、lighting uniform 和 fallback 行为。
- 文档和 handoff 记录当前已支持最小 environment lighting，但仍未支持完整 IBL。

## 后续 Phase 建议

Phase 0.30 后建议进入：

1. Cubemap resource / texture view 语义。
2. Equirectangular -> cubemap conversion pass。
3. Diffuse irradiance map。
4. Prefiltered specular environment map。
5. BRDF LUT。
6. Skybox / environment background pass。
7. bloom / auto exposure / ACES tone mapping。
