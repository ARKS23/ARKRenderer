# Phase 0.73 Renderer Structure Hygiene / Engine Integration Preparation

## 阶段背景

Phase 0.68 已经把具体效果模块整理到了 `renderer/effects/*`，Phase 0.69 也明确了 `Renderer` / `RenderScene` / `RenderView` 是未来引擎接入时的核心 public facade。

当前渲染器功能已经足够支撑 PBR、IBL、CSM、Bloom、ToneMapping、SSAO、Sandbox UI 和复杂场景验证。接下来如果继续推进自研游戏引擎接入，主要风险不再是单个渲染效果，而是代码结构和接口边界不够清爽：

- `src/renderer` 根目录同时放了 public facade、frame internal、resource contract、sample preset、settings 和几何工具。
- 新成员或未来引擎层很难一眼判断哪些头文件可以依赖，哪些只是 renderer internal。
- `FrameContext`、`FrameRenderer`、`RenderQueue` 等 internal 类型和 `Renderer`、`RenderScene`、`RenderView` 等 facade 混在同一层。
- `SceneResource`、`RendererPreset` 更偏 sandbox/sample，但目前也放在 renderer 根目录，容易被误认为正式引擎接入路径。

本阶段目标是做一次低风险结构整理，为后续 Resource Handle、Render Object、GPU Instancing 和 Engine Adapter 铺路。

## 当前判断

`effects/*` 目录现在比较清楚，暂时不需要大改：

```text
src/renderer/effects/
  bloom/
  ibl/
  shadow/
  sky/
  ssao/
  tone_mapping/
```

真正需要整理的是 `src/renderer` 根目录。整理方向不是重写架构，而是把“职责层级”显式表达出来。

## 目标

1. 明确 renderer public facade、renderer internal、resource contract、settings contract、sample/preset 的目录边界。
2. 保持 `Renderer.h`、`RenderScene.h`、`RenderView.h` 作为未来引擎接入的核心入口。
3. 将明显 internal 的帧调度、队列、pass 基类、基础几何工具移动到 `renderer/core`。
4. 将模型、网格、纹理、环境等资源类型移动到 `renderer/resources`。
5. 将后处理、阴影、质量等数据 contract 移动到 `renderer/settings`。
6. 将 sample/sandbox 辅助类型移动到更明确的 `renderer/scene` / `renderer/presets`。
7. 更新 include、CMake、tests 和文档，确保运行时行为不变。
8. 移除同名 compatibility wrapper，避免根目录出现真假入口混杂。

## 非目标

1. 不改变渲染行为。
2. 不改 shader binding / descriptor layout / GPU data layout。
3. 不做 GPU instancing。
4. 不引入 ResourceHandle / RenderObjectHandle。
5. 不重写 RenderGraph / FrameGraph。
6. 不移动 shader 文件。
7. 不改 RHI 后端结构。
8. 不把 `effects/*` 暴露为 public API。
9. 不让引擎层直接依赖 `FrameRenderer`、`FrameContext`、`RenderQueue`、`passes/*` 或 `effects/*`。

## 推荐目录结构

本阶段目标结构：

```text
src/renderer/
  Renderer.h
  Renderer.cpp
  RenderScene.h
  RenderScene.cpp
  RenderView.h
  RenderView.cpp

  core/
    Bounds.h
    Frustum.h
    Frustum.cpp
    FrameContext.h
    FrameOverlay.h
    FrameRenderer.h
    FrameRenderer.cpp
    RenderGraph.h
    RenderPass.h
    RenderPass.cpp
    RenderQueue.h
    RenderQueue.cpp

  resources/
    EnvironmentBrdfLutResource.h
    EnvironmentBrdfLutResource.cpp
    EnvironmentCubeResource.h
    EnvironmentCubeResource.cpp
    EnvironmentResource.h
    EnvironmentResource.cpp
    MeshResource.h
    MeshResource.cpp
    ModelResource.h
    ModelResource.cpp
    TextureCache.h
    TextureCache.cpp
    TextureResource.h
    TextureResource.cpp

  material/
    Material.h
    MaterialResource.h
    MaterialResource.cpp
    MaterialSystem.h

  settings/
    PostProcessingSettings.h
    PostProcessingSettings.cpp
    RendererQuality.h
    RendererQuality.cpp
    ShadowConstants.h
    ShadowDebugSettings.h

  scene/
    SceneResource.h
    SceneResource.cpp

  presets/
    RendererPreset.h
    RendererPreset.cpp

  passes/
    ClearPass.h
    ClearPass.cpp
    CubePass.h
    CubePass.cpp
    ForwardPass.h
    ForwardPass.cpp
    ImGuiPass.h
    TrianglePass.h
    TrianglePass.cpp

  effects/
    bloom/
    ibl/
    shadow/
    sky/
    ssao/
    tone_mapping/
```

## Public / Internal 边界

## Header Inventory / Ownership Map

本阶段整理后，`src/renderer` 根目录只保留 public facade：

| Header | Ownership | 说明 |
| --- | --- | --- |
| `Renderer.h` | Public facade | renderer 生命周期、resize、render 主入口 |
| `RenderScene.h` | Public facade | renderer-facing scene submission 容器 |
| `RenderView.h` | Public facade | per-frame camera、projection、postprocess、shadow、visibility 参数集合 |

其余头文件必须使用分层后的真实路径：

| Ownership | Include 路径 |
| --- | --- |
| Renderer internal | `renderer/core/*` |
| Resource contract | `renderer/resources/*` |
| Settings contract | `renderer/settings/*` |
| Sample scene helper | `renderer/scene/*` |
| Sample preset helper | `renderer/presets/*` |

本阶段不保留根目录同名 wrapper。项目尚未形成稳定外部 SDK，直接切换到清晰路径比“短期兼容旧 include”更符合后续引擎接入目标。

### Public facade

保持在 `src/renderer` 根目录：

```text
renderer/Renderer.h
renderer/RenderScene.h
renderer/RenderView.h
```

职责：

- `Renderer`：renderer 生命周期、resize、render 的主入口。
- `RenderScene`：renderer-facing scene submission，不等于 Engine Scene / ECS World。
- `RenderView`：per-frame camera、projection、postprocess、shadow、visibility 参数集合。

未来引擎优先只依赖这三类。

### Resource contract

移动到 `renderer/resources`：

```text
renderer/resources/ModelResource.h
renderer/resources/MeshResource.h
renderer/resources/TextureResource.h
renderer/resources/EnvironmentResource.h
```

这些类型仍然可以被引擎资产系统 adapter 使用，但不应该承担完整 Asset System 的职责。

不保留 `renderer/ModelResource.h` 这类根目录 wrapper；新代码必须显式 include `renderer/resources/ModelResource.h`。

### Renderer internal

移动到 `renderer/core`：

```text
FrameRenderer
FrameContext
RenderQueue
RenderPass
Bounds
Frustum
RenderGraph
FrameOverlay
```

这些类型可以被 renderer 内部、tests 和调试工具使用，但不建议引擎层直接依赖。

### Settings contract

移动到 `renderer/settings`：

```text
PostProcessingSettings
RendererQuality
ShadowConstants
ShadowDebugSettings
```

这些是可稳定暴露给 `RenderView` / `RendererQualityDesc` 的数据 contract；新代码使用 `renderer/settings/*` 真实路径。

### Sample / Sandbox 辅助

移动到：

```text
renderer/scene/SceneResource.h
renderer/presets/RendererPreset.h
```

说明：

- `SceneResource` 是 sample/sandbox 快速加载聚合器，未来真实引擎可以绕过。
- `RendererPreset` 是 sandbox 默认场景和质量预设，不应成为引擎 runtime 的强依赖。

## Shader 目录暂缓

当前 `shaders/` 仍是平铺目录：

```text
shaders/mesh.frag.hlsl
shaders/shadow.frag.hlsl
shaders/ssao.frag.hlsl
shaders/bloom.frag.hlsl
```

长期可以整理成：

```text
shaders/
  mesh/
  effects/
    shadow/
    ssao/
    bloom/
    sky/
    ibl/
    tone_mapping/
  samples/
```

但 shader 路径牵涉 CMake、shader asset smoke、runtime shader lookup 和 golden validation。本阶段不移动 shader，避免一次整理跨太多系统。

## Include 策略

本阶段 include 更新遵循：

1. Renderer internal 文件使用新路径，例如：

```cpp
#include "renderer/core/FrameContext.h"
#include "renderer/resources/ModelResource.h"
#include "renderer/settings/PostProcessingSettings.h"
```

2. Public facade 尽量只 include 必要 contract，减少把 internal 类型泄漏给引擎层。
3. 不保留旧 public/resource include 路径 wrapper；发现旧路径 include 直接改成新分层路径。
4. Tests 可以 include internal headers，但 public header smoke 只覆盖 facade / resource / settings contract。
5. `effects/*` 和 `passes/*` 不允许被 public facade 直接暴露给引擎层。

## 任务拆分

### 0.73.0 文档与范围确认

- 确认本阶段只做结构整理和 include hygiene。
- 确认不改运行时行为。
- 确认 shader 目录暂缓。
- 确认不保留 compatibility wrapper，根目录只保留 public facade。

### 0.73.1 Header Inventory / Ownership Map

- 列出 `src/renderer` 根目录所有头文件。
- 标注每个头文件属于：
  - Public facade
  - Resource contract
  - Settings contract
  - Renderer internal
  - Sample / preset
  - Removed legacy wrapper
- 同步 README 中的 renderer 接入边界说明。

### 0.73.2 Core Internal Layout

- 移动 `FrameRenderer`、`FrameContext`、`RenderQueue`、`RenderPass`、`Bounds`、`Frustum`、`FrameOverlay`、`RenderGraph` 到 `renderer/core`。
- 更新 renderer internal include。
- 更新 tests include。
- 保持 public facade 不直接暴露 core internal。

### 0.73.3 Resource / Settings Layout

- 移动 `ModelResource`、`MeshResource`、`TextureResource`、`TextureCache`、`Environment*Resource` 到 `renderer/resources`。
- 移动 `PostProcessingSettings`、`RendererQuality`、`ShadowConstants`、`ShadowDebugSettings` 到 `renderer/settings`。
- 删除根目录同名 wrapper headers。
- 更新 CMake、tests 和文档。

### 0.73.4 Scene / Preset Layout

- 移动 `SceneResource` 到 `renderer/scene`。
- 移动 `RendererPreset` 到 `renderer/presets`。
- 删除 wrapper headers，并将 `app/Application.h` 和现有 sample 路径全部迁移到新 include。
- 更新 README，明确它们是 sample/sandbox 辅助，不是引擎接入必经路径。

### 0.73.5 Include Hygiene / Compatibility Check

- 使用 `rg` 检查旧路径 include，确保源码和测试不再依赖根目录同名 wrapper。
- 保证 public header smoke 不 include `effects/*`、`passes/*`、`renderer/core/FrameContext.h`。
- 保证 internal tests 可以 include 新路径。
- 保证 `src/renderer` 根目录不再残留同名 wrapper。

### 0.73.6 Tests

建议至少运行：

```powershell
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug -R "ark_renderer_public_headers_smoke|ark_framework_headers_smoke|ark_shader_assets_smoke" --output-on-failure
ctest --preset msvc-vcpkg-debug --output-on-failure
```

必要时补充：

- include boundary smoke：验证 public facade 不依赖 internal effect。
- root layout smoke：验证 `src/renderer` 根目录只保留 public facade 源文件/头文件。

### 0.73.7 验证与收尾

- `ark_sandbox` hidden smoke，确认目录整理不影响默认场景启动。
- 对比 default frame validation，确认画面不因整理发生变化。
- 更新 `README.md` 的目录与接入边界。
- 更新本阶段文档状态和验证记录。

## 风险与控制

### 风险一：移动文件导致 include 爆炸

控制方式：

- 分批移动。
- 每批后构建。
- 删除 wrapper 后使用 `rg` 批量检查旧 include。
- internal include 统一使用新分层路径。

### 风险二：public API 被整理阶段污染

控制方式：

- `Renderer.h`、`RenderScene.h`、`RenderView.h` 保持为主要 facade。
- 不把 `FrameContext`、`FrameRenderer`、`RenderQueue` 暴露给 public smoke。
- `effects/*` 继续 internal。

### 风险三：Sample 辅助被误认为引擎主路径

控制方式：

- `SceneResource` 和 `RendererPreset` 移入更明确的目录。
- README 写明真实引擎可以绕过它们，用自己的 asset pipeline 创建 renderer resource。

### 风险四：一次整理影响 shader / golden

控制方式：

- 本阶段不移动 shader。
- 不改 pass 行为。
- 跑 frame validation 和 sandbox hidden smoke。

## 完成标准

- `src/renderer` 根目录只保留核心 public facade，不保留同名 wrapper。
- renderer internal、resource、settings、scene/preset 的目录职责清晰。
- 旧 public/resource include 路径仍可编译。
- `effects/*`、`passes/*` 不被引擎 facade 暴露。
- 全量测试通过。
- Sandbox 默认场景启动正常。
- README 和 phase 文档同步。

## 实施记录

### 0.73.0 文档与范围确认

- 本阶段限定为 renderer 目录结构整理、include hygiene 和无 wrapper 根目录验证。
- 不移动 shader，不修改 descriptor layout、GPU data layout 或运行时渲染行为。

### 0.73.1 Header Inventory / Ownership Map

- 保留 `Renderer.h`、`RenderScene.h`、`RenderView.h` 作为根目录 public facade。
- 根目录资源、设置、core、scene/preset 旧路径头文件已删除，避免与真实分层路径重名。
- README 已同步 renderer 接入边界和源码结构说明。

### 0.73.2 Core Internal Layout

- `Bounds`、`Frustum`、`FrameContext`、`FrameOverlay`、`FrameRenderer`、`RenderGraph`、`RenderPass`、`RenderQueue` 已移动到 `renderer/core`。
- renderer 内部与测试已改用 `renderer/core/*` 新路径。

### 0.73.3 Resource / Settings Layout

- `ModelResource`、`MeshResource`、`TextureResource`、`TextureCache`、`Environment*Resource` 已移动到 `renderer/resources`。
- `PostProcessingSettings`、`RendererQuality`、`ShadowConstants`、`ShadowDebugSettings` 已移动到 `renderer/settings`。
- 根目录同名 wrapper 已删除，新代码使用 `renderer/resources/*` 和 `renderer/settings/*`。

### 0.73.4 Scene / Preset Layout

- `SceneResource` 已移动到 `renderer/scene`。
- `RendererPreset` 已移动到 `renderer/presets`。
- README 已说明它们属于 sandbox/sample 辅助，不是未来引擎接入的必经路径。

### 0.73.5 Include Hygiene / Compatibility Check

- `rg` 检查源码与测试中旧路径 include，实际业务代码已迁移到新分层路径。
- `renderer_public_headers_smoke` 只覆盖 public facade 与新分层 contract，不再覆盖旧 wrapper。
- public facade 未直接暴露 `effects/*`、`passes/*` 或 `renderer/core/FrameContext.h`。

### 0.73.6 Tests

已通过：

```powershell
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug -R "ark_renderer_public_headers_smoke|ark_framework_headers_smoke|ark_shader_assets_smoke" --output-on-failure
ctest --preset msvc-vcpkg-debug --output-on-failure
```

测试结果：

- targeted header/shader smoke：3/3 passed。
- full CTest：35/35 passed。

### 0.73.7 验证与收尾

- `ark_sandbox` hidden smoke 已通过：程序启动 3 秒后正常停止，目录整理未导致默认 sandbox 启动失败。
- `ark_frame_validation_smoke` 已随全量 CTest 通过，说明默认画面验证路径未被结构整理破坏。
- README 与本阶段文档已同步。

### 0.73.8 设计修正：移除根目录同名 wrapper

- 删除 `src/renderer` 根目录下的资源、settings、core、scene/preset 同名转发头。
- 根目录现在只保留 `Renderer.*`、`RenderScene.*`、`RenderView.*`。
- `renderer_public_headers_smoke` 不再覆盖旧 include 路径，只验证 public facade 与新分层 contract。
- 重新通过构建、targeted header/shader smoke、全量 CTest 和 sandbox hidden smoke。

## 后续阶段建议

完成结构整理后，再进入真正的引擎接入能力：

1. Phase 0.74：Renderer Resource Handle / Engine-Facing Submission Contract
2. Phase 0.75：GPU Instancing Foundation
3. Phase 0.76：Render Object ID / Picking / Debug Selection
4. Phase 0.77：Lightweight RenderGraph / Transient Resource Lifetime
