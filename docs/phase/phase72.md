# Phase 0.72 SSAO Effect Foundation

## 实施状态

已完成。

- 0.72.0 文档与范围确认：已完成。
- 0.72.1 Settings / Frame Contract：已完成。
- 0.72.2 NormalDepth Prepass：已完成。
- 0.72.3 SSAO Evaluate：已完成。
- 0.72.4 Blur / Composite Path：已完成。
- 0.72.5 Sandbox UI / Diagnostics：已完成。
- 0.72.6 Tests：已完成。
- 0.72.7 验证与收尾：已完成。

## 本轮实现记录

### 0.72.0 文档与范围确认

- 确认 Phase 0.72 先做 Forward-compatible SSAO 最小闭环，不引入完整 deferred renderer / GBuffer。
- 确认 SSAO 后续实现落在 `src/renderer/effects/ssao`，保持 effect 内聚。
- 确认第一版使用 normal-depth 预通道 + AO evaluate + blur + composite 的路径。

### 0.72.1 Settings / Frame Contract

- 新增 `SsaoSettings`，挂到 `PostProcessingSettings::ssao`。
- 新增 `SsaoDebugMode`，为后续 `None / Occlusion / NormalDepth` 调试显示预留契约。
- 扩展 `sanitizePostProcessingSettings()`，统一 clamp SSAO 半径、强度、bias、power、sample count、blur radius 和 resolution scale。
- 扩展 `FrameContext`，预留 `ssaoNormalDepthView / ssaoOcclusionView / ssaoCompositeView / ssaoExtent`，这些字段只传递 pass 间引用，不拥有资源。
- 补充 `post_processing_settings_smoke`，覆盖 SSAO 默认值、合法值保留、非法值 clamp 和 `RenderView` sanitize 行为。

### 0.72.2 NormalDepth Prepass

- 新增 `SsaoPass` 的 normal-depth 几何预通道，输出 view-space normal 和 linear view depth 到 `RGBA16Float` 临时纹理。
- normal-depth 阶段重绘 forward 可见队列，跳过 blend draw items，mask 材质复用 base color alpha cutoff 做 discard。
- 预通道使用 SSAO 私有 `D32Float` depth target，按 SSAO 分辨率清空并写入，避免依赖 Forward pass 的 depth attachment 内容。

### 0.72.3 SSAO Evaluate

- 新增 `ssao.frag.hlsl` fullscreen evaluate path，通过 view-space normal-depth 重建 view position。
- 使用固定 hemisphere sampling、屏幕空间 hash 旋转和 radius / intensity / bias / power / sample count 参数生成 AO mask。
- AO 结果写入 SSAO occlusion target，供后续 blur 和 debug mode 使用。

### 0.72.4 Blur / Composite Path

- 在同一个 fullscreen shader 中通过 mode 分支实现 evaluate / blur / composite，减少 shader 文件和 pipeline 数量。
- blur 阶段支持 `blurRadius` 小半径盒滤波。
- composite 阶段将 AO 乘回 HDR scene color，并更新 `FrameContext::sceneColorView`，让 Bloom / ToneMapping 消费 SSAO 后的结果。

### 0.72.5 Sandbox UI / Diagnostics

- Sandbox UI 新增 SSAO 面板，暴露 Enabled、Radius、Intensity、Bias、Power、Samples、Blur Radius、Resolution Scale 和 Debug Mode。
- Diagnostics 面板显示 SSAO 开关、debug mode 和当前 SSAO target extent。
- 默认 sandbox preset 开启 SSAO，便于在 Sponza + DamagedHelmet + 阴影验证球场景中直接观察墙角、柱脚和接触面的 AO。

### 0.72.6 Tests

- 新增 `ark_ssao_pass_smoke`，覆盖 disabled path、target 创建、normal-depth 私有 depth、fullscreen mode、frame binding 发布和 resolution scale rebuild。
- 扩展 `ark_shader_assets_smoke`，纳入 `ssao_normal_depth.vert/frag` 和 `ssao.frag`。
- 扩展 `ark_sandbox_ui_settings_smoke` / `ark_post_processing_settings_smoke` / `ark_framework_headers_smoke`，覆盖 SSAO 设置、UI 传递和公开头文件编译。

### 0.72.7 验证与收尾

- 更新 `default_composite_scene.png` golden，让默认场景基线覆盖 SSAO 开启后的画面。
- 保持 `specular_ibl_validation_fixture.png` 和 `material_ball_validation_fixture.png` golden diff 为 0。

## 验证记录

- `cmake --build --preset msvc-vcpkg-debug`：通过。
- `ctest --preset msvc-vcpkg-debug -R "ark_ssao_pass_smoke|ark_shader_assets_smoke|ark_post_processing_settings_smoke|ark_sandbox_ui_settings_smoke|ark_framework_headers_smoke" --output-on-failure`：通过，5/5。
- `build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe --update-golden`：通过，并更新默认组合场景 golden。
- `ctest --preset msvc-vcpkg-debug --output-on-failure`：通过，35/35。
- `ark_sandbox` hidden smoke：启动成功，4 秒后停止，未出现启动即退。
- 构建过程中仍有既有 `shadow_pass_smoke.cpp` 的 `FakeDeviceContext` 对齐 warning，和本阶段 SSAO 实现无关。

## 阶段背景

Phase 0.67 ~ 0.71 已经完成 CSM、阴影调试、第一人称摄像机和调试 UI 的基础闭环，Sandbox 默认 Sponza + DamagedHelmet + 金属球场景已经可以用于观察复杂几何、阴影、Bloom、ToneMapping 和 IBL 组合效果。

下一步适合接入 SSAO（Screen Space Ambient Occlusion），用屏幕空间遮蔽增强墙角、柱脚、模型接触面和细碎几何的层次感。当前渲染器仍是 Forward 主路径，没有完整 GBuffer，因此本阶段不做 deferred renderer，而是先实现一个独立、可开关、可调参、可回退的 SSAO 最小闭环。

## 目标

1. 建立清晰的 SSAO 设置契约，接入现有 `PostProcessingSettings` / Sandbox UI。
2. 新增 `renderer/effects/ssao` 目录，放置 SSAO 内部 Pass、资源与实现。
3. 实现一个轻量 normal-depth 预通道，用额外几何 pass 输出 SSAO 所需的 view-space normal + linear depth。
4. 实现 SSAO evaluate pass，生成 AO mask。
5. 实现 blur / composite path，将 AO 结果合成回 HDR scene color，并放在 Bloom / ToneMapping 之前。
6. 在 Sandbox 中默认可通过 UI 开关和调参验证效果。
7. 补充 smoke tests，保证关闭 SSAO 时无副作用，开启 SSAO 时资源、参数、shader 路径稳定。

## 非目标

1. 不引入完整 Deferred Renderer / GBuffer 架构。
2. 不做 GTAO / HBAO / XeGTAO 等高阶 AO 算法。
3. 不做 temporal accumulation / temporal denoise。
4. 不要求 compute shader 路径，优先使用现有 graphics fullscreen pass。
5. 不进行大规模 `renderer` 目录重构。
6. 不把 SSAO 暴露成最终 engine-facing public API，只先完成 renderer 内部和 Sandbox 调试契约。

## 推荐目录结构

```text
src/renderer/effects/ssao/
  SsaoPass.h
  SsaoPass.cpp

shaders/
  ssao_normal_depth.vert.hlsl
  ssao_normal_depth.frag.hlsl
  ssao.frag.hlsl
```

说明：

- `SsaoPass` 作为效果入口，内部管理 normal-depth、AO、blur、composite 等临时纹理。
- normal-depth pass 使用场景 draw items 额外绘制一次几何，输出 view-space normal 和 linear depth。
- `ssao.frag.hlsl` 通过 fullscreen mode 分支承载 AO evaluate、blur 和 composite，与 Bloom / ToneMapping 的后处理组织方式保持一致。
- 如果后续引入 GBuffer 或 depth prepass，`SsaoPass` 可以改成复用外部 normal-depth 输入，不需要推翻 UI 和设置契约。

## 设置契约

已新增：

```cpp
enum class SsaoDebugMode : u32
{
    None = 0,
    Occlusion = 1,
    NormalDepth = 2,
};

struct SsaoSettings
{
    bool enabled = false;
    float radius = 0.6F;
    float intensity = 1.0F;
    float bias = 0.025F;
    float power = 1.5F;
    u32 sampleCount = 16;
    u32 blurRadius = 2;
    float resolutionScale = 1.0F;
    SsaoDebugMode debugMode = SsaoDebugMode::None;
};
```

接入方式：

- 优先挂到 `PostProcessingSettings` 下：`PostProcessingSettings::ssao`。
- Sandbox UI 提供 `Enabled / Radius / Intensity / Bias / Power / Sample Count / Blur Radius / Resolution Scale / Debug Mode`。
- 所有参数在进入 GPU 前做 clamp，避免 UI 调参导致非法 target size、采样数或 shader 分支异常。

Frame contract 已预留：

```cpp
rhi::TextureView* ssaoNormalDepthView = nullptr;
rhi::TextureView* ssaoOcclusionView = nullptr;
rhi::TextureView* ssaoCompositeView = nullptr;
rhi::Extent2D ssaoExtent{};
```

这些字段用于后续 `SsaoPass` 内部各阶段和后续调试显示传递结果引用，资源所有权仍留在 SSAO pass 内部。

## 渲染接入方案

推荐顺序：

```text
ShadowPass
ClearPass
SkyboxPass
ForwardPass
SsaoPass
BloomPass
ToneMappingPass
UI Overlay
```

其中 `SsaoPass` 内部执行：

1. NormalDepth 阶段  
   重绘可见 draw items，输出 `normal.xyz + linearDepth` 到 `RGBA16Float` offscreen texture。

2. SSAO Evaluate 阶段  
   读取 normal-depth，基于当前 camera projection / inverse projection 在 view space 中采样邻域深度，生成 AO mask。

3. Blur 阶段  
   对 AO mask 做小半径 blur，降低噪点。

4. Composite 阶段  
   将 AO 乘回 HDR scene color，并把 `FrameContext::sceneColorView` 更新为 composite 结果，供 Bloom / ToneMapping 继续使用。

注意：

- 第一版允许 SSAO 作为后处理乘到最终 lit color 上，这会同时影响直接光和间接光。它不是物理最严谨的做法，但实现稳定、验证直观。
- 后续如果材质/IBL 管线拆得更细，可以改成只影响 ambient / indirect lighting。

## 算法取舍

本阶段推荐使用固定 hemisphere kernel + 屏幕空间 hash/noise：

- sample kernel 可以由 CPU 生成后上传 uniform，也可以先在 shader 内使用固定采样表。
- 使用屏幕空间旋转噪声减少 banding，暂不强制引入 noise texture。
- 深度/法线来自 normal-depth texture，不依赖 swapchain depth 的 layout 和采样能力。
- AO target 第一版可使用 `RGBA8Unorm` 或项目已有稳定格式；质量稳定后再考虑 `R8Unorm` 等更紧凑格式。

## Alpha Mask 处理

SSAO normal-depth pass 应优先支持：

1. `Opaque`：正常写入 normal-depth。
2. `Mask`：如果复用材质 alpha cutoff 成本可控，则支持 alpha discard。
3. `Blend`：第一版可以跳过，避免透明物体污染 AO。

如果 Phase 0.72 实现时 alpha mask 接入成本偏高，可以先记录为已知限制，但 Sponza 植物、布料边缘会更容易暴露这个问题，建议尽量在本阶段处理。

## Sandbox 验证点

默认验证场景：

- Sponza 中庭墙角、柱脚、拱门凹槽。
- DamagedHelmet 与地面接触区域。
- 金属球与地面接触区域。
- 盆栽、窗台、旗帜附近的细小遮蔽。

调试 UI 建议显示：

- SSAO Enabled。
- Radius / Intensity / Bias / Power。
- Sample Count / Blur Radius / Resolution Scale。
- Debug Mode：`None / AO Only / NormalDepth`。

验收时应能观察到：

- 开启 SSAO 后，接触面和凹槽有更明显的暗部层次。
- 关闭 SSAO 后，画面恢复到原 Forward + IBL + Shadow 结果。
- 调整参数不会触发 device lost、swapchain 错误或资源泄漏。

## 测试计划

0.72.6 Tests 包括：

1. `settings_smoke`  
   验证 `SsaoSettings` 默认值和 clamp 逻辑。

2. `shader_assets_smoke`  
   验证新增 SSAO shader 被 CMake / shader compile 流程纳入。

3. `ssao_pass_smoke`  
   验证 SSAO 关闭时不创建不必要资源，开启时能创建 normal-depth / AO / blur / composite targets。

4. `sandbox_ui_settings_smoke`  
   验证 Sandbox UI 能读写 SSAO 参数，不破坏 Bloom / ToneMapping / Shadow UI。

5. `ctest`  
   跑现有核心测试。若已有 golden frame validation 仍存在历史差异，需要在验证记录中明确说明。

6. `ark_sandbox` hidden smoke  
   启动 Sandbox，等待数秒后关闭，确认初始化和资源释放稳定。

## 任务拆分

### 0.72.0 文档与范围确认

- 确认 SSAO 本阶段只做 Forward-compatible 最小闭环。
- 确认不引入 deferred renderer。
- 确认 SSAO 放入 `renderer/effects/ssao`。

### 0.72.1 Settings / Frame Contract

- 新增 `SsaoSettings`。
- 接入 `PostProcessingSettings`。
- 补充参数 clamp。
- 扩展 `FrameContext` 中 SSAO 所需的输入引用，保持字段命名清晰。

### 0.72.2 NormalDepth Prepass

- 新增 normal-depth shader。
- 输出 view-space normal + linear depth。
- 支持 opaque draw items。
- 尽量支持 alpha mask discard。
- 跳过 blend draw items。

### 0.72.3 SSAO Evaluate

- 新增 AO evaluate fullscreen pass。
- 支持 radius / intensity / bias / power / sample count。
- 输出 AO mask。

### 0.72.4 Blur / Composite Path

- 新增 AO blur pass。
- 新增 composite pass。
- SSAO composite 结果接入 Bloom / ToneMapping 前的 HDR scene color。
- 处理窗口 resize 和 resolution scale 变化。

### 0.72.5 Sandbox UI / Diagnostics

- UI 暴露 SSAO 参数。
- 增加 AO Only / NormalDepth 调试显示。
- 在 diagnostics 中显示 target extent、sample count、是否半分辨率等信息。

### 0.72.6 Tests

- 补充 settings / shader / pass / UI smoke tests。
- 跑 build + ctest。
- 跑 Sandbox hidden smoke。

### 0.72.7 验证与收尾

- 同步阶段文档实施记录。
- 记录 Sandbox 视觉验证结论。
- 记录已知限制和下一阶段建议。

## 风险与后续

1. 额外几何 pass 会增加 draw cost  
   这是 Forward 路径下接入 SSAO 的合理代价。后续如果引入 depth prepass / GBuffer，可以让 SSAO 复用已有 normal-depth。

2. 后处理 AO 会压暗直接光  
   第一版可接受。后续更合理的方式是把 AO 作为 ambient occlusion 项，只影响 IBL diffuse / ambient。

3. Alpha mask 不一致会导致植物或布料 AO 错误  
   本阶段尽量支持 mask discard；如果暂时不支持，需要明确写入已知限制。

4. 半分辨率上采样可能产生边缘 halo  
   第一版可默认全分辨率，半分辨率作为可选调参项。

## 完成标准

- `renderer/effects/ssao` 目录建立，内部职责清晰。
- Sandbox 可开启/关闭 SSAO，并能直观看到墙角、柱脚、模型接触面的 AO 变化。
- Bloom / ToneMapping / Shadow / CSM debug 不被破坏。
- 调整 SSAO 参数和窗口 resize 稳定。
- 测试通过或明确记录历史已知失败项。
