# Phase 0.62 默认组合场景视觉回归闭环

## 实施状态

已完成 0.62.0 文档与范围确认 ~ 0.62.7 验证与收尾。

Phase 0.61 已经把 `MeshResource` / `ModelResource` / `RenderScene` 的 bounds 数据链路接入到 `ShadowPass`，默认 Sponza + DamagedHelmet 组合场景的 directional shadow projection 不再依赖固定 origin 范围。下一阶段不建议马上进入 CSM、PCF 或更多渲染特性，而是先把当前默认组合画面纳入 frame validation，形成一个能持续捕捉视觉回归的闭环。

本阶段核心目标是：用现有 offscreen frame validation / golden image 基础，锁住默认组合场景中 Sponza KTX、DamagedHelmet、Shadow、Bloom、ACES ToneMapping、IBL 的共同输出。

## 完成内容

- `tests/frame_validation_smoke.cpp` 新增 `FrameValidationSceneMode::RendererPreset` 路径，旧 fixture case 仍沿用资产相机和原 golden 行为。
- 新增 `default_composite_scene` frame validation case：
  - 使用 `RendererScenePreset::Default` 加载 Sponza + DamagedHelmet 组合场景。
  - 验证 `SceneResourceLoadReport::loadedModelCount >= 2`，避免 Helmet 未进入场景仍误通过。
  - 验证 `RenderScene::hasBounds()`，覆盖 Phase 0.61 scene bounds 链路。
  - 使用固定侧向中庭相机，让 LDR artifact 同时看到 Sponza 与 DamagedHelmet。
  - 启用 directional shadow、scene bounds fitting、Bloom、ACES ToneMapping。
  - 使用低成本 validation IBL bake：environment cube、irradiance cube、specular prefilter 和 BRDF LUT。
- `default_composite_scene` 增加 LDR 统计检查：
  - 复用通用非黑屏、非纯色、opaque alpha 检查。
  - 增加默认组合画面 color range、亮度动态范围和中心/边缘区域差异检查。
  - `requireShadowMap` 会确认 ShadowPass 实际绑定 shadow map / sampler / strength。
- 新增 golden baseline：

```text
tests/golden/frame_validation/default_composite_scene.png
```

- 现有 `material_ball_validation_fixture.png` 和 `specular_ibl_validation_fixture.png` golden diff 保持 0。
- `ark_frame_validation_smoke --update-golden` 现在也会更新默认组合场景 baseline。

## 阶段目标

- 新增默认组合场景的 frame validation case。
- 使用固定分辨率、固定相机、固定 scene preset 和固定后处理参数，避免每次验证依赖交互视角。
- 覆盖默认 Sponza + DamagedHelmet 组合场景，确认两者都进入渲染路径。
- 覆盖默认开启的 Shadow、Bloom、ACES ToneMapping 和 IBL 路径。
- 至少提供 LDR 输出统计验证，确认画面非空、非 clear color、像素有限且亮度分布合理。
- 如果稳定性允许，新增默认组合场景 golden baseline。
- 明确 golden 更新命令和 baseline 维护策略。
- 保持现有 material ball / specular IBL golden case 行为不变。

## 非目标

- 不做 CSM、shadow cascade split、shadow texel snapping 或 PCF quality preset。
- 不重构 screenshot 系统；优先复用 `ark_frame_validation_smoke`。
- 不引入 UI 面板、debug overlay 或运行时截图工具。
- 不实现 KTX2 / BasisU 压缩纹理转码。
- 不调整材质模型、PBR shader 或 IBL 算法。
- 不为了通过 golden diff 而大幅改动默认 sandbox 视觉效果。

## 当前基础

已有基础：

- `ark_frame_validation_smoke` 已具备真实 Vulkan offscreen 渲染、HDR / LDR readback、PNG artifact 输出和 golden diff。
- `tests/golden/frame_validation/` 已包含 material ball 与 specular IBL 两张稳定 golden。
- `ark_frame_validation_smoke --update-golden` 已能更新 committed baseline PNG。
- Phase 0.56 已加入 Bloom / ToneMapping 统计 diff case。
- Phase 0.58 ~ 0.60 已把 Sponza + DamagedHelmet 默认组合场景、Sponza KTX 读取路径和默认后处理效果接入 sandbox。
- Phase 0.61 已把 scene world bounds 接入 ShadowPass 自动 fitting。

相关文件：

```text
tests/frame_validation_smoke.cpp
tests/golden/frame_validation/
src/renderer/RendererPreset.h
src/renderer/RendererPreset.cpp
src/renderer/SceneResource.h
src/renderer/SceneResource.cpp
src/renderer/FrameRenderer.h
src/renderer/FrameRenderer.cpp
src/renderer/passes/ShadowPass.cpp
src/renderer/passes/BloomPass.cpp
src/renderer/passes/ToneMappingPass.cpp
src/app/SandboxLaunchOptions.h
src/app/SandboxLaunchOptions.cpp
assets/models/sponza/
assets/models/DamagedHelmet/
docs/codex_handoff.md
README.md
```

## 建议方案

优先在 `ark_frame_validation_smoke` 中扩展一个新的 default composite case，而不是单独新增测试程序。

推荐策略：

1. 使用低分辨率 offscreen LDR 输出，例如 `320x180` 或 `256x144`。
2. 使用固定 camera，视角直接对准 Sponza 中庭里的 DamagedHelmet。
3. 使用与默认 sandbox 一致的组合场景 preset。
4. 显式启用当前默认视觉链路：
   - Sponza KTX baseColor texture。
   - DamagedHelmet PBR material。
   - directional shadow。
   - scene bounds shadow fitting。
   - Bloom。
   - ACES ToneMapping。
   - skybox / diffuse IBL / specular IBL。
5. 先做统计验证，保证画面有内容且不是纯 skybox / clear color。
6. 如果跨机器输出足够稳定，再提交 `default_composite_scene.png` golden baseline。

统计验证建议覆盖：

- LDR 像素全部有限。
- 平均亮度大于最小阈值，避免黑屏。
- 最大亮度大于平均亮度，避免纯灰画面。
- 中心区域与边缘区域存在可观测差异，避免只看到天空盒。
- RGB 通道方差达到最低阈值，避免退化到单色输出。
- 可选：Bloom enabled 与 disabled 的 LDR 差异仍可被检测。

golden diff 建议第一版使用比 material ball 更宽松的阈值，因为默认组合场景同时包含 shadow fitting、Bloom、ToneMapping 和多资源加载，驱动差异可能更明显。若 golden 维护成本过高，本阶段可以先保留统计型 baseline，把严格 PNG golden 延后到 shadow quality 稳定后再补。

## 分阶段任务

### 0.62.0 文档与范围确认

- 新增 `docs/phase/phase62.md`。
- 明确本阶段只做默认组合场景视觉回归闭环。
- 明确不做 Shadow quality、CSM、KTX2、UI 和渲染算法重构。

### 0.62.1 Frame Validation Case 设计

修改：

```text
tests/frame_validation_smoke.cpp
```

目标：

- 梳理当前 `FrameValidationCaseDesc` 能否直接表达默认组合场景。
- 如果现有 case descriptor 不够，补充最小字段：
  - preset / scene description 来源。
  - camera override。
  - post-processing override。
  - shadow settings override。
  - 是否参与 golden diff。
- 保持现有 material ball / specular IBL golden case 行为不变。

### 0.62.2 默认组合场景 Offscreen Path

修改：

```text
tests/frame_validation_smoke.cpp
```

目标：

- 新增 `default_composite_scene` case。
- 加载默认 Sponza + DamagedHelmet 组合场景。
- 固定相机位置、朝向、FOV、near / far。
- 固定环境、shadow、Bloom 和 tone mapping 参数。
- 确认 `RenderScene::bounds()` valid，并让 ShadowPass 使用 scene bounds fitting。

### 0.62.3 LDR 统计验证

修改：

```text
tests/frame_validation_smoke.cpp
```

目标：

- 为 `default_composite_scene` 输出 LDR PNG artifact。
- 增加默认组合画面的 LDR stats check。
- 避免只通过“进程不崩溃”的弱验证。
- 测试失败信息需要输出关键统计值，方便后续调阈值。

### 0.62.4 Golden Baseline Path

修改：

```text
tests/frame_validation_smoke.cpp
tests/golden/frame_validation/default_composite_scene.png
```

目标：

- 若统计输出稳定，新增默认组合场景 golden。
- 支持通过现有命令更新 baseline：

```powershell
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe --update-golden
```

- 如果新增 golden 后跨机器风险偏高，则本阶段只保留 artifact 输出和统计验证，不提交 PNG baseline。

### 0.62.5 Manual Shadow Bounds 回归

修改：

```text
tests/frame_validation_smoke.cpp
tests/shadow_pass_smoke.cpp
```

目标：

- 保证 `fitSceneBounds = true` 的默认路径被默认组合 case 覆盖。
- 保证显式 manual shadow bounds path 仍可用。
- 不要求 manual path 进入 golden；统计 smoke 足够。

### 0.62.6 Tests

已执行：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_frame_validation_smoke ark_scene_resource_smoke ark_shadow_pass_smoke ark_renderer_preset_smoke ark_sandbox
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(frame_validation|scene_resource|shadow_pass|renderer_preset)_smoke" --output-on-failure
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe --update-golden
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe
```

Sandbox hidden-window smoke：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset sponza
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset shadow-validation
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset sponza --shadow-bounds 64 --shadow-strength=1.0
```

结果：

```text
targeted build passed
targeted CTest passed: 4/4
ark_frame_validation_smoke --update-golden passed
ark_frame_validation_smoke passed with default_composite_scene golden diff mean/max/mismatch all 0
git diff --check: only CRLF warning, no whitespace errors
full Debug build passed
full CTest passed: 30/30
sandbox hidden-window smoke passed: default, sponza, shadow-validation, sponza manual --shadow-bounds
```

### 0.62.7 验证与收尾

已完成：

- 更新 `docs/phase/phase62.md` 完成状态。
- 更新 `docs/codex_handoff.md`。
- README 补充默认组合 golden baseline 说明。
- 默认组合场景 artifact 路径：

```text
build/msvc-vcpkg/test_artifacts/frame_validation/default_composite_scene.png
```

- 默认组合场景 golden 路径：

```text
tests/golden/frame_validation/default_composite_scene.png
```

- 本阶段提交 `default_composite_scene.png` baseline。

## 完成标准

- `ark_frame_validation_smoke` 覆盖默认 Sponza + DamagedHelmet 组合场景。
- 默认组合 case 能验证 Shadow、Bloom、ACES ToneMapping 和 IBL 链路。
- 默认组合 case 的 LDR 输出不是黑屏、纯天空盒或纯 clear color。
- 现有 material ball / specular IBL golden diff 不回退。
- 如果提交 golden baseline，默认组合场景 diff 在可接受阈值内稳定通过。
- targeted build / CTest 通过。
- full build / full CTest 通过。
- sandbox default / sponza / shadow-validation hidden-window smoke 启动成功。

## 风险与注意事项

- Sponza + DamagedHelmet 组合场景比现有 fixture 更重，frame validation 耗时可能上升。
- Shadow fitting 会让输出受 scene bounds 影响，后续改动模型 transform 时可能触发 golden diff。
- Bloom 与 ToneMapping 对亮度阈值敏感，严格 golden 可能比统计验证更脆。
- 不同 GPU / driver 的浮点差异可能让大场景 PNG diff 更难维护。
- 如果 default composite golden 不稳定，不要强行放宽到无意义阈值；应先保留统计验证。
- 本阶段不解决 shadow resolution 分配问题，Sponza 大场景阴影质量仍可能需要 Phase 0.64 继续处理。

## 后续方向

Phase 0.62 完成后建议继续推进：

1. Phase 0.63：Renderer public scene/resource API 收口，为后续接入引擎整理边界。
2. Phase 0.64：Shadow quality pass，加入 texel snapping、PCF quality preset 或 CSM prelude。
3. Phase 0.65：KTX2 / BasisU / 原始 mip chain 支持。
4. Phase 0.66：基础调试 UI，用于运行时调整 Bloom、ToneMapping、shadow、IBL 和 camera 参数。
