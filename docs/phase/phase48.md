# Phase 0.48 Tone-mapped LDR Readback

## 阶段判断

Phase 0.45 已经建立了真实 Vulkan 隐藏窗口下的 HDR frame validation smoke：场景通过 SkyboxPass 和 ForwardPass 渲染到 `RGBA16Float` scene color，再做 CPU readback 和统计校验。

Phase 0.46 补齐了 glTF scene cameras，让验证 fixture 可以携带稳定的默认观察视角。

Phase 0.47 增加了 material ball validation fixture，开始用更接近 PBR 材质工作流的场景覆盖 IBL、材质分布和渲染路径。

当前验证仍主要停留在 HDR scene color 层。真实用户看到的是经过 ToneMappingPass 输出到 LDR backbuffer 的画面，而 ToneMappingPass 目前虽然有 fake-RHI smoke 覆盖，也已经在 FrameRenderer 中接入 swapchain，但还没有真实 Vulkan 的 LDR readback 验证。因此下一阶段应优先建立 tone-mapped LDR 输出的可测路径，而不是直接推进 Bloom、auto exposure 或 screenshot golden。

## 目标

- 在现有 frame validation 基础上增加 tone-mapped LDR offscreen target/readback 路径。
- 复用当前 SkyboxPass + ForwardPass 的 HDR scene color 生成流程。
- 在真实 Vulkan smoke 中运行 ToneMappingPass，而不仅是 fake-RHI pipeline/layout 测试。
- 对 LDR 输出做 CPU 端统计校验，覆盖 finite、非黑、亮度范围、颜色变化和 alpha 合理性。
- 保持 specular IBL validation fixture 和 material ball validation fixture 都能通过 HDR 与 LDR 验证。
- 为后续 screenshot/golden image validation 提供稳定前置层。

## 非目标

- 不引入 PNG 输出、golden baseline 或图像 diff。
- 不改变 PBR/IBL 公式，不调整材质能量模型。
- 不引入 Bloom、auto exposure、ACES 曲线或 TAA。
- 不重构 RenderGraph 或 FrameRenderer 架构。
- 不读取 swapchain backbuffer 作为测试目标；本阶段优先使用 offscreen LDR render target，降低 present/swapchain state 对 smoke 的干扰。
- 不设置严格逐像素阈值；本阶段只做保守统计验证。

## 当前基线

- `tests/frame_validation_smoke.cpp` 已创建 `RGBA16Float` scene color、depth target 和 readback buffer。
- 当前 frame validation 会渲染 skybox 与 forward mesh，并从 HDR scene color 读回 half-float RGBA 数据做统计。
- `src/renderer/passes/ToneMappingPass.*` 已经支持采样 scene color，并通过 fullscreen triangle 输出 tone-mapped color。
- `tests/tone_mapping_pass_smoke.cpp` 已覆盖 ToneMappingPass 的 uniform、descriptor layout、pipeline 创建和 execute 调用，但没有真实 Vulkan render target/readback。
- `src/renderer/FrameRenderer.cpp` 已在正式帧流程中执行 HDR scene pass 后的 ToneMappingPass。

## 推荐方案

### 0.48.0 文档与范围确认

- 确认本阶段目标是 LDR readback validation，而不是 screenshot golden。
- 明确 LDR target 选择：
  - 优先使用 `RGBA8Unorm`。
  - ToneMapping shader 已经输出 gamma 编码后的颜色，直接写入 `RGBA8Srgb` 可能引入双重编码风险。
- 确认当前 HDR validation 继续保留，LDR validation 作为新增覆盖层。

### 0.48.1 LDR Readback Stats Foundation

在 `tests/frame_validation_smoke.cpp` 中增加 LDR 统计结构与解码逻辑：

- 新增 4 bytes-per-pixel 的 RGBA8 readback 路径。
- 将 `uint8_t` RGBA 解码为 `[0, 1]` 浮点值。
- 增加 `LdrFrameColorStats`，建议字段包括：
  - `pixels`
  - `finitePixels`
  - `nonBlackPixels`
  - `minRgb`
  - `maxRgb`
  - `meanRgb`
  - `meanLuminance`
  - `maxLuminance`
  - `meanAlpha`
  - `minAlpha`
- 增加保守校验：
  - 所有解码值在 `[0, 1]`。
  - `finitePixels == pixels`。
  - `nonBlackRatio` 大于低阈值。
  - `maxLuminance` 与 `meanLuminance` 高于低阈值，避免全黑。
  - RGB channel range 大于低阈值，避免纯色输出。
  - alpha 接近 1。

### 0.48.2 Offscreen ToneMapping Harness

在真实 Vulkan frame validation 中增加 LDR render target：

- 创建 `RGBA8Unorm` LDR texture。
- usage 至少包含：
  - `RenderTarget`
  - `TransferSrc`
- 创建 LDR texture view。
- 创建 LDR readback buffer，大小为 `width * height * 4`。
- 当前渲染顺序建议为：
  - HDR scene color transition 到 render target。
  - 执行 SkyboxPass 和 ForwardPass。
  - HDR scene color transition 到 shader resource。
  - LDR target transition 到 render target。
  - begin rendering 到 LDR target。
  - 设置 viewport/scissor。
  - `frameContext.sceneColorView = hdrSceneColorView`。
  - `frameContext.colorFormat = RGBA8Unorm`。
  - 执行 `ToneMappingPass::execute(frameContext)`。
  - end rendering。
  - LDR target transition 到 copy source。
  - copy texture to readback buffer。

需要注意：`ToneMappingPass` 自身不负责 begin/end rendering，测试 harness 必须提供完整的 dynamic rendering scope。

### 0.48.3 Fixture Integration

将 LDR validation 接入现有两个 fixture：

- `assets/models/specular_ibl_validation_fixture.gltf`
- `assets/models/material_ball_validation_fixture.gltf`

建议将 fixture 验证封装为类似：

- `validateFixtureFrameReadback(path)`
- 内部同时执行 HDR stats 和 LDR stats。

这样 Phase 0.47 的材质球覆盖不会被削弱，同时 LDR 输出也能覆盖更丰富的 roughness/metallic/material 分布。

### 0.48.4 Tests

建议执行：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_frame_validation_smoke ark_tone_mapping_pass_smoke
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe
build\msvc-vcpkg\Debug\ark_tone_mapping_pass_smoke.exe
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

如本阶段触及 sandbox 默认视觉路径，再补充：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
build\msvc-vcpkg\Debug\ark_sandbox.exe --model assets\models\material_ball_validation_fixture.gltf
```

## 完成标准

- frame validation smoke 能创建 LDR offscreen target 并完成 readback。
- ToneMappingPass 在真实 Vulkan command flow 中执行。
- HDR scene color readback 仍然保留并通过现有统计校验。
- LDR readback 对 specular IBL fixture 和 material ball fixture 都通过统计校验。
- `ark_tone_mapping_pass_smoke` 继续通过。
- 全量 build 与 CTest 通过。
- sandbox smoke 不出现启动崩溃。
- handoff 更新本阶段完成内容、验证命令与后续风险。

## 风险与注意事项

- `RGBA8Srgb` 可能与 shader 内 gamma 编码叠加，初版应优先使用 `RGBA8Unorm`。
- LDR readback buffer 的 row pitch 必须与 `copyTextureToBuffer` 的紧密布局假设一致。
- HDR scene color 在 ForwardPass 后必须正确 transition 到 shader resource，否则 ToneMappingPass 采样可能失败或触发 validation issue。
- LDR target 在 ToneMappingPass 后必须 transition 到 copy source。
- 统计阈值要保守，避免不同 GPU、driver、HDR 环境图或浮点舍入造成不稳定。
- 不建议本阶段直接读取 swapchain image；presentable image 的 ownership/layout 细节会让测试更脆。

## 后续方向

- Phase 0.49 可推进 screenshot/golden image validation，基于本阶段 LDR output 保存 PNG 或做 baseline diff。
- Phase 0.50 可整理 public scene/resource loading API，让 sandbox 和 tests 共用更稳定的加载入口。
- Phase 0.51 以后再推进 Bloom、auto exposure、film curve 或更完整的 post-processing stack。

## 实施同步

Phase 0.48 已完成 0.48.0 文档与范围确认到 0.48.4 Tests：

- `tests/frame_validation_smoke.cpp` 已新增 `RGBA8Unorm` LDR offscreen target 与独立 readback buffer。
- `ark_frame_validation_smoke` 现在保留原有 `RGBA16Float` HDR scene color readback，同时在真实 Vulkan command flow 中执行 `ToneMappingPass`，并读取 tone-mapped LDR 输出。
- LDR 统计覆盖 finite/range、非黑比例、亮度范围、颜色变化和 alpha opaque 检查。
- specular IBL quad-grid fixture 与 material ball fixture 都会同时经过 HDR stats 和 LDR stats。
- SkyboxPass、ForwardPass、ToneMappingPass 的测试作用域已延长到 GPU idle 后，避免 command buffer 仍引用已析构 pass 私有资源。

验证结果：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_frame_validation_smoke ark_tone_mapping_pass_smoke
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe
build\msvc-vcpkg\Debug\ark_tone_mapping_pass_smoke.exe
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
ark_sandbox hidden-window smoke
```

结果：

```text
targeted frame/tone-mapping smoke build passed
ark_frame_validation_smoke passed
ark_tone_mapping_pass_smoke passed
git diff --check: only line-ending warnings, no whitespace errors
full build passed
CTest: 22/22 tests passed
default sandbox hidden-window smoke passed
```

本阶段仍未引入 screenshot/golden image system，也没有读取 swapchain backbuffer；LDR 验证目标明确保持为 offscreen `RGBA8Unorm` target。
