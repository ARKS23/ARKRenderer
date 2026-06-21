# Phase 0.68 Renderer Directory Hygiene / Effects Module Layout

## 实施状态

Phase 0.67 已经完成 CSM 最小闭环：ShadowPass 可以生成 cascade shadow texture array，ForwardPass / mesh shader 可以采样 CSM，sandbox UI 也能调节 CSM 参数并查看 cascade diagnostics。

本阶段只整理 renderer internal effects layout；public API boundary / engine-facing contract 留到下一阶段单独设计。

随着 Shadow、Bloom、ToneMapping、IBL、Skybox、后续 SSAO 和 debug visualization 持续增加，`src/renderer` 根目录已经开始混合放置三类文件：

- renderer core：`Renderer`、`FrameRenderer`、`FrameContext`、`RenderView`、`RenderQueue`。
- scene/resource 基础设施：`RenderScene`、`ModelResource`、`MeshResource`、`TextureResource`、`EnvironmentResource`。
- 具体效果模块：`ShadowCascade*`、`ShadowPass`、`BloomPass`、`ToneMappingPass`、`SkyboxPass`、IBL bake generators。

本阶段建议先做一次轻量目录整理，把具体效果模块归入 `renderer/effects/*`，但不改变运行时行为、不重写 pass contract、不引入新的抽象层。

## 阶段目标

- 建立清晰的 renderer 目录分层，降低后续添加 SSAO、shadow debug、更多 post effects 时的认知成本。
- 将 Shadow 相关文件迁入 `renderer/effects/shadow/`，形成第一个完整 effect module。
- 将 Bloom / ToneMapping / Skybox / IBL bake 相关文件迁入对应 effect 目录。
- 保持类名、namespace、public contract 和渲染行为不变。
- 更新 include 路径、测试引用和文档，确保目录移动是纯结构调整。
- 避免一次性重构 `Renderer` / `FrameRenderer` / `RenderQueue` 等核心调度代码。

## 非目标

- 不改 shader 逻辑。
- 不改 descriptor binding contract。
- 不改 RenderPass 执行顺序。
- 不改 CSM、Bloom、ToneMapping、IBL 的视觉行为。
- 不引入新第三方库。
- 不做 SSAO 实现，只预留目录规划。
- 不做 GPU-driven、render graph 重写或 pass registry。
- 不在本阶段拆分 `Renderer` 大类内部的 environment bake 调度逻辑；只移动文件和 include。
- 不定义 renderer public API / engine-facing contract；该边界在目录稳定后作为下一阶段单独设计。

## 推荐目录结构

建议使用复数 `effects`，表示多个独立效果模块：

```text
src/renderer/
  core/
    FrameContext.h
    FrameOverlay.h
    FrameRenderer.cpp
    FrameRenderer.h
    RenderGraph.h
    RenderPass.cpp
    RenderPass.h
    RenderView.cpp
    RenderView.h
    Renderer.cpp
    Renderer.h
    RendererPreset.cpp
    RendererPreset.h
    RendererQuality.cpp
    RendererQuality.h

  scene/
    Bounds.h
    Frustum.cpp
    Frustum.h
    MeshResource.cpp
    MeshResource.h
    ModelResource.cpp
    ModelResource.h
    RenderQueue.cpp
    RenderQueue.h
    RenderScene.cpp
    RenderScene.h
    SceneResource.cpp
    SceneResource.h

  resource/
    TextureCache.cpp
    TextureCache.h
    TextureResource.cpp
    TextureResource.h
    EnvironmentResource.cpp
    EnvironmentResource.h
    EnvironmentCubeResource.cpp
    EnvironmentCubeResource.h
    EnvironmentBrdfLutResource.cpp
    EnvironmentBrdfLutResource.h

  material/
    Material.h
    MaterialResource.cpp
    MaterialResource.h
    MaterialSystem.h

  effects/
    shadow/
      ShadowPass.cpp
      ShadowPass.h
      ShadowConstants.h
      ShadowCascade.h
      ShadowCascadeBuilder.cpp
      ShadowCascadeBuilder.h

    bloom/
      BloomPass.cpp
      BloomPass.h

    tone_mapping/
      ToneMappingPass.cpp
      ToneMappingPass.h
      PostProcessingSettings.cpp
      PostProcessingSettings.h

    sky/
      SkyboxPass.cpp
      SkyboxPass.h
      SandboxEnvironment.cpp
      SandboxEnvironment.h

    ibl/
      CubemapOrientation.h
      EnvironmentCubeConverter.cpp
      EnvironmentCubeConverter.h
      EnvironmentIrradianceGenerator.cpp
      EnvironmentIrradianceGenerator.h
      EnvironmentSpecularPrefilterGenerator.cpp
      EnvironmentSpecularPrefilterGenerator.h
      EnvironmentBrdfLutGenerator.cpp
      EnvironmentBrdfLutGenerator.h

    ssao/
      后续 Phase 再创建具体文件

  passes/
    ClearPass.cpp
    ClearPass.h
    CubePass.cpp
    CubePass.h
    ForwardPass.cpp
    ForwardPass.h
    ImGuiPass.h
    TrianglePass.cpp
    TrianglePass.h
```

第一阶段可以先只移动 `effects/*`，暂时保留 `core/scene/resource` 重排到后续阶段。这样 diff 更小，风险更低。

## 依赖方向

推荐保持如下依赖方向：

```text
app/sandbox
  -> renderer/core
      -> renderer/effects/*
      -> renderer/scene
      -> renderer/resource
      -> renderer/material
      -> rhi

renderer/effects/*
  -> renderer/core contract
  -> renderer/scene/resource/material
  -> rhi
```

约束：

- `effects/*` 可以依赖 `FrameContext`、`RenderPass`、`RenderView`、`RenderQueue` 等核心 contract。
- `scene/` 和 `resource/` 不应反向依赖具体 effect。
- `FrameRenderer` / `Renderer` 是 composition layer，可以 include 具体 effect pass。
- effect 之间不要直接互相依赖；需要共享数据时通过 `FrameContext` 或明确的 resource contract。
- include 使用完整项目路径，例如 `renderer/effects/shadow/ShadowPass.h`，避免 `../` 相对路径。

## 迁移策略

### 0.68.0 文档与范围确认

- 确认本阶段是目录整理，不做渲染行为改动。
- 确认先整理 `effects/*`，不一次性迁移全部 renderer core / scene / resource。
- 确认不创建空的 SSAO 实现，只在文档中保留目标路径。

实现结果：
- 本阶段范围确认聚焦 renderer internal effects layout。
- Public API boundary / engine-facing contract 明确延后到下一阶段，避免在目录迁移时提前暴露内部实现细节。
- `core/scene/resource` 的完整重排暂不执行，只迁移已成形的 effect 文件。

### 0.68.1 Shadow Effect Module

- 移动：
  - `passes/ShadowPass.*` -> `effects/shadow/ShadowPass.*`
  - `ShadowConstants.h` -> `effects/shadow/ShadowConstants.h`
  - `ShadowCascade.h` -> `effects/shadow/ShadowCascade.h`
  - `ShadowCascadeBuilder.*` -> `effects/shadow/ShadowCascadeBuilder.*`
- 更新 `FrameContext`、`RenderView`、`FrameRenderer`、`ForwardPass`、tests 中的 include。
- 保持 `ShadowPass` 类名和 public API 不变。

实现结果：
- 已将 `ShadowPass`、`ShadowConstants`、`ShadowCascade`、`ShadowCascadeBuilder` 迁入 `renderer/effects/shadow/`。
- `FrameContext`、`RenderView`、`FrameRenderer`、`ShadowPass`、`ForwardPass` 相关测试 include 已更新到新路径。
- 类名、namespace、CSM contract、ShadowPass 执行路径均保持不变。

### 0.68.2 Bloom / ToneMapping Effect Modules

- 移动：
  - `passes/BloomPass.*` -> `effects/bloom/BloomPass.*`
  - `passes/ToneMappingPass.*` -> `effects/tone_mapping/ToneMappingPass.*`
  - `PostProcessingSettings.*` -> `effects/tone_mapping/PostProcessingSettings.*`
- 更新 sandbox UI、runtime settings、FrameRenderer、tests include。
- 保持 Bloom / ToneMapping runtime settings 行为不变。

实现结果：
- 已将 `BloomPass` 迁入 `renderer/effects/bloom/`。
- 已将 `ToneMappingPass` 和 `PostProcessingSettings` 迁入 `renderer/effects/tone_mapping/`。
- `RenderView`、`FrameRenderer`、sandbox UI settings tests 和对应 pass smoke tests 已更新 include。

### 0.68.3 Sky / IBL Effect Modules

- 移动：
  - `passes/SkyboxPass.*` -> `effects/sky/SkyboxPass.*`
  - `SandboxEnvironment.*` -> `effects/sky/SandboxEnvironment.*`
  - `EnvironmentCubeConverter.*` -> `effects/ibl/EnvironmentCubeConverter.*`
  - `EnvironmentIrradianceGenerator.*` -> `effects/ibl/EnvironmentIrradianceGenerator.*`
  - `EnvironmentSpecularPrefilterGenerator.*` -> `effects/ibl/EnvironmentSpecularPrefilterGenerator.*`
  - `EnvironmentBrdfLutGenerator.*` -> `effects/ibl/EnvironmentBrdfLutGenerator.*`
  - `CubemapOrientation.h` -> `effects/ibl/CubemapOrientation.h`
- 保留 `EnvironmentResource`、`EnvironmentCubeResource`、`EnvironmentBrdfLutResource` 在 resource 层，避免把资源类型误归入 effect。

实现结果：
- 已将 `SkyboxPass`、`SandboxEnvironment` 迁入 `renderer/effects/sky/`。
- 已将 cubemap orientation 与 IBL bake generators 迁入 `renderer/effects/ibl/`。
- `EnvironmentResource`、`EnvironmentCubeResource`、`EnvironmentBrdfLutResource` 继续保留在当前 renderer resource 位置，未误归入 effect。

### 0.68.4 Include Hygiene / Compatibility Check

- 使用 `rg '#include "renderer/' src tests` 检查旧路径。
- 不建议长期保留旧路径 wrapper header；如果必须临时兼容，也应在文档中标注删除时机。
- 确保所有新 include 都走 `renderer/effects/...`。
- 确保没有 effect 文件使用 `../` include。

实现结果：
- `src/tests/apps` 中旧的 moved-effect include 路径已清零。
- 新 include 统一使用 `renderer/effects/...`。
- `src/renderer/effects` 内没有 `../` 相对 include。
- `src/renderer/passes` 当前只保留 `ClearPass`、`CubePass`、`ForwardPass`、`ImGuiPass`、`TrianglePass`。

### 0.68.5 Tests

建议覆盖：

- Full Debug build。
- Shadow 相关 targeted CTest：
  - `ark_shadow_pass_smoke`
  - `ark_shadow_cascade_builder_smoke`
  - `ark_forward_pass_pipeline_smoke`
  - `ark_shader_assets_smoke`
- Bloom / ToneMapping targeted CTest：
  - `ark_bloom_pass_smoke`
  - `ark_tone_mapping_pass_smoke`
  - `ark_sandbox_ui_settings_smoke`
- IBL / Sky targeted CTest：
  - `ark_environment_cube_resource_smoke`
  - `ark_environment_cube_conversion_smoke`
  - `ark_irradiance_generator_smoke`
  - `ark_specular_prefilter_smoke`
  - `ark_brdf_lut_smoke`
- Full CTest，记录已知 golden diff 状态。

验证结果：
- Full Debug build 通过：`cmake --build build/msvc-vcpkg --config Debug`。
- Targeted CTest 通过 17/17，覆盖 headers、Shadow、Bloom、ToneMapping、Sky、IBL、sandbox UI 相关 smoke tests。
- Full CTest 结果为 32/33：除 `ark_frame_validation_smoke/default_composite_scene` 外均通过。
- `default_composite_scene` 仍是 0.67.3 以来记录的默认组合场景 golden diff（meanAbsError=0.0628158），本阶段不更新 PNG baseline。

### 0.68.6 验证与收尾

- 更新本文件实施状态。
- 检查 `git diff --stat`，确认主要是 rename / include update。
- 运行 `git diff --check`。
- sandbox hidden-window smoke。
- 手动确认 sandbox 仍能显示 Sponza + DamagedHelmet + shadow probe spheres，CSM/Bloom/ToneMapping/IBL/UI 仍可使用。
- 提交并推送。

收尾结果：
- `git diff --check` 未发现空白错误，仅有仓库既有 LF/CRLF 提示。
- sandbox hidden-window smoke 通过，默认组合场景可启动并稳定运行 4 秒。
- 本文件已同步 0.68.0 ~ 0.68.6 实施状态。

## 风险与应对

### 风险一：include 大面积修改导致漏改

应对：
- 每个模块迁移后立刻 build。
- 使用 `rg` 搜索旧路径。
- 优先迁移强内聚模块 Shadow，确认方式稳定后再迁移 Bloom/ToneMapping/IBL。

### 风险二：目录移动掩盖行为改动

应对：
- 本阶段不修改算法和 pass contract。
- commit message 明确标注 directory / include hygiene。
- 如果发现必须改行为才能编译，单独记录并尽量拆出独立 commit。

### 风险三：Effect 与 Resource 边界不清

应对：
- Resource 类型留在 `renderer/resource`。
- Generator / Pass / Bake logic 放到 `renderer/effects/*`。
- `Renderer` / `FrameRenderer` 负责组装 effect，不让 resource 层反向 include effect。

### 风险四：后续文档编号与 Shadow Debug 计划冲突

应对：
- 本阶段把 0.68 用于目录卫生。
- Shadow Debug Visualization 顺延到下一个 phase，更适合在目录稳定后实现。

## 完成标准

- `src/renderer` 根目录中的 effect 相关文件明显减少。
- Shadow、Bloom、ToneMapping、Sky/IBL 文件可以按目录直接定位。
- include 路径表达模块归属。
- Build 和 targeted tests 通过。
- sandbox smoke 通过。
- 不引入渲染行为变化。

## 后续方向

目录稳定后，建议继续：

1. Phase 0.69：Shadow Debug Visualization，包括 cascade color、shadow map preview、light frustum debug。
2. Phase 0.70：SSAO effect foundation，落到 `renderer/effects/ssao/`。
3. Phase 0.71：GPU instanced rendering foundation，减少多球体、多重复 mesh 的 draw call 压力。
4. Phase 0.72：Material / Texture debug views，方便复杂 glTF 场景排查。
