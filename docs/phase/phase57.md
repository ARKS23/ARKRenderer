# Phase 0.57 Transparent Sorting / Blend Bucket Back-to-Front

## 完成状态

Phase 0.57 已完成：
- `RenderQueue` 新增 camera-aware build path：`build(const RenderScene&, const glm::vec3& cameraPosition)`。
- 旧 `build(const RenderScene&)` 保持兼容。
- `DrawItem` 新增 `sortDistanceSq`。
- Blend bucket 使用 stable back-to-front sort。
- Opaque / Mask bucket 保持原有稳定提交顺序。
- `Renderer` 主路径使用 `RenderView::cameraPosition()` 构建 queue。
- `ark_frame_validation_smoke` 使用 validation camera position 构建 queue。
- `ark_render_scene_queue_smoke` 覆盖 bucket 顺序、Blend back-to-front 和同距离稳定排序。

实际验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_render_scene_queue_smoke ark_frame_validation_smoke ark_sandbox
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(render_scene_queue|frame_validation)_smoke" --output-on-failure
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

结果：

```text
targeted build passed
targeted CTest passed: 2/2
git diff --check: only line-ending warnings, no whitespace errors
full build passed
full CTest passed: 28/28
sandbox hidden-window smoke passed for default, material-ball, specular-validation, and bloom-validation --bloom --tone-mapping aces
```

## 阶段判断

Phase 0.56 已把 Bloom / ToneMapping 的视觉验证闭环补齐，当前 Forward 渲染链路已经具备：
- glTF PBR 材质加载。
- Opaque / Mask / Blend alpha mode。
- ForwardPass 根据材质 alpha mode 创建不同 pipeline state。
- RenderQueue 已按 Opaque、Mask、Blend 分桶。

当前缺口在于：Blend bucket 只被放到 Opaque / Mask 之后，但 bucket 内部仍保持提交顺序。对于透明物体，提交顺序不稳定会导致常见错误：
- 后面的透明面可能被前面的透明面错误混合。
- 同一场景中调整节点顺序会改变透明结果。
- 后续接入更复杂材质、粒子或引擎场景时，Blend path 不够可信。

因此 Phase 0.57 建议优先补齐 **透明物体 back-to-front 排序**。这是 Forward 渲染走向可用渲染器的基础正确性工作，范围小、收益明确，也不会改变默认 opaque 场景的视觉基线。

## 目标

- 保持现有 Opaque / Mask / Blend 分桶策略。
- 对 Blend bucket 按相机距离从远到近排序。
- Opaque / Mask bucket 第一版保持现有稳定提交顺序。
- `RenderQueue` 提供带相机位置的 build path。
- 保持旧的 `RenderQueue::build(const RenderScene& scene)` 兼容，避免一次性改动过大。
- 扩展 `ark_render_scene_queue_smoke`，覆盖 bucket 顺序与 Blend back-to-front 排序。
- 默认 sandbox、frame validation golden、Bloom / ToneMapping 验证不应发生变化。

## 非目标

- 不做 weighted blended OIT。
- 不做 per-pixel linked list OIT。
- 不做 depth peeling。
- 不改 ForwardPass blend state。
- 不做透明物体 depth pre-pass。
- 不做透明物体与 skybox / postprocess 的特殊合成策略。
- 不做 opaque front-to-back 性能排序。
- 不引入 mesh bounds / primitive bounds 资源结构。
- 不修改 glTF loader 或材质语义。

## 现有基础

相关代码：

```text
src/renderer/RenderQueue.h
src/renderer/RenderQueue.cpp
src/renderer/RenderScene.h
src/renderer/RenderView.h
src/renderer/passes/ForwardPass.cpp
tests/render_scene_queue_smoke.cpp
```

当前 `RenderQueue::build(const RenderScene& scene)` 行为：
- 遍历 `scene.models()` 展开 `ModelPrimitiveInstance`。
- 遍历 `scene.objects()` 生成 `DrawItem`。
- 根据 `MaterialResource::renderState().alphaMode` 分到 Opaque / Mask / Blend。
- 最终顺序为 Opaque bucket、Mask bucket、Blend bucket。
- bucket 内部保持提交顺序。

当前 `DrawItem`：

```cpp
struct DrawItem {
    MeshResource* mesh = nullptr;
    MaterialResource* material = nullptr;
    glm::mat4 modelMatrix{1.0f};
    std::string debugName;
};
```

## 推荐 API

### RenderQueue Build

建议保留旧接口：

```cpp
void build(const RenderScene& scene);
```

新增显式相机位置接口：

```cpp
void build(const RenderScene& scene, const glm::vec3& cameraPosition);
```

旧接口可调用新接口并传入 `{0.0f, 0.0f, 0.0f}`，用于保持现有测试和调用点兼容。

可选接口：

```cpp
void build(const RenderScene& scene, const RenderView& view);
```

第一版更推荐 `glm::vec3 cameraPosition`，减少 `RenderQueue.h` 对 `RenderView.h` 的依赖；调用方如果有 `RenderView`，可以传 `view.cameraPosition()`。

### DrawItem Sort Key

建议扩展：

```cpp
struct DrawItem {
    MeshResource* mesh = nullptr;
    MaterialResource* material = nullptr;
    glm::mat4 modelMatrix{1.0f};
    std::string debugName;
    float sortDistanceSq = 0.0f;
};
```

第一版 sort position 使用 `modelMatrix[3]` 的世界空间 translation：

```cpp
glm::vec3 sortPosition = glm::vec3{modelMatrix[3]};
float sortDistanceSq = glm::dot(sortPosition - cameraPosition, sortPosition - cameraPosition);
```

注意：
- 这不是严格的 mesh bounds center。
- 对小型透明物体、测试 fixture、普通 scene object 已经足够。
- 后续引入 MeshResource bounds 后，再把 sort position 改为 transformed bounds center。

### Blend Bucket Sort

排序规则：

```cpp
std::stable_sort(blendItems.begin(), blendItems.end(), [](const DrawItem& lhs, const DrawItem& rhs) {
    return lhs.sortDistanceSq > rhs.sortDistanceSq;
});
```

使用 `stable_sort` 的原因：
- 距离相等时保留提交顺序。
- 避免透明物体同距时 frame-to-frame 顺序抖动。

Opaque / Mask 不排序：
- 避免改变现有测试和默认画面。
- front-to-back opaque sorting 属于性能优化，可作为后续独立阶段。

## 调用点策略

第一版可以只改 `RenderQueue` API 和测试，不强制修改所有调用点。

如果当前主渲染路径调用的是：

```cpp
queue.build(scene);
```

建议在能拿到 `RenderView` 的位置改为：

```cpp
queue.build(scene, view.cameraPosition());
```

需要检查：

```text
src/renderer/Renderer.cpp
tests/frame_validation_smoke.cpp
tests/render_scene_queue_smoke.cpp
```

如果某些测试没有相机，继续使用旧接口即可。

## 测试计划

### 0.57.0 文档与范围确认

- 新增 `docs/phase/phase57.md`。
- 明确第一版只处理 Blend bucket back-to-front。
- 明确不做 OIT、不做 opaque front-to-back、不做 bounds center。

### 0.57.1 RenderQueue API

修改：

```text
src/renderer/RenderQueue.h
src/renderer/RenderQueue.cpp
```

目标：
- 新增 `build(const RenderScene& scene, const glm::vec3& cameraPosition)`。
- 旧 `build(const RenderScene& scene)` 保持兼容。
- `DrawItem` 增加 `sortDistanceSq`。
- 所有 draw item 在 push 时写入 sort key。

### 0.57.2 Blend Bucket Back-to-Front

修改：

```text
src/renderer/RenderQueue.cpp
```

目标：
- Blend bucket append 前执行 stable back-to-front sort。
- Opaque / Mask bucket 保持原顺序。
- sort key 使用 world translation 到 camera position 的 distance squared。

### 0.57.3 Renderer / Validation 调用点接入

检查并按需修改：

```text
src/renderer/Renderer.cpp
tests/frame_validation_smoke.cpp
```

目标：
- 主渲染路径如果有 `RenderView`，使用 camera position build queue。
- frame validation 如果手动 build queue，也传入 validation camera position。
- 没有 camera 的旧测试继续走兼容接口。

### 0.57.4 Tests

修改：

```text
tests/render_scene_queue_smoke.cpp
```

建议新增 case：
- 三个 Blend object，分别放在 near / mid / far。
- camera 在 origin。
- 验证 Blend bucket 顺序为 far、mid、near。
- 插入 Opaque / Mask object，验证整体仍为 Opaque、Mask、Blend。
- 构造两个距离相等的 Blend object，验证稳定顺序。

建议执行：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_render_scene_queue_smoke ark_frame_validation_smoke ark_sandbox
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(render_scene_queue|frame_validation)_smoke" --output-on-failure
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

### 0.57.5 验证与收尾

- 更新 `docs/phase/phase57.md` 完成状态。
- 更新 `docs/codex_handoff.md`。
- 确认 golden baseline 未被修改。
- sandbox hidden-window smoke 覆盖默认场景和一个带透明 fixture 的最小路径；如果当前没有透明 fixture，默认场景 smoke 即可。
- 提交并推送。

## 完成标准

- `RenderQueue` 支持 camera-aware build。
- Blend bucket 按 back-to-front 排序。
- Opaque / Mask 仍在 Blend 前面。
- Opaque / Mask bucket 内部稳定顺序不变。
- Blend 同距离 item 保持稳定顺序。
- `ark_render_scene_queue_smoke` 覆盖排序行为。
- `ark_frame_validation_smoke` 通过。
- full build 和 full CTest 通过。
- 默认 sandbox smoke 通过。

## 风险与注意事项

- 使用 model translation 作为 sort position 对大型透明 mesh 不完全准确，但足够作为第一版。
- 透明排序仍不能解决交叉透明面、同 mesh 内部三角形排序、多个透明层交叠等 OIT 问题。
- 如果透明物体写 depth 或 depth test 策略不合理，排序也无法完全修复；本阶段不改 pipeline state。
- 如果旧接口默认 camera position 为 origin，使用旧接口的测试中 Blend 排序可能从“提交顺序”变为“按 origin 距离排序”。需要让旧接口是否排序保持明确：
  - 推荐旧接口仍调用新接口 `{0,0,0}`，并同步更新测试预期。
  - 如需完全保持旧行为，可以新增内部 flag，但第一版不建议增加复杂度。

## 后续方向

Phase 0.57 完成后建议：

1. Phase 0.58：Directional Shadow Map Foundation。
2. Phase 0.59：`KHR_materials_emissive_strength`。
3. Phase 0.60：Renderer Public API / Engine Integration Boundary。
4. 后续透明专项：mesh bounds center、transparent depth pre-pass、weighted blended OIT 或 per-material sorting policy。
