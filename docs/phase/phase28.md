# Phase 0.28 Tone Mapping Settings / Color Pipeline

## 阶段判断

Phase 0.27 已经把 frame renderer 从直接写 swapchain backbuffer 推进到两段渲染：

```text
ForwardPass
    -> RGBA16Float scene color
    -> ToneMappingPass
    -> swapchain backbuffer
    -> Present
```

这让 Cook-Torrance direct BRDF 的输出有了 HDR 承载空间，也为后续 IBL、bloom、exposure、color grading 留出了基础入口。

Phase 0.28 开始前，`ToneMappingPass` 仍是最小硬编码实现：

```hlsl
static const float Exposure = 1.0f;

float3 applyToneMapping(float3 color) {
    color *= Exposure;
    return color / (color + 1.0f);
}

float3 linearToSrgb(float3 color) {
    return pow(saturate(color), 1.0f / 2.2f);
}
```

这个基线状态可以验证 HDR scene color 闭环，但还不适合作为后续画质功能的稳定地基：

- exposure 写死在 shader 中，无法由 camera/view 或 renderer 设置控制。
- tone mapping 的输入/输出颜色空间约定只存在于 shader 代码里，没有形成 renderer 级语义。
- swapchain 目前是 `BGRA8Unorm`，shader 手动 linear-to-sRGB 是合理的；但如果后续切换到 sRGB swapchain，就可能出现 double encode 风险。
- 后续 IBL、bloom、auto exposure 都需要明确 `scene color 是 linear HDR`、`post pass 如何映射到 output`。
- 如果继续直接进入 IBL，会把 environment lighting、HDR texture、post exposure、output encoding 多个问题混在一起。

因此 Phase 0.28 建议先把 ToneMappingPass 从“硬编码 shader 常量”升级为“明确的 renderer 数据流 + 颜色空间约定”。这不是画面大功能，但它能把 Phase 0.27 的 HDR pipeline 收口成可继续扩展的基础。

## 目标

Phase 0.28 目标：

- 定义最小 tone mapping settings。
- exposure 不再硬编码在 shader 中。
- `ToneMappingPass` 通过 uniform buffer 接收 tone mapping 参数。
- 明确当前 color pipeline：
  - material / lighting shader 输出 linear HDR color。
  - scene color render target 是 linear `RGBA16Float`。
  - tone mapping shader 输入 linear HDR scene color。
  - tone mapping shader 输出当前 swapchain 需要的 display encoded color。
- 保持当前默认画面行为：默认 exposure 仍为 `1.0`，默认 output gamma 仍等效 `2.2`。
- 保持当前 tone mapping operator 为 Reinhard，不切换到 ACES 或 filmic。
- 保持 `FrameRenderer` 两段 render scope 不变。
- 保持 `ForwardPass`、material descriptor layout、lighting uniform 和 mesh shaders 不变。
- 保持 Phase 0.22 texture transform、Phase 0.23 alpha bucket、Phase 0.24 culling、Phase 0.25 scene light / camera、Phase 0.26 direct BRDF、Phase 0.27 HDR scene color 路径不回退。
- 增加 smoke tests 覆盖 settings public API、ToneMappingPass uniform 数据流和 shader source token。
- 文档明确当前仍不是 auto exposure、bloom、IBL 或完整 post-process stack。

## 非目标

Phase 0.28 暂不做：

- 不做 IBL / environment map。
- 不做 irradiance map。
- 不做 prefiltered specular environment map。
- 不做 BRDF LUT。
- 不做 bloom。
- 不做 auto exposure / histogram。
- 不做 eye adaptation。
- 不做 ACES 参数化。
- 不做 color grading / LUT。
- 不做 UI / ImGui exposure 调参面板。
- 不做 screenshot / golden image test。
- 不做 RenderGraph 重构。
- 不做 post-process stack / pass graph。
- 不做 compute post-processing。
- 不做 swapchain format 自动选择策略。
- 不把 swapchain 切换为 sRGB format。
- 不做 HDR texture loader。
- 不做 glTF lights / cameras。
- 不做 shader reflection 或 bindless descriptor。

## 模块边界

继续遵守现有设计文档和最近 phase：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase25.md
docs/phase/phase26.md
docs/phase/phase27.md
```

边界要求：

- `asset/` 不参与本阶段。
- `rhi/` 优先不新增 API；已有 uniform buffer、descriptor、sampler、texture view、draw 能覆盖本阶段。
- `rhi/vulkan/` 原则上不需要修改，除非发现 uniform descriptor 或 dynamic rendering 现有路径缺陷。
- `RenderView` 可以保存 view/camera 相关 tone mapping settings；ToneMappingPass 只消费，不决定默认 view policy。
- `FrameContext` 只传递 pass 需要的轻量 frame/view 语义，不拥有资源。
- `FrameRenderer` 仍拥有 HDR scene color 生命周期。
- `ToneMappingPass` 负责自己的 descriptor layout、uniform buffer、descriptor set、sampler、pipeline 和 shader。
- `ToneMappingPass` 不拥有 scene color。
- `ForwardPass` 不知道 tone mapping settings。
- mesh shaders 不参与本阶段。
- color pipeline 约定写在文档和必要注释中，避免散落在 shader 常量里。

## Phase 0.27 基线行为（实现前）

Phase 0.28 开始前，`RenderView` 只保存 camera 语义：

```cpp
glm::mat4 m_View;
glm::mat4 m_Projection;
glm::vec3 m_CameraPosition;
```

Phase 0.28 开始前，`FrameContext` 为 post pass 提供：

```cpp
rhi::TextureView* sceneColorView = nullptr;
rhi::Format colorFormat = rhi::Format::Unknown;
rhi::Format depthFormat = rhi::Format::Unknown;
```

Phase 0.28 开始前，`ToneMappingPass` descriptor layout：

```text
binding 0: SampledImage scene color
binding 1: Sampler scene sampler
```

Phase 0.28 开始前，`ToneMappingPass` 每帧执行：

```text
if scene color view changed:
    update sampled image + sampler descriptors

bind pipeline
bind descriptor set
draw fullscreen triangle
```

Phase 0.28 开始前，shader 参数全部硬编码：

```text
Exposure = 1.0
Reinhard tone mapping
linear-to-sRGB gamma = 2.2
```

这意味着 renderer 侧无法控制 exposure，也没有任何测试能验证 ToneMappingPass 是否正确上传 post-process 参数。

## 颜色空间约定

Phase 0.28 应明确以下约定。

### 输入纹理

glTF baseColor / emissive 等颜色纹理继续走 sRGB texture format：

```text
TextureColorSpace::Srgb -> rhi::Format::RGBA8Srgb
```

normal / metallicRoughness / occlusion 等数据纹理继续走 linear：

```text
TextureColorSpace::Linear -> rhi::Format::RGBA8Unorm
```

### Forward 输出

`mesh.frag.hlsl` 输出 linear lighting result：

```text
material sampling + lighting
    -> linear HDR color
    -> RGBA16Float scene color
```

不要在 mesh shader 中做 display gamma encoding。

### Scene Color

`FrameRenderer` 的 scene color 是：

```text
format: rhi::Format::RGBA16Float
usage: RenderTarget | ShaderResource
color space: linear HDR
```

### Tone Mapping 输出

当前 swapchain color format 是：

```cpp
rhi::Format::BGRA8Unorm
```

因此当前 tone mapping shader 输出前需要做 linear-to-sRGB style encoding，避免把 linear color 直接写入普通 UNORM backbuffer 后显示偏暗。

如果后续 swapchain 改为 sRGB format，例如 `BGRA8Srgb`，则必须重新评估 output encoding，避免 shader 手动 encode 后再由 framebuffer sRGB encode 一次。

Phase 0.28 不切换 swapchain format，只把当前策略显式参数化和文档化。

## 建议设计

### ToneMappingSettings

建议在 renderer 层定义最小 settings：

```cpp
struct ToneMappingSettings {
    float exposure = 1.0f;
    float outputGamma = 2.2f;
};
```

放置位置建议：

```text
src/renderer/RenderView.h
```

理由：

- exposure 通常与 camera/view 相关。
- 当前应用每帧已经传入 `RenderView`。
- 不需要引入新的 renderer-wide settings owner。
- 后续多 camera / 多 view 时，每个 view 可有自己的 exposure。

`RenderView` 建议新增：

```cpp
const ToneMappingSettings& toneMappingSettings() const;
void setToneMappingSettings(const ToneMappingSettings& settings);
```

默认值必须保持当前 shader 行为：

```text
exposure = 1.0
outputGamma = 2.2
```

### FrameContext

有两种选择：

方案 A：`ToneMappingPass` 直接从 `frameContext.view` 读取 settings。

```cpp
const ToneMappingSettings settings =
    frameContext.view ? frameContext.view->toneMappingSettings() : ToneMappingSettings{};
```

方案 B：`FrameContext` 显式带一份 settings。

```cpp
ToneMappingSettings toneMapping;
```

建议 Phase 0.28 采用方案 A：

- 改动更小。
- 与 Phase 0.25 camera position 从 `RenderView` 读取保持一致。
- `FrameContext` 不必继续膨胀。
- 测试可以直接构造 `RenderView` 设置 exposure，再让 ToneMappingPass 捕获 uniform。

如果后续引入 view family / renderer settings，再考虑把 tone mapping settings 从 `RenderView` 抽出。

### ToneMappingUniform

建议在 `ToneMappingPass.cpp` 中定义：

```cpp
struct alignas(16) ToneMappingUniform {
    float exposure = 1.0f;
    float inverseOutputGamma = 1.0f / 2.2f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

static_assert(sizeof(ToneMappingUniform) == 16);
```

使用 `inverseOutputGamma` 而不是 `outputGamma`，让 shader 直接执行：

```hlsl
pow(color, g_ToneMapping.inverseOutputGamma)
```

CPU 侧需要防御非法 gamma：

```text
outputGamma <= 0 -> fallback 2.2 或 inverse 1.0 / 2.2
exposure < 0     -> clamp 到 0
```

建议最小规则：

```cpp
exposure = max(settings.exposure, 0.0f);
inverseOutputGamma = settings.outputGamma > 0.0f ? 1.0f / settings.outputGamma : 1.0f / 2.2f;
```

### ToneMappingPass Descriptor Layout

当前 layout：

```text
binding 0: SampledImage
binding 1: Sampler
```

Phase 0.28 建议新增：

```text
binding 2: UniformBuffer, Fragment
```

`ToneMappingPass` 需要新增：

```cpp
std::array<Scope<rhi::Buffer>, FramesInFlight> m_UniformBuffers;
```

创建时每个 frame slot 一个 `ToneMappingUniformBuffer`，避免多帧并行时覆盖。

每次 `execute()` 更新当前 frame slot uniform：

```cpp
const ToneMappingUniform uniform = makeToneMappingUniform(frameContext);
context.updateBuffer(*m_UniformBuffers[frameSlot], &uniform, sizeof(uniform));
```

descriptor set 更新可以在 setup 后一次完成 uniform binding；sampled image 仍按 scene color view 是否变化更新。

### Shader

`shaders/tonemap.frag.hlsl` 建议改为：

```hlsl
struct ToneMappingUniform {
    float exposure;
    float inverseOutputGamma;
    float padding0;
    float padding1;
};

[[vk::binding(2, 0)]]
ConstantBuffer<ToneMappingUniform> g_ToneMapping;

float3 applyToneMapping(float3 color) {
    color *= g_ToneMapping.exposure;
    return color / (color + 1.0f);
}

float3 linearToOutput(float3 color) {
    return pow(saturate(color), g_ToneMapping.inverseOutputGamma);
}
```

命名建议使用 `linearToOutput()`，而不是继续写死 `linearToSrgb()`，因为这里表达的是当前 output encoding policy。虽然默认仍是近似 sRGB gamma，但语义上后续可切换。

### 默认策略

默认值：

```text
exposure = 1.0
outputGamma = 2.2
```

这应让 default sandbox 的默认亮度大致保持 Phase 0.27 行为。

### 可选但不建议本阶段做的内容

不建议本阶段新增 tone mapping mode enum：

```cpp
enum class ToneMappingOperator {
    Reinhard,
    ACES,
};
```

原因：

- 当前只有一个 operator。
- shader 分支和测试会增加复杂度。
- ACES/filmic 参数最好等 bloom / exposure / color grading 阶段一起考虑。

也不建议本阶段新增 renderer 全局 settings manager。当前 `RenderView` 足够承载最小 settings。

## 实施顺序

### 0.28.0 文档与范围确认

目标：

- 新增 `docs/phase/phase28.md`。
- 明确本阶段是 tone mapping settings 和 color pipeline 收口。
- 明确不做 IBL、bloom、auto exposure、RenderGraph。

审核点：

- 不重复 Phase 0.27 HDR scene color / ToneMappingPass 基础闭环。
- 不把本阶段扩大成完整 post-process stack。

### 0.28.1 RenderView ToneMappingSettings

目标：

- 新增 `ToneMappingSettings`。
- `RenderView` 保存 settings。
- 提供 getter / setter。
- 默认 exposure / outputGamma 对齐当前 shader 行为。

审核点：

- `RenderView::setDefaultPerspective()` 不应重置用户设置，除非明确设计为 camera preset。
- 旧 `setMatrices()` 行为不应改变。
- `framework_headers_smoke` 覆盖 public struct 编译。

### 0.28.2 ToneMappingPass Uniform Buffer

目标：

- `ToneMappingPass` 增加 `ToneMappingUniform`。
- 增加 per-frame uniform buffer。
- descriptor layout 新增 binding 2 uniform buffer。
- descriptor set 写入 binding 2。
- execute 时从 `frameContext.view` 读取 settings 并更新 uniform。

审核点：

- per-frame buffer 避免 in-flight 覆盖。
- sampled image / sampler binding 不回退。
- scene color view 仍由 `FrameContext::sceneColorView` 提供。

### 0.28.3 Tone Mapping Shader 参数化

目标：

- `tonemap.frag.hlsl` 删除 hardcoded `Exposure`。
- 新增 `ToneMappingUniform` constant buffer。
- `applyToneMapping()` 使用 uniform exposure。
- output encoding 使用 uniform inverse gamma。

审核点：

- 默认值仍等效 Phase 0.27。
- shader source smoke 覆盖 `ToneMappingUniform`、`exposure`、`inverseOutputGamma`、`linearToOutput`。

### 0.28.4 Tests

目标：

- `framework_headers_smoke` 覆盖 `ToneMappingSettings`。
- `shader_assets_smoke` 覆盖 tone mapping shader source token。
- 新增或扩展 smoke test 捕获 ToneMappingPass uniform 数据。

建议测试策略：

```text
tests/tone_mapping_pass_smoke.cpp
```

或扩展现有 `forward_pass_pipeline_smoke.cpp` 的 fake RHI / fake context。

推荐新增独立 test：

- 构造 fake `RenderDevice`。
- 构造 fake `DeviceContext` 捕获 `ToneMappingUniformBuffer` update。
- 构造 fake scene color view。
- 构造 `RenderView`，设置 exposure / gamma。
- 调用 `ToneMappingPass::setup()` 和 `execute()`。
- 验证：
  - 创建 pipeline。
  - draw fullscreen triangle。
  - uniform exposure 被上传。
  - inverseOutputGamma 符合预期。
  - descriptor layout 包含 sampled image / sampler / uniform buffer。

审核点：

- 测试不需要真实 Vulkan。
- 不引入 screenshot golden test。

### 0.28.5 CMake

目标：

- 如新增 `ark_tone_mapping_pass_smoke`，加入 `CMakeLists.txt`。
- 该测试需要依赖 `ark_shaders`，因为 `ToneMappingPass::setup()` 会加载 `tonemap.vert.spv` / `tonemap.frag.spv`。

审核点：

- 只在 `ARK_DXC_SUPPORTED` 下加入依赖 shader bytecode 的 test。
- 不影响非 DXC 平台已有 tests。

### 0.28.6 收尾

目标：

- 更新本文档实现状态和验证记录。
- 更新 `docs/codex_handoff.md`。
- 记录仍未支持：
  - auto exposure
  - bloom
  - ACES/filmic operator
  - sRGB swapchain policy
  - full post-process stack
  - IBL

## 审核检查点

- `ToneMappingPass` 不再 hardcode exposure。
- 默认 exposure / gamma 保持 Phase 0.27 默认视觉行为。
- scene color 仍是 linear `RGBA16Float`。
- mesh shader 不做 gamma encode。
- tone mapping shader 是唯一 output encoding 位置。
- `ForwardPass` pipeline format 仍使用 `FrameContext::colorFormat` / `depthFormat`。
- `FrameRenderer` 仍拥有 scene color 生命周期。
- `ToneMappingPass` 不拥有 scene color。
- descriptor layout 新增 uniform binding 后 shader binding 一致。
- per-frame uniform buffer 避免 in-flight 数据覆盖。
- shader source smoke 覆盖 tone mapping uniform token。
- fake RHI smoke 覆盖 uniform 数据流。
- default sandbox 行为不回退。
- DamagedHelmet optional smoke 不回退。

## 验证计划

0.28.0 ~ 0.28.3 完成后，另一台有 Windows/MSVC/vcpkg/DXC 环境的机器需要至少通过：

```powershell
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
```

建议 targeted build：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_shader_assets_smoke
cmake --build --preset msvc-vcpkg-debug --target ark_framework_headers_smoke
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
```

如果继续实现 0.28.4 并新增 `ark_tone_mapping_pass_smoke`，再补充：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_tone_mapping_pass_smoke
build/msvc-vcpkg/Debug/ark_tone_mapping_pass_smoke.exe
```

runtime smoke：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

## 当前实现状态

已完成 0.28.0 ~ 0.28.3 的代码编写，尚未在当前 Mac 环境运行 build/test。

已完成：

- 新增本文档，明确 tone mapping settings / color pipeline 收口范围。
- `RenderView` 新增 `ToneMappingSettings`，包含 `exposure` 和 `outputGamma`，并提供 getter / setter。
- `RenderView::setDefaultPerspective()` 不重置 tone mapping settings。
- `ToneMappingPass` 新增 `ToneMappingUniform`，每个 frame slot 一个 `ToneMappingUniformBuffer`。
- `ToneMappingPass` descriptor layout 新增 binding 2 `UniformBuffer`，fragment stage 可见。
- `ToneMappingPass::execute()` 从 `frameContext.view->toneMappingSettings()` 读取 settings 并更新当前 frame slot uniform。
- CPU 侧最小防御：`exposure < 0` clamp 到 `0`，`outputGamma <= 0` fallback 到 `2.2`。
- `tonemap.frag.hlsl` 删除 hardcoded `Exposure`，改用 binding 2 的 `ConstantBuffer<ToneMappingUniform>`。
- shader 使用 `g_ToneMapping.exposure` 和 `g_ToneMapping.inverseOutputGamma`，output helper 命名为 `linearToOutput()`。
- `shader_assets_smoke` 已更新 tone mapping shader source token。
- `framework_headers_smoke` 已覆盖 `ToneMappingSettings` public API 编译路径。

尚未完成：

- 0.28.4 的独立 fake RHI `ToneMappingPass` uniform 数据流 smoke test 尚未新增。
- `CMakeLists.txt` 尚未加入新的 `ark_tone_mapping_pass_smoke` target。
- 当前 Mac 环境未运行 `cmake --build`、`ctest` 或 runtime sandbox smoke。

## 完成标准

Phase 0.28 完成时应满足：

- `RenderView` 或明确的 renderer view settings 能表达 tone mapping settings。
- exposure 不再硬编码在 shader。
- output gamma / output encoding 策略有明确默认值和文档说明。
- `ToneMappingPass` 通过 uniform buffer 接收 settings。
- `tonemap.frag.hlsl` 使用 uniform exposure / inverse gamma。
- scene color linear HDR 与 output encoding 约定写入文档。
- smoke tests 覆盖 public settings、shader source 和 ToneMappingPass uniform 数据流。
- build、CTest、default sandbox、DamagedHelmet optional smoke 通过。
- 文档和 handoff 记录当前已支持 tone mapping settings / color pipeline 最小收口。
- 文档明确仍未支持 auto exposure、bloom、IBL、ACES/filmic 参数化和完整 post-process stack。

## 后续 Phase 建议

Phase 0.28 后建议进入：

1. IBL / environment map 前置：HDR image loading 与 environment texture resource。
2. IBL / environment map / BRDF LUT 最小闭环。
3. bloom 或 exposure 参数化 UI / config。
4. renderer 级资源 / 场景加载入口整理。
5. 基于 camera 和 bounds 的 Blend bucket back-to-front sorting。
6. pipeline / shader / descriptor layout 的 deferred destruction。
7. 更完整的 RenderGraph 设计。
