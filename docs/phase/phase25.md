# Phase 0.25 可配置 Scene Light / Camera

## 阶段判断

Phase 0.24 已经把 glTF `doubleSided` 精确落实到 ForwardPass raster culling。到这里，当前 forward 渲染主线的材质、纹理、alpha state、RenderQueue 顺序和基本 pipeline state 已经比较稳定：

```text
glTF / asset
    -> ModelResource / MaterialResource
    -> RenderScene
    -> RenderQueue
    -> ForwardPass
    -> mesh shaders
```

下一处最影响后续画面质量和架构清晰度的缺口，是 camera / light 仍然没有成为 renderer 可消费的 scene data。

当前 camera 已经通过 `RenderView` 传入 `FrameContext`，但它只保存 view/projection matrix；ForwardPass 的 light 仍在 `makeLightingUniform()` 中硬编码：

```cpp
uniform.lightDirection = glm::vec4{glm::normalize(glm::vec3{-0.35f, -0.8f, -0.45f}), 0.0f};
uniform.lightColor = glm::vec4{1.0f, 0.96f, 0.88f, 1.0f};
uniform.ambientColor = glm::vec4{0.08f, 0.09f, 0.11f, 1.0f};
uniform.cameraPosition = glm::vec4{0.0f, 0.0f, -4.0f, 1.0f};
```

这会导致：

- `RenderScene` 只表达 drawables，不表达 lighting。
- `ForwardPass` 内部知道默认 light policy，不利于后续多场景、sandbox 配置或真实 scene loading。
- direct lighting BRDF、HDR/tone mapping 和 IBL 都缺少稳定的输入语义。
- 未来 Blend sorting 需要 camera position，目前只能临时从 view matrix 反推。

Phase 0.25 的重点，是把“当前已经存在的默认 camera/light”从硬编码逻辑提升为明确的数据入口。它不是光照质量升级 phase，而是 scene/view 语义整理 phase。

## 目标

Phase 0.25 目标：

- 为 renderer 层定义最小 scene lighting 数据结构。
- `RenderScene` 能保存一个默认 directional light 和 ambient light。
- 保持当前默认光照数值，确保 default sandbox 画面不发生无意变化。
- `ForwardPass` 从 `FrameContext::scene` 读取 lighting，而不是硬编码 light direction/color/ambient。
- `RenderView` 提供稳定的 camera position 获取方式，供 ForwardPass lighting uniform 和后续 RenderQueue sorting 使用。
- `RenderView::setDefaultPerspective()` 继续生成当前默认 camera。
- 保持 `LightingUniform` shader binding 和 descriptor layout 不变。
- 保持 `ForwardPass` pipeline key、alpha behavior、doubleSided culling 不回退。
- 新增或扩展 smoke tests，覆盖 scene light / camera position 到 ForwardPass uniform 的数据流。

## 非目标

Phase 0.25 暂不做：

- 不做多光源数组。
- 不做 point light / spot light / area light。
- 不做 shadow map。
- 不做 glTF camera 导入。
- 不做 glTF `KHR_lights_punctual`。
- 不做 editor / UI / ImGui 光照调参面板。
- 不做 orbit camera、WASD camera 或输入系统改造。
- 不做完整 direct lighting BRDF 升级。
- 不做 HDR framebuffer / tone mapping。
- 不做 IBL / environment map / BRDF LUT。
- 不做 RenderGraph 重构。
- 不做 transparent back-to-front sorting。
- 不做 shader descriptor layout 改动。
- 不做资源/场景加载入口的大改。

## 模块边界

继续遵守现有设计文档：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase23.md
docs/phase/phase24.md
```

边界要求：

- `asset/` 暂不解析 glTF camera 或 light。
- `RenderScene` 保存场景语义，包括 drawables 和 lighting，不创建 GPU 资源。
- `RenderView` 保存 camera 语义，包括 view/projection 和 camera position，不创建 GPU 资源。
- `FrameContext` 只借用 `RenderScene` / `RenderView`，不拥有它们。
- `ForwardPass` 只把 scene/view 数据转成 uniform，不负责选择默认 scene policy。
- RHI / Vulkan 不知道 light 或 camera。
- shader binding layout 保持不变，除非发现现有 `LightingUniform` 不能表达本阶段目标。
- `Application` / sandbox 可以设置默认 view；renderer 内部 default scene 可以设置默认 light。

## 当前行为

当前 application 每帧传入一个空 scene 和 default view：

```cpp
RenderScene scene;
RenderView view;
view.setDefaultPerspective(currentExtent);
...
m_Renderer->render(scene, view);
```

`Renderer` 如果收到空 scene，会使用内部 default sandbox scene：

```cpp
RenderScene& renderScene = scene.empty() && !m_DefaultScene.empty() ? m_DefaultScene : scene;
m_RenderQueue.build(renderScene);
```

`RenderView::setDefaultPerspective()` 使用固定 camera：

```cpp
m_View = glm::lookAt(glm::vec3{0.0f, 0.0f, -4.0f}, glm::vec3{0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
m_Projection = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 0.1f, 100.0f);
m_Projection[1][1] *= -1.0f;
```

`ForwardPass` 当前从 `RenderView` 读取 view/projection；camera position 通过 inverse view 临时反推：

```cpp
const glm::mat4 inverseView = glm::affineInverse(frameContext.view->viewMatrix());
uniform.cameraPosition = inverseView[3];
```

但 light direction/color/ambient 仍然固定在 ForwardPass 内。

因此当前数据流可以概括为：

```text
camera matrix: app RenderView -> FrameContext -> ForwardPass camera uniform
camera position: ForwardPass 从 RenderView view matrix 反推
lighting: ForwardPass hardcoded
```

Phase 0.25 之后应变为：

```text
camera matrix: app RenderView -> FrameContext -> ForwardPass camera uniform
camera position: RenderView -> FrameContext -> ForwardPass lighting uniform
lighting: RenderScene -> FrameContext -> ForwardPass lighting uniform
```

## 建议设计

### Scene Lighting

建议在 renderer 层定义最小 light 语义：

```cpp
struct DirectionalLight {
    glm::vec3 direction{glm::normalize(glm::vec3{-0.35f, -0.8f, -0.45f})};
    float intensity = 1.0f;
    glm::vec3 color{1.0f, 0.96f, 0.88f};
};

struct SceneLighting {
    DirectionalLight mainLight;
    glm::vec3 ambientColor{0.08f, 0.09f, 0.11f};
    float ambientIntensity = 1.0f;
};
```

也可以先不拆 `intensity`，直接保存最终 radiance/color：

```cpp
struct DirectionalLight {
    glm::vec3 direction{...};
    glm::vec3 color{1.0f, 0.96f, 0.88f};
};

struct SceneLighting {
    DirectionalLight mainLight;
    glm::vec3 ambientColor{0.08f, 0.09f, 0.11f};
};
```

本阶段推荐第二种，理由是它和当前 `LightingUniform` 完全对齐，能最小化 shader / uniform 变化。`intensity` 可以在 Phase 0.26 BRDF 或 Phase 0.27 HDR 后再引入，避免在 LDR 输出链路里过早讨论物理单位。

`RenderScene` 提供：

```cpp
const SceneLighting& lighting() const;
void setLighting(const SceneLighting& lighting);
```

默认值必须和 Phase 0.24 的 hardcoded values 一致。

### Camera View

`RenderView` 继续保存 view/projection matrix，但应补充 camera position：

```cpp
void setMatrices(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition);
const glm::vec3& cameraPosition() const;
```

兼容现有接口：

```cpp
void setMatrices(const glm::mat4& view, const glm::mat4& projection);
```

旧接口可以继续从 inverse view 反推 `cameraPosition`，但建议把反推封装在 `RenderView` 内，避免 ForwardPass 重复承担 camera 语义。

`setDefaultPerspective()` 设置：

```text
position = (0, 0, -4)
target   = (0, 0, 0)
up       = (0, 1, 0)
fovY     = 60 degrees
near     = 0.1
far      = 100
```

可选新增 helper：

```cpp
void setLookAtPerspective(const glm::vec3& position,
                          const glm::vec3& target,
                          const glm::vec3& up,
                          float verticalFovRadians,
                          float nearPlane,
                          float farPlane,
                          rhi::Extent2D extent);
```

本阶段是否增加该 helper，取决于测试和 sandbox 配置是否需要。不要为了未来完整 camera system 提前引入控制器类。

### ForwardPass

`ForwardPass::makeLightingUniform()` 改为：

```text
lighting = frameContext.scene ? frameContext.scene->lighting() : default SceneLighting
cameraPosition = frameContext.view ? frameContext.view->cameraPosition() : default camera position
```

并写入现有 uniform：

```cpp
uniform.lightDirection = glm::vec4{normalize(lighting.mainLight.direction), 0.0f};
uniform.lightColor = glm::vec4{lighting.mainLight.color, 1.0f};
uniform.ambientColor = glm::vec4{lighting.ambientColor, 1.0f};
uniform.cameraPosition = glm::vec4{view.cameraPosition(), 1.0f};
```

注意事项：

- 如果 light direction 近似零向量，应 fallback 到默认方向，避免 shader 中 normalize 产生无效值。
- 保持 `LightingUniform` size 和 binding 不变。
- 不改变 `mesh.frag.hlsl` 的 BRDF/lighting 公式。
- 不改变 descriptor set layout。

## 测试策略

本阶段优先补 renderer 层 smoke，不依赖真实 Vulkan。

建议新增或扩展：

```text
tests/forward_pass_lighting_smoke.cpp
```

测试目标：

- 构造自定义 `RenderScene::lighting()`。
- 构造自定义 `RenderView` camera position。
- 使用 fake RHI / fake DeviceContext 捕获 `updateBuffer()` 写入的 `LightingUniform`。
- 验证 ForwardPass 写入：
  - light direction 来自 scene。
  - light color 来自 scene。
  - ambient color 来自 scene。
  - camera position 来自 view。
- 验证未设置自定义 lighting 时使用默认值，并与 Phase 0.24 hardcoded values 一致。
- 验证 `RenderView::setDefaultPerspective()` 的 camera position 是 `(0, 0, -4)`。

可选扩展：

- 在 `render_scene_queue_smoke` 或独立 scene test 中覆盖 `RenderScene::setLighting()` 默认值和 round-trip。
- 在 `framework_headers_smoke` 中覆盖新增 public structs 能编译。

不建议：

- 不用截图验证 light uniform。
- 不依赖真实 Vulkan。
- 不为了测试改变 ForwardPass public API。
- 不把 `LightingUniform` 放进 public header；测试可通过 fake context 捕获 bytes，或通过较小 helper 保持 internal 结构稳定。

## 实施顺序

### 0.25.0 文档与范围确认

目标：

- 新增 `docs/phase/phase25.md`。
- 明确主线是可配置 scene light / camera。
- 明确不做 BRDF、HDR、IBL、RenderGraph、glTF lights/cameras。

审核点：

- 不重复 Phase 0.23 RenderQueue alpha bucket。
- 不重复 Phase 0.24 doubleSided culling。
- 不把 light/camera 语义放进 RHI/Vulkan。

### 0.25.1 RenderScene lighting 数据

目标：

- 新增 renderer 层最小 light 数据结构。
- `RenderScene` 保存 `SceneLighting`，默认值对齐当前 hardcoded light。
- 提供 `lighting()` / `setLighting()`。

审核点：

- `RenderScene` 仍不拥有 GPU 资源。
- 不引入 asset loader 改动。
- 默认值保证旧 sandbox 光照不无意变化。

### 0.25.2 RenderView camera position

目标：

- `RenderView` 保存 camera position。
- `setDefaultPerspective()` 写入当前默认 camera position。
- `setMatrices()` 兼容旧调用，必要时从 view matrix 反推 position。

审核点：

- projection Y flip 行为不变。
- view/projection public getter 不变。
- 不引入输入系统或 camera controller。

### 0.25.3 ForwardPass 接入

目标：

- `ForwardPass::makeLightingUniform()` 从 `FrameContext::scene` 和 `FrameContext::view` 取数据。
- 移除 light hardcode 作为主要路径，只保留 default fallback。
- 保持 descriptor layout / shader binding / uniform size 不变。

审核点：

- `LightingUniform` 仍为 64 bytes。
- binding 13 不变。
- `mesh.frag.hlsl` 不需要修改。
- alpha / doubleSided pipeline 行为不回退。

### 0.25.4 Tests

目标：

- 覆盖 `RenderScene` lighting 默认值与 set/get。
- 覆盖 `RenderView` default camera position。
- 覆盖 ForwardPass lighting uniform 来自 scene/view。

审核点：

- 测试不依赖真实 Vulkan。
- 测试不通过截图判断。
- 不为测试破坏模块边界。

### 0.25.5 验证与收尾

目标：

- 更新本文档实现状态。
- 按需同步 `docs/codex_handoff.md`。
- 记录后续 Phase 0.26 进入 direct lighting BRDF 的前置条件已经满足。

建议运行：

```powershell
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

## 审核检查点

- `ForwardPass` 不再硬编码 light 作为唯一来源。
- `RenderScene` 能表达最小 lighting。
- `RenderView` 能稳定提供 camera position。
- 默认 sandbox 画面不因默认值漂移而明显变化。
- `LightingUniform` binding 和 size 不变。
- shader descriptor layout 不变。
- alpha blend/depth-write 行为不回退。
- doubleSided culling 行为不回退。
- RenderQueue alpha bucket ordering 不回退。
- 不引入 RHI/Vulkan 对 light/camera 的依赖。
- 不引入 glTF lights/cameras 解析。

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

已完成 0.25.0 ~ 0.25.5：

- 本文档已明确 Phase 0.25 范围、非目标、模块边界、测试策略和验证计划。
- `RenderScene` 已新增 renderer 层最小 lighting 语义：
  - `DirectionalLight`
  - `SceneLighting`
  - `RenderScene::lighting()`
  - `RenderScene::setLighting()`
- 默认 lighting 与 Phase 0.24 hardcoded values 对齐：
  - direction `(-0.35, -0.8, -0.45)`，由 ForwardPass 写 uniform 时归一化
  - color `(1.0, 0.96, 0.88)`
  - ambient `(0.08, 0.09, 0.11)`
- `RenderView` 已保存 camera position：
  - `setDefaultPerspective()` 写入默认 `(0, 0, -4)`
  - `setMatrices(view, projection, cameraPosition)` 支持显式 camera position
  - 旧 `setMatrices(view, projection)` 仍可用，并把反推 camera position 封装在 `RenderView` 内部
- `ForwardPass::makeLightingUniform()` 已从 `FrameContext::scene` / `FrameContext::view` 读取 lighting 和 camera position。
- `ForwardPass` 不再把 hardcoded light 作为唯一数据来源；无 scene/view 时仍保留默认 fallback。
- `LightingUniform` size、binding 13、descriptor layout 和 shader 均未修改。
- `ark_forward_pass_pipeline_smoke` 已扩展 fake context 捕获 `ForwardLightingUniformBuffer` 写入，覆盖：
  - `RenderScene` lighting 默认值和 set/get
  - `RenderView` default camera position
  - 自定义 scene lighting / view camera position 进入 ForwardPass lighting uniform
  - Phase 0.24 doubleSided cull/depth/blend pipeline state 不回退
- `ark_render_scene_queue_smoke` 已覆盖 `RenderScene` lighting 默认值和 set/get。
- `ark_framework_headers_smoke` 已覆盖新增 public structs 能编译。

本轮 Phase 0.25 验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_forward_pass_pipeline_smoke ark_render_scene_queue_smoke ark_framework_headers_smoke
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
build/msvc-vcpkg/Debug/ark_render_scene_queue_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：

```text
targeted build passed
ark_forward_pass_pipeline_smoke passed
ark_render_scene_queue_smoke passed
ark_framework_headers_smoke passed
full build passed
CTest: 9/9 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
```

## 完成标准

Phase 0.25 完成时应满足：

- `RenderScene` 保存并暴露可配置 scene lighting。
- `RenderView` 保存并暴露 camera position。
- `ForwardPass` 的 light direction/color/ambient/camera position uniform 来自 scene/view。
- 默认 lighting 和 default camera 与 Phase 0.24 行为对齐。
- tests 覆盖 scene lighting、camera position 和 ForwardPass uniform 数据流。
- build、CTest、default sandbox、DamagedHelmet optional smoke 通过。
- 文档明确仍未支持多光源、shadow、glTF lights/cameras、完整 BRDF、HDR、IBL、RenderGraph。

## 后续 Phase 建议

Phase 0.25 后建议进入：

1. 更完整的 direct lighting BRDF。
2. HDR framebuffer / tone mapping。
3. IBL / environment map / BRDF LUT。
4. renderer 级资源 / 场景加载入口整理。
5. 基于 camera 和 bounds 的 Blend bucket back-to-front sorting。
6. pipeline / shader / descriptor layout 的 deferred destruction。
