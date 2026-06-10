# Phase 0.24 doubleSided Culling 精确化

## 阶段判断

Phase 0.20 已经完成 glTF `alphaMode` / `alphaCutoff` / `doubleSided` 到 renderer material render state 和 ForwardPass pipeline key 的最小闭环：

```text
glTF material.doubleSided
    -> asset::MaterialData::doubleSided
    -> MaterialResource::renderState().doubleSided
    -> ForwardPass::ForwardPipelineKey::doubleSided
```

但当时为了避免在未确认 glTF winding、projection Y flip 和 Vulkan `frontFace` 约定前引入剔除回归，`ForwardPass` 仍强制使用：

```cpp
pipelineDesc.rasterState.cullMode = rhi::CullMode::None;
```

这意味着当前所有材质都会双面绘制，即使 glTF material 的 `doubleSided=false`。这在语义上不完整，也会让默认单面材质失去 back-face culling 的性能和可见性行为。

Phase 0.23 已经完成 RenderQueue alpha 分桶，`ForwardPass` pipeline variant 路径保持稳定。下一步最合适的小闭环，是把 `doubleSided` 真正映射到 raster culling：

```text
doubleSided = true
    -> cullMode = None

doubleSided = false
    -> cullMode = Back
```

本阶段重点不是引入新材质能力，而是把 Phase 0.20 已经读取和缓存的 glTF core material 语义落实到实际 pipeline state。

## 目标

Phase 0.24 目标：

- 明确当前 glTF winding、`RenderView` projection Y flip 和 Vulkan `frontFace` 的约定。
- `ForwardPass` 根据 `MaterialRenderState::doubleSided` 设置 raster cull mode。
- `doubleSided=true` 的 material 使用 `CullMode::None`。
- `doubleSided=false` 的 material 使用 `CullMode::Back`。
- 显式设置 `rasterState.frontFace`，避免依赖默认值造成约定不清。
- 保持现有 `ForwardPipelineKey` 的 color format / depth format / alpha mode / doubleSided variant 语义。
- 保持 Phase 0.20 alpha blend / depth write 行为不回退。
- 保持 Phase 0.23 RenderQueue alpha bucket ordering 不回退。
- 新增 smoke tests 覆盖 ForwardPass pipeline desc 中的 cull mode / front face。
- default sandbox 和 DamagedHelmet optional smoke 不出现启动即退出或明显剔除回归。

## 非目标

Phase 0.24 暂不做：

- 不做完整 transparent sorting。
- 不做 Blend bucket back-to-front sorting。
- 不做 OIT / weighted blended transparency / depth peeling。
- 不做双面材质的 normal 翻面或 `gl_FrontFacing` 着色分支。
- 不做 two-sided lighting 模型。
- 不做 mesh winding 重写。
- 不做 asset 导入时修正顶点顺序。
- 不做 tangent basis 或 MikkTSpace 改造。
- 不做 HDR framebuffer / tone mapping。
- 不做 IBL / environment map / BRDF LUT。
- 不做 RenderGraph 重构。
- 不做 descriptor layout / shader binding 改动。

## 模块边界

继续遵守现有设计文档：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase20.md
docs/phase/phase23.md
```

边界要求：

- `asset/` 只读取并保存 glTF `doubleSided`，不参与 GPU culling 决策。
- `MaterialResource` 只暴露 `MaterialRenderState`，不创建 pipeline。
- `RenderQueue` 不参与 culling 策略，仍只负责 draw item 展平和 alpha bucket ordering。
- `ForwardPass` 是本阶段唯一的 renderer 层落点。
- RHI enum 和 Vulkan 映射已经存在，除非发现映射错误，否则不修改 RHI public shape。
- shader 不需要知道 `doubleSided`。
- 不为了测试给 `MaterialResource` 增加 public setter。

## 当前行为

当前数据已经完整流到 `ForwardPass`：

```cpp
key.alphaMode = renderState.alphaMode;
key.doubleSided = renderState.doubleSided;
```

但 pipeline 创建时 cull mode 仍固定为 `None`：

```cpp
pipelineDesc.rasterState.cullMode = rhi::CullMode::None;
```

因此当前状态可以概括为：

```text
doubleSided=true  -> pipeline key variant exists -> actual cullMode None
doubleSided=false -> pipeline key variant exists -> actual cullMode None
```

这会导致：

- `doubleSided=false` 不会进行 back-face culling。
- `ForwardPipelineKey::doubleSided` 当前会创建不同 variant，但两个 variant 的 raster state 实际等价。
- 单面 glTF material 和双面 glTF material 在可见性上没有区别。

## Front Face 约定

glTF mesh primitive 的 front face 语义以逆时针 winding 为正面。当前 renderer 使用 Vulkan，并且 `RenderView::setDefaultPerspective()` 中有 projection Y flip：

```cpp
m_Projection = glm::perspectiveRH_ZO(...);
m_Projection[1][1] *= -1.0f;
```

因此本阶段不能盲目假设：

```cpp
cullMode = Back
frontFace = CounterClockwise
```

需要先确认在当前 HLSL matrix 乘法、clip space、viewport 和 Vulkan front-face 判断组合下，实际可见的 glTF 正面对应 `FrontFace::CounterClockwise` 还是 `FrontFace::Clockwise`。

建议策略：

- 优先用小 fixture 或现有 quad/triangle fixture 验证单面材质不会被错误剔除。
- 如果 `Back + CounterClockwise` 导致默认 fixture 或 DamagedHelmet 明显消失，应首先检查 front-face 约定。
- 如果只是 front-face 方向不匹配，应在 `ForwardPass` 显式设置正确的 `frontFace`，而不是回退到全局 `CullMode::None`。
- 不要通过改 mesh indices 或 asset winding 来掩盖 renderer convention 问题。

本阶段结论：

- `ForwardPass` 显式使用 `FrontFace::CounterClockwise`。
- 该约定保持 glTF CCW front-face 语义，并与当前 `RenderView` projection Y flip、正高度 viewport 和已有 fixture/DamagedHelmet smoke 路径兼容。
- 如果后续引入可配置 viewport 翻转或不同投影约定，需要重新审视该 front-face 选择。

## 建议设计

新增最小 helper：

```cpp
rhi::CullMode makeCullMode(const MaterialRenderState& renderState) {
    return renderState.doubleSided ? rhi::CullMode::None : rhi::CullMode::Back;
}
```

并在 ForwardPass pipeline 创建中使用：

```cpp
pipelineDesc.rasterState.cullMode = makeCullMode(renderState);
pipelineDesc.rasterState.frontFace = ...;
```

本阶段使用：

```cpp
pipelineDesc.rasterState.frontFace = rhi::FrontFace::CounterClockwise;
```

审核点：

- 不修改 `ForwardPipelineKey`，因为 `doubleSided` 已经在 key 中。
- 不修改 `MaterialRenderState`，因为需要的字段已经存在。
- 不修改 descriptor layout / pipeline layout / shaders。
- 不改变 Blend material 的 depth write off 行为。
- 不改变 Mask material 的 alpha cutoff discard 行为。
- `doubleSided=true` 和 `doubleSided=false` 的 pipeline desc 必须产生不同 `cullMode`。

## 测试策略

已有覆盖：

- `gltf_loader_smoke` 已覆盖 glTF `doubleSided` 读取。
- `model_resource_smoke` 已覆盖 `MaterialResource::renderState().doubleSided` 保留。
- `framework_headers_smoke` 已覆盖 public structs 能编译。

本阶段需要补的不是 asset/material 数据流，而是 ForwardPass pipeline state。

建议新增：

```text
tests/forward_pass_pipeline_smoke.cpp
```

测试目标：

- 使用 fake `RenderDevice` 捕获 `createGraphicsPipeline()` 收到的 `GraphicsPipelineDesc`。
- 构造 `doubleSided=false` material，触发 ForwardPass 创建 pipeline，验证：
  - `rasterState.cullMode == Back`
  - `rasterState.frontFace` 等于本阶段确认后的约定
- 构造 `doubleSided=true` material，触发 ForwardPass 创建 pipeline，验证：
  - `rasterState.cullMode == None`
  - `rasterState.frontFace` 仍被显式设置
- 保持 alpha mode 行为：
  - Opaque / Mask depth write on
  - Blend depth write off
  - Blend alpha blending 不回退

实现建议：

- 可复用 `model_resource_smoke` 中 fake RHI 的模式，但避免无关的大量复制。
- 如果 ForwardPass 直接测试成本过高，可先抽取一个小的 production helper 生成 raster state；但不要为测试暴露不自然的 public setter。
- 测试不应依赖真实 Vulkan。
- 测试不应通过截图判断 cull state；pipeline desc smoke 更稳定。

可选 fixture：

```text
assets/models/double_sided_culling_fixture.gltf
```

建议只有在 pipeline desc smoke 不足以确认 front-face 约定时再新增。该 fixture 可包含：

- 一个 `doubleSided=false` 单面 quad。
- 一个 `doubleSided=true` 双面 quad。
- 明确命名 material，便于 loader/resource smoke。

## 实施顺序

### 0.24.0 文档与范围确认

目标：

- 新增 `docs/phase/phase24.md`。
- 明确主线是 `ForwardPass` doubleSided culling 精确化。
- 明确不做完整 transparent sorting、two-sided lighting、HDR、IBL、RenderGraph。

审核点：

- 不重复 Phase 0.20 glTF alpha/doubleSided 读取工作。
- 不重复 Phase 0.23 RenderQueue alpha bucket 工作。
- 不修改 asset/RHI/Vulkan，除非 front-face 映射被证明确实有误。

### 0.24.1 Front-face 约定确认

目标：

- 确认当前 glTF winding + `RenderView` projection Y flip + Vulkan `frontFace` 的有效组合。
- 决定 ForwardPass 应设置 `FrontFace::CounterClockwise` 还是 `FrontFace::Clockwise`。

审核点：

- 不通过改 mesh indices 解决 front-face 问题。
- 不因为单个 fixture 异常就回退到全局 `CullMode::None`。
- 需要把结论写回本文档。

### 0.24.2 ForwardPass cull mode 接入

目标：

- `doubleSided=true` 使用 `CullMode::None`。
- `doubleSided=false` 使用 `CullMode::Back`。
- 显式设置 `rasterState.frontFace`。
- 移除或更新旧 TODO。

审核点：

- `ForwardPipelineKey` 保持包含 `doubleSided`。
- alpha mode 的 blend/depth-write 行为不变。
- descriptor layout / pipeline layout / shader 不变。

### 0.24.3 Tests

目标：

- 新增或扩展 smoke tests 覆盖 ForwardPass pipeline cull state。
- 覆盖 doubleSided true / false 两种路径。
- 覆盖至少一个 alpha blend/depth-write 回归断言。

审核点：

- 测试不依赖真实 Vulkan。
- 测试直接检查 pipeline desc，而不是靠截图。
- 不为了测试破坏 `MaterialResource` 或 `ForwardPass` 的职责边界。

### 0.24.4 验证与收尾

目标：

- 更新本文档实现状态和 front-face 结论。
- 按需同步 `docs/codex_handoff.md`。
- 记录当前仍未做 two-sided lighting 和完整透明排序。

建议运行：

```powershell
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

## 审核检查点

- `doubleSided=false` 不再使用 `CullMode::None`。
- `doubleSided=true` 仍使用 `CullMode::None`。
- `frontFace` 显式设置，并且文档说明选择原因。
- `ForwardPipelineKey::doubleSided` 与实际 raster state 对齐。
- Opaque / Mask / Blend 的 alpha pipeline 行为不回退。
- RenderQueue alpha bucket ordering 不回退。
- 不修改 shader binding / descriptor layout / pipeline layout。
- 不修改 mesh winding 或 asset indices。
- default sandbox 行为不回退。
- DamagedHelmet optional smoke 不回退。

## 验证计划

必须通过：

```powershell
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
```

smoke：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

## 当前实现状态

已完成 0.24.0 ~ 0.24.4：

- 本文档已明确 Phase 0.24 范围、非目标、front-face 风险、模块边界、测试策略和验证计划。
- 已确认本阶段 `ForwardPass` 显式使用 `FrontFace::CounterClockwise`。
- `ForwardPass` 已按 `MaterialRenderState::doubleSided` 设置 cull mode：
  - `doubleSided=false` -> `CullMode::Back`
  - `doubleSided=true` -> `CullMode::None`
- `ForwardPass` 不再把所有 material 固定为 `CullMode::None`。
- 已新增 `ark_forward_pass_pipeline_smoke`，通过 fake RHI 捕获 `GraphicsPipelineDesc`，覆盖：
  - single-sided opaque: Back culling、depth write on、blend off
  - double-sided mask: None culling、depth write on、blend off
  - single-sided blend: Back culling、depth write off、blend on
- `docs/codex_handoff.md` 已同步到 Phase 0.24 完成态。

本轮 Phase 0.24 验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_forward_pass_pipeline_smoke
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：

```text
targeted build passed
ark_forward_pass_pipeline_smoke passed
full build passed
CTest: 9/9 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
```

## 完成标准

Phase 0.24 完成时应满足：

- `doubleSided=false` material 创建 Back culling pipeline。
- `doubleSided=true` material 创建 None culling pipeline。
- `frontFace` 约定已验证并写入文档。
- tests 覆盖 doubleSided true / false 的 ForwardPass pipeline desc。
- alpha blend/depth-write、RenderQueue bucket、texture transform 路径不回退。
- build、CTest、default sandbox、DamagedHelmet optional smoke 通过。
- 文档明确仍未支持 two-sided lighting、transparent sorting、OIT、HDR、IBL、RenderGraph。

## 后续 Phase 建议

Phase 0.24 后建议进入：

1. 可配置 scene light / camera。
2. 更完整的 direct lighting BRDF。
3. HDR framebuffer / tone mapping。
4. IBL / environment map / BRDF LUT。
5. renderer 级资源 / 场景加载入口整理。
6. 基于 camera 和 bounds 的 Blend bucket back-to-front sorting。
