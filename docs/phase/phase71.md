# Phase 0.71 Shadow Debug Visualization

## 实施状态

待开始。

Phase 0.67 已经接入 CSM 基础路径，Phase 0.68 完成 renderer effects 目录整理，Phase 0.69 明确 public/internal API 边界，Phase 0.70 计划补齐 sandbox first-person camera。完成相机漫游后，sandbox 才能更方便地进入 Sponza 局部区域观察阴影问题。

本阶段建议聚焦 **Shadow Debug Visualization**。目标是让阴影问题可以被快速定位：到底是 cascade split、light-space ortho fit、shadow map 内容、shader cascade selection、bias / PCF，还是场景尺度导致的问题。

## 阶段目标

- 增加 Shadow Debug Settings contract，默认关闭，不影响正常渲染路径。
- 在 sandbox UI 中提供阴影调试入口，便于实时切换和观察。
- 增加 CSM cascade color overlay，用不同颜色显示当前像素命中的 cascade。
- 增加 shadow diagnostics 面板，展示 cascade count、split distance、cascade extent、filter mode、draw count / visible count 等关键信息。
- 评估并接入 shadow map preview 的最小路径；若 ImGui texture preview 后端能力不足，本阶段先完成数据和 UI contract，并把实际图像预览拆成后续任务。
- 保持 shadow debug 代码不污染 `Renderer` / `RenderScene` 的主职责。
- 保持默认 sandbox 画面不变，debug mode 必须显式打开。

## 非目标

- 不重写 ShadowPass / CSM 架构。
- 不调整 CSM split 算法、PCF 算法或 bias 策略。
- 不实现 light frustum 3D gizmo。
- 不实现完整 RenderDoc 风格 GPU debugger。
- 不引入新第三方库。
- 不做第二轮大规模目录重排。
- 不改变默认 frame validation golden。

## 目录策略

Phase 0.71 不建议继续大规模整理目录。Phase 0.68 / 0.69 已经把 effects 和 public API 边界稳定下来，本阶段只做由 Shadow Debug 功能驱动的小范围新增。

推荐策略：

```text
src/renderer/
  ShadowDebugSettings.h              # Public data contract：RenderView 可暴露的调试开关。

src/renderer/effects/shadow/
  ShadowDebugOverlay.h/.cpp          # Internal：shadow debug mode 数据整理 / shader contract 辅助。
  ShadowDebugPreview.h/.cpp          # Internal：shadow map preview 的最小封装，若本阶段需要。
```

如果第一版只需要 shader cascade color overlay，可以暂时不新增 `.cpp`，先保留 data contract 和 shader constant path。不要把 debug 逻辑塞进 `Renderer`。

## Shadow Debug Settings Contract

建议新增轻量数据结构：

```cpp
enum class ShadowDebugMode
{
    None,
    CascadeColor,
    ShadowFactor,
    LightDepth,
};

struct ShadowDebugSettings
{
    bool enabled{false};
    ShadowDebugMode mode{ShadowDebugMode::None};
    bool showPreview{false};
    u32 previewCascadeIndex{0};
};
```

contract 原则：

- 属于 renderer public data contract，可以放在 `src/renderer/`。
- 只描述用户想看的 debug mode，不暴露 pass 内部资源。
- 由 `RenderView` 或 sandbox runtime settings 传入 frame。
- 默认关闭。
- preview cascade index 必须在 renderer 内部 clamp。

## Cascade Color Overlay

第一版最值得做的是 cascade color overlay：

```text
cascade 0 -> red
cascade 1 -> green
cascade 2 -> blue
cascade 3 -> yellow
```

shader 中基于当前像素选中的 cascade index 混合调试颜色：

```text
finalColor = lerp(litColor, cascadeColor, debugAlpha)
```

这可以快速判断：

- split 是否符合预期。
- 远近 cascade 是否覆盖正确区域。
- camera near/far 或 split lambda 是否导致 cascade 分布不合理。
- Sponza 中是否出现明显 cascade seam。

其中 `debugAlpha` 可以先固定为 `0.35`，后续再暴露给 UI。

## Shadow Diagnostics UI

sandbox UI 建议增加一个 Shadow Debug 区域：

```text
Shadow
  [x] Enable Shadow
  Strength
  Bias
  Filter Mode
  Map Extent

Shadow Debug
  [ ] Enable Debug
  Mode: None / Cascade Color / Shadow Factor / Light Depth
  [ ] Show Shadow Map Preview
  Preview Cascade: 0..N-1

CSM Diagnostics
  Cascade Count
  Split Distances
  Cascade Extents
  Map Extent
  Visible Draws / Total Draws
```

diagnostics 信息尽量读已有状态，不要为了 UI 反向修改 pass 内部职责。

## Shadow Map Preview 最小路径

shadow map preview 有两种路径：

### 路径 A：先做 metadata preview

只在 UI 中显示：

- shadow map extent。
- cascade count。
- selected cascade index。
- layer / view 描述。
- depth format。
- 当前是否可预览。

优点：

- 风险最低。
- 不需要 ImGui texture descriptor 支持。
- 仍能把 UI contract 和数据流打通。

### 路径 B：接入 ImGui texture preview

如果当前 ImGui backend 已经支持 sampled image descriptor，可以把 shadow map depth view 转成可采样 preview。

注意点：

- depth image layout 要正确。
- preview 不应该破坏 shadow pass layout transition。
- depth compare sampler 和普通 sampling sampler 可能要分开。
- Vulkan descriptor lifetime 必须跟 frame / resource lifetime 对齐。
- depth value 需要可视化映射，不能直接当颜色显示。

建议本阶段优先做路径 A，并在代码结构上为路径 B 留口子。

## 分阶段任务

### 0.71.0 文档与范围确认

- 确认本阶段只做 shadow debug visualization，不改默认渲染行为。
- 确认不做大规模目录重排。
- 确认 `ShadowDebugSettings` 属于 public data contract，具体实现留在 `effects/shadow/`。

### 0.71.1 Shadow Debug Settings Contract

- 新增 `ShadowDebugSettings.h`。
- 在 `RenderView` 中增加 getter / setter。
- 补充 sanitize 逻辑：
  - disabled 时 mode 回到 `None`。
  - preview cascade index clamp 到有效范围。
- 增加 public headers smoke 覆盖。

### 0.71.2 Frame / Shader Constant Path

- 将 debug mode 传入 forward shader constants。
- 保持默认 mode 为 `None`。
- 确认 descriptor / uniform 更新不影响普通渲染。

### 0.71.3 Cascade Color Overlay

- 在 `mesh.frag.hlsl` 中增加 debug mode 分支。
- 实现 cascade color overlay。
- 默认关闭，不改变 frame validation golden。
- 增加 shader asset smoke，确认 shader 中存在 debug mode / cascade color contract。

### 0.71.4 Sandbox Shadow Debug UI

- 在 sandbox UI 中增加 Shadow Debug 区域。
- 支持开关、mode 选择、preview cascade index。
- 显示 CSM split / extent / filter / cascade count 等 diagnostics。
- 不要求第一版完成图像化 depth preview。

### 0.71.5 Shadow Map Preview Assessment

- 检查当前 ImGui backend 是否适合显示 Vulkan image view。
- 如果条件成熟，接入只读 preview。
- 如果条件不足，记录限制，并保留 metadata preview。

### 0.71.6 Tests

- Debug build 通过。
- phase relevant tests 通过。
- `renderer_public_headers_smoke` 覆盖新增 public header。
- shader smoke 覆盖 debug mode contract。
- sandbox hidden smoke 通过。

### 0.71.7 验证与收尾

- 文档同步实际实现结果。
- 记录 shadow map preview 是否完成图像化显示。
- 确认默认 sandbox 画面不变。
- 提交和推送。

## 风险与应对

### 风险一：debug 分支污染 shader 主路径

应对：

- debug mode 默认 `None`。
- 分支尽量放在 shading 输出末端。
- 只在 debug mode 开启时覆盖或混合颜色。

### 风险二：shadow map preview 破坏资源状态

应对：

- 第一版优先 metadata preview。
- 图像 preview 只在 backend 能安全管理 descriptor / layout 时接入。
- 不为了 preview 改乱 shadow pass layout transition。

### 风险三：public API 暴露过多内部细节

应对：

- public 只放 `ShadowDebugSettings`。
- cascade matrices、shadow texture view、pass resource handle 不向上暴露。
- diagnostics 可以走 renderer snapshot / UI adapter，不让引擎直接依赖 pass 对象。

## 完成标准

- sandbox 可以显式开启 cascade color overlay。
- 默认 sandbox 画面不变。
- UI 能显示当前 CSM / shadow diagnostics。
- public settings / internal implementation 边界清晰。
- shadow debug 相关测试通过。
- 文档记录 shadow map preview 的实际接入状态。

## 后续方向

完成 Shadow Debug Visualization 后，建议继续：

1. Phase 0.72：SSAO effect foundation，落到 `renderer/effects/ssao/`。
2. Phase 0.73：GPU instanced rendering foundation，减少重复 mesh 的 draw call 压力。
3. Phase 0.74：Renderer debug visualization framework，把 shadow / SSAO / material / IBL debug view 统一收口。
