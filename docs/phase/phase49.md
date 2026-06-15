# Phase 0.49 Screenshot / Golden Image Validation Foundation

## 阶段判断

Phase 0.48 已经把真实 Vulkan frame validation 从 HDR scene color 统计验证推进到了 tone-mapped `RGBA8Unorm` LDR readback：测试可以在离屏目标中渲染 SkyboxPass、ForwardPass 和 ToneMappingPass，并读取最终可见色彩层做 CPU 统计。

这一步解决了“是否渲染出非黑、有限、亮度合理、alpha 合理画面”的问题，但还不能回答“画面空间结构是否正确”“材质球是否仍在预期位置”“IBL 高光和粗糙度梯度是否出现明显退化”。因此下一阶段应优先建立 screenshot / golden image validation foundation，把 Phase 0.48 的 LDR bytes 保存为稳定图片，并引入保守的图像基线比较策略。

本阶段不建议直接进入 Bloom、auto exposure、ACES 或更复杂后处理。后处理会显著改变最终画面，如果没有截图和 golden 对比，视觉回归会越来越依赖人工观察。

## 目标

- 基于 `ark_frame_validation_smoke` 现有 `RGBA8Unorm` LDR readback，保存 PNG 截图 artifact。
- 为 specular IBL quad-grid fixture 和 material ball fixture 都生成最终 LDR 截图输出。
- 建立稳定 artifact 目录约定，优先写入 build 目录，避免污染源代码目录。
- 引入最小 golden baseline 管理策略，支持后续提交稳定基线图片。
- 建立容忍式 image diff，而不是逐像素完全相等：
  - 校验尺寸一致。
  - 校验通道数量和像素数量一致。
  - 统计 mean absolute error。
  - 统计 max channel error。
  - 可选统计 luminance mean / histogram 差异。
- 保留 Phase 0.48 的 HDR/LDR 统计验证，golden diff 作为新增覆盖层，而不是替代原有 smoke。
- 文档化 GPU/driver 差异下的阈值策略，避免 golden 测试变成脆弱的像素锁死。

## 非目标

- 不引入运行时截图 UI 或用户截图快捷键。
- 不读取 swapchain backbuffer；继续使用 offscreen LDR render target 作为测试目标。
- 不要求跨 GPU bit-exact 输出。
- 不在本阶段引入 Bloom、auto exposure、TAA、ACES curve 或完整 post-processing stack。
- 不重构 FrameRenderer / RenderGraph。
- 不把 public scene/resource loading API 纳入本阶段；它可以在 golden foundation 稳定后作为下一阶段推进。
- 不新增 HDR 资源依赖；当前 fixture 和默认/procedural environment 已足够支撑本阶段测试。

## 当前基线

- `tests/frame_validation_smoke.cpp` 已经可以渲染两个 fixture：
  - `assets/models/specular_ibl_validation_fixture.gltf`
  - `assets/models/material_ball_validation_fixture.gltf`
- frame validation 已经同时读取：
  - `RGBA16Float` HDR scene color。
  - `RGBA8Unorm` tone-mapped LDR output。
- LDR stats 已经覆盖 finite/range、non-black ratio、luminance range、color variation 和 alpha opaque。
- `ToneMappingPass` 已经在真实 Vulkan command flow 中运行，而不只是 fake-RHI smoke。
- 项目已有 `stb` 依赖，可优先评估使用 `stb_image_write.h` 写 PNG，避免新增第三方依赖。

## 推荐方案

### 0.49.0 文档与范围确认

- 明确本阶段目标是“最终 LDR 画面的截图 artifact 与 golden validation foundation”。
- 明确截图来源是 Phase 0.48 的 offscreen `RGBA8Unorm` LDR readback。
- 明确本阶段不处理 swapchain capture、不做运行时截图功能。
- 明确 golden 阈值采用保守容忍，而不是 bit-exact。

### 0.49.1 PNG Writer Foundation

建议先做测试侧最小封装，而不是直接做 renderer public API：

- 新增小型 PNG 写出 helper，可放在 `tests/frame_validation_smoke.cpp` 内部或测试工具文件中。
- 使用 `stb_image_write.h` 写出 RGBA8 PNG。
- 输入保持简单：
  - width
  - height
  - tightly packed RGBA8 bytes
  - output path
- 输出路径建议：

```text
build/test_artifacts/frame_validation/specular_ibl_validation_fixture.png
build/test_artifacts/frame_validation/material_ball_validation_fixture.png
```

如果后续需要 runtime screenshot，再把这层 helper 上移到 renderer/asset utility。当前阶段先保持测试私有，降低 API 承诺。

### 0.49.2 Frame Validation Artifact Output

在 `ark_frame_validation_smoke` 中复用现有 LDR readback bytes：

- 每个 fixture 完成 LDR stats 后写出 PNG artifact。
- artifact 命名使用稳定 fixture id，而不是从完整路径直接派生。
- 写出前创建目录。
- 写出失败应让测试失败，因为 screenshot artifact 是本阶段核心产物。

建议输出日志保持英文，便于 CI 和命令行读取，例如：

```text
Wrote frame validation artifact: build/test_artifacts/frame_validation/material_ball_validation_fixture.png
```

### 0.49.3 Golden Baseline Policy

建议支持两层模式：

- 默认模式：
  - 总是写 artifact 到 build 目录。
  - 如果仓库内存在 golden baseline，则执行 image diff。
  - 如果 golden baseline 暂未存在，可以先只运行 artifact + stats validation，并在文档中记录原因。
- 更新模式：
  - 可通过 `--update-golden` 或类似参数，把当前 artifact 写入仓库基线目录。
  - 基线目录建议：

```text
tests/golden/frame_validation/specular_ibl_validation_fixture.png
tests/golden/frame_validation/material_ball_validation_fixture.png
```

golden 比较建议采用阈值：

- `meanAbsError`：低阈值，用于捕捉整体偏色、曝光变化、shader 退化。
- `maxChannelError`：较宽松阈值，用于容忍少量边缘、深度、浮点舍入差异。
- `mismatchedPixelRatio`：可选，用于防止局部大面积变化被平均值掩盖。

初版阈值应偏保守，先防止明显回归；后续有 CI/GPU 稳定数据后再逐步收紧。

### 0.49.4 Fixture Integration

两个 fixture 都应进入截图与 golden 路径：

- specular IBL quad-grid fixture：
  - 更适合观察 roughness / metallic 网格、IBL 高光方向和强度。
  - 适合捕捉 descriptor、prefiltered mip、BRDF LUT、tone mapping 的明显回归。
- material ball fixture：
  - 更接近常见 PBR 材质球检查方式。
  - 适合捕捉法线、球面高光、粗糙度连续变化和相机视角问题。

如果初期只提交一张 golden，优先选择 material ball fixture；但实现上建议两个 fixture 都写 artifact。

### 0.49.5 Tests

建议本阶段至少执行：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_frame_validation_smoke
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

如果本阶段触及 sandbox 默认路径或 renderer resource lifetime，再补充：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
build\msvc-vcpkg\Debug\ark_sandbox.exe assets\models\material_ball_validation_fixture.gltf
```

## 完成标准

- `ark_frame_validation_smoke` 能稳定写出两个 fixture 的 LDR PNG artifact。
- artifact 使用 tone-mapped `RGBA8Unorm` output，而不是 HDR scene color。
- PNG 写出路径稳定，目录不存在时自动创建。
- 现有 HDR/LDR stats validation 继续通过。
- 如果提交 golden baseline：
  - golden 文件位于固定测试目录。
  - 默认测试会执行容忍式 image diff。
  - diff 失败时输出 mean/max/mismatch 等关键数据。
- 全量 build 和 CTest 通过。
- handoff 更新本阶段完成内容、测试命令和遗留风险。

## 风险与注意事项

- 不同 GPU/driver 可能带来少量浮点差异，golden 不能做严格逐像素相等。
- ToneMapping shader 已经输出 gamma 编码后的 LDR，截图写出时不要再次做颜色空间转换。
- 当前 LDR target 是 `RGBA8Unorm`，PNG 写出应按普通 RGBA8 bytes 处理。
- 需要确认 readback bytes 是 tightly packed；如果未来 `copyTextureToBuffer` 支持 row pitch，PNG writer 需要显式处理 stride。
- Golden baseline 不应频繁更新；每次更新都应对应明确的视觉或渲染算法变更。
- Artifact 输出应位于 build 目录，避免普通测试运行污染 git working tree。
- 如果选择提交 golden PNG，注意文件体积和稳定性；测试分辨率保持当前 `256x144` 是合理起点。

## 后续方向

Phase 0.49 完成后，建议优先级如下：

1. Public scene/resource loading API：把模型、环境、相机、quality config 的加载边界从 sandbox 特例整理成 renderer 可复用入口。
2. Renderer quality config API：暴露 specular prefilter size、BRDF LUT size、irradiance size 等质量配置。
3. 后处理栈：在截图/golden 能兜底后，再推进 Bloom、auto exposure、ACES tone mapping。
4. Blend sorting：基于 camera position 和 bounds 做 Blend bucket back-to-front 排序。
5. Deferred destruction：整理 pipeline、shader、descriptor、texture view 等 GPU resource lifetime。

## 实施同步

Phase 0.49 已完成 0.49.0 文档与范围确认到 0.49.5 Tests：

- `tests/frame_validation_smoke.cpp` 新增测试私有 PNG writer foundation，使用 `stb_image_write.h` 将 tone-mapped `RGBA8Unorm` LDR readback 保存为 PNG。
- `ark_frame_validation_smoke` 现在默认把两个 fixture 的 LDR artifact 写入：

```text
build/msvc-vcpkg/test_artifacts/frame_validation/specular_ibl_validation_fixture.png
build/msvc-vcpkg/test_artifacts/frame_validation/material_ball_validation_fixture.png
```

- 新增 `--update-golden` 模式，用于更新仓库内 golden baseline：

```text
tests/golden/frame_validation/specular_ibl_validation_fixture.png
tests/golden/frame_validation/material_ball_validation_fixture.png
```

- 默认测试模式会在 golden 存在时执行容忍式 image diff，并输出：
  - `meanAbsError`
  - `maxChannelError`
  - `mismatchedPixelRatio`
- 初始阈值保持保守：
  - `meanAbsError <= 0.02`
  - `maxChannelError <= 0.25`
  - `mismatchedPixelRatio <= 0.10`
- 现有 HDR/LDR stats validation 继续保留；golden diff 是新增验证层。
- `tests/dependency_smoke.cpp` 已补充 `stb_image_write.h` 头文件覆盖。

本阶段生成并提交了两张 `256x144` 初始 golden PNG。它们来自 Phase 0.48 已建立的 offscreen LDR target，不读取 swapchain backbuffer，也不做额外颜色空间转换。

验证结果：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_frame_validation_smoke ark_dependency_smoke
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe --update-golden
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe
build\msvc-vcpkg\Debug\ark_dependency_smoke.exe
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
ark_sandbox hidden-window smoke
```

结果：

```text
targeted frame-validation/dependency smoke build passed
ark_frame_validation_smoke --update-golden passed
ark_frame_validation_smoke passed with golden diff mean/max/mismatch all 0
ark_dependency_smoke passed
git diff --check: only line-ending warnings, no whitespace errors
full build passed
CTest: 22/22 tests passed
default sandbox hidden-window smoke passed
```

遗留说明：

- Golden 阈值目前偏宽松，目标是先捕捉明显视觉回归；后续有更多 GPU/CI 样本后可以收紧。
- 当前截图能力仍是测试私有 helper，不是 runtime screenshot API。
- Artifact 输出在 build 目录；只有 `--update-golden` 会写入仓库内 baseline。
