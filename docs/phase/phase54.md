# Phase 0.54 Bloom Visual Validation Fixture

## 完成情况

Phase 0.54 已完成：
- 新增 `assets/models/bloom_validation_fixture.gltf`，包含暗背景、暖色/冷色高 emissive 几何和验证相机。
- 新增 `RendererScenePreset::BloomValidation`，支持 `bloom-validation`、`bloom`、`emissive-bloom` 等别名。
- sandbox 支持 `--preset bloom-validation` 与 `--preset bloom-validation --bloom`。
- 新增 `ark_bloom_validation_fixture_smoke`，覆盖 fixture 加载、emissive material、preset 解析和 sandbox `ApplicationDesc`。
- 更新 `ark_renderer_preset_smoke`、`ark_framework_headers_smoke` 与 README 示例。
- 默认 sandbox、默认 Bloom 开关和现有 frame validation 基线保持不变。

## 实际验证

已执行并通过：
```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_renderer_preset_smoke ark_framework_headers_smoke ark_bloom_validation_fixture_smoke ark_frame_validation_smoke ark_sandbox
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(renderer_preset|framework_headers|bloom_validation_fixture)_smoke" --output-on-failure
```

收尾阶段继续执行 full build、full CTest、`git diff --check` 和 sandbox hidden-window smoke。

## 阶段判断

Phase 0.53 已经完成 Physically Based Bloom 的基础闭环：

- `PostProcessingSettings` / `BloomSettings` 已存在。
- `RenderView`、`ApplicationDesc` 和 sandbox CLI 已能传递 Bloom 配置。
- `BloomPass` 已接入 `FrameRenderer`，位于 HDR scene color 之后、`ToneMappingPass` 之前。
- Bloom 默认关闭，默认 golden baseline 不变。
- `ark_bloom_pass_smoke` 已覆盖 fake RHI 下的 pass、descriptor、uniform 和 draw sequence。

当前缺口不是 Bloom 管线是否存在，而是 **没有一个稳定、明显、可回归的视觉验证场景**。

现有 `material-ball` 和 `specular-validation` 可以启动 Bloom，但不一定稳定地产生强烈辉光。下一阶段应先补齐 Bloom validation fixture 和 preset，让开发者可以一眼确认 Bloom 是否生效，也让自动化测试可以验证“启用 Bloom 确实改变画面，默认路径仍不改变”。

## 目标

- 新增一个专门用于 Bloom 验证的 glTF fixture。
- fixture 使用高亮 emissive 材质或高强度 HDR 亮部，让 Bloom 在 `--bloom` 下清晰可见。
- 新增 `RendererScenePreset::BloomValidation`，sandbox 支持：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation --bloom
```

- 保持默认 sandbox、默认 preset 和默认 golden 不变。
- 建立 Bloom enabled path 的最小统计验证，证明 Bloom 对最终 LDR 输出产生可测变化。
- 文档和 handoff 明确：本阶段是可视化验证闭环，不是 Bloom 算法重写。

## 非目标

- 不重写 Phase 0.53 的 BloomPass。
- 不启用默认 Bloom。
- 不更新默认 golden baseline。
- 不做 Auto Exposure。
- 不引入 ACES / filmic tone mapping。
- 不做 lens dirt、ghost、anamorphic streak、star filter。
- 不做完整截图系统重构。
- 不把 emissive 扩展到 KHR_materials_emissive_strength；第一版先用 glTF core `emissiveFactor`。

## 现有基础

当前项目已经具备完成本阶段所需的大部分链路：

- `GltfLoader` 已解析 `emissiveFactor` 和 `emissiveTexture`。
- `MaterialResource` 已保存 emissive factor / texture / texcoord / transform。
- `ForwardPass` 已上传 emissive uniform 和 emissive texture binding。
- `mesh.frag.hlsl` 已将 emissive 加到 linear HDR lighting 输出：

```text
return (indirect + direct) * inputs.occlusion + inputs.emissive;
```

这意味着 Bloom validation fixture 可以先用高 `emissiveFactor` 的简单几何体实现，不必先新增纹理或 shader 特性。

## 推荐 Fixture 设计

新增：

```text
assets/models/bloom_validation_fixture.gltf
```

建议场景结构：

- 深色背景板或暗色地面，便于观察辉光轮廓。
- 一个或多个高 emissive 几何体：
  - 细长灯条。
  - 小面积白色/蓝色/橙色发光矩形。
  - 中央高亮核心，周围低亮度材质做对比。
- 一个普通 PBR 球或粗糙金属球，验证 Bloom 不应吞掉正常材质细节。
- 相机或默认构图应让高亮对象位于画面中央或略偏上。

建议材质：

```text
dark material:
  baseColorFactor: [0.02, 0.02, 0.025, 1.0]
  metallicFactor: 0.0
  roughnessFactor: 0.8

emissive strip:
  baseColorFactor: [1.0, 1.0, 1.0, 1.0]
  emissiveFactor: [8.0, 6.0, 3.0]
  metallicFactor: 0.0
  roughnessFactor: 0.4

cool emissive accent:
  emissiveFactor: [1.0, 4.0, 10.0]
```

注意：

- glTF core `emissiveFactor` 在规范中通常是 0..1，但 tinygltf / 当前 loader 可承载 float 值。第一版可用高于 1 的数值作为项目内部验证 fixture；文档中必须说明这是本地测试资源策略。
- 如果后续希望完全符合 glTF emissive 强度规范，应再单独支持 `KHR_materials_emissive_strength`。
- fixture 应尽量使用内联简单 mesh 和材质，减少外部贴图依赖。

## RendererPreset 接入

扩展：

```text
src/renderer/RendererPreset.h
src/renderer/RendererPreset.cpp
```

新增：

```cpp
enum class RendererScenePreset {
    Default,
    MaterialBall,
    SpecularValidation,
    DebugOrientation,
    BloomValidation,
};
```

解析别名建议：

```text
bloom-validation
bloom
emissive-bloom
```

`resolveRendererPreset()` 应将该 preset 解析到：

```text
assets/models/bloom_validation_fixture.gltf
sceneName: BloomValidationScene
modelName: BloomValidationModel
environmentName: BloomValidationEnvironment
```

环境策略：

- 第一版继续使用默认 HDR / procedural fallback。
- 不强行新增 HDR 环境资源。
- 让 Bloom 主要由 emissive fixture 触发，而不是依赖环境高光。

## Sandbox Path

目标命令：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation --bloom
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation --bloom --bloom-intensity 0.12
```

行为要求：

- 不带 `--bloom` 时，fixture 应显示为普通 HDR/tone-mapped emissive 亮部，但没有 Bloom pass 扩散。
- 带 `--bloom` 时，亮部周围应出现明显辉光。
- `--bloom-intensity`、`--bloom-scatter`、`--bloom-threshold` 仍沿用 Phase 0.53 CLI。
- 默认 `ark_sandbox.exe` 不变。

## 自动化验证方向

### 统计验证优先

本阶段不必立即新增严格 golden baseline。推荐先建立统计 smoke：

```text
tests/bloom_validation_fixture_smoke.cpp
```

或扩展：

```text
tests/frame_validation_smoke.cpp
```

建议验证：

- fixture 能被 `GltfLoader` 加载。
- fixture 至少存在一个 emissiveFactor > 1 的 material。
- `RendererPreset` 能解析 `bloom-validation`。
- sandbox `makeSandboxApplicationDesc()` 能解析 `--preset bloom-validation --bloom`。
- Bloom enabled frame 与 Bloom disabled frame 的 LDR readback 统计存在可测差异。

### LDR 统计建议

如果复用 offscreen frame validation，可比较同一 fixture 在以下两组配置下的 LDR 输出：

```text
A: bloom-validation, Bloom disabled
B: bloom-validation, Bloom enabled
```

建议统计：

- `meanLuminance(B) > meanLuminance(A)`。
- `brightPixelCount(B) >= brightPixelCount(A)`。
- 亮部附近的非高亮区域 luminance 提升，证明不是只有 emissive core 自身变亮。
- 默认 frame validation golden diff 仍为 0。

如果当前 frame validation 不方便传 post-processing settings，可先只做 fixture / preset / CLI / sandbox hidden-window smoke，把 LDR 对比留到下一小步，但文档必须记录该缺口。

## 实施拆分

### 0.54.0 文档与范围确认

- 明确本阶段目标是 Bloom 可视化验证闭环。
- 明确不改 Bloom 算法、不改默认画面、不改默认 golden。
- 明确优先用 emissive fixture 触发 Bloom。

### 0.54.1 Bloom Validation Fixture

- 新增 `assets/models/bloom_validation_fixture.gltf`。
- fixture 包含高 emissive 材质和暗背景。
- 确认 `GltfLoader` / `ModelResource` 能加载。
- 如发现 emissive factor 被 clamp 或 shader 没产生 HDR 亮度，再最小修正材质链路。

### 0.54.2 RendererPreset 接入

- 新增 `RendererScenePreset::BloomValidation`。
- `parseRendererScenePreset()` 支持 `bloom-validation`、`bloom`、`emissive-bloom`。
- `resolveRendererPreset()` 指向 bloom fixture。
- 更新 `ark_renderer_preset_smoke`。

### 0.54.3 Sandbox Bloom Validation Path

- 确认 `--preset bloom-validation --bloom` 能启用 Bloom。
- 更新 README 的 sandbox 示例。
- 做 hidden-window smoke：

```powershell
ark_sandbox hidden-window smoke with --preset bloom-validation
ark_sandbox hidden-window smoke with --preset bloom-validation --bloom
```

### 0.54.4 Tests

建议新增或扩展：

```text
tests/bloom_validation_fixture_smoke.cpp
tests/renderer_preset_smoke.cpp
tests/framework_headers_smoke.cpp
tests/frame_validation_smoke.cpp
```

覆盖：

- glTF fixture 文件存在且可加载。
- emissive material 存在，且至少一个通道 > 1。
- RendererPreset 解析 bloom validation。
- SandboxLaunchOptions 将 `--preset bloom-validation --bloom` 转成正确 `ApplicationDesc`。
- 默认 frame validation golden 仍不变。
- 如果本阶段实现 LDR 对比，Bloom enabled 输出应与 disabled 输出存在统计差异。

### 0.54.5 验证与收尾

建议执行：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_renderer_preset_smoke ark_framework_headers_smoke ark_bloom_validation_fixture_smoke ark_frame_validation_smoke ark_sandbox
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(renderer_preset|framework_headers|bloom_validation_fixture|frame_validation)_smoke" --output-on-failure
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

Sandbox smoke：

```powershell
ark_sandbox hidden-window smoke with --preset bloom-validation
ark_sandbox hidden-window smoke with --preset bloom-validation --bloom
ark_sandbox hidden-window smoke with --preset bloom-validation --bloom --bloom-intensity 0.12
```

文档同步：

- 更新 `docs/phase/phase54.md` 完成状态。
- 更新 `docs/codex_handoff.md`。
- README 保留中文，并补充 bloom validation 示例。

## 完成标准

- 存在稳定的 Bloom validation fixture。
- sandbox 支持 `--preset bloom-validation --bloom`。
- 不带 `--bloom` 的默认路径仍不执行 Bloom。
- 默认 frame validation golden diff 仍为 0。
- fixture / preset / CLI / framework smoke 覆盖新增路径。
- 如实现 LDR 统计验证，Bloom enabled 与 disabled 输出存在明确差异。
- full build 与 CTest 通过。
- handoff 记录本阶段结果和剩余风险。

## 风险与注意事项

- 高 emissiveFactor 是为了验证 Bloom 的本地测试资源策略，不等同于完整 glTF emissive strength 支持。
- 如果 emissive 太强，tone mapping 后可能大片过曝；应控制面积和强度，让 glow 可见但画面仍可读。
- Bloom enabled 的统计验证要避免只比较中心发光物体自身，应尽量观察周边区域亮度提升。
- 不要为了让 Bloom 明显而提高默认 Bloom intensity。
- 不要更新默认 golden baseline；如新增 bloom-specific golden，应单独命名并明确 opt-in。
- 如果当前 frame validation API 难以传入 post-processing settings，不要硬改过大范围；先完成 fixture / preset / sandbox path。

## 后续方向

Phase 0.54 完成后，下一步建议：

1. Tone mapping operator presets：Linear / Reinhard / ACES，为 Bloom 后的高亮压缩提供更合理视觉结果。
2. Auto Exposure prelude：建立 luminance mip 或 histogram 资源，为动态曝光做准备。
3. Bloom golden validation：在 bloom validation fixture 稳定后，再考虑 bloom-specific golden baseline。
4. KHR_materials_emissive_strength：如果希望更规范地表达高强度 emissive，再补 glTF extension 支持。
5. Transparent sorting：继续深化 Blend bucket 的 back-to-front 排序。
