# Phase 0.47 Material Ball Visual Fixture

## 阶段判断

Phase 0.46 已经补齐 glTF camera / scene camera selection 的最小闭环，`ark_frame_validation_smoke` 也已经能使用资产相机渲染 offscreen HDR scene color。当前渲染器已经具备 HDR scene color、ToneMappingPass、skybox、diffuse irradiance IBL、prefiltered specular IBL、BRDF LUT、roughness / metallic 材质参数链路和最小统计型 readback 验证。

下一阶段不应急着进入 Bloom、shadow、RenderGraph 或完整 screenshot/golden system。更稳的方向是建立一个高信号的材质球视觉验证夹具：用稳定 camera、HDR environment 和一组曲面材质球观察 PBR/IBL 的核心表现。当前 quad-grid fixture 能验证数据链路，但平面不擅长暴露高光 lobe、反射方向、曲面法线变化和 roughness 过渡问题。

Phase 0.47 的核心目标是：让 renderer 有一个可重复、可自动化、比平面材质网格更接近真实观察的 material ball validation scene，为后续 tone-mapped LDR readback 和 screenshot/golden image system 打基础。

## 目标

- 新增一个小体积、可提交、可稳定加载的 material ball glTF fixture。
- 使用 Phase 0.46 的 scene camera 能力，为 fixture 提供稳定观察视角。
- 使用当前 HDR / cubemap / irradiance / specular IBL 链路，不修改 PBR shader 公式。
- 覆盖典型 roughness / metallic 组合，观察曲面高光、反射、漫反射和金属/非金属差异。
- 补充 loader / model resource / render queue / frame validation smoke，确认 fixture 能进入真实渲染路径。
- 保持验证阈值为统计型和结构型，不在本阶段引入 screenshot/golden baseline。

## 非目标

- 不做 screenshot / golden image system。
- 不做 tone-mapped LDR target readback。
- 不改 Cook-Torrance direct BRDF、diffuse IBL 或 split-sum specular IBL 公式。
- 不新增 Bloom、auto exposure、ACES、shadow、multi-light 或 `KHR_lights_punctual`。
- 不引入完整 public scene/resource loading API。
- 不引入复杂 glTF extensions、animation、skin、morph target 或 editor camera UI。
- 不依赖未提交的大型外部模型或图片资源。

## 当前基线

- `assets/models/specular_ibl_validation_fixture.gltf` 已提供 15 个 roughness / metallic quad material grid，并带有稳定 perspective camera。
- `ark_frame_validation_smoke` 已能创建真实 Vulkan backend、隐藏 window、offscreen `RGBA16Float` scene color、depth 和 readback buffer。
- `RenderView::setPerspectiveCamera()` 已能从 fixture camera 构建 view/projection/cameraPosition。
- `ForwardPass` 已绑定 irradiance cubemap、prefiltered specular cubemap 和 BRDF LUT，并在 mesh shader 中执行 diffuse + specular IBL。
- 当前验证仍主要基于平面材质网格，缺少球面法线变化和高光形态观察。

## 推荐路线

本阶段建议按“资产夹具先行，验证路径复用”的路线推进：

1. 新增 material ball fixture，优先让现有 loader 和 renderer 路径无改动加载。
2. 复用 Phase 0.46 scene camera，保证 frame validation 视角稳定。
3. 扩展 asset/model smoke，确认材质球数量、材质参数、camera 和 render queue 展开正确。
4. 新增或扩展真实 Vulkan frame validation smoke，用统计读回验证画面不是黑屏、不是全 clear，并能观察到 roughness / metallic 区域差异。
5. 文档记录该 fixture 是后续 screenshot/golden 的前置，不把 golden baseline 塞进本阶段。

## 设计方案

### Material Ball Fixture

建议新增：

```text
assets/models/material_ball_validation_fixture.gltf
```

fixture 结构建议：

- 一个共享 UV sphere 几何数据：
  - positions
  - normals
  - optional texcoords
  - indices
- 15 个 material，沿用 Phase 0.44 的 roughness / metallic 组合：
  - roughness：`0.05, 0.25, 0.5, 0.75, 1.0`
  - metallic：`0.0, 0.5, 1.0`
- 15 个 mesh definition，每个 mesh primitive 复用同一组 accessor，但绑定不同 material。
- 15 个 node instance，用 translation 排成 5 列 x 3 行的材质球阵列。
- 一个 perspective camera node，观察整个阵列。
- 可选一个空 root node 统一缩放 / 平移整个 fixture。

这样可以避免为每个球重复写入几何数据，同时符合 glTF “material 属于 primitive，不属于 node override” 的模型。

### Sphere Geometry

实际第一版使用较小的程序化 UV sphere：

- segments：`12`
- rings：`6`
- accessor 半径：`1.0`，node scale：`0.45`
- normals 使用 normalized sphere position。
- UV 可以先保留，用于后续 texture validation；本阶段材质以 factor 为主。
- 不需要 tangent，除非后续引入 normal map fixture。

这版优先控制 fixture 体积和 smoke 稳定性。后续进入 screenshot/golden image system 时，如果需要更平滑的材质球轮廓，可以在不改变材质/camera 命名契约的前提下提高 tessellation。

### Materials

每个材质建议使用稳定命名：

```text
MB_M00_R005
MB_M00_R025
MB_M00_R050
...
MB_M100_R100
```

材质参数建议：

- baseColorFactor：非金属行使用中性灰或略偏暖灰，例如 `(0.8, 0.76, 0.68, 1.0)`。
- metallicFactor：按行设置 `0.0 / 0.5 / 1.0`。
- roughnessFactor：按列设置 `0.05 / 0.25 / 0.5 / 0.75 / 1.0`。
- alphaMode：`OPAQUE`。
- doubleSided：`false`。
- baseColor texture：当前 renderer/MaterialResource 仍要求 baseColor slot 存在，因此本阶段复用已有 `assets/textures/xiaowei.png`，材质验证重点仍放在 baseColorFactor、metallicFactor 和 roughnessFactor。

### Camera And Environment

fixture 应携带一个 perspective camera：

- 位置建议在阵列正前方偏高处，例如 `(0.0, 1.2, 6.0)`。
- 朝向看向阵列中心。
- `yfov` 建议 `35 ~ 45` 度。
- `znear = 0.1`，`zfar = 100.0`。
- `aspectRatio = 16 / 9`。

environment 使用现有 renderer 默认路径：

- 有 `assets/HDR/2k.hdr` 时使用真实 HDR。
- 缺失时使用程序化 HDR fallback。
- 不要求为本阶段新增 HDR 贴图。

### Frame Validation Strategy

本阶段仍使用统计型验证，不做 golden image：

- 复用 offscreen `RGBA16Float` scene color readback。
- 验证 finite pixel ratio。
- 验证 non-black ratio。
- 验证 luminance mean / max 在合理范围内。
- 验证至少存在高光区域，避免 specular IBL 完全失效。
- 可选：按屏幕区域粗略采样不同 material ball 区域，确认 smooth metallic 与 rough dielectric 的亮度/高光分布存在差异。

区域差异验证要保守，避免不同 GPU / driver / HDR fallback 造成过脆阈值。第一版可以只做整体统计，区域差异作为后续增强。

## 实施拆分

### 0.47.0 文档与范围确认

- 确认本阶段只做 material ball visual fixture 和最小验证路径。
- 明确不进入 screenshot/golden、tone-mapped LDR readback 或 shader 算法修改。
- 记录 fixture 资源策略、材质组合、camera 和测试目标。

### 0.47.1 Fixture Asset Foundation

- 新增 `material_ball_validation_fixture.gltf`。
- 写入共享 sphere geometry accessors。
- 写入 15 个 material 和 15 个 mesh/node instance。
- 写入 scene camera。
- 保持文件可提交、体积可控、无外部大资源依赖。

### 0.47.2 Loader And Model Smoke Coverage

- 扩展 `ark_gltf_loader_smoke`：
  - fixture 可加载。
  - material count / mesh count / instance count 正确。
  - roughness / metallic factors 正确。
  - scene camera 存在且 transform 合理。
- 扩展 `ark_model_resource_smoke`：
  - fixture 能进入 `ModelResource`。
  - 15 个 draw item 能被展开。
  - material factors 能进入 renderer resource。

### 0.47.3 Render Queue / ForwardPass Fixture Coverage

- 扩展 `ark_forward_pass_pipeline_smoke` 或新增专用 fixture smoke。
- 验证 15 个材质球 draw 的 per-material roughness / metallic uniform。
- 验证 specular IBL enable flag 和 max mip 仍正确传递。
- 保持 existing quad-grid fixture 测试不被删除；材质球 fixture 是补充，不是替换。

### 0.47.4 Material Ball Frame Validation

- 新增 `ark_material_ball_validation_smoke`，或扩展 `ark_frame_validation_smoke` 支持选择 fixture。
- 使用 fixture scene camera。
- 使用当前 skybox + ForwardPass + HDR scene color readback。
- 输出统计信息，便于后续调参。
- 阈值只约束“可渲染且有材质差异信号”，不约束精确像素。

### 0.47.5 Tests

- targeted build。
- targeted smoke：
  - `ark_gltf_loader_smoke`
  - `ark_model_resource_smoke`
  - `ark_forward_pass_pipeline_smoke`
  - `ark_material_ball_validation_smoke` 或更新后的 `ark_frame_validation_smoke`
  - `ark_framework_headers_smoke` 如有 public header 变更
- full build。
- full CTest。

### 0.47.6 验证与收尾

- 运行 default sandbox smoke，确认本阶段资源新增不影响默认启动。
- 如 sandbox 支持加载指定 glTF，额外 smoke material ball fixture。
- 更新 `docs/codex_handoff.md`，记录 Phase 0.47 完成内容、验证命令和后续建议。
- 记录仍未完成的 screenshot/golden、tone-mapped LDR readback 和 public scene loading API。

## Tests

建议命令：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_gltf_loader_smoke ark_model_resource_smoke ark_forward_pass_pipeline_smoke ark_frame_validation_smoke
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

本轮实际验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_gltf_loader_smoke ark_model_resource_smoke ark_forward_pass_pipeline_smoke ark_frame_validation_smoke
build\msvc-vcpkg\Debug\ark_gltf_loader_smoke.exe
build\msvc-vcpkg\Debug\ark_model_resource_smoke.exe
build\msvc-vcpkg\Debug\ark_forward_pass_pipeline_smoke.exe
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
build\msvc-vcpkg\Debug\ark_sandbox.exe
build\msvc-vcpkg\Debug\ark_sandbox.exe assets/models/material_ball_validation_fixture.gltf
```

结果：

```text
targeted glTF/model/ForwardPass/frame-validation build passed
ark_gltf_loader_smoke passed
ark_model_resource_smoke passed
ark_forward_pass_pipeline_smoke passed
ark_frame_validation_smoke passed
git diff --check: only line-ending warnings, no whitespace errors
full build passed
CTest: 22/22 tests passed
default sandbox smoke passed
material_ball_validation_fixture sandbox smoke passed
```

## 完成标准

- material ball fixture 能被 `GltfLoader` 稳定加载。
- fixture 至少包含 15 个材质球，覆盖 3 档 metallic x 5 档 roughness。
- fixture 携带 perspective scene camera，并能通过 `RenderView::setPerspectiveCamera()` 使用。
- `ModelResource` 和 `RenderQueue` 能展开所有材质球 draw。
- ForwardPass material uniform 能正确反映每个球的 roughness / metallic。
- frame validation 能渲染材质球 scene 并通过 HDR readback 统计检查。
- full build、CTest 和 sandbox smoke 通过。
- handoff 记录 Phase 0.47 状态和下一阶段建议。

## 实施结果

- 新增 `assets/models/material_ball_validation_fixture.gltf`，使用单文件 glTF + embedded binary data URI，不新增 `.bin`。
- fixture 包含 15 个材质球：3 行 metallic (`0.0 / 0.5 / 1.0`) x 5 列 roughness (`0.05 / 0.25 / 0.5 / 0.75 / 1.0`)。
- 每个材质球使用独立 mesh primitive definition 绑定不同 material，同时复用同一组 sphere accessors。
- 当前 sphere 为 12 segments x 6 rings 的小型 UV sphere，单 primitive 为 91 vertices / 432 indices，node scale 为 `0.45`，用于 smoke 和后续 golden system 前置验证。
- fixture 携带 `MaterialBallCamera` / `MaterialBallCameraNode`，camera 位于 `(0, 0, 6)`，`yfov = 45°`，`aspectRatio = 16:9`。
- 为兼容当前 renderer 的 baseColor texture requirement，材质球 baseColorTexture 复用已有 `../textures/xiaowei.png`；roughness/metallic/baseColorFactor 仍由 material factor 明确驱动。
- `ark_gltf_loader_smoke` 已覆盖 fixture shape、material factors、camera、sphere basis 和 transforms。
- `ark_model_resource_smoke` 已覆盖 `ModelResource` 创建、fallback texture cache、MaterialResource factors/render state 和 RenderQueue 15 draw 展开。
- `ark_frame_validation_smoke` 已参数化为同时渲染旧 specular quad-grid fixture 和新 material ball fixture，并通过真实 Vulkan offscreen `RGBA16Float` readback 统计检查。
- `ark_forward_pass_pipeline_smoke` 保留 Phase 0.44 的 15 格 roughness/metallic material uniform coverage；本阶段不把真实 glTF loader 硬耦进 fake-RHI pipeline smoke，避免测试职责混杂。

## 风险与注意事项

- glTF material 绑定在 mesh primitive 上，不是 node override；若多个球要共享几何但使用不同材质，应使用多个 mesh primitive definition 复用同一批 accessors。
- sphere normals 必须稳定，否则 specular highlight 会被错误法线污染。
- roughness `0.05` 会产生小而亮的高光，统计阈值不能只看 mean luminance。
- metallic `1.0` 的 baseColor 会影响 specular tint，材质命名和 factor 要清楚。
- HDR fallback 与真实 HDR 环境的亮度分布不同，frame validation 阈值要保守。
- 不要把本阶段做成 screenshot diff；先建立 fixture 和统计信号。
- 如果新增 `.bin` 文件，注意大小和路径，避免依赖被 `.gitignore` 排除。

## 后续 Phase 建议

Phase 0.47 完成后，推荐继续：

- Phase 0.48 Tone-mapped LDR Readback：读取 ToneMappingPass 输出，验证最终用户可见画面的亮度和编码范围。
- Phase 0.49 Screenshot / Golden Image System：基于稳定 camera、material ball fixture 和 readback harness 引入 PNG/baseline/image diff。
- Phase 0.50 Public Scene Loading API：把默认模型、环境、相机和材质资源加载从 sandbox 特例整理成公开入口。
- Phase 0.51 Post Processing Baseline：在已有 tone mapping 和视觉验证基础上进入 Bloom、auto exposure 或 ACES。
