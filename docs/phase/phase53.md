# Phase 0.53 Physically Based Bloom Foundation

## 完成状态

已完成 0.53.0 文档与范围确认到 0.53.7 验证与收尾。

本阶段新增默认关闭的 Physically Based Bloom 基础闭环：`PostProcessingSettings` 提供 Bloom 配置入口，`RenderView` 持有后处理设置，`ApplicationDesc` 与 sandbox CLI 可显式传入 Bloom 参数，`FrameRenderer` 在 HDR scene color 与 `ToneMappingPass` 之间接入可选 `BloomPass`。默认路径不启用 Bloom，现有 golden baseline 不更新。

实际实现采用一个 `shaders/bloom.frag.hlsl` fragment shader，通过 uniform mode 区分 prefilter / downsample / upsample / composite 四个阶段，并复用现有 fullscreen triangle vertex shader。BloomPass 输出独立的 HDR composite texture，再替换 `FrameContext::sceneColorView` 供 ToneMappingPass 采样，避免原地读写同一 scene color。

## 阶段判断

Phase 0.52 已经把 sandbox / validation 的场景入口和质量入口整理成 preset：

- `SceneResource` 负责模型、环境与 fallback 加载。
- `RendererQualityDesc` 负责默认 IBL bake 质量。
- `RendererPreset` 负责把常用 scene / quality 组合成稳定入口。
- `ark_frame_validation_smoke` 已经具备 tone-mapped LDR artifact 与 golden image 回归。

这意味着 renderer 已经可以开始推进会改变最终画面的后处理效果。下一阶段建议优先做 **Physically Based Bloom**，而不是先做 Auto Exposure、ACES 或 RenderGraph 重构。

原因：

- Bloom 是当前 HDR scene color / tone mapping 路线最自然的后续视觉增强。
- 项目已有 `BloomPass.h` 占位，但尚未建立实际 pass、shader、资源和测试。
- 0.49-0.52 的 golden / preset 基础可以保护默认画面不被意外改变。
- Physically Based Bloom 可以先以可控、默认关闭的方式接入，不必立即更新 golden baseline。

## 核心原则

本阶段 Bloom 应按 physically based bloom 思路实现，而不是传统的“硬阈值亮部贴图 + 简单模糊”。

第一版原则：

- Bloom 在 **linear HDR scene color** 上运行，位置在 `ForwardPass/SkyboxPass` 之后、`ToneMappingPass` 之前。
- Bloom source 来自 HDR scene color，不从 tone-mapped LDR backbuffer 反推。
- 默认路径不改变当前画面：Bloom 默认关闭，或默认强度为 0。
- 启用 Bloom 时，使用 HDR 能量进行多级 downsample / upsample / composite。
- 允许保留 soft threshold / knee 作为艺术控制，但不使用 hard clip 截断亮部。
- composite 应回到 HDR scene color，再交给现有 `ToneMappingPass` 统一输出。
- 第一版优先实现稳定、可测、可调的基础闭环，不追求复杂镜头污渍、鬼影、星芒或自动曝光联动。

推荐公式方向：

```text
scene HDR color
    -> prefilter / soft response
    -> mip-chain downsample
    -> progressive upsample
    -> additive or scatter composite into HDR scene color
    -> ToneMappingPass
```

## 目标

- 新增 Bloom 设置结构，作为 renderer-facing post-processing 配置入口。
- 实现 `BloomPass` 的基础 GPU 资源、shader、descriptor、pipeline 与执行路径。
- 建立 HDR bloom ping-pong / mip resource 管理。
- 在 `FrameRenderer` 中接入 Bloom，但默认不改变当前渲染结果。
- sandbox 提供显式启用 Bloom 的入口，方便人工观察。
- 新增 smoke tests，覆盖 Bloom 设置 sanitize、pass 资源形态、shader source、默认关闭回归。
- 保持现有 golden image 默认 diff 为 0，不更新 baseline。

## 非目标

- 不做 Auto Exposure。
- 不替换现有 `ToneMappingPass`。
- 不引入 ACES / filmic tone mapping 切换。
- 不做 RenderGraph 重构。
- 不做 compute shader bloom；当前 RHI 的 fullscreen graphics pass 路线更直接。
- 不做 lens dirt、ghost、anamorphic streak、star filter。
- 不要求 Bloom 默认启用。
- 不更新默认 golden baseline。

## 推荐 API

### BloomSettings

建议在 renderer 层新增一个小的后处理设置入口，例如：

```text
src/renderer/PostProcessingSettings.h
src/renderer/PostProcessingSettings.cpp
```

第一版可以只包含 Bloom：

```cpp
struct BloomSettings {
    bool enabled = false;
    float intensity = 0.0f;
    float scatter = 0.6f;
    float threshold = 1.0f;
    float softKnee = 0.5f;
    u32 maxMipCount = 6;
};

struct PostProcessingSettings {
    BloomSettings bloom;
};

PostProcessingSettings sanitizePostProcessingSettings(const PostProcessingSettings& settings);
```

建议保留 `PostProcessingSettings` 外层结构，是为了后续自然容纳：

- Auto Exposure。
- Tone mapping operator selection。
- Color grading。
- Vignette / sharpening。

### RenderView 接入

当前 `RenderView` 已经包含：

```cpp
ToneMappingSettings toneMappingSettings;
```

本阶段建议新增：

```cpp
PostProcessingSettings postProcessingSettings;
```

并提供：

```cpp
const PostProcessingSettings& postProcessingSettings() const;
void setPostProcessingSettings(const PostProcessingSettings& settings);
```

这样 Bloom 可以像 ToneMapping 一样由 view 控制，不需要直接塞进 `FrameRenderer` 或全局 renderer 状态。

## Physically Based Bloom 设计

### 线性 HDR 输入

BloomPass 输入必须是 `FrameContext::sceneColorView`，该 texture 当前为 `RGBA16Float`。

Bloom 不应该采样 swapchain backbuffer，也不应该采样 tone-mapped LDR target。

### Prefilter

第一版建议提供 soft response：

```text
response = smooth soft threshold based on luminance
bloomSource = hdrColor * response
```

注意：

- `threshold` 是艺术控制，不是硬裁切。
- 当 `threshold <= 0` 时可以近似全量 HDR low-pass bloom。
- 默认 Bloom 关闭，所以 threshold 默认值不会影响现有画面。

### Downsample

建立 bloom mip chain：

```text
mip0: half resolution
mip1: quarter resolution
mip2: eighth resolution
...
```

建议第一版使用 `RGBA16Float`，保持 HDR 范围。

每一级 downsample 采样上一层，进行低通滤波。不要在 downsample 中做 gamma 空间操作。

### Upsample

从最小 mip 逐级上采样回较大 mip，并与上一层累计：

```text
upsampled = blur(smallerMip)
current = current + upsampled * scatter
```

`scatter` 控制扩散半径感，`intensity` 控制最终贡献。

### Composite

Composite 阶段把 Bloom 结果加回 HDR scene color：

```text
sceneColor = sceneColor + bloomColor * intensity
```

实现上有两个选择：

1. BloomPass 输出到新的 HDR texture，ToneMappingPass 采样 BloomPass 输出。
2. BloomPass composite 到一个新的 HDR scene color，然后替换 `FrameContext::sceneColorView`。

建议第一版采用 **输出新的 HDR post scene color** 的方式，避免原地读写同一 texture。

## RHI / FrameContext 需求

当前 RHI 已支持：

- `TextureUsage::RenderTarget`
- `TextureUsage::ShaderResource`
- `TextureViewDesc::baseMipLevel`
- dynamic rendering
- fullscreen triangle pass
- sampled image + sampler descriptor

第一版 Bloom 可以沿用现有 graphics pipeline 模式，不需要新增 compute API。

可能需要补充或确认：

- `TextureView` 对不同 mip level 的 view 创建路径稳定。
- `BloomPass` 能在 resize 时重建 mip resources。
- `FrameContext` 可传递 Bloom 后的 scene color view 给 `ToneMappingPass`。
- `FrameRenderer` 对 BloomPass 输出 texture 做正确 resource barrier。

## 实施拆分

### 0.53.0 文档与范围确认

- 明确本阶段目标是 Physically Based Bloom foundation。
- 明确 Bloom 默认不改变当前画面。
- 明确第一版走 fullscreen graphics pass，不走 compute。
- 明确不做 Auto Exposure / ACES / RenderGraph。

### 0.53.1 PostProcessing Settings

新增：

```text
src/renderer/PostProcessingSettings.h
src/renderer/PostProcessingSettings.cpp
```

实现：

- `BloomSettings`
- `PostProcessingSettings`
- `sanitizePostProcessingSettings()`

测试覆盖：

- 默认 Bloom disabled。
- intensity / scatter / threshold / softKnee clamp。
- max mip count clamp。

### 0.53.2 Bloom Shader Assets

实际新增 shader：

```text
shaders/bloom.frag.hlsl
```

可复用现有 fullscreen vertex shader 思路。

CMake 中加入 shader 编译：

```text
bloom.frag.spv
```

第一版为了减少 shader 文件和 pipeline 数量，将四个阶段合并为单 shader mode。测试会检查 mode、linear HDR、threshold / scatter / intensity 路径。

### 0.53.3 BloomPass Resource Foundation

扩展：

```text
src/renderer/passes/BloomPass.h
src/renderer/passes/BloomPass.cpp
```

建议 BloomPass 拥有：

- descriptor set layout。
- sampler。
- uniform buffer。
- fullscreen vertex shader。
- prefilter / downsample / upsample / composite fragment shaders。
- pipeline layout。
- per-format pipeline cache。
- bloom mip textures / views。
- post scene color texture / view。

职责：

- `setup()` 创建静态资源。
- `prepare()` 根据 extent 和 settings 创建或重建 bloom resources。
- `execute()` 在 settings disabled 时快速返回。
- Bloom enabled 时执行 prefilter、downsample、upsample、composite。
- 输出 Bloom 后的 HDR scene color view，供 ToneMappingPass 采样。

实际资源策略：

- 使用 `RGBA16Float` 独立 downsample / upsample target，不在同一 texture 上读写不同 mip。
- 每个 fullscreen draw 使用独立 uniform buffer 与 descriptor set，避免同帧 descriptor 覆盖。
- disabled path 只保留静态 shader / sampler / layout，不创建 bloom target，不改变 scene color view。
- extent、settings 或 level count 变化时重建 Bloom target。

### 0.53.4 FrameRenderer 接入

当前流程：

```text
ClearPass / SkyboxPass / ForwardPass
    -> ToneMappingPass
```

目标流程：

```text
ClearPass / SkyboxPass / ForwardPass
    -> BloomPass optional
    -> ToneMappingPass
```

默认 settings 下 Bloom 不执行，`ToneMappingPass` 仍采样原 scene color。

Bloom enabled 时，`ToneMappingPass` 采样 BloomPass composite 后的 HDR scene color。

### 0.53.5 Sandbox Bloom Path

在 `SandboxLaunchOptions` 中增加显式入口：

```powershell
ark_sandbox.exe --bloom
ark_sandbox.exe --bloom --bloom-intensity 0.08
ark_sandbox.exe --preset material-ball --bloom
ark_sandbox.exe --preset specular-validation --bloom
```

建议默认值偏保守：

```text
intensity: 0.04 ~ 0.08
scatter: 0.6
threshold: 1.0
softKnee: 0.5
```

如果当前默认场景缺少明显 HDR 高亮，可以后续新增 bloom validation fixture 或 emissive material path。第一版不强行扩大本阶段范围。

### 0.53.6 Tests

建议新增：

```text
tests/post_processing_settings_smoke.cpp
tests/bloom_pass_smoke.cpp
```

测试覆盖：

- post-processing settings 默认值与 sanitize。
- BloomPass public header 编译。
- BloomPass descriptor layout shape。
- BloomPass disabled 时不创建不必要输出或不改变 scene color path。
- BloomPass enabled 时创建预期数量的 mip resources。
- Bloom shader source 包含 linear HDR、threshold / scatter / intensity 相关路径。
- `ark_frame_validation_smoke` 默认 golden diff 仍为 0。

如果第一版 smoke 暂时难以完整捕获真实 Vulkan Bloom 输出，可以先用 fake RHI 捕获 pipeline / descriptor / uniform / draw 次数，再用 sandbox smoke 做启动验证。

### 0.53.7 验证与收尾

建议执行：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_post_processing_settings_smoke ark_bloom_pass_smoke ark_framework_headers_smoke ark_shader_assets_smoke ark_frame_validation_smoke ark_sandbox
build\msvc-vcpkg\Debug\ark_post_processing_settings_smoke.exe
build\msvc-vcpkg\Debug\ark_bloom_pass_smoke.exe
build\msvc-vcpkg\Debug\ark_framework_headers_smoke.exe
build\msvc-vcpkg\Debug\ark_shader_assets_smoke.exe
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

Sandbox smoke：

```powershell
ark_sandbox hidden-window smoke
ark_sandbox hidden-window smoke with --bloom
ark_sandbox hidden-window smoke with --preset material-ball --bloom
ark_sandbox hidden-window smoke with --preset specular-validation --bloom
```

## 完成标准

- Bloom 设置 API 存在，并有明确 sanitize 行为。
- Bloom 默认关闭或默认强度为 0，现有默认画面不变。
- `BloomPass` 能在 HDR scene color 与 ToneMappingPass 之间运行。
- Bloom 资源随 viewport resize 正确重建。
- Bloom 使用 linear HDR texture，不采样 LDR backbuffer。
- Bloom enabled 时 sandbox 能看到可观察的辉光效果。
- 默认 `ark_frame_validation_smoke` golden diff 仍为 0。
- 新增 smoke tests 覆盖设置、shader、pass 基本行为。
- full build 与 CTest 通过。
- handoff 更新本阶段结果与后续风险。

## 本阶段验证结果

已完成：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_post_processing_settings_smoke ark_bloom_pass_smoke ark_framework_headers_smoke ark_shader_assets_smoke ark_frame_validation_smoke ark_sandbox
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(post_processing_settings|bloom_pass|framework_headers|shader_assets|frame_validation)_smoke" --output-on-failure
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
ark_sandbox hidden-window smoke
ark_sandbox hidden-window smoke with --bloom
ark_sandbox hidden-window smoke with --preset material-ball --bloom
ark_sandbox hidden-window smoke with --preset specular-validation --bloom
```

结果：

- targeted build 通过。
- `ark_post_processing_settings_smoke` 通过。
- `ark_bloom_pass_smoke` 通过。
- `ark_framework_headers_smoke` 通过。
- `ark_shader_assets_smoke` 通过。
- `ark_frame_validation_smoke` 通过，默认 golden diff 保持 0。
- `git diff --check` 仅有 CRLF 行尾提示，无 whitespace 错误。
- full build 通过。
- CTest 27/27 通过。
- 默认 sandbox、`--bloom`、`--preset material-ball --bloom`、`--preset specular-validation --bloom` hidden-window smoke 通过。

## 风险与注意事项

- Bloom 很容易造成整体画面过曝，因此默认不能改变现有 golden。
- 当前 renderer 没有 Auto Exposure，Bloom intensity 必须保守。
- 如果使用 hard threshold，会让低亮度高光过渡不自然；本阶段应优先 soft response。
- 如果 composite 阶段读写同一 texture，容易触发 undefined behavior；第一版建议输出到独立 HDR texture。
- 需要严格区分 linear HDR 与 gamma/LDR，Bloom shader 中不要做 gamma 变换。
- 多 mip texture view 与 resource barrier 是本阶段最容易出错的 RHI 细节。
- 如果默认资源没有足够强的 HDR 高亮，sandbox 中 Bloom 效果可能不明显，需要后续补充 bloom validation fixture。

## 后续方向

Phase 0.53 完成后，建议优先级如下：

1. Bloom validation fixture：补一个带 emissive / 高亮窗格 / 金属高光的 fixture，用于稳定观察 Bloom。
2. Tone mapping operator presets：在现有 tone mapping 设置上增加 ACES / Reinhard / Linear 等 operator。
3. Auto Exposure prelude：建立 luminance mip 或 histogram 基础资源。
4. Transparent sorting：基于 camera position 和 bounds 做 Blend bucket back-to-front 排序。
5. RenderGraph 深化：在后处理 pass 增多后，再考虑声明式 pass dependency。
