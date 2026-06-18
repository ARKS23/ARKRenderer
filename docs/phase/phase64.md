# Phase 0.64 Shadow Quality / Stability Pass

## 实施状态

已完成 0.64.1 Shadow quality contract ~ 0.64.6 验证与收尾。

Phase 0.61 已经把 `RenderScene::bounds()` 接入 `ShadowPass`，Phase 0.62/0.63 也把默认组合场景、frame validation 和公共 preset/profile 接口收口到稳定路径上。现在阴影链路已经“能用”，但仍属于基础版单张 directional shadow map：在大场景、相机移动和复杂几何下，仍可能出现闪烁、边缘抖动、bias 不够稳定或远近尺度兼顾不佳的问题。

本阶段的目标不是引入更重的阴影体系，而是先把现有阴影路径做得更稳、更清楚、更适合默认 sandbox 和 Sponza 场景观察。

## 阶段目标

- 降低相机移动时的阴影抖动和 shimmer。
- 提升 Sponza / default composite / shadow-validation 场景下的阴影可读性。
- 保持现有 scene-bounds fitting 路径为主线，不破坏 fallback 行为。
- 让 shadow quality 的接口尽量简单，避免把阴影参数散落到多个入口。
- 保持 default sandbox 和 frame validation 的职责边界不变。

## 非目标

- 不做 CSM / cascade split。
- 不重构 `RenderScene` / `Bounds` / 资源加载链路。
- 不引入运行时 UI 或 debug overlay。
- 不改变 Bloom / ToneMapping / IBL / 默认组合场景的职责。
- 不大改材质系统或 forward lighting 结构。

## 当前基础

现有阴影链路已经具备：

- `ShadowSettings`，包含 `enabled`、`strength`、`bias`、`mapExtent`、`orthographicHalfExtent`、`nearPlane`、`farPlane`、`lightDistance`、`fitSceneBounds`。
- `ShadowPass` 的 scene-bounds fitting 路径。
- `mesh.frag.hlsl` 中的 shadow map / sampler 绑定与 direct lighting 阴影采样。
- 默认 sandbox、`shadow-validation` preset、`ark_shadow_pass_smoke` 和 `ark_frame_validation_smoke` 覆盖。

也就是说，这一阶段是“把已有单张 shadow map 做稳”的阶段，不是重新发明阴影系统。

## 推荐方案

### 0.64.1 Shadow quality contract

先收口阴影质量接口，判断是否需要新增非常少量的控制字段。

建议优先级：

- 先保留 `fitSceneBounds` 作为主路径。
- 如果需要更好的过滤，优先增加一个很小的 PCF 控制面，而不是引入复杂 bias 组合。
- 如果需要稳定性，优先增加 texel snapping，而不是先上 cascade。

如果新增字段，建议仍然挂在 `ShadowSettings`，保持 renderer-facing 的单一 view 入口。

本阶段确定的 contract：

```cpp
enum class ShadowFilterMode : u32 {
    Hard = 0,
    Pcf3x3 = 1,
    Pcf5x5 = 2,
};

struct ShadowSettings {
    bool stabilizeProjection = true;
    ShadowFilterMode filterMode = ShadowFilterMode::Hard;
    float filterRadiusTexels = 1.0f;
};
```

说明：

- `stabilizeProjection` 预留给 0.64.2 texel snapping。
- `filterMode` / `filterRadiusTexels` 预留给 0.64.3 PCF filtering。
- `FrameContext` 和 `ForwardPass` 已把 filter mode / radius 贯通到 `LightingUniform.shadow.zw`。
- `mesh.frag.hlsl` 已按 contract 接入 Hard / PCF 3x3 / PCF 5x5 三档采样路径。
- sandbox 新增 `--shadow-filter` 和 `--shadow-filter-radius`，仍遵循 Phase 0.63 的 override mask 机制。

### 0.64.2 ShadowPass texel snapping

在 `fitSceneBounds` 路径中，对 light-space center 做 texel 对齐：

- 用 shadow map 分辨率和当前 light-space 投影范围推导 texel size。
- 将 light-space 原点或中心点 snap 到 texel grid。
- 保持 fallback fixed-box 路径不变。

目标是减少相机微动导致的阴影边界漂移。

当前实现：

- 仅作用于 `ShadowSettings::fitSceneBounds == true` 的 scene-bounds fitting 路径。
- 仅在 `ShadowSettings::stabilizeProjection == true` 时启用。
- 使用 world origin 作为稳定参考点，把参考点在最终 shadow texel 空间对齐到 texel grid。
- 对 `left/right` 和 Vulkan Y 翻转后的 `bottom/top` 分别做稳定化平移。
- 不改变 shadow box 尺寸，不影响 fixed manual `--shadow-bounds` fallback 路径。
- `ark_shadow_pass_smoke` 覆盖：
  - 稳定化后参考点落在 texel grid 上。
  - 关闭 `stabilizeProjection` 时不强制对齐。
  - scene bounds center 仍保持在半个 shadow texel 的 NDC 范围内。

### 0.64.3 PCF shadow filtering

在 `mesh.frag.hlsl` 中把现有 shadow compare 扩展为最小 PCF：

- 保留 `Hard` 单次 compare 路径，作为最低成本和调试基线。
- `Pcf3x3` 使用半径 1 的固定 grid kernel。
- `Pcf5x5` 使用半径 2 的固定 grid kernel。
- `filterRadiusTexels` 控制 kernel sample offset 的 texel 半径缩放。
- shader 通过 `g_ShadowMap.GetDimensions()` 推导 texel size，避免 CPU 侧额外传递 shadow map 尺寸。
- PCF 仅作用于 direct lighting 阴影，不影响 IBL、ambient、emissive。
- shadow UV 边界外的 PCF tap 按 lit 处理，避免靠近 shadow map 边缘时产生额外黑边。

优先目标是“更稳、更柔和”，不是追求复杂软阴影。

### 0.64.4 Preset / sandbox 默认值

把 shadow quality 默认值统一放回 preset/profile：

- `RendererPreset` 继续负责 default / shadow-validation 的阴影参数。
- sandbox CLI 只处理 override，不复制默认逻辑。
- 若新增 shadow filter 参数，默认值应在 preset 中明确给出。

### 0.64.5 Tests

需要覆盖：

- `ark_shadow_pass_smoke`
- `ark_renderer_preset_smoke`
- `ark_framework_headers_smoke`
- `ark_shader_assets_smoke`
- `ark_frame_validation_smoke`
- `ark_forward_pass_pipeline_smoke`

如果 PCF 或 texel snapping 导致可见变化：

- 先判断是预期改进还是 bug。
- 若是预期改进，再同步更新 default composite golden。

当前 targeted 验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_shader_assets_smoke ark_forward_pass_pipeline_smoke ark_shadow_pass_smoke ark_renderer_preset_smoke ark_framework_headers_smoke ark_frame_validation_smoke ark_sandbox
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(shader_assets|forward_pass_pipeline|shadow_pass|renderer_preset|framework_headers|frame_validation)_smoke" --output-on-failure
```

结果：

```text
targeted build passed
targeted CTest passed: 6/6
ark_frame_validation_smoke passed without golden update
```

### 0.64.6 验证与收尾

完成内容：

- `docs/phase/phase64.md` 已同步到完成态。
- `docs/codex_handoff.md` 已记录 Phase 0.64 最新交接摘要。
- targeted build / CTest 已通过。
- full Debug build / full CTest 已通过。
- sandbox hidden-window smoke 已覆盖 default / sponza / shadow-validation / sponza-pcf5。

最终验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_shader_assets_smoke ark_forward_pass_pipeline_smoke ark_shadow_pass_smoke ark_renderer_preset_smoke ark_framework_headers_smoke ark_frame_validation_smoke ark_sandbox
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(shader_assets|forward_pass_pipeline|shadow_pass|renderer_preset|framework_headers|frame_validation)_smoke" --output-on-failure
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
ark_sandbox hidden-window smoke for default, sponza, shadow-validation, and sponza pcf5x5 override
```

结果：

```text
targeted build passed
targeted CTest passed: 6/6
ark_frame_validation_smoke passed without golden update
full Debug build passed
full CTest passed: 30/30
sandbox hidden-window smoke passed for default, sponza, shadow-validation, and sponza-pcf5
```

## 完成标准

- default sandbox 阴影更稳，Sponza 大场景更少抖动。
- `shadow-validation` 更适合看阴影边界和 bias。
- 阴影接口仍然简洁，职责不扩散到 app / scene resource。
- targeted build / CTest 通过。
- full build / full CTest 通过。
- sandbox default / sponza / shadow-validation hidden-window smoke 通过。

## 后续方向

如果 0.64 完成后阴影质量仍不足，再进入 CSM 会更合理。那时可以把 split、atlas、cascade fit、debug visualization 一起规划，而不是继续在单张 shadow map 上堆参数。
