# Phase 0.67 CSM / Shadow Validation Scene

## 实施状态

已完成 0.67.0 文档与范围确认、0.67.1 Sandbox Shadow Validation Scene、0.67.2 CSM Settings / Data Contract、0.67.3 Cascade Split / Matrix Builder，以及 0.67.4 ShadowPass CSM Render Path。

Phase 0.61 ~ 0.66 已经完成 scene bounds、ShadowPass scene-fit、PCF shadow filtering、sandbox debug UI、运行时参数桥接、Frustum 数据结构、DrawItem world bounds 和 forward visibility diagnostics。当前渲染器已经可以在默认 Sponza + DamagedHelmet 场景中观察 Shadow、Bloom、ACES ToneMapping、IBL 和 camera frustum culling 统计。

下一阶段建议推进 **CSM / Cascaded Shadow Map 最小闭环**。原因是当前单张 directional shadow map 已经能工作，但面对更大的 Sponza 场景时会遇到典型问题：近处阴影分辨率不够、远处覆盖范围浪费、相机移动时阴影质量不稳定。CSM 能把相机视锥按深度切成多个 cascade，让近处使用更高有效 texel density，远处使用更大覆盖范围，是继续提升大场景阴影质量的自然下一步。

本阶段同时纳入 sandbox shadow validation scene 调整：放大 Sponza，并在中庭放置一组球体，用来更直观地观察阴影接触、软边、cascade 切换、Bloom、ToneMapping 和 IBL 在复杂场景中的组合效果。

## 阶段目标

- 建立 CSM 数据结构与质量 contract，支持 2 ~ 4 个 cascade。
- 基于 `RenderView` 的 camera view-projection 和现有 `Bounds3` / `Frustum` 能力计算 cascade split 与 light-space 矩阵。
- 让 ShadowPass 支持最小多 cascade 渲染路径。
- 让 ForwardPass / mesh shader 能选择正确 cascade 并采样对应 shadow map。
- 在 sandbox UI 中提供 CSM 关键参数与调试统计。
- 放大默认 Sponza，并加入一组阴影观察球体，形成稳定的视觉验证场景。
- 保持 Phase 0.66 的 queue contract：camera culling 只影响 forward visible queue，不影响 shadow caster full queue。

## 非目标

- 不做 VSM / EVSM / MSM。
- 不做 contact shadow、screen-space shadow 或 ray traced shadow。
- 不做 GPU-driven culling、Hi-Z、occlusion culling 或 clustered rendering。
- 不做完整 shadow atlas allocator；本阶段可以使用固定 cascade count 和固定 per-cascade extent。
- 不做复杂 cascade debug drawing；第一版只需要 UI 数值和可选 shader debug mode。
- 不做真正 GPU instanced rendering；球体可以先作为普通 additional model / SceneObject 验证。
- 不引入新第三方库。

## 当前状态判断

当前 renderer 已经具备推进 CSM 的关键基础：

- `RenderScene` 能提供 scene world bounds。
- `ShadowPass` 已经有 scene-fit light view-projection 和 texel snapping 基础。
- `Frustum` 已经能从 view-projection 提取平面并判断 AABB 可见性。
- `RenderQueue` 已经区分 full queue 和 forward-visible queue 的消费语义。
- `RenderView` 已经能承载 runtime settings，sandbox UI 能写回 shadow / post-processing / visibility 参数。
- 默认场景已有 Sponza + DamagedHelmet，足够暴露大场景阴影质量问题。

目前不支持真正的 GPU instanced draw。RHI 层已有 `instanceCount` 和 `PerInstance` 基础，但 renderer 仍然是每个 `DrawItem` 一次 `drawIndexed()`。因此本阶段的阴影观察球体优先走现有模型/对象路径，避免把 CSM 和 instancing 两个较大的方向混在一起。

## Sandbox Shadow Validation Scene

### Sponza 放大

当前默认 Sponza scale 在 `src/renderer/RendererPreset.cpp` 中集中配置：

```cpp
constexpr float DefaultSponzaScale = 8.0f;
```

本阶段建议把默认验证场景中的 Sponza 调大到 `8.0f` 或 `10.0f`。选择原则：

- 摄像机初始位置仍然能看到中庭和 DamagedHelmet。
- 场景 bounds 明显增大，单张 shadow map 的分辨率压力更明显。
- CSM 开启后，近处接触阴影和远处建筑阴影能形成可见对比。
- 不让 far plane / orbit camera 交互变得难用。

如果直接改全局 `DefaultSponzaScale` 会影响 Sponza preset、Default preset 和 ShadowValidation preset，应在文档和测试中明确这是预期行为；如果希望风险更小，也可以只为 `Default` / `ShadowValidation` preset 增加单独 scale 常量。

### 阴影观察球体

建议在 Sponza 中庭放置一组球体，用于观察：

- 球体自身投影到地面 / 墙面的接触阴影。
- Sponza 建筑结构投影到球体上的阴影。
- 不同 roughness / metallic 材质在 Shadow + IBL + ToneMapping 下的表现。
- cascade split 附近的阴影连续性。
- Bloom 开启时高亮材质是否影响阴影可读性。

实现建议优先级：

1. 第一版可以复用 `assets/models/material_ball_validation_fixture.gltf`，作为一个 additional model 放入中庭，并通过 transform 放大/摆放。
2. 如果现有 material ball fixture 的球体细分不足或位置不适合，则新增轻量 `shadow_probe_spheres.gltf`，包含 3 ~ 5 个更圆的高细分球体和简单 PBR 材质。
3. 暂不做程序化 sphere mesh runtime 生成，除非后续需要把 primitive mesh builder 纳入 renderer 公共能力。

推荐第一版布局：

- DamagedHelmet 保持在中庭中心附近，作为复杂 PBR 模型。
- 球体沿横向或斜向放置 3 ~ 5 个，覆盖近处、中距和接近 cascade split 的区域。
- 球体半径保持比 Helmet 小，避免喧宾夺主。
- 材质建议包含 matte gray、rough metal、smooth dielectric、bright emissive-like base color 这几类，用于同时观察阴影、IBL 和 tone mapping。

## CSM 关键设计

### Cascade Settings

建议新增最小设置结构，挂到 `ShadowSettings` 或新的 `CascadeShadowSettings`：

```cpp
struct CascadeShadowSettings {
    bool enabled = false;
    u32 cascadeCount = 4;
    float splitLambda = 0.65f;
    float maxDistance = 80.0f;
    rhi::Extent2D cascadeExtent{2048, 2048};
    bool stabilize = true;
};
```

设计约束：

- `cascadeCount` 第一版支持 1 / 2 / 4 即可。
- `splitLambda` 用于 linear split 和 logarithmic split 混合。
- `maxDistance` 限制 shadow 覆盖距离，避免 far plane 过远导致级联浪费。
- `stabilize` 复用 Phase 0.64 texel snapping 思路，降低相机移动时的闪烁。

### Cascade Data

建议新增 renderer 层数据结构：

```cpp
struct ShadowCascade {
    float nearDistance = 0.0f;
    float farDistance = 0.0f;
    glm::mat4 lightViewProjection{1.0f};
    Bounds3 worldBounds;
};

struct CascadeShadowFrameData {
    std::array<ShadowCascade, MaxShadowCascades> cascades;
    u32 cascadeCount = 0;
};
```

第一版可以先固定 `MaxShadowCascades = 4`。后续如果做 atlas 或 bindless，再考虑动态数量。

### Split 计算

建议使用经典 practical split scheme：

```text
linear = near + (far - near) * i / cascadeCount
log = near * pow(far / near, i / cascadeCount)
split = mix(linear, log, splitLambda)
```

注意：

- near/far 使用 camera near 和 `min(cameraFar, shadow.maxDistance)`。
- split 结果需要单调递增。
- 需要避免 near 太小导致 log split 精度异常。
- 测试中覆盖 `splitLambda = 0`、`0.5`、`1`。

### Light Matrix Fit

每个 cascade 的 light-space 矩阵建议这样计算：

- 取 camera frustum slice 的 8 个 world-space corners。
- 计算 slice bounds 或 bounding sphere。
- 使用 main light direction 构造 light view。
- 在 light space 中拟合 orthographic bounds。
- 对 ortho center 做 texel snapping。
- 生成 `lightViewProjection`。

第一版可以继续使用 AABB / sphere fit 的保守方式，目标是稳定和正确，而不是最紧投影。

## 渲染接入方案

### ShadowPass

本阶段有两种实现路径（采用第二种）：

1. **最小风险路径**：为每个 cascade 创建独立 shadow texture / view，循环渲染 ShadowPass。
2. **更接近最终路径**：创建 shadow texture array，每个 cascade 一个 array layer。

ShadowPass 仍然消费 full queue，不使用 camera-cull 后的 forward queue。

### ForwardPass / Shader

Forward shader 需要新增：

- cascade light view-projection 数组。
- cascade split distances。
- cascade count。
- shadow map array 或多个 shadow map descriptor。

片元阶段选择 cascade：

- 使用 view-space depth 或 camera-space distance。
- 找到第一个 `depth <= cascadeSplit[i]` 的 cascade。
- 用该 cascade 的 light matrix 做 shadow map lookup。

第一版可以继续使用现有 PCF 过滤，只是针对选中的 cascade 采样。

### UI

Sandbox Shadow 面板建议增加：

- CSM Enabled。
- Cascade Count。
- Split Lambda。
- Shadow Distance。
- Cascade Extent。
- Stabilize。
- Debug Cascade Index / Show Cascade Color。

第一版 debug color 可以作为可选任务：开启后在 Forward shader 中按 cascade 混入颜色，方便观察 split 是否合理。

## 分阶段任务

### 0.67.0 文档与范围确认

- 确认本阶段目标是 CSM 最小闭环和 shadow validation scene。
- 确认不做 instanced rendering、不做 GPU culling、不做 VSM/EVSM。
- 确认 Sponza 放大和球体观察对象作为本阶段验证资产调整。
- 确认 ShadowPass 仍消费 full queue。

实现结果：
- 本阶段范围确认聚焦 CSM 最小闭环和 sandbox shadow validation scene。
- CSM 渲染核心留到 0.67.2 之后推进；0.67.1 只调整验证场景、preset contract 和加载测试。
- GPU instanced rendering 暂不纳入本阶段，球体观察对象继续走现有 additional model / DrawItem 路径。
- ShadowPass full queue contract 保持不变，不引入 camera-cull shadow caster 风险。

### 0.67.1 Sandbox Shadow Validation Scene

- 调整默认 Sponza scale，建议从 `5.0f` 提升到 `8.0f` 或 `10.0f`。（已调整完成）
- 将 DamagedHelmet 重新放置到中庭合适高度和位置。
- 添加一组球体观察对象；若 `material_ball_validation_fixture.gltf` 的低细分球体不够圆，则改用专门的高细分 `shadow_probe_spheres.gltf`。
- 确认 scene bounds、shadow scene-fit、frustum culling stats 在更大场景下仍正常。
- 补充 preset / scene resource 测试，确认默认场景 loaded model count 和 additional model transform 合理。

实现结果：
- `DefaultSponzaScale` 从 `5.0f` 提升到 `8.0f`，让默认 Sponza / Sponza / ShadowValidation preset 都进入更适合 CSM 验证的大场景尺度。
- 默认 sandbox 继续加载 Sponza 作为主模型，并保留 DamagedHelmet 作为中庭复杂 PBR 模型。
- 默认 sandbox 新增 `shadow_probe_spheres.gltf` 作为 `DefaultSandboxShadowProbeSpheres`，通过 transform 放置到中庭附近，用于观察接触阴影、受影、IBL 和 tone mapping 组合表现。
- `shadow_probe_spheres.gltf` 使用 48 segments x 24 rings 的高细分 UV sphere，避免旧 material ball fixture 的低面数多边形边缘影响阴影视觉判断。
- `ShadowValidation` preset 也新增同一组 shadow probe spheres，便于后续专门验证阴影。
- 更新 `ark_renderer_preset_smoke`，锁定默认场景 additional model 数量、Sponza scale、Helmet transform 和 shadow probe transform。
- 更新 `ark_scene_resource_smoke`，验证 Sponza + DamagedHelmet + shadow probe spheres 三模型组合能实际加载，并能进入 RenderQueue。
- 更新 `ark_gltf_loader_smoke`，锁定 `shadow_probe_spheres.gltf` 的 5 个高细分球体、材质数量、顶点/索引数量和基础 tangent/normal 数据。
- Targeted build 通过：`ark_renderer_preset_smoke`、`ark_scene_resource_smoke`、`ark_sandbox`。
- Targeted CTest 通过：`ark_renderer_preset_smoke`、`ark_scene_resource_smoke`。
- Sandbox hidden-window smoke 通过，默认组合场景可启动并稳定运行 4 秒。

### 0.67.2 CSM Settings / Data Contract

- 新增 cascade shadow settings。
- 新增 cascade frame data / cascade descriptor。
- 保持默认关闭或以单 cascade fallback 兼容现有阴影路径。
- 将设置接入 `RenderView` / `RendererPreset` / sandbox runtime settings。

实现结果：
- 在 `ShadowSettings` 中新增 `CascadeShadowSettings cascades`，当前默认关闭 CSM，多 cascade 渲染路径仍保持未接入。
- 新增 `ShadowConstants.h` 与 `MaxShadowCascadeCount = 4`，第一版 contract 固定最多 4 级 cascade，后续 ShadowPass texture array / atlas 继续沿用该上限。
- `CascadeShadowSettings` 包含 `enabled / cascadeCount / splitLambda / maxDistance / cascadeExtent / stabilize`，覆盖后续 split/matrix builder 和 UI 所需的最小参数。
- `RenderView::setShadowSettings()` 会 sanitize cascade settings：`cascadeCount` 归一到 `1 / 2 / 4`，`splitLambda` 限制到 `[0, 1]`，`maxDistance` 保持大于 shadow near plane，`cascadeExtent` 限制到 `[128, 4096]`。
- 新增 `ShadowCascade` 与 `CascadeShadowFrameData`，用于描述每级 cascade 的 near/far distance、light view-projection 和 world bounds。
- `FrameContext` 新增 `cascadeShadows` 字段，作为 ShadowPass 到 ForwardPass 的 CSM 帧数据传递位置。
- `ShadowPass` 在清空 shadow binding 时同步清空 `cascadeShadows`，避免未来读到旧帧 cascade 数据。
- 运行时桥接继续复用 `RenderViewProfileDesc -> SandboxRuntimeSettings -> RenderView` 的现有路径，没有让 UI 直接修改 renderer 内部对象。
- Targeted build 通过：`ark_framework_headers_smoke`、`ark_renderer_preset_smoke`、`ark_sandbox_ui_settings_smoke`、`ark_shadow_pass_smoke`、`ark_forward_pass_pipeline_smoke`。
- Targeted CTest 通过：`ark_framework_headers_smoke`、`ark_renderer_preset_smoke`、`ark_sandbox_ui_settings_smoke`、`ark_shadow_pass_smoke`、`ark_forward_pass_pipeline_smoke`。

### 0.67.3 Cascade Split / Matrix Builder

- 实现 split 计算。
- 实现 camera frustum slice corner 计算。
- 实现 per-cascade light-space ortho fit。
- 复用 texel snapping 稳定每级 shadow projection。
- 补充单元测试覆盖 split 单调性、lambda 行为、矩阵有限性和 bounds 合理性。

实现结果：
- 新增 `ShadowCascadeBuilder`，集中负责 CSM split 计算和 per-cascade light-space matrix 构建，避免把 CSM 数学继续塞进 `ShadowPass`。
- 新增 `computeCascadeSplitDistances()`，使用 practical split scheme，并覆盖 `splitLambda = 0 / 0.5 / 1` 的行为。
- `RenderView` 新增 camera clip range 保存与查询接口；默认透视、glTF camera、orbit preset 都会写入 near/far，CSM split 使用 `cameraNear -> min(cameraFar, shadow.cascades.maxDistance)`。
- builder 会根据当前 view/projection 生成每个 camera frustum slice 的 world bounds，并用主方向光方向构造 light view。
- 每级 cascade 使用 light-space AABB 保守拟合 orthographic projection，并沿用 Vulkan Y 翻转约定和 texel snapping 逻辑稳定投影。
- `ShadowPass::prepare()` 在发布单 shadow binding 的同时生成 `FrameContext::cascadeShadows`；当前仍不改变真实单 shadow map 渲染路径。
- 新增 `ark_shadow_cascade_builder_smoke`，验证 split 单调性、lambda 行为、矩阵 finite、cascade bounds 可投影到 clip 体内。
- 更新 `ark_shadow_pass_smoke`，验证 CSM 开启后 `ShadowPass` 能发布 cascade frame data，关闭 shadow 时会清空旧数据。
- Targeted build 通过：`ark_shadow_cascade_builder_smoke`、`ark_shadow_pass_smoke`、`ark_framework_headers_smoke`、`ark_forward_pass_pipeline_smoke`、`ark_renderer_preset_smoke`。
- Targeted CTest 通过：`ark_shadow_cascade_builder_smoke`、`ark_shadow_pass_smoke`、`ark_framework_headers_smoke`、`ark_forward_pass_pipeline_smoke`、`ark_renderer_preset_smoke`。
- Full Debug build 通过。
- Full CTest 结果为 32/33：`ark_frame_validation_smoke` 的 `default_composite_scene` golden diff 超阈值（meanAbsError=0.0628158），本阶段不顺手更新默认组合场景 PNG baseline，留到视觉基线同步任务单独处理。

### 0.67.4 ShadowPass CSM Render Path

- 支持按 cascade 渲染 shadow map。
- 明确 shadow map 资源布局：texture array 或多 texture。
- 保持 PCF 参数、bias、strength 与现有 ShadowSettings 兼容。
- 保持 full queue 消费，不被 camera frustum culling 影响。

实现结果：
- RHI 新增 `TextureViewType::Texture2DArray`，Vulkan backend 映射到 `VK_IMAGE_VIEW_TYPE_2D_ARRAY`；`Texture2D` 单层 view 仍要求 `arrayLayerCount = 1`，避免旧路径语义变宽。
- `ShadowPass` 根据 `ShadowSettings::cascades.enabled` 选择 shadow target：非 CSM 继续创建单层 `Texture2D` shadow map；CSM 创建一张 `D32Float` texture array，层数等于 sanitized cascade count，边长使用 `cascades.cascadeExtent`。
- CSM 资源拆分为两类 view：`m_ShadowMapView` 是整张 texture array 的采样 view，逐 cascade 渲染时使用 `m_ShadowCascadeViews[index]` 这样的单层 `Texture2D` view。
- `ShadowPass::execute()` 在 CSM 开启时只对 array texture 做一次 `DepthStencilWrite -> ShaderResource` 生命周期转换，中间按 cascade 逐层 begin/end rendering 并写入对应 `lightViewProjection`。
- 单 shadow map 路径继续走旧的 draw contract，默认 sandbox 不会因为本阶段提前启用 CSM shader 采样而改变。
- `ark_shadow_pass_smoke` 拆分覆盖单图路径和 CSM render path：验证 CSM texture array desc、sampled view、per-layer render target view、每级 cascade draw 次数和 barrier 次数。
- Targeted build 通过：`ark_shadow_cascade_builder_smoke`、`ark_shadow_pass_smoke`、`ark_framework_headers_smoke`、`ark_forward_pass_pipeline_smoke`、`ark_renderer_preset_smoke`。
- Targeted CTest 通过：`ark_shadow_cascade_builder_smoke`、`ark_shadow_pass_smoke`、`ark_framework_headers_smoke`、`ark_forward_pass_pipeline_smoke`、`ark_renderer_preset_smoke`。
- Full Debug build 通过。
- Full CTest 结果为 32/33：`ark_frame_validation_smoke` 的 `default_composite_scene` golden diff 仍超阈值（meanAbsError=0.0628158），与 0.67.3 的已知视觉基线问题一致，本阶段不更新 PNG baseline。
- Sandbox hidden-window smoke 通过，默认非 CSM 路径可正常启动。

### 0.67.5 ForwardPass / Shader CSM Sampling

- 扩展 lighting uniform 或新增 shadow uniform，传递 cascade matrices 和 split distances。
- shader 根据 view-space depth 选择 cascade。
- 采样对应 cascade shadow map。
- 保留 non-CSM fallback，避免已有 shadow tests 回归。

### 0.67.6 Sandbox UI / Diagnostics

- Shadow 面板增加 CSM 开关和 cascade 参数。
- 显示 cascade count、split distances、shadow distance、shadow texture extent。
- 可选增加 cascade debug color mode。
- 默认场景中确认 Sponza、DamagedHelmet 和球体均可用于观察阴影。

### 0.67.7 Tests

建议覆盖：

- CSM settings sanitize / default。
- split distances 单调递增。
- `splitLambda = 0 / 1` 行为正确。
- cascade matrix 全部 finite。
- ShadowPass CSM 资源创建 smoke。
- ForwardPass shader descriptor / uniform contract smoke。
- sandbox runtime settings 能写回 CSM 设置。
- 默认 shadow validation scene loaded model count 符合预期。
- 旧 non-CSM shadow path 不回归。

### 0.67.8 验证与收尾

- 更新 `docs/phase/phase67.md` 实施状态。
- targeted build / CTest。
- full Debug build / full CTest。
- sandbox hidden-window smoke。
- 手动打开 sandbox，确认：
  - Sponza 尺寸更适合作为大场景。
  - DamagedHelmet 位于中庭可见区域。
  - 球体能投射和接收阴影。
  - CSM 开关、split lambda、shadow distance 调整后画面有可观察变化。
  - Bloom、ACES ToneMapping、IBL、Frustum Culling 仍正常。
- 提交并推送。

## 风险与约束

- 最大风险是 shadow map descriptor 和 shader uniform 改动较大，容易影响现有 ForwardPass 资源绑定。
- 第二风险是 cascade light matrix fit 不稳定，导致相机移动时阴影抖动。
- 第三风险是放大 Sponza 后 scene bounds 变大，单 cascade fallback 阴影质量看起来更差；这是验证 CSM 的预期压力，但不应导致渲染错误。
- 第四风险是把 CSM 与 instanced rendering 混在一起，导致 RenderQueue、descriptor、shader 改动过大。

应对方式：

- 先完成 shadow validation scene 和 CSM data contract，再改 ShadowPass。
- CSM 默认可以先关闭，保证旧路径可回归。
- shader descriptor 改动保留 fallback 资源，避免没有 CSM 资源时崩溃。
- 球体先复用现有 fixture 或小型 glTF 资产，不引入 instancing 和新库。

## 完成标准

- 默认 sandbox 场景中 Sponza 更大，DamagedHelmet 和球体观察对象可见。
- CSM 开启后，近处阴影质量明显优于单张 shadow map。
- CSM 关闭时，现有单 shadow map 路径仍可工作。
- ShadowPass 不受 camera frustum culling 错误影响。
- Sandbox UI 可以调节 CSM 参数并观察结果。
- targeted tests 和 full CTest 通过。
- sandbox hidden-window smoke 通过。

## 后续方向

0.67 完成后，建议继续：

1. Phase 0.68：Shadow Debug Visualization，包括 cascade split overlay、shadow map preview、light frustum debug。
2. Phase 0.69：GPU instanced rendering foundation，将相同 mesh/material 的多个 DrawItem 合批。
3. Phase 0.70：Material / Texture debug views，用于复杂 glTF 场景排查。
4. Phase 0.71：Auto exposure / histogram / tone mapping debug controls。
