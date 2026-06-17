# Phase 0.61 Scene Bounds / Shadow Bounds Fitting Foundation

## 阶段判断

Phase 0.60 已经让 Sponza 的 KTX1 diffuse/baseColor 贴图进入真实 texture resource path。默认 sandbox 现在具备较完整的综合验证画面：Sponza + DamagedHelmet、Shadow、Bloom、ACES ToneMapping、IBL 和 KTX 贴图恢复都能在同一场景中观察。

当前最影响后续推进的是：场景没有统一 bounds 数据流，ShadowPass 仍使用固定的 `ShadowSettings::orthographicHalfExtent`、`farPlane` 和 `lightDistance`。这对 Sponza 这种大场景很脆：bounds 太小会裁掉投影，bounds 太大会降低阴影分辨率；每次调整模型 scale、相机或默认场景时，都要手调 shadow 参数。

Phase 0.61 建议先补齐最小 scene bounds foundation，并让 directional shadow map 可以基于 scene bounds 自动拟合正交投影盒。这个阶段不是做 CSM，而是让当前单张 shadow map 不再完全依赖硬编码参数。

## 目标

- 增加可复用的 AABB / bounds 数据结构。
- 为 mesh primitive 计算 local bounds。
- 让 `MeshResource` / `ModelResource` 暴露 bounds。
- 让 `RenderScene` 能汇总 world-space scene bounds。
- `ShadowPass` 根据 scene bounds 和主光方向生成更合适的 light view-projection。
- 保留现有手动 shadow 参数和 CLI override，不破坏调试入口。
- 为后续 camera framing、transparent sorting bounds center、frustum culling、CSM 和引擎接入打基础。
- 增加 smoke tests，覆盖 bounds 计算、模型实例 transform、组合场景 world bounds 和 shadow fitting。

## 非目标

- 不做 Cascaded Shadow Maps。
- 不做 PCF/PCSS/VSM/ESM/EVSM 等 shadow filtering。
- 不做 frustum culling 或 occlusion culling。
- 不做 automatic camera framing。
- 不做 RenderGraph。
- 不做 runtime debug UI。
- 不更新 screenshot/golden baseline，除非后续单独推进默认组合场景 baseline。
- 不重构 `RenderQueue` 的提交结构；本阶段只补 bounds 数据和 ShadowPass 使用点。

## 当前基础

相关代码：

```text
src/asset/MeshData.h
src/renderer/MeshResource.h
src/renderer/MeshResource.cpp
src/renderer/ModelResource.h
src/renderer/ModelResource.cpp
src/renderer/RenderScene.h
src/renderer/RenderScene.cpp
src/renderer/RenderQueue.h
src/renderer/RenderQueue.cpp
src/renderer/RenderView.h
src/renderer/passes/ShadowPass.h
src/renderer/passes/ShadowPass.cpp
src/app/SandboxLaunchOptions.h
src/app/SandboxLaunchOptions.cpp
tests/model_resource_smoke.cpp
tests/render_scene_queue_smoke.cpp
tests/scene_resource_smoke.cpp
tests/shadow_pass_smoke.cpp
tests/renderer_preset_smoke.cpp
tests/framework_headers_smoke.cpp
```

当前状态：

- `MeshPrimitiveData` 有 vertex positions，但没有记录 local bounds。
- `MeshResource` 创建 GPU buffer，但不暴露 mesh bounds。
- `ModelResource` 保存 primitive instances 和 local transforms，但不计算 model bounds。
- `RenderScene` 保存 model/object 列表，但不汇总 scene bounds。
- `ShadowPass::makeLightViewProjection()` 以 world origin 为 light target，并使用固定 `orthographicHalfExtent`。
- sandbox 已提供 `--shadow-bounds`、`--shadow-strength`、`--shadow-extent` 等手动参数。
- `ShadowSettings` 已有 sanitize/clamp 逻辑，可继续作为 manual override 和 fallback 参数。

## 推荐方案

### Bounds 类型

建议新增轻量 bounds 类型，位置可以是：

```text
src/renderer/Bounds.h
```

或如果后续希望 asset / renderer 共用，也可以放到：

```text
src/core/Bounds.h
```

推荐第一版放在 `renderer` 层，避免 asset 层过早引入 glm 语义；如果 `MeshData` 需要保存 bounds，则可使用简单 float array 或在 renderer create 时计算。

建议 API：

```cpp
struct Bounds3 {
    glm::vec3 min;
    glm::vec3 max;
    bool valid = false;

    glm::vec3 center() const;
    glm::vec3 extent() const;
    glm::vec3 halfExtent() const;
};

Bounds3 makeInvalidBounds();
Bounds3 makeBoundsFromPoint(glm::vec3 point);
void expandBounds(Bounds3& bounds, glm::vec3 point);
void mergeBounds(Bounds3& bounds, const Bounds3& other);
Bounds3 transformBounds(const Bounds3& bounds, const glm::mat4& transform);
```

注意事项：

- invalid bounds 不能参与 shadow fitting。
- transform AABB 时应变换 8 个 corner，再重新取 min/max。
- 对非 uniform scale、rotation、negative scale 都要正确。
- 空 mesh / 无 vertex 的 primitive 应得到 invalid bounds。

### Mesh / Model Bounds 数据流

建议顺序：

1. `MeshResource::create()` 从 `asset::MeshPrimitiveData::vertices.position` 计算 local bounds。
2. `ModelResource::create()` 合并 primitive mesh bounds 和 primitive instance local transform，得到 model local bounds。
3. `ModelResource` 暴露：

```cpp
const Bounds3& localBounds() const;
```

4. `RenderScene` 对每个 `SceneModel` 使用 `transformBounds(model.localBounds(), sceneModel.transform)` 合并 scene bounds。
5. `RenderScene` 对每个 standalone `SceneObject` 使用 `mesh.localBounds()` + object transform 合并。
6. `RenderScene` 暴露：

```cpp
Bounds3 bounds() const;
bool hasBounds() const;
```

为了避免每帧重复算大量 bounds，第一版可以在 `RenderScene::addModel()` / `addObject()` 时增量更新 `m_Bounds`。`clear()` 时重置。如果后续支持 runtime transform mutation，再改为 dirty/rebuild 模式。

### Shadow Bounds Fitting

`ShadowPass` 当前 light target 固定为 origin。建议改为：

- 如果 `frameContext.scene` 有 valid bounds：
  - light target = scene bounds center。
  - 计算 scene bounds 8 个 corners 在 light view 空间下的 min/max。
  - ortho left/right/bottom/top 使用 light-space min/max，并加 padding。
  - near/far 使用 light-space depth range，并加 padding。
  - light position 可沿 `-lightDirection` 放到 bounds 外侧。
- 如果 scene bounds invalid：
  - 保持现有 fixed settings 路径。

第一版不必把 shadow map texel snapping 做进去，但可以在文档中保留后续项。为了避免极端 bounds 让 shadow 参数失控，仍应经过 clamp：

- 最小 half extent 不低于 `ShadowSettings::orthographicHalfExtent` 或一个小阈值。
- far plane 不低于 `ShadowSettings::farPlane` 中的合理下限。
- padding 使用 bounds diagonal 或 fixed margin。
- 保留 `ShadowSettings::nearPlane` 下限。

### Manual Override 策略

需要避免“自动 fitting 接入后，用户手动调参失效”。建议 Phase 0.61 增加一个明确开关：

```cpp
enum class ShadowBoundsMode {
    Manual,
    FitScene,
};
```

或在 `ShadowSettings` 中加入：

```cpp
bool fitSceneBounds = true;
```

推荐最小方案：

- 默认 sandbox / renderer preset 使用 `fitSceneBounds = true`。
- 用户显式传入 `--shadow-bounds` 时，保留 manual behavior，可把 `fitSceneBounds` 关掉。
- `shadow-validation` preset 可以继续开启 fitting，但保留最低参数作为 fallback。

如果不想增加 CLI 复杂度，也可以第一版只在内部自动使用 scene bounds，同时让 `orthographicHalfExtent` 作为 min clamp。但这样用户显式 `--shadow-bounds` 的语义会变得不够直观。更推荐显式 mode。

## 分阶段任务

### 0.61.0 文档与范围确认

- 新增 `docs/phase/phase61.md`。
- 明确本阶段只做 scene bounds + single directional shadow fitting。
- 明确不做 CSM、culling、screenshot baseline 或 RenderGraph。

### 0.61.1 Bounds 数据结构

已完成最小 `Bounds3` 基础结构和 smoke test：

- 新增 `src/renderer/Bounds.h`。
- 支持 invalid / point / expand / merge / transform helpers。
- `transformBounds()` 通过 8 个 corner 重新合并，能正确处理 rotation、non-uniform scale 和 negative scale。
- 新增 `tests/bounds_smoke.cpp` 覆盖基础行为。

下一步再把 `Bounds3` 接入 `MeshResource`、`ModelResource` 和 `RenderScene`。

修改：

```text
src/renderer/Bounds.h
tests/render_scene_queue_smoke.cpp 或新增 tests/bounds_smoke.cpp
```

目标：

- 提供 invalid / point / expand / merge / transform helpers。
- 覆盖 translation、scale、rotation、negative scale 和 invalid bounds。

### 0.61.2 MeshResource / ModelResource Bounds

已完成：

- `MeshResource` 在 `create()` 阶段从 `asset::MeshPrimitiveData::vertices.position` 计算 primitive local AABB。
- `MeshResource::localBounds()` 暴露 mesh local bounds，并在 deferred release / immediate reset 后恢复 invalid bounds。
- `ModelResource` 按实际 `ModelPrimitiveInstance` 合并 primitive mesh bounds 和 instance local transform，得到 model local bounds。
- `ModelResource::localBounds()` 暴露 model local bounds，供后续 `RenderScene` world bounds 与 `ShadowPass` fitting 使用。
- `tests/model_resource_smoke.cpp` 覆盖单 mesh bounds、多 primitive mesh bounds、多 instance transform bounds。
- `tests/framework_headers_smoke.cpp` 纳入 `renderer/Bounds.h` 公共头编译覆盖。

修改：

```text
src/renderer/MeshResource.h
src/renderer/MeshResource.cpp
src/renderer/ModelResource.h
src/renderer/ModelResource.cpp
tests/model_resource_smoke.cpp
```

目标：

- MeshResource 保存 local bounds。
- ModelResource 按 primitive instance transform 合并 model local bounds。
- 测试覆盖单 mesh、多 mesh、多 instance、non-uniform scale 和空输入。

### 0.61.3 RenderScene World Bounds

已完成：

- `RenderScene` 增加 world-space `Bounds3` 聚合状态。
- `RenderScene::bounds()` / `RenderScene::hasBounds()` 暴露当前 scene world bounds。
- `addModel()` 使用 `transformBounds(model.localBounds(), sceneModel.transform)` 合并 model world bounds。
- `addObject()` 使用 `transformBounds(mesh.localBounds(), object.transform)` 合并 standalone mesh object world bounds。
- invalid resource bounds 会被忽略，保证未创建资源的旧 queue smoke path 不会产生错误 scene bounds。
- `clear()` 会清空 models / objects，并把 scene bounds 恢复为 invalid；environment policy 仍沿用原行为。
- `tests/render_scene_queue_smoke.cpp` 覆盖 invalid resource bounds 和 clear reset。
- `tests/scene_resource_smoke.cpp` 覆盖 standalone object world bounds 以及 Sponza + DamagedHelmet composite model bounds。

修改：

```text
src/renderer/RenderScene.h
src/renderer/RenderScene.cpp
tests/render_scene_queue_smoke.cpp
tests/scene_resource_smoke.cpp
```

目标：

- `RenderScene` 增量维护 world bounds。
- `addModel()` 和 `addObject()` 都能进入 scene bounds。
- `clear()` 后 bounds invalid。
- Sponza + DamagedHelmet composite scene 能得到 valid large-scene bounds。

### 0.61.4 ShadowPass FitScene Path

已完成：

- `ShadowSettings` 增加 `fitSceneBounds`，默认开启。
- `ShadowPass` 在 `fitSceneBounds == true` 且 `RenderScene::hasBounds()` 时，根据 scene world bounds 计算 light view-projection。
- scene fitting 会把 scene bounds 的 8 个角点投到 light-space，用 light-space min/max 生成 ortho projection。
- `orthographicHalfExtent`、`nearPlane`、`farPlane` 和 `lightDistance` 仍作为 fallback / 最小 clamp 使用。
- scene bounds invalid 或 `fitSceneBounds == false` 时保留旧的固定 origin + manual half extent path。
- sandbox 显式传入 `--shadow-bounds` / `--shadow-bounds=` 时会关闭 `fitSceneBounds`，保持手动 shadow bounds 行为可预测。
- `tests/shadow_pass_smoke.cpp` 覆盖 scene bounds fitting 与 manual fallback。
- `tests/framework_headers_smoke.cpp` / `tests/renderer_preset_smoke.cpp` 覆盖 sandbox 默认开启 fitting、手动 `--shadow-bounds` 关闭 fitting。

修改：

```text
src/renderer/RenderView.h
src/renderer/passes/ShadowPass.cpp
src/app/SandboxLaunchOptions.h
src/app/SandboxLaunchOptions.cpp
tests/shadow_pass_smoke.cpp
tests/renderer_preset_smoke.cpp
tests/framework_headers_smoke.cpp
```

目标：

- `ShadowSettings` 增加 bounds fitting mode 或开关。
- `ShadowPass` 根据 scene bounds 计算 light view-projection。
- scene bounds invalid 时保持旧 fixed 参数 fallback。
- CLI 显式 `--shadow-bounds` 时 manual behavior 可预测。
- 测试验证 fitted matrix 与 scene bounds 变化相关，而不是始终固定 origin / fixed half extent。

### 0.61.5 Tests

已完成：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_model_resource_smoke ark_render_scene_queue_smoke ark_scene_resource_smoke ark_shadow_pass_smoke ark_renderer_preset_smoke ark_framework_headers_smoke ark_bounds_smoke
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(model_resource|render_scene_queue|scene_resource|shadow_pass|renderer_preset|framework_headers|bounds)_smoke" --output-on-failure
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

结果：

- targeted build 通过。
- targeted CTest 通过：7/7。
- full Debug build 通过。
- full CTest 通过：30/30。
- `git diff --check` 通过，仅有 Windows CRLF 行尾提示。

Sandbox hidden-window smoke 已完成：

- default sandbox 启动成功。
- `--preset shadow-validation` 启动成功。
- `--preset sponza --shadow-bounds 64 --shadow-strength=1.0` 启动成功。

建议执行：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_model_resource_smoke ark_render_scene_queue_smoke ark_scene_resource_smoke ark_shadow_pass_smoke ark_renderer_preset_smoke ark_framework_headers_smoke ark_sandbox
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(model_resource|render_scene_queue|scene_resource|shadow_pass|renderer_preset|framework_headers)_smoke" --output-on-failure
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

Sandbox smoke：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset sponza
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset shadow-validation
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset sponza --shadow-bounds 64 --shadow-strength=1.0
```

### 0.61.6 验证与收尾

已完成：

- `docs/phase/phase61.md` 已同步完成状态和验证结果。
- `docs/codex_handoff.md` 已同步到 Phase 0.61。
- `README.md` 已补充默认 shadow 自动 scene bounds fitting 与 `--shadow-bounds` 手动覆盖关系。
- 已记录手动 shadow bounds 与 automatic fitting 的关系：默认自动 fitting；显式 `--shadow-bounds` 关闭 fitting 并使用 manual half extent path。
- 本轮未提交/推送；等待用户明确要求。

- 更新 `docs/phase/phase61.md` 完成状态。
- 更新 `docs/codex_handoff.md`。
- 如 README 中有 shadow / sandbox 说明变化，同步更新。
- 记录手动 shadow bounds 与 automatic fitting 的关系。
- 提交并推送。

## 完成标准

- `MeshResource` / `ModelResource` / `RenderScene` 均能暴露 valid bounds。
- 默认 Sponza + DamagedHelmet 组合场景能得到合理 world bounds。
- ShadowPass 在 scene bounds valid 时不再固定以 origin 为目标，而是以 scene bounds 生成 light projection。
- 手动 `--shadow-bounds` 行为可预测，不被自动 fitting 悄悄吞掉。
- 旧 shadow fallback path 仍可用。
- targeted smoke tests 通过。
- full build / full CTest 通过。
- sandbox default、sponza、shadow-validation hidden-window smoke 启动成功。

## 风险与注意事项

- AABB transform 必须按 8 个 corner 重新计算，不能只 transform min/max。
- 如果 bounds 包含很远的离群物体，single shadow map 会被拉大并降低近处阴影质量；这是 CSM 或 shadow focus 策略要解决的问题，不在本阶段一次完成。
- fitting 后 shadow matrix 变化会影响视觉输出，因此不应在本阶段顺手更新 golden baseline。
- 如果 CLI manual override 语义不清，会让调试很痛苦。建议尽早明确 manual / fitScene 的优先级。
- 当前透明物体是否投射阴影不是本阶段重点，仍沿用现有 RenderQueue / ShadowPass 行为。
- 后续若做 runtime scene mutation，需要把 `RenderScene` bounds 从增量维护升级为 dirty rebuild。

## 后续方向

Phase 0.61 完成后建议：

1. Phase 0.62：默认组合场景 screenshot baseline，锁住 Sponza KTX、Shadow、Bloom、ToneMapping 和 IBL 的最终画面。
2. Phase 0.63：Renderer public scene/resource API 收口，为后续接入引擎整理边界。
3. Phase 0.64：Shadow quality pass，加入 texel snapping、PCF quality preset 或 CSM prelude。
4. Phase 0.65：KTX2 / BasisU / 原始 mip chain 支持。
