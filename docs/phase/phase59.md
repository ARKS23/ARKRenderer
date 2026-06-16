# Phase 0.59 Complex Scene Shadow Visibility Closure

## 实施状态

已完成 0.59.0 文档与范围确认到 0.59.6 验证与收尾的开发工作。当前落地范围：

- 默认 Sponza scale 固定为 `5.0f`，`Default` preset 保持 Sponza + DamagedHelmet 组合场景，`Sponza` preset 保持纯 Sponza 验证路径。
- 默认大场景相机调整为 target `(0.0, 3.2, 0.6)`、distance `26.0`、pitch `-12`、far plane `512.0`，避免模型放大后被远距离相机抵消。
- 默认 sandbox shadow settings 调整为 strength `1.0`、map extent `2048`、orthographic half extent `64.0`、far plane `256.0`、light distance `96.0`。
- 默认/Sponza 场景降低 environment intensity 到 `0.55` 并降低 ambient；`shadow-validation` 降到 `0.35`，增强 direct light 与 shadow 对比。
- `shadow-validation` preset 最低 shadow bounds / far plane / light distance 同步提升，避免大场景下 shadow map 覆盖不足。
- smoke tests 已同步覆盖默认 preset、scene resource composite、sandbox camera、framework header usage 和 shadow validation 参数。

验证结果：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_renderer_preset_smoke ark_scene_resource_smoke ark_sandbox_camera_controller_smoke ark_framework_headers_smoke ark_shadow_pass_smoke ark_forward_pass_pipeline_smoke ark_sandbox
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(renderer_preset|scene_resource|sandbox_camera_controller|framework_headers|shadow_pass|forward_pass_pipeline)_smoke" --output-on-failure
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

结果：

```text
targeted build passed
targeted CTest passed: 6/6
git diff --check: only CRLF warnings, no whitespace errors
full build passed
full CTest passed: 29/29
sandbox hidden-window smoke passed: default, sponza, shadow-validation, bloom-validation
```

## 阶段判断

Phase 0.58 已经把 Sponza fallback 加载、默认 Sponza + DamagedHelmet 组合场景、Directional Shadow Map、Bloom 和 ACES ToneMapping 接到 sandbox 路径中。当前主要问题不再是“有没有功能”，而是“默认打开是否能稳定、清晰地看出这些功能”。

这一阶段建议聚焦默认视觉闭环：让 sandbox 默认画面成为一个可靠的综合验证入口。目标是打开 `ark_sandbox.exe` 后能直观看到大场景、Helmet、斜向主光、接触关系、阴影、Bloom 和 ACES ToneMapping 的组合效果。

## 目标

- 校准默认 Sponza + DamagedHelmet 的尺寸、位置和相机视角。
- 增强默认阴影可见性，避免 shadow map 已启用但肉眼不明显。
- 让 `shadow-validation` preset 成为一眼可验证阴影的稳定入口。
- 保持 Bloom / ACES 默认开启路径可见，但不把本阶段变成后处理重构。
- 保留 Sponza `.ktx` 贴图 fallback 现状，不在本阶段实现 KTX/KTX2 解码。
- 补充测试，覆盖 preset、sandbox 默认参数、shadow settings 和场景组合描述。
- 更新文档，明确默认 sandbox 的验证用途和当前材质限制。

## 非目标

- 不做 Cascaded Shadow Maps。
- 不做 PCSS、VSM、ESM、EVSM 或 contact shadow。
- 不做自动 scene bounds fitting。
- 不做完整 shadow debug UI。
- 不做 KTX/KTX2 原生解码。
- 不做 Sponza 材质质量修复。
- 不做 RenderGraph、deferred renderer 或 clustered lighting。
- 不更新 golden baseline，除非本阶段明确加入新的视觉基线。

## 当前基础

相关代码：

```text
src/renderer/RendererPreset.cpp
src/app/SandboxLaunchOptions.h
src/app/SandboxLaunchOptions.cpp
src/renderer/passes/ShadowPass.h
src/renderer/passes/ShadowPass.cpp
src/renderer/passes/ForwardPass.cpp
src/renderer/FrameRenderer.cpp
shaders/mesh.frag.hlsl
shaders/shadow.vert.hlsl
shaders/shadow.frag.hlsl
tests/renderer_preset_smoke.cpp
tests/scene_resource_smoke.cpp
tests/sandbox_camera_controller_smoke.cpp
tests/shadow_pass_smoke.cpp
tests/forward_pass_pipeline_smoke.cpp
```

当前默认路径已经具备：

- `RendererScenePreset::Default` 使用 Sponza 作为主模型。
- 默认场景追加 DamagedHelmet 作为第二模型。
- sandbox 默认开启 ACES ToneMapping、Bloom 和 ShadowSettings。
- `ShadowPass` 在 scene pass 前渲染 directional shadow map。
- `ForwardPass` 采样 shadow map，并只影响 direct lighting。
- Sponza 贴图走 texture load failure fallback，几何可用于复杂场景验证。

## 推荐方案

### 默认组合场景校准

统一整理默认场景的几个核心参数：

- Sponza 主模型 scale。
- DamagedHelmet translate / scale。
- 默认 camera target / distance / pitch / yaw。
- shadow orthographic bounds / light distance / far plane。

建议原则：

- 改大 Sponza 时不要同步等比例拉远相机，否则视觉上会像模型没有变大。
- Helmet 应明确落在 Sponza 中庭附近，能形成接触关系和阴影参考。
- 相机默认视角应同时看到 Helmet 和足够多的 Sponza 几何。
- 如果模型被裁剪，优先调整 camera far plane 和 shadow far plane，而不是继续拉远相机。

### 阴影可见性增强

当前阴影链路已存在，下一步重点是参数可见性：

- 降低默认 ambient / environment intensity，减少 IBL 把阴影冲淡。
- 调整主光方向，让 Helmet 或中庭结构投影到可见地面/墙面上。
- 提高 `shadowStrength`，确保默认画面能看出明暗变化。
- 根据默认 Sponza scale 调整 `orthographicHalfExtent`、`lightDistance`、`farPlane`。

需要注意：阴影只影响 direct lighting，IBL、ambient 和 emissive 仍然存在。因此如果环境光太强，即使 shadow factor 生效，画面上也可能不明显。

### Shadow Validation Preset

`shadow-validation` 应该承担更明确的职责：不是“看起来像默认场景”，而是“专门验证阴影一定可见”。

建议：

- 使用更低的 environment intensity。
- 使用更强的斜向主光。
- 使用更大的 shadow map 或更合适的 shadow bounds。
- 相机对准可见投影区域。
- 必要时追加一个简单 shadow catcher / block fixture，但优先先复用 Sponza + Helmet，避免新增资产负担。

### Sandbox 参数整理

建议保留以下手动调试入口：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset sponza
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset shadow-validation
build\msvc-vcpkg\Debug\ark_sandbox.exe --shadow-strength=1.0
build\msvc-vcpkg\Debug\ark_sandbox.exe --shadow-bounds=48
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset shadow-validation --shadow-bounds=48 --shadow-strength=1.0
```

如果默认参数变多，暂时不建议加入复杂 UI；先通过 preset 和 CLI 保持工程推进速度。

## 分阶段任务

### 0.59.0 文档与范围确认

- 新增 `docs/phase/phase59.md`。
- 明确本阶段只做复杂场景默认视觉闭环。
- 明确不做 KTX/KTX2、CSM、自动 bounds fitting 和新渲染架构。

### 0.59.1 默认场景尺度与摆位校准

修改：

```text
src/renderer/RendererPreset.cpp
tests/renderer_preset_smoke.cpp
tests/scene_resource_smoke.cpp
```

目标：

- 确认 `DefaultSponzaScale` 的最终建议值。
- 调整 DamagedHelmet 的位置和缩放，使其位于 Sponza 中庭可观察区域。
- 确认 `Default` 和 `Sponza` preset 的语义差异：
  - `Default`：Sponza + DamagedHelmet 综合验证。
  - `Sponza`：纯 Sponza 几何验证。

### 0.59.2 默认相机校准

修改：

```text
src/app/SandboxLaunchOptions.cpp
tests/sandbox_camera_controller_smoke.cpp
tests/renderer_preset_smoke.cpp
```

目标：

- 默认相机能看到 Helmet 和足够大的 Sponza 空间。
- 不用手动滚轮就能判断场景组合是否正确。
- near/far plane 适配当前默认大场景。

### 0.59.3 默认阴影参数校准

修改：

```text
src/app/SandboxLaunchOptions.h
src/app/SandboxLaunchOptions.cpp
src/renderer/RendererPreset.cpp
tests/renderer_preset_smoke.cpp
tests/framework_headers_smoke.cpp
```

目标：

- 默认 `ShadowSettings` 适配当前 Sponza scale。
- `shadow-validation` preset 使用更明显的阴影参数。
- 主光、ambient、environment intensity 不互相抵消阴影效果。

### 0.59.4 Shadow 可见性验证路径

可选修改：

```text
src/renderer/RendererPreset.h
src/renderer/RendererPreset.cpp
assets/models/shadow_validation_fixture.gltf
tests/renderer_preset_smoke.cpp
tests/scene_resource_smoke.cpp
```

目标：

- 优先通过现有 Sponza + Helmet 达成阴影可见。
- 如果现有资产仍然不稳定，再新增轻量 shadow validation fixture。
- fixture 必须简单、可读、可维护，不承担材质展示任务。

### 0.59.5 Tests

建议执行：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_renderer_preset_smoke ark_scene_resource_smoke ark_sandbox_camera_controller_smoke ark_framework_headers_smoke ark_shadow_pass_smoke ark_forward_pass_pipeline_smoke ark_sandbox
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(renderer_preset|scene_resource|sandbox_camera_controller|framework_headers|shadow_pass|forward_pass_pipeline)_smoke" --output-on-failure
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

Sandbox smoke：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset sponza
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset shadow-validation
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation
```

### 0.59.6 验证与收尾

- 更新 `docs/phase/phase59.md` 的完成状态。
- 更新 `docs/codex_handoff.md`。
- 如默认参数变化，更新 `README.md` 的 sandbox 说明。
- 记录当前 Sponza 仍是 texture fallback material path。
- 提交并推送。

## 完成标准

- 默认 sandbox 打开后可以看到 Sponza + DamagedHelmet 组合场景。
- 默认画面中 Helmet 与场景空间关系清晰，不再只是空背景中的单模型。
- 默认阴影或 `shadow-validation` preset 中的阴影肉眼可辨。
- Bloom 与 ACES ToneMapping 默认路径仍正常工作。
- `--preset sponza` 仍保持纯 Sponza 验证路径。
- 显式传入模型路径时不自动追加 DamagedHelmet。
- 相关 smoke tests 通过。
- full build 和 full CTest 通过。

## 风险与注意事项

- Sponza 贴图仍走 `.ktx` fallback，因此不要把本阶段画面当作最终材质质量。
- 默认场景 scale、相机和 shadow bounds 是联动参数，单独调一个值可能造成视觉上“没有变化”。
- 环境光过强会显著降低阴影对比度。
- shadow bounds 太大时阴影分辨率会下降；太小时模型会超出 shadow map 覆盖。
- 当前是单 directional shadow map，大场景里无法同时兼顾近处接触阴影和远处覆盖质量，这是后续 CSM 的职责。

## 后续方向

Phase 0.59 完成后，推荐优先级：

1. Phase 0.60：KTX/KTX2 Texture Loader 或 Sponza 贴图转换管线。
2. Phase 0.61：Shadow bounds fitting / CSM prelude。
3. Phase 0.62：Screenshot baseline for default composite scene。
4. Phase 0.63：Renderer public scene API / engine integration boundary。
5. Phase 0.64：更完整的材质扩展与复杂 glTF 资产兼容。
