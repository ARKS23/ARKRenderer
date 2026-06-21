# Phase 0.69 Renderer Public API Boundary / Engine-Facing Contract

## 实施状态

已完成 0.69.0 文档与范围确认、0.69.1 Public Header Inventory、0.69.2 Renderer Lifecycle Contract、0.69.3 Scene / View Contract、0.69.4 Include Boundary Smoke、0.69.5 Tests，以及 0.69.6 验证与收尾。

本阶段实际改动：

- 补充 `src/renderer` 当前物理目录结构和 public/internal 逻辑边界。
- 将 `PostProcessingSettings` 与 `ShadowConstants` 提升为 `renderer/` public data contract，避免 `RenderView` 直接暴露 `renderer/effects/*` include 路径。
- 为 `Renderer`、`RenderScene`、`RenderView`、`SceneResource`、`RendererQuality`、`RendererPreset` 和 `EnvironmentResource` 补充必要中文职责注释。
- 新增 `ark_renderer_public_headers_smoke`，专门验证 public facade / resource contract 头文件可独立 include。
- README 已补充 renderer 接入边界说明。

验证结果：

- `cmake --build build\msvc-vcpkg --config Debug`：通过。
- Phase 0.69 相关 CTest 10/10：通过。
- 排除既有视觉 golden 差异用例 `ark_frame_validation_smoke` 后的 CTest 33/33：通过。
- sandbox hidden-window smoke：通过。
- `git diff --check`：通过，仅有 Windows 换行提示。

Phase 0.68 已经完成 renderer internal effects layout 整理：Shadow、Bloom、ToneMapping、Sky 和 IBL bake 相关文件已经归入 `src/renderer/effects/*`，`passes/` 只保留 Forward / Clear / Cube / Triangle / ImGui 等基础 pass。当前 renderer 内部模块边界比之前更清楚，适合进一步定义对外 API 边界。

本阶段建议聚焦 **Renderer Public API Boundary / Engine-Facing Contract**。目标不是马上接入完整引擎，也不是重写 renderer 架构，而是先回答几个长期问题：

- 上层应用或未来引擎应该 include 哪些头文件？
- 哪些类是稳定 facade，哪些只是 renderer internal implementation？
- 引擎的 Scene / Camera / Asset / Material 应该如何映射到 renderer 的 `RenderScene` / `RenderView` / Resource？
- `effects/*`、`FrameRenderer`、`FrameContext`、`RenderQueue` 是否应该暴露给引擎层？
- 后续做 SSAO、Shadow Debug、Instancing 时，哪些改动不应该污染 public API？

## 阶段目标

- 明确 renderer 的 public facade 头文件集合。
- 明确 renderer internal 模块集合，避免未来引擎直接依赖 pass / effect 实现。
- 梳理 renderer 生命周期 contract：create / resize / render / destroy。
- 梳理 scene submission contract：`RenderScene` 接收什么，不接收什么。
- 梳理 view contract：`RenderView` 负责相机矩阵、裁剪面和画面参数，不负责输入交互或 RHI 资源。
- 梳理 resource ownership contract：`ModelResource`、`MeshResource`、`MaterialResource`、`TextureResource`、`EnvironmentResource` 是 renderer-owned GPU resource。
- 补充必要中文注释和文档，降低后续接入引擎时的误用风险。
- 保持渲染行为不变，尽量只做边界文档、注释、轻量 include smoke。

## 非目标

- 不实现完整引擎接入层。
- 不引入 ECS、Scene Graph、Transform Hierarchy 或 Asset Database。
- 不做 ABI 稳定承诺；本项目仍以源码级 API 为主。
- 不重排 `core/scene/resource` 目录。
- 不把 `effects/*` 暴露为 public API。
- 不重写 `Renderer`、`FrameRenderer`、`RenderQueue` 或 render graph。
- 不实现 SSAO、Shadow Debug Visualization、GPU Instancing。
- 不引入新第三方库。

## Renderer 目录结构建议

Phase 0.68 已经完成 effects 目录整理，因此 Phase 0.69 不建议继续做大规模文件迁移。本阶段更适合把 **当前物理目录结构** 和 **推荐逻辑边界** 写清楚：哪些文件可以被上层应用或未来引擎 adapter 直接 include，哪些目录只是 renderer 内部实现细节。

推荐先按下面的结构理解 `src/renderer`：

```text
src/renderer/
  Renderer.*                 # Public facade：renderer 创建、resize、render 的主入口。
  RenderScene.*              # Public facade：renderer-facing scene submission。
  RenderView.*               # Public facade：相机、视图、后处理、阴影、可见性参数集合。
  RendererPreset.*           # Public-ish：sandbox / sample 默认场景和质量配置。
  RendererQuality.*          # Public-ish：质量参数 contract。
  SceneResource.*            # Public-ish：sample/sandbox 资源加载聚合器。
  PostProcessingSettings.*   # Public contract：Bloom 等后处理参数。
  ShadowConstants.h          # Public contract：CPU/shader 共享的 shadow 数据上限。

  ModelResource.*            # Resource contract：模型级 GPU resource。
  MeshResource.*             # Resource contract：mesh / primitive 级 GPU resource。
  TextureResource.*          # Resource contract：texture GPU resource。
  EnvironmentResource.*      # Resource contract：IBL / sky environment resource。

  material/
    MaterialResource.*       # Resource contract：PBR material resource。

  FrameRenderer.*            # Internal：帧级调度和 pass 编排。
  FrameContext.*             # Internal：单帧内 pass 共享上下文。
  FrameOverlay.*             # Internal-ish：debug / UI overlay 数据桥。
  RenderQueue.*              # Internal：可见性、排序、draw item 构建。
  RenderGraph.*              # Internal：后续 render graph 演进入口。
  RenderPass.*               # Internal：pass 抽象基类。

  passes/
    ForwardPass.*            # Internal：基础 forward mesh pass。
    ClearPass.*              # Internal：清屏 / sample pass。
    CubePass.*               # Internal：cubemap / cube draw helper pass。
    ImGuiPass.*              # Internal：UI pass。
    TrianglePass.*           # Internal：早期 sample / smoke pass。

  effects/
    shadow/                  # Internal：shadow map / CSM 实现。
    bloom/                   # Internal：physical based bloom。
    tone_mapping/            # Internal：tone mapping pass 实现。
    sky/                     # Internal：skybox / sandbox environment。
    ibl/                     # Internal：environment cubemap / irradiance / specular / BRDF LUT bake。
    ssao/                    # Future internal：SSAO effect foundation。
```

边界规则：

- `Renderer`、`RenderScene`、`RenderView` 是最核心的 public facade。
- `PostProcessingSettings` 和 `ShadowConstants` 属于 public data contract，因为 `RenderView` 会直接暴露这些参数。
- `ModelResource`、`MeshResource`、`MaterialResource`、`TextureResource`、`EnvironmentResource` 是 renderer resource contract，可以作为引擎资产进入 renderer 的边界对象。
- `SceneResource` 和 `RendererPreset` 更偏 sample / sandbox / quick validation，未来引擎可以不用它们。
- `FrameRenderer`、`FrameContext`、`RenderQueue`、`RenderPass`、`passes/*`、`effects/*` 默认都是 internal。
- 后续新增 SSAO、shadow debug、debug view、GPU profiling 时，优先落在 internal 目录中，只有稳定的控制参数再上浮到 `RenderView`、`RendererQuality` 或专门的 public settings。
- 暂时不引入 `renderer/public` / `renderer/internal` / `renderer/api` 目录；等真正接入引擎并发现 public API 稳定后，再考虑 wrapper headers 或物理目录重排。

## Public / Internal 边界建议

### Public Facade

短期建议上层应用和未来引擎只直接依赖这些 renderer facade：

```text
renderer/Renderer.h
renderer/RenderScene.h
renderer/RenderView.h
renderer/RendererPreset.h
renderer/RendererQuality.h
renderer/SceneResource.h
renderer/PostProcessingSettings.h
renderer/ShadowConstants.h
renderer/ModelResource.h
renderer/MeshResource.h
renderer/TextureResource.h
renderer/EnvironmentResource.h
renderer/material/MaterialResource.h
```

说明：

- `Renderer.h` 是渲染系统入口，提供 `createRenderer()`、`render()`、`resize()`。
- `RenderScene.h` 是 renderer scene submission 容器，接收 renderer resource 指针和 transform。
- `RenderView.h` 是每帧 camera / view / post-processing / shadow / visibility 参数集合。
- `RendererPreset.h` / `RendererQuality.h` 是 sandbox 和快速验证使用的 preset contract，未来引擎可以选择不用。
- `SceneResource.h` 是当前 glTF/HDR 快速加载辅助，适合作为 demo / sandbox / sample path；长期引擎可用自己的 asset pipeline 替代。
- `ModelResource` / `MeshResource` / `MaterialResource` / `TextureResource` / `EnvironmentResource` 是 renderer-owned 资源类型，可作为引擎资产进入 renderer 的边界对象。

### Renderer Internal

这些模块默认不作为引擎接入 API：

```text
renderer/FrameRenderer.h
renderer/FrameContext.h
renderer/FrameOverlay.h
renderer/RenderPass.h
renderer/RenderQueue.h
renderer/RenderGraph.h
renderer/effects/*
renderer/passes/*
renderer/effects/shadow/ShadowCascade*
renderer/effects/ibl/*Generator*
```

说明：

- `FrameRenderer` 负责组装内部 pass，不应由引擎直接调度。
- `FrameContext` 是帧内数据总线，字段会随 renderer feature 演进变化。
- `RenderQueue` 是 renderer extraction 后的 draw item 列表，不是引擎 scene object。
- `effects/*` 是具体效果实现，后续可以重构、拆分或替换，不应形成外部依赖。
- `passes/*` 是 renderer 内部 pass，不应该成为上层应用控制渲染顺序的入口。

## Engine-Facing Contract

### 生命周期

引擎或应用侧推荐只管理 renderer 生命周期：

```cpp
ark::RendererDesc desc{};
desc.nativeWindow = nativeWindow;
desc.extent = {width, height};
desc.quality = quality;
desc.defaultScene = defaultSceneDesc;

ark::Scope<ark::Renderer> renderer = ark::createRenderer(desc);
renderer->resize(width, height);
renderer->render(scene, view, overlay);
```

约束：

- `RendererDesc` 只保存创建 renderer 所需外部输入，不保存后端对象。
- `Renderer` 拥有 RHI backend、default scene resource、frame renderer 和内部效果资源。
- 上层应用不直接创建或执行 `ShadowPass` / `BloomPass` / `ToneMappingPass`。
- window resize 通过 `Renderer::resize()` 通知 renderer。
- 每帧只提交 `RenderScene`、`RenderView` 和可选 `FrameOverlay`。

### Scene Submission

`RenderScene` 是 renderer-facing scene，不是完整引擎世界：

```cpp
scene.addModel(modelResource, worldTransform, debugName);
scene.addObject(meshResource, materialResource, worldTransform, debugName);
scene.setLighting(lighting);
scene.setEnvironment(environment);
```

约束：

- `RenderScene` 不负责 ECS、父子层级、动画更新、物理同步或 gameplay state。
- `RenderScene` 当前保存 `ModelResource*` / `MeshResource*` / `MaterialResource*` / `EnvironmentResource*`，资源生命周期必须长于本帧渲染。
- `RenderScene` 的 `Bounds3` 是 renderer 用于 shadow fit、frustum culling 和后续空间优化的世界 bounds。
- 未来引擎可以通过 adapter 把 Engine Scene 抽取为 `RenderScene`，而不是让 renderer 直接依赖引擎 Scene。

### View / Camera

`RenderView` 描述一帧从哪个视角、用哪些画面参数渲染：

- view matrix。
- projection matrix。
- camera position。
- camera near/far。
- tone mapping settings。
- bloom/post-processing settings。
- shadow / CSM settings。
- visibility settings。

约束：

- `RenderView` 不负责输入交互；sandbox camera controller 是 app 层逻辑。
- `RenderView` 不负责创建 RHI 资源。
- `RenderView` 不负责 draw call 执行。
- 坐标约定继续沿用 README：世界坐标右手系，view 使用 RH 语义，projection 使用 RH_ZO，Vulkan Y 翻转通过 `projection[1][1] *= -1`。

### Resource Ownership

短期 contract：

- `ModelResource`、`MeshResource`、`MaterialResource`、`TextureResource`、`EnvironmentResource` 是 renderer-owned GPU resource wrapper。
- `SceneResource` 是当前 sample/sandbox 资产加载聚合器，负责从 glTF/HDR 快速生成 `RenderScene`。
- 长期引擎接入时，可以保留 renderer resource 类型，但由 engine asset pipeline 负责加载和缓存，再提交给 renderer。
- renderer 不直接拥有 engine asset database，也不直接依赖 engine entity id。

建议后续引擎适配形态：

```text
Engine Scene / ECS / Asset System
  -> Renderer Adapter / Extraction Layer
      -> RenderScene
      -> RenderView
      -> Renderer Resources
          -> Renderer::render()
```

## Include Contract

### 应用层允许 include

```cpp
#include "renderer/Renderer.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/RendererPreset.h"
#include "renderer/RendererQuality.h"
#include "renderer/SceneResource.h"
#include "renderer/PostProcessingSettings.h"
#include "renderer/ShadowConstants.h"
```

### 应用层不建议 include

```cpp
#include "renderer/FrameRenderer.h"
#include "renderer/FrameContext.h"
#include "renderer/RenderQueue.h"
#include "renderer/effects/shadow/ShadowPass.h"
#include "renderer/effects/bloom/BloomPass.h"
#include "renderer/effects/ibl/EnvironmentCubeConverter.h"
```

例外：

- tests 可以 include internal headers 进行 contract smoke。
- sandbox debug UI 可以读取诊断信息，但不应直接操作 pass 内部对象。
- sample / tool 可以使用 `SceneResource` 作为快速加载路径。

## 分阶段任务

### 0.69.0 文档与范围确认

- 确认本阶段聚焦 public/internal boundary，不做渲染行为改动。
- 确认 `effects/*` 是 renderer internal。
- 确认真正 engine adapter 不在本阶段实现。

### 0.69.1 Public Header Inventory

- 列出当前 public facade headers。
- 列出当前 internal headers。
- 同步 `src/renderer` 当前物理目录图，明确 public facade、resource contract、internal pass/effect 的逻辑分层。
- 检查 `apps/sandbox` 当前 include 是否依赖过多 internal headers。
- 检查 tests 中 internal include 是否只用于 smoke / contract 覆盖。
- 如有必要，补充 `framework_headers_smoke`，保证 public facade headers 可独立 include。

### 0.69.2 Renderer Lifecycle Contract

- 审查 `RendererDesc`、`Renderer`、`createRenderer()`。
- 补充中文注释，说明上层只通过 `render()` / `resize()` 驱动 renderer。
- 确认 `RendererDesc` 不泄漏 Vulkan backend 细节。
- 文档记录 renderer owns backend / frame renderer / default resources。

### 0.69.3 Scene / View Contract

- 审查 `RenderScene` 和 `RenderView` 注释。
- 补充 `RenderScene` 是 renderer-facing scene，不是 engine world。
- 补充 `RenderView` 是 per-frame view state，不负责 input / resource / draw execution。
- 明确 resource pointer lifetime 要覆盖本帧渲染。

### 0.69.4 Include Boundary Smoke

- 增加或更新 smoke test，验证 public facade headers 能独立 include。
- 新增 `ark_renderer_public_headers_smoke`，只覆盖 public facade / resource contract，不直接 include `effects/*` 或 `passes/*`。
- 可选增加 internal include audit 文档，不强制从代码层禁止 internal include。
- 不创建 wrapper headers，不做 `renderer/api/` 目录迁移。

### 0.69.5 Tests

建议覆盖：

- Full Debug build。
- `ark_framework_headers_smoke`。
- `ark_renderer_preset_smoke`。
- `ark_scene_resource_smoke`。
- `ark_render_scene_queue_smoke`。
- `ark_sandbox_ui_settings_smoke`。
- Full CTest，记录已知 golden diff 状态。
- sandbox hidden-window smoke。

### 0.69.6 验证与收尾

- 更新本文件实施状态。
- 更新 README 中的 renderer public API / coordinate convention / engine-facing note。
- 运行 `git diff --check`。
- 提交并推送。

## 风险与应对

### 风险一：过早设计过重 API

应对：

- 本阶段只定义源码级边界，不做 ABI 稳定承诺。
- 不引入 `renderer/api` / `renderer/internal` 目录，除非后续真的需要。
- 保留当前 include 路径，降低迁移成本。

### 风险二：把 internal pass 暴露给引擎

应对：

- 文档明确 `effects/*`、`passes/*`、`FrameRenderer`、`FrameContext` 默认 internal。
- 应用层只通过 `Renderer` / `RenderScene` / `RenderView` 交互。
- 如果未来需要自定义 pass，单独设计 plugin / render extension contract。

### 风险三：RenderScene 与 Engine Scene 混淆

应对：

- 明确 `RenderScene` 是 renderer submission data。
- 引擎自己的 ECS / scene graph 通过 adapter 抽取成 `RenderScene`。
- renderer 不直接依赖 engine entity、component 或 asset database。

### 风险四：Resource 生命周期不清

应对：

- 文档和注释明确 resource pointer 必须至少覆盖本帧。
- 后续可以增加 `RendererResourceHandle` 或 resource manager，但本阶段不做。

## 完成标准

- 文档清楚说明 public facade 和 internal implementation。
- 文档包含清晰的 `src/renderer` 目录结构图，并明确本阶段不做第二轮大规模文件迁移。
- `Renderer`、`RenderScene`、`RenderView` 的中文注释能帮助避免误用。
- `ark_renderer_public_headers_smoke` 能覆盖 public facade headers，`framework_headers_smoke` 继续覆盖全量头文件编译。
- sandbox 和 tests 不因边界整理发生行为变化。
- README 记录 renderer 接入方式和坐标约定。

## 后续方向

完成 public API boundary 后，建议继续：

1. Phase 0.70：Sandbox First-Person Camera，让 Sponza 等大型场景可以自由漫游，方便观察局部阴影和后处理效果。
2. Phase 0.71：Shadow Debug Visualization，包括 cascade color、shadow diagnostics、shadow map preview assessment。
3. Phase 0.72：SSAO effect foundation，落到 `renderer/effects/ssao/`。
4. Phase 0.73：GPU instanced rendering foundation，减少重复 mesh 的 draw call 压力。
5. Phase 0.74：Material / Texture debug views，方便复杂 glTF 场景排查。
