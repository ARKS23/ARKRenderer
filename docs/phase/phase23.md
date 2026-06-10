# Phase 0.23 RenderQueue Alpha 分桶

## 阶段判断

Phase 0.20 已经完成 glTF `alphaMode` / `alphaCutoff` / `doubleSided` 到 ForwardPass pipeline variant 的最小闭环：

```text
glTF material alphaMode
    -> asset::MaterialData
    -> MaterialResource::renderState()
    -> ForwardPass pipeline key
    -> Blend material depth write off + alpha blending
    -> Mask material alpha cutoff discard
```

Phase 0.21 和 Phase 0.22 又补齐了 per-slot UV selection 与 `KHR_texture_transform`。因此当前材质采样路径已经足够完整，下一处会直接影响真实画面正确性的缺口是 draw ordering。

目前 `RenderQueue` 只按 scene / model insertion order 展平 draw items，`ForwardPass` 也按 queue 顺序绘制。Blend material 虽然已经关闭 depth write，但如果它排在 opaque / mask draw 之前，或者多个 blend draw 顺序不合适，画面仍可能出现明显透明错误。

Phase 0.23 的重点是补齐最小 alpha 分桶：

```text
RenderScene
    -> RenderQueue::build()
        -> opaque / mask bucket
        -> blend bucket
    -> ForwardPass 按 queue 顺序绘制
```

目标不是完整透明排序，而是先保证 opaque / mask 总是在 blend 前绘制，为后续 back-to-front sorting 留出结构。

## 目标

Phase 0.23 目标：

- `RenderQueue` 能按 material alpha mode 对 draw item 分桶。
- Opaque 与 Mask draw items 排在 Blend draw items 前。
- 每个 bucket 内保持原 scene / model insertion order，避免引入不必要的排序抖动。
- `ForwardPass` 不需要理解 bucket，只继续按 `RenderQueue::drawItems()` 顺序绘制。
- 保持 Phase 0.20 的 blend/depth-write 行为不回退。
- 保持 Phase 0.21 / 0.22 的 UV selection 与 texture transform 路径不回退。
- 新增 smoke tests 覆盖 Opaque / Mask / Blend 分桶顺序。
- 文档明确仍未做完整 back-to-front transparent sorting、OIT 或 per-pixel transparency。

## 非目标

Phase 0.23 暂不做：

- 不做完整透明排序。
- 不做 order-independent transparency。
- 不做 weighted blended OIT。
- 不做 depth peeling。
- 不做每个 blend draw 的 back-to-front 排序。
- 不做按 mesh bounds / camera distance 的精确排序。
- 不做多 pass transparent resolve。
- 不做 HDR framebuffer / tone mapping。
- 不做 IBL / environment map / BRDF LUT。
- 不做 RenderGraph 重构。
- 不做 descriptor / pipeline / shader layout 改动。
- 不做 doubleSided culling 精确化；这应进入独立 phase。

## 模块边界

继续遵守现有设计文档：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase20.md
docs/phase/phase21.md
docs/phase/phase22.md
```

边界要求：

- `asset/` 不参与 draw ordering。
- `MaterialResource` 只暴露 material render state，不负责排序。
- `RenderScene` 仍只保存 scene 语义，不做 draw ordering。
- `RenderQueue` 负责把 scene 展平成 draw list，并执行本阶段的 alpha bucket ordering。
- `ForwardPass` 只消费 queue，不参与排序策略。
- RHI / Vulkan 不需要知道 alpha bucket。
- `FrameRenderer` pass 顺序不变，仍由手动调度执行 `ClearPass -> ForwardPass`。

## 当前行为

当前 `RenderQueue::build()` 行为可以概括为：

```cpp
for scene model:
    append all model primitive instances

for scene object:
    append object
```

这有两个特点：

- scene/model/object 的插入顺序会直接成为 draw 顺序。
- material alpha mode 不影响 draw 顺序。

`ForwardPass` 当前行为：

```cpp
for item in queue.drawItems():
    update object/material uniform
    getOrCreatePipeline(item.material.renderState())
    drawIndexed()
```

因此 alpha 分桶最自然的落点是 `RenderQueue`，而不是 `ForwardPass`。

## 建议设计

新增一个 queue 内部分类概念：

```cpp
enum class DrawBucket {
    Opaque,
    Mask,
    Blend,
};
```

分类规则：

```text
AlphaMode::Opaque -> Opaque bucket
AlphaMode::Mask   -> Mask bucket
AlphaMode::Blend  -> Blend bucket
```

输出顺序建议：

```text
Opaque bucket
Mask bucket
Blend bucket
```

原因：

- Opaque 与 Mask 都保持 depth write on，可以先写深度。
- Blend 已经 depth write off，应在 opaque/mask 后绘制。
- Mask 使用 discard，但输出仍是非混合不透明片段，放在 opaque 后或与 opaque 同组都可接受。
- 为测试和调试清晰，建议将 Mask 单独 bucket，但它仍排在 Blend 前。

稳定性要求：

- bucket 内不重新排序。
- 同一 model 的 primitive instances 在相同 bucket 内保持原遍历顺序。
- scene objects 在相同 bucket 内保持原插入顺序。

## 数据结构策略

最小实现可以在 `RenderQueue::build()` 内使用临时 vector：

```cpp
std::vector<DrawItem> opaqueItems;
std::vector<DrawItem> maskItems;
std::vector<DrawItem> blendItems;
```

遍历 scene 时不直接写入 `m_DrawItems`，而是 append 到对应 bucket。遍历结束后：

```cpp
m_DrawItems.clear();
append(opaqueItems);
append(maskItems);
append(blendItems);
```

优点：

- 不改变 `DrawItem` public shape。
- `ForwardPass` 零改动或最小改动。
- tests 可以直接验证 `drawItems()` 顺序。

可选增强：

- `DrawItem` 增加 `asset::AlphaMode alphaMode` 仅用于 debug / test。
- `RenderQueue` 提供 bucket range 查询，例如 `opaqueRange()` / `blendRange()`。

建议 Phase 0.23 先不做可选增强，除非测试明显需要。

## 透明排序策略

Phase 0.23 只做粗分桶，不做完整排序。

Blend bucket 内继续保持 insertion order。这意味着：

- 单个透明物体在很多简单场景中会比当前更稳，因为它至少位于 opaque/mask 后。
- 多个透明物体之间仍可能排序错误。
- 同一个透明 mesh 内部三角形顺序仍不会被修正。

这些限制应写入文档与 handoff，避免把 alpha bucket 误称为完整 transparent sorting。

后续真正 transparent sorting 可以基于本阶段结构继续扩展：

```text
Blend bucket
    -> compute sort key from camera position and draw bounds
    -> stable back-to-front sort
```

但这需要 bounds、camera position 和更明确的 sort policy，不应塞进 Phase 0.23。

## 测试策略

优先扩展：

```text
tests/render_scene_queue_smoke.cpp
```

建议新增测试：

- 创建 Opaque / Mask / Blend 三种 `MaterialResource`。
- 构造 scene insertion order 为：Blend、Opaque、Mask、Blend、Opaque。
- `RenderQueue::build()` 后期望顺序为：Opaque、Opaque、Mask、Blend、Blend。
- 验证每个 bucket 内保持原相对顺序。
- 验证 model primitive instances 和 scene objects 都能进入分桶逻辑。
- 验证 empty scene 仍清空旧 draw items。

如果 `MaterialResource::create()` 需要完整 texture set 才能产生 render state，可以选择：

- 继续使用 `MaterialResource::create()` + fallback textures 的现有 fake device 路径。
- 或在测试中构建最小可用 `MaterialResource` helper，复用 `model_resource_smoke` 的 fake device / texture cache 模式。

不要为了测试而给 `MaterialResource` 暴露 setter。

## 实施顺序

### 0.23.0 文档与范围确认

目标：

- 新增 `docs/phase/phase23.md`。
- 明确主线是 `RenderQueue` alpha 分桶。
- 明确不做完整 transparent sorting / OIT / HDR / IBL / RenderGraph。

审核点：

- 不重复 Phase 0.20 alpha pipeline 工作。
- 不修改 `ForwardPass` pipeline state。
- 不把排序策略放进 asset/RHI。

### 0.23.1 RenderQueue bucket 分类

目标：

- `RenderQueue::build()` 按 material alpha mode 分类。
- Opaque / Mask 排在 Blend 前。
- bucket 内保持稳定 insertion order。

审核点：

- `DrawItem::isDrawable()` 语义不变。
- 空 scene build 后仍清空旧 queue。
- invalid model primitive / object 仍跳过。

### 0.23.2 Tests

目标：

- 扩展 `render_scene_queue_smoke`。
- 覆盖 Opaque / Mask / Blend 分桶顺序。
- 覆盖 bucket 内稳定顺序。

审核点：

- 测试不依赖真实 Vulkan。
- 不引入真实图片资产读取作为 queue test 前置条件。

### 0.23.3 验证与收尾

目标：

- 更新本文档实现状态。
- 记录当前仍非完整透明排序。
- 按需同步 `docs/codex_handoff.md`。

建议运行：

```powershell
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

## 审核检查点

- `RenderQueue` 是唯一新增排序策略落点。
- `ForwardPass` 仍只按 queue 顺序绘制。
- Opaque / Mask 在 Blend 前。
- Blend bucket 内未声称完整排序。
- bucket 内顺序稳定。
- alphaMode 仍来自 `MaterialResource::renderState()`。
- `MaterialResource` 不新增仅用于测试的 public setter。
- descriptor layout / pipeline layout / shader 不变。
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

已完成 0.23.0 ~ 0.23.3：

- 本文档已明确 Phase 0.23 范围、非目标、模块边界、测试策略与验证计划。
- `RenderQueue::build()` 已按 material alpha mode 将 draw item 分入 Opaque / Mask / Blend bucket。
- 输出顺序为 Opaque、Mask、Blend；每个 bucket 内保持原 scene / model traversal order。
- `ForwardPass`、descriptor layout、pipeline layout、shader 均未修改，仍只消费 `RenderQueue::drawItems()`。
- `render_scene_queue_smoke` 已覆盖 scene object 的 Blend、Opaque、Mask、Blend、Opaque 输入顺序，并验证输出为 Opaque、Opaque、Mask、Blend、Blend。
- `model_resource_smoke` 已覆盖 model primitive instance 展开后的 alpha bucket ordering，并验证 bucket 内稳定顺序。
- default sandbox / DamagedHelmet optional smoke 已重新运行。
- `docs/codex_handoff.md` 已同步到 Phase 0.23 完成态。

本轮 0.23 验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_render_scene_queue_smoke ark_model_resource_smoke
build/msvc-vcpkg/Debug/ark_render_scene_queue_smoke.exe
build/msvc-vcpkg/Debug/ark_model_resource_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：

```text
targeted build passed
ark_render_scene_queue_smoke passed
ark_model_resource_smoke passed
full build passed
CTest: 8/8 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
```

## 完成标准

Phase 0.23 完成时应满足：

- `RenderQueue` 按 material alpha mode 生成稳定 draw order。
- Opaque 和 Mask draw items 总是在 Blend draw items 前。
- Blend draw items bucket 内保持原相对顺序，并明确不是完整透明排序。
- `ForwardPass` 无需关心 alpha bucket，只消费 queue。
- tests 覆盖 queue 分桶与稳定顺序。
- build、CTest、default sandbox、DamagedHelmet optional smoke 通过。
- 文档明确仍未支持 OIT、depth peeling、weighted blended transparency、camera-distance back-to-front sorting、HDR、IBL、RenderGraph。

## 后续 Phase 建议

Phase 0.23 后建议进入：

1. doubleSided culling 精确化。
2. 可配置 scene light / camera。
3. 更完整的 direct lighting BRDF。
4. HDR framebuffer / tone mapping。
5. IBL / environment map / BRDF LUT。
6. renderer 级资源 / 场景加载入口整理。
7. 基于 camera 和 bounds 的 Blend bucket back-to-front sorting。
