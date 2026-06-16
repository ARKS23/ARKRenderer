# Phase 0.56 Bloom / ToneMapping Visual Validation Closure

## 完成状态

Phase 0.56 已完成：
- `ark_frame_validation_smoke` 已抽象出可配置 `FrameValidationCaseDesc` / `FrameValidationResult`。
- frame validation case 已支持传入 `ToneMappingSettings` 与 `PostProcessingSettings`。
- `ark_frame_validation_smoke` 在原有 material ball / specular IBL golden case 基础上，新增 `bloom-validation` Bloom off / on LDR 统计 diff。
- `ark_frame_validation_smoke` 新增 `Reinhard` / `ACES` / `Linear` tone mapping operator 的 LDR 统计 diff。
- Bloom / ToneMapping validation artifact 会写入现有 `test_artifacts/frame_validation/` 目录。
- 默认 sandbox、默认 Bloom 开关、默认 tone mapping operator 与现有 golden baseline 保持不变。

实际验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_frame_validation_smoke ark_bloom_pass_smoke ark_tone_mapping_pass_smoke ark_shader_assets_smoke ark_post_processing_settings_smoke ark_sandbox
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(frame_validation|bloom_pass|tone_mapping_pass|shader_assets|post_processing_settings)_smoke" --output-on-failure
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

结果：

```text
targeted build passed
targeted CTest passed: 5/5
git diff --check: only line-ending warnings, no whitespace errors
full build passed
full CTest passed: 28/28
sandbox hidden-window smoke passed for bloom-validation, bloom-validation --bloom, bloom-validation --bloom --tone-mapping aces, and bloom-validation --bloom --tone-mapping linear
```

## 阶段判断

Phase 0.53 已完成 Physically Based Bloom foundation，Phase 0.54 已加入 `bloom-validation` fixture 与 sandbox preset，Phase 0.55 已完成 `Reinhard` / `Linear` / `ACES` tone mapping operator。

现在 Bloom 和 ToneMapping 已经能进入实际渲染链路，但自动化验证仍偏向 smoke：
- `ark_bloom_pass_smoke` 主要验证 pass 资源、uniform、descriptor 和 draw sequence。
- `ark_tone_mapping_pass_smoke` 主要验证 operator uniform 写入。
- `ark_frame_validation_smoke` 已有 HDR / LDR readback、统计和 golden diff，但默认只覆盖 material ball / specular IBL fixture，尚未覆盖 Bloom enabled path 与 tone mapping operator 差异。

因此 Phase 0.56 的目标不是增加新渲染效果，而是把现有 Bloom / ToneMapping 输出从“能跑”推进到“可验证、可回归、可定位”。

## 目标

- 建立 `bloom-validation` 的 LDR 视觉统计验证。
- 验证 Bloom disabled / enabled 在同一 fixture 下产生可测差异。
- 验证 `Reinhard` / `Linear` / `ACES` 在同一 HDR 输入下产生可测 LDR 差异。
- 保持默认 sandbox、默认 Bloom 开关、默认 tone mapping operator 和现有 golden baseline 不变。
- 让 frame validation 输出必要 artifact，方便后续观察失败样本。
- 更新测试、README / handoff 中与验证入口相关的说明。

## 非目标

- 不重写 Bloom 算法。
- 不改变 Bloom 默认关闭策略。
- 不改变默认 tone mapping operator，默认仍为 `Reinhard`。
- 不更新现有默认 golden baseline。
- 不引入 Auto Exposure、histogram、luminance mip chain。
- 不做 color grading / LUT / vignette / film grain / sharpen。
- 不引入完整截图系统或 UI overlay。
- 不重构 `FrameRenderer` 为完整 RenderGraph。

## 现有基础

相关代码路径：

```text
src/renderer/RenderView.h
src/renderer/PostProcessingSettings.h
src/renderer/passes/BloomPass.cpp
src/renderer/passes/ToneMappingPass.cpp
src/renderer/FrameRenderer.cpp
tests/frame_validation_smoke.cpp
tests/bloom_pass_smoke.cpp
tests/tone_mapping_pass_smoke.cpp
assets/models/bloom_validation_fixture.gltf
```

当前 `tests/frame_validation_smoke.cpp` 已具备：
- hidden GLFW window。
- Vulkan backend 创建。
- fixture glTF 加载。
- procedural environment 转 cubemap。
- `SkyboxPass` + `ForwardPass` 渲染到 RGBA16F scene color。
- `ToneMappingPass` 渲染到 RGBA8 LDR texture。
- HDR / LDR texture readback。
- LDR artifact png 写出。
- golden image diff。

Phase 0.56 应优先复用这条链路，而不是另写一套离屏渲染测试框架。

## 推荐方案

### Frame Validation 配置化

先把 `frame_validation_smoke` 中的固定 fixture 渲染路径抽成可配置描述：

```cpp
struct FrameValidationCaseDesc {
    Path fixtureRelativePath;
    const char* fixtureName = "";
    const char* fixtureId = "";
    const char* sceneModelName = "";
    ToneMappingSettings toneMapping;
    PostProcessingSettings postProcessing;
    bool compareGolden = true;
    bool writeArtifact = true;
};
```

渲染函数应允许：
- 指定 fixture。
- 指定 `ToneMappingSettings`。
- 指定 `PostProcessingSettings`。
- 选择是否参与 golden diff。
- 返回 LDR bytes 和统计结果，便于 case 之间做差异比较。

建议新增结果结构：

```cpp
struct FrameValidationResult {
    std::vector<u8> hdrBytes;
    std::vector<u8> ldrBytes;
    FrameColorStats hdrStats;
    LdrFrameColorStats ldrStats;
};
```

### Bloom Enabled / Disabled 统计验证

新增 `bloom-validation` 对比 case：

```text
A: --preset bloom-validation, Bloom disabled, Reinhard
B: --preset bloom-validation, Bloom enabled, Reinhard
```

建议断言：
- 两次 frame 都能成功渲染并 readback。
- `B.meanLuminance > A.meanLuminance + epsilon`。
- `B.maxLuminance >= A.maxLuminance`。
- `imageDiff(A, B).meanAbsError` 高于最小阈值。
- `imageDiff(A, B).mismatchedPixelRatio` 高于最小阈值。

第一版阈值应保守，避免不同 GPU / driver 下误报。推荐从以下量级开始：

```text
meanAbsError > 0.002
mismatchedPixelRatio > 0.01
meanLuminanceDelta > 0.001
```

如果实际运行后差异过小，优先调整 `bloom-validation` case 的 Bloom 参数，而不是放宽到没有意义的阈值：

```cpp
postProcessing.bloom.enabled = true;
postProcessing.bloom.intensity = 0.08f;
postProcessing.bloom.scatter = 0.7f;
postProcessing.bloom.threshold = 0.8f;
postProcessing.bloom.softKnee = 0.5f;
```

### ToneMapping Operator 统计验证

新增同一 HDR scene 的 LDR 输出对比：

```text
A: bloom-validation, Bloom enabled, Reinhard
B: bloom-validation, Bloom enabled, ACES
C: bloom-validation, Bloom enabled, Linear
```

建议断言：
- `Reinhard` 与 `ACES` 的 LDR 输出存在可测 diff。
- `Linear` 与 `Reinhard` 的 LDR 输出存在可测 diff。
- 所有输出 alpha 合法，RGB 范围合法，非黑像素数量合理。
- 默认 frame validation golden 仍只覆盖默认 `Reinhard` 路径，不因本 case 更新。

注意：
- `Linear` 可能产生更多裁剪，不能用“更好看”作为判断，只验证差异与合法性。
- `ACES` 当前是 fitted approximation，不是完整 ACES color pipeline。
- ToneMapping operator diff 不建议第一版写 golden，先使用统计 diff 降低维护成本。

### Artifact 策略

建议将新 case artifact 写入现有 frame validation artifact root：

```text
build/msvc-vcpkg/test_artifacts/frame_validation/
```

建议文件名：

```text
bloom_validation_reinhard_bloom_off.png
bloom_validation_reinhard_bloom_on.png
bloom_validation_aces_bloom_on.png
bloom_validation_linear_bloom_on.png
```

默认 golden 仍只使用：

```text
tests/golden/frame_validation/material_ball_validation_fixture.png
tests/golden/frame_validation/specular_ibl_validation_fixture.png
```

Bloom / ToneMapping operator case 暂时不进入 golden baseline，避免频繁维护多张后处理基线图。

## 分阶段任务

### 0.56.0 文档与范围确认

- 新增 `docs/phase/phase56.md`。
- 明确本阶段是视觉验证闭环，不改变默认画面。
- 明确 Bloom / ToneMapping 新 case 第一版走统计 diff，不新增 golden baseline。

### 0.56.1 Frame Validation Case Foundation

修改：

```text
tests/frame_validation_smoke.cpp
```

目标：
- 将固定 fixture 渲染逻辑改为可配置 case。
- 支持传入 `ToneMappingSettings`。
- 支持传入 `PostProcessingSettings`。
- 支持选择是否 golden diff。
- 保持现有 material ball / specular IBL golden case 行为不变。

验证：
- `ark_frame_validation_smoke` 默认 golden diff 仍为 0。

### 0.56.2 Bloom Validation Statistical Diff

修改：

```text
tests/frame_validation_smoke.cpp
```

目标：
- 增加 `bloom-validation` disabled / enabled 两帧对比。
- 计算 LDR diff stats。
- 验证 Bloom enabled path 对最终 LDR 输出产生稳定可测变化。
- 写出 Bloom off / on artifact。

验证：
- `ark_frame_validation_smoke` 在无 `--update-golden` 下通过。
- 默认 golden 不更新。

### 0.56.3 ToneMapping Operator Statistical Diff

修改：

```text
tests/frame_validation_smoke.cpp
```

目标：
- 增加 Reinhard / ACES / Linear tone mapping 输出对比。
- 验证 operator 确实影响最终 LDR 输出。
- 写出不同 operator artifact。
- 保持 `ToneMappingSettings` 默认值和默认 frame validation 不变。

验证：
- `ark_tone_mapping_pass_smoke` 仍通过。
- `ark_frame_validation_smoke` 覆盖 operator 差异。

### 0.56.4 Sandbox Regression Smoke

目标：
- 继续覆盖以下 hidden-window smoke：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation --bloom
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation --bloom --tone-mapping aces
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation --bloom --tone-mapping linear
```

说明：
- 该项主要验证真实 sandbox 入口不崩溃。
- 视觉差异由 `ark_frame_validation_smoke` 的 offscreen readback 负责。

### 0.56.5 Tests

建议执行：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_frame_validation_smoke ark_bloom_pass_smoke ark_tone_mapping_pass_smoke ark_shader_assets_smoke ark_post_processing_settings_smoke ark_sandbox
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(frame_validation|bloom_pass|tone_mapping_pass|shader_assets|post_processing_settings)_smoke" --output-on-failure
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

Sandbox smoke：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation --bloom
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation --bloom --tone-mapping aces
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation --bloom --tone-mapping linear
```

### 0.56.6 验证与收尾

- 更新 `docs/phase/phase56.md` 完成状态。
- 更新 `docs/codex_handoff.md`。
- 如 README 中缺少验证入口说明，则补充简短命令。
- 确认默认 golden baseline 未被修改。
- 确认 artifact 输出路径不会被提交。
- 提交并推送。

## 完成标准

- `ark_frame_validation_smoke` 同时覆盖：
  - 现有 material ball golden case。
  - 现有 specular IBL golden case。
  - `bloom-validation` Bloom off / on 统计 diff。
  - `bloom-validation` Reinhard / ACES / Linear 统计 diff。
- 默认 golden baseline 不变。
- Bloom enabled 对 LDR 输出有稳定可测差异。
- ToneMapping operator 对 LDR 输出有稳定可测差异。
- 相关 targeted tests 通过。
- full build 和 full CTest 通过。
- sandbox hidden-window smoke 覆盖 Bloom / ToneMapping 组合。

## 风险与注意事项

- 不同 GPU / driver 的 floating point 和采样差异可能影响统计阈值；第一版阈值应保守。
- Bloom diff 过小通常说明 fixture 或 Bloom 参数不够敏感，应优先调整 validation case 的参数。
- Linear tone mapping 的输出可能有明显裁剪，这是预期调试行为，不应作为失败条件。
- 不要在本阶段引入 golden baseline 大量扩张，否则后续 shader 调整维护成本会快速上升。
- 如果 `frame_validation_smoke` 变得过慢，可以后续拆出独立 `ark_postprocess_frame_validation_smoke`，但第一版优先复用已有测试入口。

## 后续方向

Phase 0.56 完成后建议：

1. Phase 0.57：Transparent sorting，补齐 Blend bucket back-to-front 排序。
2. Phase 0.58：Directional Shadow Map Foundation。
3. Phase 0.59：`KHR_materials_emissive_strength`，让 Bloom fixture 与 glTF emissive 语义更完整。
4. Phase 0.60：Renderer Public API / Engine Integration Boundary。
