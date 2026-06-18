# Phase 0.66 Visibility / Frustum Culling Foundation

## 实施状态

已完成 0.66.0 文档与范围确认、0.66.1 Frustum 数据结构、0.66.2 DrawItem World Bounds、0.66.3 Visibility Build Path、0.66.4 Sandbox Visibility Diagnostics、0.66.5 Tests，以及 0.66.6 验证与收尾。

Phase 0.61 ~ 0.65 已经完成了 scene bounds、shadow scene-fit、PCF shadow filtering、sandbox debug UI 和运行时参数调节基础。当前默认 sandbox 已经具备 Sponza + DamagedHelmet、Shadow、Bloom、ACES ToneMapping、IBL、KTX texture path 和 debug UI。下一阶段适合把现有 bounds 能力推进到可见性系统，为后续 CSM、shadow debug visualization 和更复杂场景性能优化打基础。

本阶段建议先做 **Visibility / Frustum Culling Foundation**，而不是直接进入 CSM。原因是 CSM 本身也需要稳定的视锥、bounds、caster/receiver 范围和调试统计。如果先把可见性接口打清楚，后续 CSM 的实现会更稳，也更不容易把 RenderQueue、ShadowPass 和 ForwardPass 的职责搅在一起。

## 阶段目标

- 新增相机视锥数据结构，支持从 view-projection 矩阵提取 6 个裁剪平面。
- 让 `DrawItem` 或等价可见性记录携带 world-space AABB。
- 在 renderer 内建立清晰的 visibility contract：Forward 可按 camera frustum cull，Shadow caster 不能被 camera frustum 误剔除。
- 为 sandbox debug UI 提供基础可见性统计，例如 total / visible / culled / shadow caster count。
- 为后续 CSM、shadow caster culling、debug visualization、GPU occlusion / Hi-Z 预留接口。

## 非目标

- 不做 GPU occlusion culling。
- 不做 Hi-Z depth pyramid。
- 不做 BVH、octree、scene graph 或复杂空间加速结构。
- 不做 per-triangle / per-cluster culling。
- 不做完整 CSM。
- 不把 camera frustum culling 直接应用到全局共享 RenderQueue 后再喂给 ShadowPass。
- 不为了本阶段改写材质系统、资源系统或 glTF loader。

## 为什么现在适合做锥体剔除

当前项目已经具备做 object-level frustum culling 的关键前置条件：

- `Bounds3` 已经提供 AABB 表达、合并和 transform 能力。
- `MeshResource` 已经能从顶点生成 local bounds。
- `ModelResource` 已经能基于 primitive instance 合并 model local bounds。
- `RenderScene` 已经能合并 world scene bounds。
- `ShadowPass` 已经验证了 scene bounds 在真实渲染路径中的价值。
- 默认 Sponza + DamagedHelmet 场景已经足够复杂，适合观察可见性统计和剔除效果。

因此，本阶段不需要新资源，也不需要新库。GLM + 现有 bounds 系统足够完成 CPU object-level frustum culling。

## 关键正确性约束

### 不要直接剔除共享 RenderQueue

当前渲染流程中，RenderQueue 会被 ShadowPass 和 ForwardPass 共同消费。如果直接在 `RenderQueue::build()` 中按 camera frustum 删除 draw item，会导致一个经典阴影错误：

- 物体本身在相机视锥外；
- 但它的阴影投射到了相机视锥内；
- 如果该物体被相机剔除，ShadowPass 就无法渲染它；
- 最终画面中会丢失本该存在的阴影。

因此本阶段的硬约束是：

- Camera frustum culling 只能作用于主视图 Forward / visible queue。
- ShadowPass 初期继续使用未被 camera cull 的 shadow caster queue。
- 后续如需 shadow caster culling，应基于 light frustum、cascade bounds 或 receiver bounds 单独实现。

### Bounds 类型

本阶段使用 AABB。

理由：

- 当前 `Bounds3` 已经是 AABB。
- glTF primitive、model instance、scene object 都能自然生成 world AABB。
- CPU object-level frustum culling 用 AABB 足够稳定。
- OBB / bounding sphere 可以后续作为精度或性能优化，不作为本阶段目标。

## 推荐接口设计

### Frustum

建议新增 `src/renderer/Frustum.h` / `Frustum.cpp`，提供最小接口：

```cpp
class Frustum {
public:
    static Frustum fromViewProjection(const glm::mat4& viewProjection);

    bool intersects(const Bounds3& bounds) const;
    bool contains(const Bounds3& bounds) const;
};
```

实现要求：

- 从 view-projection 矩阵提取 left / right / bottom / top / near / far 平面。
- 平面需要归一化，避免距离测试受矩阵缩放影响。
- `intersects(Bounds3)` 使用 AABB positive vertex / negative vertex 或 8-corner 测试均可，优先选择简单稳定实现。
- 对 invalid bounds 保守返回 visible，避免资源异常时错误剔除。

### DrawItem World Bounds

建议为 `DrawItem` 增加：

```cpp
Bounds3 worldBounds;
```

生成规则：

- `SceneModel`：`mesh.localBounds` 通过 `model.transform * instance.localTransform` 转换为 world bounds。
- `SceneObject`：`mesh.localBounds` 通过 object transform 转换为 world bounds。
- 如果 mesh bounds invalid，则 draw item 保守视为 visible。

这里需要补充中文注释，说明 world bounds 是用于 CPU visibility / debug / shadow range 的粗粒度包围盒，不代表精确碰撞体。

### Visibility Result

建议不要让 `RenderQueue::build()` 同时承担“收集 draw item、剔除、统计、按 pass 分队列”的所有职责。更稳的做法是引入一个轻量 build desc / result：

```cpp
struct RenderQueueBuildDesc {
    const RenderScene* scene = nullptr;
    const glm::vec3* cameraPosition = nullptr;
    const Frustum* cameraFrustum = nullptr;
    bool enableFrustumCulling = false;
};

struct RenderQueueStats {
    usize totalItems = 0;
    usize visibleItems = 0;
    usize culledItems = 0;
    usize invalidBoundsItems = 0;
};
```

第一版可以保持 `RenderQueue` 内部实现简单：

- `build(scene, cameraPosition)` 保持兼容旧路径。
- 新增 `build(desc)` 支持可选 frustum culling。
- `stats()` 暴露最近一次 build 的统计。

如果实现过程中发现 shared queue 边界容易混乱，可以进一步拆成：

- `m_ForwardQueue`
- `m_ShadowQueue`

其中 forward queue 支持 camera culling，shadow queue 初期不做 camera culling。

### FrameRenderer / Renderer 边界

推荐规则：

- `Renderer` 负责根据 `RenderScene`、`RenderView` 和 settings 构建队列。
- `FrameRenderer` 只消费已经准备好的 queue，不自行决定可见性策略。
- `ShadowPass` 不知道 camera culling 细节。
- `ForwardPass` 只接收 forward-visible draw items。

如果本阶段要避免大改 `FrameContext`，可以先做到：

- DrawItem 携带 world bounds；
- RenderQueue 具备可选 culling 和 stats；
- sandbox UI 展示 stats；
- 默认渲染路径先不启用剔除；
- 后续再单独改 FrameContext 支持 forward/shadow 双队列。

但更推荐本阶段直接建立双队列或明确的 visibility result，因为这能一次性消除 shadow correctness 风险。

## Sandbox UI 调试项

建议在 Diagnostics 或新增 Visibility 面板中加入：

- Frustum Culling Enabled。
- Total draw items。
- Forward visible items。
- Culled items。
- Invalid bounds items。
- Shadow caster items。

第一版不需要画 debug lines。只要能看到统计变化，就足够验证基础链路。

后续 Phase 可以加入：

- 视锥线框显示。
- AABB debug draw。
- culled / visible 颜色标记。
- cascade split overlay。

## 分阶段任务

### 0.66.0 文档与范围确认

- 确认本阶段目标是 visibility / frustum culling foundation。
- 确认本阶段不做 CSM，不做 GPU culling。
- 确认 camera frustum culling 不得影响 ShadowPass shadow caster。
- 确认使用 AABB 作为本阶段 bounds 类型。

当前确认结果：

- Phase 0.66 聚焦 CPU object-level visibility foundation，不直接推进 CSM。
- 本阶段使用现有 `Bounds3` AABB，暂不引入 OBB、bounding sphere 或空间加速结构。
- Camera frustum culling 只能用于 Forward / visible queue，ShadowPass 初期必须继续使用未被 camera cull 的 shadow caster queue。
- Runtime 开关和 debug stats 后续继续走 sandbox debug UI / runtime settings bridge，不让 UI 直接修改 renderer 内部对象。

### 0.66.1 Frustum 数据结构

- 新增 `Frustum` 和 plane 表达。
- 从 view-projection 矩阵提取 6 个平面。
- 支持 `intersects(Bounds3)`。
- 对 invalid bounds 保守返回 visible。
- 补充必要中文注释。

实现结果：

- 新增 `src/renderer/Frustum.h` / `src/renderer/Frustum.cpp`。
- 新增 `FrustumPlane` 和 `FrustumPlaneId`。
- `Frustum::fromViewProjection()` 从 world-to-clip view-projection 矩阵提取 left / right / bottom / top / near / far 6 个平面。
- 按当前 Vulkan / GLM zero-to-one depth 约定处理裁剪空间：`-w <= x <= w`、`-w <= y <= w`、`0 <= z <= w`。
- 所有平面在提取后归一化，供稳定 signed distance 测试使用。
- `intersects(Bounds3)` 使用 center signed distance + projected radius 做保守相交测试；invalid bounds 返回 true，避免错误剔除资源异常对象。
- `contains(Bounds3)` 使用 center signed distance - projected radius 做完全包含测试；invalid bounds 返回 false。
- 当前实现保留 `intersects()` / `contains()` 二值接口，但内部写法已经接近 `Outside / Intersecting / Inside` 三态分类，方便后续 visibility stats、CSM 和 debug visualization 扩展。
- 新增 `ark_frustum_smoke`，覆盖 identity zero-to-one clip volume、真实 perspective view-projection、invalid bounds 保守策略和平面归一化。

### 0.66.2 DrawItem World Bounds

- 为 draw item 增加 world bounds。
- 在 RenderQueue build 时计算 mesh primitive 的 world AABB。
- 保持已有 opaque / mask / blend bucket 和透明排序逻辑。
- 统计 invalid bounds item 数量。
- 补充必要中文注释。

实现结果：
- `DrawItem` 新增 `worldBounds`，作为 CPU visibility / debug 用的粗粒度 world-space AABB。
- `RenderQueue` 在收集 `SceneModel` primitive instance 和 `SceneObject` 时，使用 `mesh.localBounds()` 与最终 `modelMatrix` 计算 draw-item world bounds。
- 新增最小版 `RenderQueueStats`，当前记录 `totalItems` 与 `invalidBoundsItems`，为 0.66.3 的 visible / culled stats 预留扩展点。
- 保持 opaque / mask / blend bucket 逻辑和 Blend back-to-front 排序不变。
- 当前阶段只补齐 world bounds 数据，不启用 camera frustum culling。

### 0.66.3 Visibility Build Path

- 新增可选 frustum culling build path。
- 增加 RenderQueue stats 或 Visibility stats。
- 默认保持兼容旧调用。
- 明确 Forward queue 和 Shadow caster queue 的关系。
- 如调整 FrameContext，需要保持 ShadowPass 不消费 camera-cull 后的队列。

实现结果：
- 新增 `RenderQueueBuildDesc`，通过 `scene / cameraPosition / cameraFrustum / enableFrustumCulling` 显式描述 build 策略。
- 旧接口 `build(scene)` 与 `build(scene, cameraPosition)` 保持兼容，默认不启用 frustum culling，仍生成全量 draw queue。
- `build(desc)` 在 `enableFrustumCulling=true` 且提供 `cameraFrustum` 时按 `DrawItem::worldBounds` 执行 CPU object-level culling。
- `RenderQueueStats` 扩展为 `totalItems / visibleItems / culledItems / invalidBoundsItems`。
- invalid bounds 保守视为 visible，避免资源异常或尚未生成 bounds 的对象被误裁剪。
- 当前阶段仍不改变 ShadowPass / FrameRenderer 消费路径；Shadow caster 队列不会被 camera frustum culling 隐式影响。
- 测试覆盖默认全量队列、显式 culling 队列、invalid bounds 保守策略、empty desc、bucket 顺序与 Blend 远到近排序。

### 0.66.4 Sandbox Visibility Diagnostics

- 在 debug UI 中显示 visibility stats。
- 提供 frustum culling 开关。
- 默认策略建议：
  - 开发阶段默认关闭，便于回归；
  - 测试稳定后 sandbox 可默认开启 forward culling；
  - Shadow caster path 保持不受 camera culling 影响。

实现结果：
- `RenderView` 新增 `VisibilitySettings`，当前只包含 `enableFrustumCulling`；sandbox UI 仍通过 `SandboxRuntimeSettings` 写回 `RenderView`，不直接修改 renderer 内部对象。
- `Renderer` 每帧始终构建完整 `m_RenderQueue`，作为 shadow caster / fallback full queue；当 `visibility.enableFrustumCulling=true` 时，额外构建 `m_ForwardRenderQueue`。
- `FrameContext` 新增可选 `forwardQueue`；`ShadowPass` 继续消费 `queue`，`ForwardPass` 优先消费 `forwardQueue`，不存在时回退到 `queue`。
- `SandboxDebugUi` 新增 `Visibility` 面板，显示 Forward visible / total、culled、invalid bounds 和 shadow caster 数量，并提供 `Frustum Culling` 开关。
- UI 统计来自上一帧 renderer 提交到 overlay 的 `FrameContext`，因此保持 UI 生命周期简单，允许一帧延迟。

### 0.66.5 Tests

建议覆盖：

- Frustum plane extraction smoke。
- AABB inside / outside / intersect。
- invalid bounds 保守可见。
- RenderQueue culling 后 opaque / mask / blend 数量正确。
- 透明物体排序不被破坏。
- Shadow caster queue 不使用 camera-cull 后的 forward visible queue。
- sandbox UI settings bridge 中 visibility 开关可写回 runtime settings。

实现结果：
- 新增 / 更新 `ark_frustum_smoke`、`ark_render_scene_queue_smoke`、`ark_model_resource_smoke`、`ark_forward_pass_pipeline_smoke`、`ark_sandbox_ui_settings_smoke` 与 `ark_framework_headers_smoke` 覆盖本阶段契约。
- `ark_forward_pass_pipeline_smoke` 覆盖 `FrameContext::forwardQueue` 优先级，确认 ForwardPass 使用 camera-visible queue，同时保留 `queue` 作为 shadow / fallback full queue。
- `ark_sandbox_ui_settings_smoke` 与 `ark_framework_headers_smoke` 覆盖 `VisibilitySettings` 从 preset / runtime settings 写回 `RenderView` 的路径。
- `ark_render_scene_queue_smoke` 覆盖默认全量队列、显式 frustum culling、invalid bounds 保守可见、bucket 顺序和透明排序。

### 0.66.6 验证与收尾

- 更新 `docs/phase/phase66.md` 实施状态。
- 更新必要 handoff。
- targeted build / CTest。
- full Debug build / full CTest。
- sandbox hidden-window smoke。
- 如启用 UI 统计，手动确认默认 Sponza + DamagedHelmet 场景中 stats 正常变化。
- 提交并推送。

验证结果：
- Targeted build 通过：`ark_render_scene_queue_smoke`、`ark_model_resource_smoke`、`ark_frustum_smoke`、`ark_forward_pass_pipeline_smoke`、`ark_sandbox_ui_settings_smoke`、`ark_framework_headers_smoke`、`ark_sandbox`。
- Targeted CTest 通过：7/7。
- Full Debug CTest 通过：32/32。
- Sandbox hidden-window smoke 通过：`ark_sandbox.exe` 启动稳定运行 4 秒后由脚本关闭。

## 风险与约束

- 最大风险是把 camera-visible queue 错误复用于 ShadowPass，导致离屏投影物阴影消失。
- 第二风险是 bounds transform 错误，尤其是非均匀缩放或 instance local transform 合并顺序错误。
- 第三风险是透明物体排序与 culling build path 混在一起，导致 blend 顺序回归。
- 第四风险是 UI 开关直接修改 renderer 内部对象，破坏现有 runtime settings bridge。

应对方式：

- 保持 culling 数据流显式，不隐式修改全局共享队列。
- DrawItem world bounds 只作为粗粒度可见性数据，不影响材质和资源生命周期。
- 所有 runtime 开关继续走 sandbox runtime settings，再写入 RenderView 或 build desc。
- Shadow caster culling 延后到 light-space / cascade-space 设计清楚之后。

## 完成标准

- 项目拥有可复用的 Frustum / AABB intersection 基础能力。
- DrawItem 或 VisibilityItem 携带 world-space bounds。
- Forward visibility 可以选择启用 camera frustum culling。
- ShadowPass 不会因为 camera frustum culling 丢失 shadow caster。
- sandbox UI 可以观察基础 visibility stats。
- targeted tests 和 full CTest 通过。
- 默认 sandbox 场景正常启动，Sponza + DamagedHelmet、Shadow、Bloom、ToneMapping、IBL 行为不回归。

## 后续方向

0.66 完成后，建议进入：

1. Phase 0.67：CSM / Cascaded Shadow Map 设计与最小实现。
2. Phase 0.68：Shadow debug visualization，包括 shadow map preview、cascade split overlay、light frustum debug。
3. Phase 0.69：Auto exposure / histogram / tone mapping debug controls。
4. Phase 0.70：Material / texture debug view，用于复杂 glTF 资产验证。
