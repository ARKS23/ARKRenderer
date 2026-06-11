# Phase 0.27 HDR Scene Color / Tone Mapping

## 阶段判断

Phase 0.26 已经把 `mesh.frag.hlsl` 中的 direct lighting 从旧的 specular power 模型升级为 Cook-Torrance direct BRDF：

```text
glTF metallic-roughness inputs
    -> GGX / Smith / Schlick Fresnel
    -> direct directional lighting
    -> current backbuffer
```

这让材质响应更接近 PBR，但当前输出仍然直接写入 swapchain backbuffer。也就是说，BRDF 结果在最后显示前没有经过 HDR scene color 和 tone mapping：

```text
ForwardPass
    -> swapchain backbuffer
    -> Present
```

这个结构会限制后续画面质量：

- direct BRDF 输出容易在 LDR backbuffer 上过早钳制。
- 高光、金属材质和更高 light intensity 缺少稳定承载空间。
- 后续 IBL、bloom、exposure、color grading 都缺少 scene color 输入。
- `ForwardPass` 当前 pipeline format 仍从 swapchain 推导，不适合渲染到中间 HDR attachment。

Phase 0.27 的重点，是把 frame renderer 从“ForwardPass 直接写 backbuffer”推进到“ForwardPass 写 HDR scene color，ToneMappingPass 再写 backbuffer”的最小闭环：

```text
ForwardPass
    -> RGBA16Float scene color
    -> ToneMappingPass
    -> swapchain backbuffer
    -> Present
```

这不是完整后处理框架，也不是 RenderGraph。它只是为 PBR 输出建立一个清晰、可测试、可继续扩展的中间色缓冲。

## 目标

Phase 0.27 目标：

- 在 renderer 层引入 HDR scene color render target。
- scene color 格式使用 `rhi::Format::RGBA16Float`。
- scene color usage 至少包含 `TextureUsage::RenderTarget | TextureUsage::ShaderResource`。
- `FrameRenderer` 负责 scene color 的创建、resize 和一帧内资源状态转换。
- `ForwardPass` 渲染到 scene color，而不是直接渲染到 swapchain backbuffer。
- `ToneMappingPass` 采样 scene color，并输出到 swapchain backbuffer。
- 新增 fullscreen triangle tone mapping shaders。
- tone mapping 初版支持固定 exposure 和基础 filmic/Reinhard/ACES 映射中的一种。
- `ForwardPass` pipeline format 从当前 render target format 获取，不再强依赖 swapchain color format。
- 保留当前 depth buffer 路径，depth 仍使用 swapchain depth buffer。
- 保留 Phase 0.20 alpha mask / blend 行为。
- 保留 Phase 0.23 RenderQueue alpha bucket ordering。
- 保留 Phase 0.24 doubleSided culling 行为。
- 保留 Phase 0.25 scene lighting / camera position 数据入口。
- 保留 Phase 0.26 direct lighting BRDF，不回退 shader 公式。
- 扩展 shader asset smoke test，覆盖 tone mapping shader 编译和关键源码。
- 扩展 pipeline / renderer smoke test，覆盖 HDR format 与 ForwardPass format 解耦。
- 文档明确当前仍不是完整 PBR 后处理栈。

## 非目标

Phase 0.27 暂不做：

- 不做 IBL / environment map。
- 不做 irradiance map。
- 不做 prefiltered specular environment map。
- 不做 BRDF LUT。
- 不做 bloom。
- 不做 auto exposure / histogram。
- 不做 eye adaptation。
- 不做 color grading / LUT。
- 不做 TAA / FXAA / MSAA resolve。
- 不做 shadow map。
- 不做 point light / spot light / area light。
- 不做 glTF `KHR_lights_punctual`。
- 不做 glTF material extensions，例如 clearcoat、sheen、transmission、ior、specular。
- 不做 transparent back-to-front sorting。
- 不做 OIT。
- 不做 RenderGraph 重构。
- 不做 bindless descriptor。
- 不做多 render target / GBuffer。
- 不做 compute post-processing。
- 不做 screenshot golden test。
- 不做 UI / ImGui exposure 调参面板。

## 模块边界

继续遵守现有设计文档和最近 phase：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase24.md
docs/phase/phase25.md
docs/phase/phase26.md
```

边界要求：

- `rhi/` 优先不新增抽象；已有 texture、texture view、sampler、descriptor、draw、barrier 能覆盖本阶段。
- `rhi/vulkan/` 原则上不需要新增能力；如发现 `RGBA16Float` sampled/render target 路径缺失，只做必要修复。
- `asset/` 不参与本阶段，除了 shader 编译清单继续通过 CMake 管理。
- `renderer/` 是本阶段主要落点。
- `FrameRenderer` 负责 scene color 生命周期和 render scope 调度。
- `FrameContext` 只增加 pass 间共享的必要 frame 语义，不持有资源所有权。
- `ForwardPass` 只做 render target format 解耦，不引入 post-process 知识。
- `ToneMappingPass` 负责自己的 shader、pipeline、descriptor layout、descriptor set 和 sampler。
- `shaders/mesh.frag.hlsl` 原则上不修改；本阶段新增 tone mapping shader。
- `tests/` 优先扩展 smoke tests，不引入截图基准。

## 当前行为

当前 `FrameContext` 只暴露 swapchain backbuffer：

```cpp
rhi::TextureView* backBufferView = nullptr;
rhi::Extent2D extent{};
rhi::ClearColor clearColor{};
```

当前 `FrameRenderer` 一帧内只有一个 render scope：

```text
backbuffer -> RenderTarget
depth      -> DepthStencilWrite

beginRendering(backbuffer + depth)
    ClearPass
    ForwardPass
endRendering()

backbuffer -> Present
```

当前 `ForwardPass` pipeline key 使用 swapchain format：

```cpp
const rhi::SwapChainDesc& swapChainDesc = frameContext.swapChain->getDesc();
key.colorFormat = swapChainDesc.colorFormat;
key.depthFormat = swapChainDesc.depthFormat;
```

这在直接渲染到 backbuffer 时成立，但在 HDR scene color 阶段会变成问题：

```text
actual color attachment: RGBA16Float scene color
pipeline color format:  swapchain color format
```

Vulkan dynamic rendering 要求 pipeline 创建时的 attachment format 与实际 rendering attachment format 匹配。因此 Phase 0.27 必须先解耦 `ForwardPass` 的 format 来源。

## 已有前置能力

当前 RHI 已经具备本阶段需要的大部分能力：

- `rhi::Format::RGBA16Float` 已存在。
- Vulkan format mapping 已覆盖 `RGBA16Float`。
- `TextureUsage::RenderTarget` 已存在。
- `TextureUsage::ShaderResource` 已存在。
- `ResourceState::RenderTarget` 已存在。
- `ResourceState::ShaderResource` 已存在。
- `RenderDevice::createTexture()` 已存在。
- `RenderDevice::createTextureView()` 已存在。
- `RenderDevice::createSampler()` 已存在。
- `DescriptorType::SampledImage` 已存在。
- `DescriptorType::Sampler` 已存在。
- `DeviceContext::draw()` 已存在，可用于 fullscreen triangle。
- `GraphicsPipelineDesc` 已支持 dynamic rendering 的 `colorFormat` / `depthFormat`。

因此本阶段不需要先设计新的 RHI API。重点是把已有能力串成 renderer 级闭环。

## 建议设计

### FrameContext 渲染目标语义

建议在 `FrameContext` 中增加当前 render target format 和 scene color 输入：

```cpp
rhi::Format colorFormat = rhi::Format::Unknown;
rhi::Format depthFormat = rhi::Format::Unknown;
rhi::TextureView* sceneColorView = nullptr;
```

含义：

- `colorFormat` 表示当前 render scope 的 color attachment format。
- `depthFormat` 表示当前 render scope 的 depth attachment format。
- `sceneColorView` 表示 tone mapping pass 的 HDR 输入。

`FrameContext` 不拥有这些资源，只作为 pass 调度时的轻量上下文。

### ForwardPass Format 解耦

`ForwardPass::getOrCreatePipeline()` 应从 `FrameContext` 获取 format：

```cpp
key.colorFormat = frameContext.colorFormat;
key.depthFormat = frameContext.depthFormat;
```

建议保留防御性 fallback：

```text
if frameContext.colorFormat == Unknown and swapChain exists:
    use swapchain color format

if frameContext.depthFormat == Unknown and swapChain exists:
    use swapchain depth format
```

这样旧测试或临时调用路径不至于立刻失效，但 `FrameRenderer` 在正式路径中必须明确设置 format。

审核点：

- `ForwardPass` 不应该知道 HDR scene color。
- `ForwardPass` 不应该采样 scene color。
- `ForwardPass` 只关心当前 pipeline attachment format。
- pipeline cache key 必须包含 color/depth format。

### HDR Scene Color 资源

`DefaultFrameRenderer` 建议持有：

```cpp
rhi::RenderDevice* m_Device = nullptr;
rhi::Extent2D m_Extent{};
Scope<rhi::Texture> m_SceneColor;
Scope<rhi::TextureView> m_SceneColorView;
```

scene color 描述：

```cpp
rhi::TextureDesc desc{};
desc.debugName = "FrameRenderer.SceneColor";
desc.extent = frameContext.extent;
desc.format = rhi::Format::RGBA16Float;
desc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
```

texture view 描述：

```cpp
rhi::TextureViewDesc viewDesc{};
viewDesc.format = rhi::Format::RGBA16Float;
```

资源创建时机：

- `setup()` 保存 `RenderDevice*`。
- `render()` 中按需 `ensureSceneColor(frameContext.extent)`。
- `resize()` 标记尺寸变化，下一帧重建。

资源释放策略：

- 如果 resize 发生在 GPU idle 之后，可以直接重建。
- 如果未来支持 in-flight resize，应使用 `DeviceContext::deferReleaseTexture()` / `deferReleaseTextureView()`。
- Phase 0.27 可以先沿用当前 swapchain resize 的同步假设，但文档和注释要说明边界。

### FrameRenderer 两段渲染

建议把当前单 render scope 拆成两个 render scope。

第一段：scene pass

```text
sceneColor -> RenderTarget
depth      -> DepthStencilWrite

FrameContext.colorFormat = RGBA16Float
FrameContext.depthFormat = swapchain.depthFormat

beginRendering(sceneColor + depth)
    ClearPass
    ForwardPass
endRendering()
```

第二段：tone mapping pass

```text
sceneColor -> ShaderResource
backbuffer -> RenderTarget

FrameContext.colorFormat = swapchain.colorFormat
FrameContext.depthFormat = Unknown
FrameContext.sceneColorView = sceneColorView

beginRendering(backbuffer)
    ToneMappingPass
endRendering()

backbuffer -> Present
```

注意：

- tone mapping pass 不需要 depth attachment。
- tone mapping pass 使用 fullscreen triangle，不需要 vertex buffer。
- tone mapping pass 的 color format 应该是 swapchain color format。
- scene color 在 tone mapping 前必须 transition 到 `ShaderResource`。
- backbuffer 在 present 前必须 transition 到 `Present`。

### ToneMappingPass

`ToneMappingPass` 建议拥有：

```text
DescriptorSetLayout
DescriptorSet
Sampler
PipelineLayout
VertexShader
FragmentShader
PipelineState
```

descriptor binding 建议：

```text
binding 0: SampledImage sceneColor
binding 1: Sampler sceneSampler
```

shader 文件建议：

```text
shaders/tonemap.vert.hlsl
shaders/tonemap.frag.hlsl
```

fullscreen triangle vertex shader 可以使用 `SV_VertexID`：

```hlsl
struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};
```

fragment shader 最小输入：

```hlsl
[[vk::binding(0, 0)]] Texture2D<float4> g_SceneColor;
[[vk::binding(1, 0)]] SamplerState g_SceneSampler;
```

初版 tone mapping 建议：

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

如果想让观感更接近常见 PBR sample，也可以用 ACES fitted：

```hlsl
float3 acesFitted(float3 x);
```

两种都可接受。Phase 0.27 更看重架构闭环，不强求最终美术观感。建议优先选公式短、测试关键字明确、不会引入额外 uniform 的实现。

### CMake Shader 编译

需要把新增 shader 加入编译清单：

```cmake
ark_compile_hlsl_shader("${PROJECT_SOURCE_DIR}/shaders/tonemap.vert.hlsl" vs_6_0 tonemap.vert)
ark_compile_hlsl_shader("${PROJECT_SOURCE_DIR}/shaders/tonemap.frag.hlsl" ps_6_0 tonemap.frag)
```

并保证 `ark_shaders` 依赖包含新 SPIR-V 输出。

### Clear 行为

scene pass：

- scene color loadOp 使用 `Clear`。
- clear color 继续使用 `FrameContext::clearColor`。
- depth loadOp 使用 `Clear`，clear depth 为 `1.0f`。

tone mapping pass：

- backbuffer loadOp 可以使用 `Clear` 或 `DontCare`。
- 初版建议仍使用 `Clear`，便于调试失败时看到明确背景。
- backbuffer storeOp 必须使用 `Store`。

## 测试策略

优先扩展：

```text
tests/shader_assets_smoke.cpp
tests/forward_pass_pipeline_smoke.cpp
```

### Shader Assets Smoke

新增验证：

- `tonemap.vert.spv` 能加载并通过 SPIR-V magic 校验。
- `tonemap.frag.spv` 能加载并通过 SPIR-V magic 校验。
- `tonemap.vert.hlsl` 包含 `SV_VertexID`。
- `tonemap.frag.hlsl` 包含 `Texture2D`。
- `tonemap.frag.hlsl` 包含 `SamplerState`。
- `tonemap.frag.hlsl` 包含 `Exposure` 或 `exposure`。
- `tonemap.frag.hlsl` 包含 `pow` 或明确的 sRGB 转换 helper。
- `tonemap.frag.hlsl` 包含 tone mapping helper，例如 `applyToneMapping` / `acesFitted` / `reinhardToneMap`。

保留既有 mesh shader BRDF smoke：

- `distributionGGX`
- `geometrySmith`
- `fresnelSchlick`
- `specularDenominator`

### ForwardPass Pipeline Smoke

扩展或新增断言：

- `FrameContext::colorFormat = rhi::Format::RGBA16Float` 时，ForwardPass pipeline 使用 RGBA16Float。
- `FrameContext::depthFormat` 能继续进入 pipeline key。
- swapchain color format 不再是 ForwardPass pipeline 的唯一来源。

如果现有 fake RHI 测试难以直接观察 pipeline desc，可在 fake device 中记录最近一次 `GraphicsPipelineDesc`，保持测试小而清晰。

### FrameRenderer Smoke

可选新增 fake RHI smoke，覆盖：

- scene color texture desc format 为 `RGBA16Float`。
- scene color usage 包含 `RenderTarget` 和 `ShaderResource`。
- 第一段 beginRendering 使用 scene color view。
- 第二段 beginRendering 使用 backbuffer view。
- barrier 顺序包含 scene color `RenderTarget -> ShaderResource`。
- backbuffer 最终进入 `Present`。

如果 fake RHI 成本过高，本阶段可以先依赖 runtime sandbox smoke，但至少要用 source/pipeline smoke 守住关键行为。

### Runtime Smoke

必须运行：

```powershell
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
```

建议运行：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

验收关注：

- 窗口能启动。
- 默认 fixture 不全黑、不全白、不闪烁。
- DamagedHelmet 能启动。
- high roughness / low roughness 材质仍能区分。
- alpha mask / blend 行为不明显回退。

## 实施顺序

### 0.27.0 文档与范围确认

目标：

- 新增 `docs/phase/phase27.md`。
- 明确本阶段只做 HDR scene color / tone mapping 最小闭环。
- 明确不做 IBL、bloom、auto exposure、RenderGraph、shadow、多光源。

审核点：

- 不重复 Phase 0.26 BRDF 改造。
- 不提前引入复杂后处理框架。
- 不扩大 RHI API 面。

### 0.27.1 FrameContext / ForwardPass Format 解耦

目标：

- `FrameContext` 增加 `colorFormat` / `depthFormat`。
- `ForwardPass` pipeline key 使用当前 render target format。
- 保留必要 fallback，避免旧测试路径崩溃。
- 扩展 ForwardPass pipeline smoke。

审核点：

- `ForwardPass` 不依赖 HDR scene color 资源。
- `ForwardPass` 不新增 descriptor binding。
- pipeline key 保持包含 alpha mode、doubleSided、color/depth format。

### 0.27.2 HDR Scene Color 资源

目标：

- `DefaultFrameRenderer` 持有 scene color texture/view。
- scene color 使用 `RGBA16Float`。
- scene color usage 包含 `RenderTarget | ShaderResource`。
- resize 后能重建 scene color。

审核点：

- 不把 scene color 放进 `RenderScene`。
- 不让 pass 拥有 frame-level render target。
- 不引入多 render target。

### 0.27.3 ToneMappingPass 接入

目标：

- 新增 `ToneMappingPass.cpp`。
- 创建 descriptor layout、descriptor set、sampler、pipeline layout、shader、pipeline。
- 新增 `tonemap.vert.hlsl` / `tonemap.frag.hlsl`。
- CMake 编译新增 shaders。
- `ToneMappingPass` 采样 `FrameContext::sceneColorView`。

审核点：

- fullscreen triangle 不需要 vertex buffer。
- tone mapping pipeline color format 使用 swapchain color format。
- descriptor binding 与 HLSL binding 一致。

### 0.27.4 FrameRenderer 两段渲染

目标：

- 第一段 scene pass 渲染到 scene color + depth。
- 第二段 tone mapping pass 渲染到 backbuffer。
- 正确设置 viewport/scissor。
- 正确设置 scene pass 与 tone mapping pass 的 `FrameContext::colorFormat` / `depthFormat`。
- 正确插入 scene color 和 backbuffer barrier。

审核点：

- scene color 在 tone mapping 前进入 `ShaderResource`。
- backbuffer 在 tone mapping 前进入 `RenderTarget`。
- backbuffer 在 present 前进入 `Present`。
- depth 不传入 tone mapping render scope。

### 0.27.5 Tests

目标：

- 扩展 `shader_assets_smoke`。
- 扩展 `forward_pass_pipeline_smoke`。
- 按可维护成本决定是否新增 frame renderer fake RHI smoke。
- 确保 CTest 覆盖新增 shader 资产。

审核点：

- 测试不依赖真实窗口，除 runtime smoke 外。
- smoke test 能捕获 tonemap shader 缺失或 ForwardPass format 回退。

### 0.27.6 验证与收尾

目标：

- 更新本文档实现状态。
- 同步 `docs/codex_handoff.md`。
- 记录验证命令和结果。
- 确认工作区无非预期改动。

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

- `ForwardPass` pipeline format 不再硬编码来自 swapchain。
- `FrameRenderer` 创建 `RGBA16Float` scene color。
- scene color usage 包含 `RenderTarget` 和 `ShaderResource`。
- scene pass 渲染到 scene color，而不是 backbuffer。
- tone mapping pass 渲染到 backbuffer。
- tone mapping pass 不使用 depth。
- scene color 在采样前 transition 到 `ShaderResource`。
- backbuffer 在 present 前 transition 到 `Present`。
- tone mapping shader 被 CMake 编译。
- shader asset smoke 覆盖 tonemap SPIR-V。
- Phase 0.26 direct BRDF helper 不回退。
- alpha mask / blend 不回退。
- texture transform / UV selection 不回退。
- scene lighting / camera position 数据来源不回退。
- 文档明确仍未支持 IBL、bloom、auto exposure、shadow、多光源和完整 post-process stack。

## 验证计划

必须通过：

```powershell
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
```

建议单独跑：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_shader_assets_smoke
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
```

runtime smoke：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

## 当前实现状态

已完成 0.27.0 ~ 0.27.6：

- 本文档已明确 Phase 0.27 范围、非目标、模块边界、测试策略和验证计划。
- `FrameContext` 已新增当前 render scope 的 `colorFormat` / `depthFormat`，以及 post pass 采样用的 `sceneColorView`。
- `ForwardPass` pipeline key 已优先使用 `FrameContext::colorFormat` / `FrameContext::depthFormat`，不再强依赖 swapchain color format。
- `DefaultFrameRenderer` 已创建 `RGBA16Float` scene color texture/view。
- scene color usage 已设置为 `TextureUsage::RenderTarget | TextureUsage::ShaderResource`。
- `FrameRenderer` 已从单 render scope 拆成两段：
  - scene pass：`ClearPass` + `ForwardPass` 渲染到 HDR scene color + depth。
  - post pass：`ToneMappingPass` 采样 scene color，输出到 swapchain backbuffer。
- `ToneMappingPass` 已拥有 descriptor layout、per-frame descriptor sets、sampler、pipeline layout、shader 和 pipeline。
- 新增 `shaders/tonemap.vert.hlsl`，通过 `SV_VertexID` 输出 fullscreen triangle。
- 新增 `shaders/tonemap.frag.hlsl`，采样 HDR scene color，执行固定 exposure + Reinhard tone mapping + linear-to-sRGB。
- CMake 已编译 `tonemap.vert.spv` 和 `tonemap.frag.spv`。
- `shader_assets_smoke` 已覆盖 tonemap shader SPIR-V 和关键源码 token。
- `forward_pass_pipeline_smoke` 已覆盖 `RGBA16Float` FrameContext attachment format 进入 ForwardPass pipeline desc。
- `framework_headers_smoke` 已覆盖 `FrameContext` 新字段和 scene color usage 的 public header 编译路径。
- Phase 0.26 direct BRDF、alpha state、texture transform、scene lighting / camera position 路径未回退。

本轮 Phase 0.27 验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_shader_assets_smoke
cmake --build --preset msvc-vcpkg-debug --target ark_forward_pass_pipeline_smoke
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
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
full build passed
CTest: 9/9 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
```

## 完成标准

Phase 0.27 完成时应满足：

- Forward scene pass 输出到 `RGBA16Float` scene color。
- ToneMappingPass 从 scene color 采样并输出到 swapchain backbuffer。
- ForwardPass pipeline format 与当前 render target format 一致。
- backbuffer 不再作为 ForwardPass 的直接 color attachment。
- tone mapping shaders 编译并通过 shader asset smoke。
- 相关 smoke tests 覆盖 HDR format、tonemap shader 和关键调度行为。
- build、CTest、default sandbox、DamagedHelmet optional smoke 通过。
- 文档和 handoff 记录当前已支持 HDR scene color / tone mapping。
- 文档明确仍未支持 IBL、bloom、auto exposure、shadow、多光源、完整 glTF PBR extensions。

## 后续 Phase 建议

Phase 0.27 后建议进入：

1. IBL / environment map / BRDF LUT。
2. bloom 或 basic exposure 参数化。
3. renderer 级资源 / 场景加载入口整理。
4. 基于 camera 和 bounds 的 Blend bucket back-to-front sorting。
5. pipeline / shader / descriptor layout 的 deferred destruction。
6. 更完整的 RenderGraph 设计。
