# Phase 0.55 Tone Mapping Operator Presets

## 完成状态

Phase 0.55 已完成：
- 新增 `ToneMappingOperator`，支持 `Reinhard`、`Linear`、`ACES`。
- `ToneMappingSettings` 新增 `operatorType`，默认仍为 `Reinhard`。
- `ApplicationDesc` 和 sandbox CLI 新增 tone mapping operator 入口。
- `ToneMappingPass` uniform 写入 operator id，`tonemap.frag.hlsl` 支持 Linear / Reinhard / ACES fitted approximation。
- README、shader source smoke、framework headers smoke、post-processing CLI smoke、renderer preset smoke 和 tone mapping pass smoke 已同步。
- 默认 frame validation golden 保持不变。

实际验证：
```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_tone_mapping_pass_smoke ark_framework_headers_smoke ark_shader_assets_smoke ark_post_processing_settings_smoke ark_renderer_preset_smoke ark_sandbox
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(tone_mapping_pass|framework_headers|shader_assets|post_processing_settings|renderer_preset|frame_validation)_smoke" --output-on-failure
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

Sandbox hidden-window smoke 已覆盖 default、`--tone-mapping aces`、`--tone-mapping=linear`、`--preset bloom-validation --bloom --tone-mapping aces`。

## 阶段判断

Phase 0.53 已完成默认关闭的 Physically Based Bloom foundation，Phase 0.54 已补齐 `bloom-validation` 视觉验证 fixture 与 sandbox preset。现在高亮 emissive、Bloom 和 HDR scene color 已经能进入后处理链路，但最终输出仍只有现有的 exposure + Reinhard + output gamma 路径。

这意味着下一步最适合推进 **Tone Mapping Operator Presets**：
- 它直接改善 Bloom / emissive 高亮压缩的观感。
- 它复用现有 `ToneMappingPass`、`RenderView::ToneMappingSettings`、shader uniform 和 frame validation 基础。
- 它可以保持默认 operator 为当前 Reinhard，默认 sandbox 与 golden baseline 不变。
- 它为后续 Auto Exposure、color grading、Bloom golden validation 提供更稳定的输出策略。

## 目标

- 新增 tone mapping operator 枚举，第一版支持：
  - `Reinhard`：默认值，保持当前行为。
  - `Linear`：只做 exposure 后的线性输出裁剪，便于调试。
  - `ACES`：ACES fitted / approximation，用于更自然地压缩 Bloom 和 emissive 高亮。
- 扩展 `ToneMappingSettings`，让 `RenderView` 可以携带 operator。
- 扩展 `ToneMappingPass` uniform，shader 按 operator 选择 tone mapping 函数。
- sandbox 增加 tone mapping CLI 入口，便于快速观察：
```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation --bloom --tone-mapping aces
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset material-ball --tone-mapping linear
```

- 默认 `ark_sandbox.exe`、默认 preset、默认 frame validation golden 不变。
- 更新 smoke tests 和文档。

## 非目标

- 不做 Auto Exposure。
- 不引入 luminance histogram / mip readback。
- 不改变默认 exposure。
- 不启用默认 Bloom。
- 不更新默认 golden baseline。
- 不做 color grading、LUT、vignette、sharpen、film grain。
- 不重构 `FrameRenderer` 或引入 RenderGraph。

## 现有基础

当前相关链路：
- `src/renderer/RenderView.h`
  - `ToneMappingSettings` 当前包含 `exposure` 和 `outputGamma`。
  - `RenderView::setToneMappingSettings()` 已存在。
- `src/renderer/passes/ToneMappingPass.cpp`
  - 每 frame slot 上传 `ToneMappingUniform`。
  - 当前 uniform 为 16 bytes：`exposure`、`inverseOutputGamma`、`padding0`、`padding1`。
- `shaders/tonemap.frag.hlsl`
  - 当前固定执行：
```text
color *= exposure
color = color / (color + 1.0)
linearToOutput(color)
```

因此第一版可以复用 `padding0` 作为 operator id，保持 uniform 大小仍为 16 bytes，避免 descriptor 和 buffer shape 大改。

## 推荐 API

### RenderView / ToneMappingSettings

建议扩展：
```cpp
enum class ToneMappingOperator : u32 {
    Reinhard = 0,
    Linear = 1,
    ACES = 2,
};

struct ToneMappingSettings {
    float exposure = 1.0f;
    float outputGamma = 2.2f;
    ToneMappingOperator operatorType = ToneMappingOperator::Reinhard;
};
```

建议新增：
```cpp
ToneMappingOperator parseToneMappingOperator(
    std::string_view name,
    ToneMappingOperator fallback = ToneMappingOperator::Reinhard);
```

可选：如果不想让 `RenderView.h` 承担 string parsing，可以把 parser 放到 `SandboxLaunchOptions.cpp` 的匿名 namespace。考虑后续文档和测试复用，建议 renderer 层提供公共 parser。

### ToneMappingPass Uniform

建议保持 16 bytes：
```cpp
struct alignas(16) ToneMappingUniform {
    float exposure;
    float inverseOutputGamma;
    float operatorType;
    float padding0;
};
```

shader 侧：
```hlsl
struct ToneMappingUniform {
    float exposure;
    float inverseOutputGamma;
    float operatorType;
    float padding0;
};
```

operator 用 float 传入，shader 中转成 int 或通过阈值判断。第一版不需要 bit-level 精确 enum packing。

## Shader 设计

### Linear

用于调试 HDR/tone-mapping 前后关系：
```hlsl
float3 toneMapLinear(float3 color) {
    return color;
}
```

最终仍会走 `saturate` 和 gamma 输出。

### Reinhard

保持当前默认行为：
```hlsl
float3 toneMapReinhard(float3 color) {
    return color / (color + 1.0f);
}
```

### ACES

建议采用常见 fitted approximation，简单、稳定、适合第一版：
```hlsl
float3 toneMapACES(float3 color) {
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}
```

注意：
- 这不是完整 ACES color pipeline，也不是 ACEScg / ODT。
- 文档和代码命名可以用 `ACES`，但注释应明确为 fitted approximation。
- 默认仍为 Reinhard，避免现有 frame golden 变化。

## Sandbox CLI

扩展：
```text
--tone-mapping reinhard
--tone-mapping linear
--tone-mapping aces
--tone-mapping=reinhard
--tone-mapping=linear
--tone-mapping=aces
```

建议别名：
```text
reinhard
linear
aces
aces-fitted
filmic
```

`filmic` 第一版可解析到 ACES，方便用户操作；文档说明它当前是 ACES fitted approximation。

缺失值行为：
- `--tone-mapping` 后没有值时记录 `missingToneMappingValue = true`。
- 保持 fallback 为默认 `Reinhard`。

无效值行为：
- 不报错退出。
- 保持现有 operator。
- smoke test 覆盖 fallback。

## 测试计划

### 0.55.0 文档与范围确认

- 新增 `docs/phase/phase55.md`。
- 明确默认 operator 不变，不更新 golden baseline。
- 明确 ACES 是 approximation，不是完整 ACES 管线。

### 0.55.1 ToneMappingSettings API

修改：
```text
src/renderer/RenderView.h
```

目标：
- 新增 `ToneMappingOperator`。
- `ToneMappingSettings` 新增 `operatorType`。
- 默认值为 `Reinhard`。

测试：
- `ark_framework_headers_smoke` 覆盖 operator enum 和 settings 赋值。

### 0.55.2 Sandbox Tone Mapping CLI

修改：
```text
src/app/SandboxLaunchOptions.h
src/app/SandboxLaunchOptions.cpp
```

目标：
- `SandboxLaunchOptions` 保存 `ToneMappingSettings` 或 tone-mapping override。
- `ApplicationDesc` / `makeSandboxApplicationDesc()` 能把 operator 传到 `Application`。
- 支持 `--tone-mapping value` 与 `--tone-mapping=value`。

注意：
- 如果 `ApplicationDesc` 当前只有 `postProcessing`，需要确认 tone mapping settings 是否已有入口。
- 如没有，可新增 `ToneMappingSettings toneMapping` 到 `ApplicationDesc`，并在 `Application::run()` 写入 `RenderView`。

测试：
- `ark_renderer_preset_smoke` 或新增 `ark_sandbox_launch_options_smoke` 覆盖 parse。
- `ark_framework_headers_smoke` 覆盖 public header。

### 0.55.3 ToneMappingPass / Shader 接入

修改：
```text
src/renderer/passes/ToneMappingPass.cpp
shaders/tonemap.frag.hlsl
```

目标：
- uniform 写入 operator id。
- shader 分支选择 Linear / Reinhard / ACES。
- 默认 shader 输出保持 Reinhard。

测试：
- 扩展 `ark_tone_mapping_pass_smoke`：
  - 捕获 `operatorType`。
  - 验证默认为 0。
  - 验证 ACES / Linear 能上传正确 id。
- 扩展 `ark_shader_assets_smoke`：
  - 搜索 `toneMapACES`、`toneMapReinhard`、`operatorType`。

### 0.55.4 Default Frame Validation

目标：
- 默认 `ark_frame_validation_smoke` golden diff 仍为 0。
- 不新增/更新默认 baseline。

可选：
- 用 `bloom-validation` 手动 sandbox smoke 对比：
```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation --bloom --tone-mapping reinhard
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation --bloom --tone-mapping aces
```

### 0.55.5 Tests

建议执行：
```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_tone_mapping_pass_smoke ark_framework_headers_smoke ark_shader_assets_smoke ark_renderer_preset_smoke ark_frame_validation_smoke ark_sandbox
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(tone_mapping_pass|framework_headers|shader_assets|renderer_preset|frame_validation)_smoke" --output-on-failure
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

Sandbox smoke：
```powershell
ark_sandbox hidden-window smoke with --tone-mapping reinhard
ark_sandbox hidden-window smoke with --tone-mapping linear
ark_sandbox hidden-window smoke with --preset bloom-validation --bloom --tone-mapping aces
```

### 0.55.6 验证与收尾

- 更新 `docs/phase/phase55.md` 完成状态。
- 更新 `docs/codex_handoff.md`。
- 更新 `README.md` sandbox 参数示例。
- 提交并推送。

## 完成标准

- `ToneMappingOperator` 存在并默认 `Reinhard`。
- sandbox 支持 `--tone-mapping reinhard|linear|aces`。
- `ToneMappingPass` uniform 上传 operator。
- `tonemap.frag.hlsl` 支持 Linear / Reinhard / ACES。
- 默认 frame validation golden diff 不变。
- 相关 smoke tests 通过。
- full build 和 full CTest 通过。
- handoff 记录阶段结果。

## 风险与注意事项

- ACES approximation 会改变画面观感，因此必须 opt-in，默认保持 Reinhard。
- Linear operator 可能大面积过曝，这是调试功能，不应作为默认。
- 不要把 `filmic` 做成含糊的默认行为；若支持该别名，明确映射到 ACES fitted。
- 不要在本阶段引入 Auto Exposure，否则 golden 和视觉判断会同时变动，风险过大。
- 如果 uniform padding 被复用为 operator id，应同步更新 C++ smoke test 捕获结构和 shader source smoke。

## 后续方向

Phase 0.55 完成后建议：
1. Phase 0.56：Bloom validation LDR statistical diff，用 `bloom-validation` 比较 Bloom disabled/enabled。
2. Phase 0.57：Auto Exposure prelude，建立 luminance mip / histogram resource foundation。
3. Phase 0.58：KHR_materials_emissive_strength，让 emissive intensity 更符合 glTF extension 语义。
4. Phase 0.59：Transparent sorting，补齐 Blend bucket back-to-front 排序。
