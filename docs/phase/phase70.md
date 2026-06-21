# Phase 0.70 Sandbox First-Person Camera

## 实施状态

已完成。

Phase 0.67 已经接入 CSM 基础路径，Phase 0.68 完成 renderer effects 目录整理，Phase 0.69 明确 public/internal API 边界。当前 sandbox 默认可以加载 Sponza + DamagedHelmet，并具备阴影、IBL、Bloom、Tone Mapping 等效果验证能力，但相机交互仍偏向 orbit / pan / zoom。

对于 Sponza 这类大型室内场景，orbit 相机不利于进入走廊、中庭、柱廊等局部区域观察阴影、Bloom、Tone Mapping、IBL、后续 SSAO 和 debug overlay。因此在继续 Shadow Debug Visualization 之前，建议先补一层 sandbox first-person / fly camera。

本阶段聚焦 **Sandbox First-Person Camera**。目标是让 sandbox 具备更自然的场景漫游能力，同时保持 renderer public API 和 `RenderView` contract 不被 UI / 输入系统污染。

## 阶段目标

- 为 sandbox camera 增加 Orbit / FirstPerson 两种模式。
- 增加键盘移动输入：WASD、上升、下降、加速。
- 增加右键鼠标视角控制，支持 yaw / pitch 旋转。
- 保留现有 orbit 相机行为，避免默认画面和已有测试回归。
- 在 sandbox UI 中暴露 camera mode、move speed、fast multiplier、reset 等基础设置。
- 保持 `RenderView` 只接收最终 view / projection / camera 参数，不感知输入设备。
- 补充 focused tests，验证 first-person movement、mode switching 和 orbit path 不回归。

## 非目标

- 不实现完整编辑器相机系统。
- 不实现碰撞、重力、地面吸附或物理角色控制。
- 不引入 ECS、Scene Graph 或 Transform Hierarchy。
- 不改变 renderer core、RHI 或 pass 的职责边界。
- 不让 `Renderer`、`RenderScene`、`RenderView` 依赖 GLFW / ImGui / sandbox input。
- 不调整 Shadow Debug、SSAO 或 GPU Instancing 的实现。
- 不做目录大规模重排。

## 当前相机限制

当前 `SandboxCameraController` 的主要职责是生成 sandbox 的 view / projection：

```text
src/app/
  SandboxCameraController.h
  SandboxCameraController.cpp
  Input.h
```

现状更适合围绕模型观察：

- 鼠标拖拽旋转 orbit。
- 平移用于调整中心点。
- 滚轮缩放距离。
- reset 回到默认构图。

这套交互对 DamagedHelmet 等单模型验证很顺手，但对 Sponza 大场景有几个问题：

- 很难进入场景内部观察局部阴影。
- 观察 shadow acne / peter panning / CSM split seam 时不够直观。
- 后续 Shadow Debug overlay、SSAO 和 material debug view 需要频繁移动到特定位置。
- 使用 orbit 缩放进入大型场景时，中心点和距离容易变得不直觉。

因此 first-person camera 应该作为 sandbox 体验能力补齐，而不是 renderer 功能。

## 设计原则

### 一：相机交互属于 app / sandbox

输入采样、按键映射、鼠标捕获、UI 是否拦截输入，都属于 `src/app` 或 sandbox 层。

renderer 层只需要最终结果：

```text
Camera state -> view matrix / projection matrix -> RenderView
```

这样后续接入引擎时，引擎可以用自己的 Camera Component / Input System，只把最终 `RenderView` 填给 renderer。

### 二：先扩展现有 controller，不急于拆多个类

本阶段建议先在 `SandboxCameraController` 内增加 mode 分支，而不是马上拆成 `OrbitCameraController` / `FirstPersonCameraController`。

原因：

- 当前 controller 规模还可控。
- 测试已经围绕 `SandboxCameraController` 建立。
- first-person 与 orbit 共享 projection / viewport / reset / sanitize 等逻辑。
- 后续如果 editor camera 继续膨胀，再拆策略类也不迟。

### 三：默认行为保守

默认仍建议保持现有 orbit 行为，避免打开 sandbox 时视角突然变化。first-person 通过 UI 或快捷键切换。

后续如果专门做 “Sponza exploration preset”，可以让该 preset 默认进入 first-person，但不建议本阶段强改全局默认。

## Camera Mode Contract

建议新增 sandbox 相机模式：

```cpp
enum class SandboxCameraMode
{
    Orbit,
    FirstPerson,
};
```

`SandboxCameraControllerDesc` 建议扩展：

```cpp
struct SandboxCameraControllerDesc
{
    SandboxCameraMode mode{SandboxCameraMode::Orbit};

    glm::vec3 target{0.0F, 0.0F, 0.0F};
    float distance{5.0F};

    glm::vec3 position{0.0F, 1.5F, 5.0F};
    float yawRadians{0.0F};
    float pitchRadians{0.0F};

    float moveSpeed{4.0F};
    float fastMoveMultiplier{4.0F};

    float fovYRadians{glm::radians(60.0F)};
    float nearPlane{0.1F};
    float farPlane{500.0F};
};
```

说明：

- `target + distance` 服务 orbit。
- `position + yaw / pitch` 服务 first-person。
- projection 参数继续两种模式共享。
- 需要 sanitize pitch，避免相机翻转。
- yaw 正方向需要与当前项目坐标约定一致，并在注释中写清楚。

## Input Contract

建议扩展 `InputSnapshot`：

```cpp
struct InputSnapshot
{
    bool orbitActive{false};
    bool panActive{false};
    bool zoomActive{false};

    bool lookActive{false};
    bool moveForward{false};
    bool moveBackward{false};
    bool moveLeft{false};
    bool moveRight{false};
    bool moveUp{false};
    bool moveDown{false};
    bool fastMove{false};

    bool resetCamera{false};
    float mouseDeltaX{0.0F};
    float mouseDeltaY{0.0F};
    float scrollDelta{0.0F};
};
```

推荐按键：

```text
Right Mouse Button：视角旋转
W / S：前进 / 后退
A / D：左移 / 右移
E 或 Space：上升
Q 或 Ctrl：下降
Shift：加速
R：重置相机
```

UI 捕获输入时，sandbox 应该避免相机误动：

- ImGui wants mouse 时，不处理 look / orbit / pan。
- ImGui wants keyboard 时，不处理 WASD / reset。
- 鼠标 look 可只在右键按下时生效，降低误触概率。

## First-Person Movement

建议 `SandboxCameraController` 增加带 `deltaSeconds` 的 update：

```cpp
void update(const InputSnapshot& input, float deltaSeconds);
```

现有 `update(const InputSnapshot& input)` 可以保留为兼容入口，内部传入固定时间步或只处理无时间依赖的 orbit 输入。

first-person movement 建议流程：

1. 根据鼠标 delta 更新 yaw / pitch。
2. pitch clamp 到安全范围，例如 `[-89°, 89°]`。
3. 从 yaw / pitch 计算 forward / right / up。
4. 根据 WASD / up / down 组合移动向量。
5. 对移动向量 normalize，避免斜向移动变快。
6. 使用 `moveSpeed * multiplier * deltaSeconds` 更新 position。
7. 根据 position / forward 生成 view matrix。

伪代码：

```cpp
glm::vec3 forward = directionFromYawPitch(yaw, pitch);
glm::vec3 right = normalize(cross(forward, worldUp));
glm::vec3 up = normalize(cross(right, forward));

glm::vec3 move{0.0F};
move += input.moveForward ? forward : glm::vec3{0.0F};
move -= input.moveBackward ? forward : glm::vec3{0.0F};
move += input.moveRight ? right : glm::vec3{0.0F};
move -= input.moveLeft ? right : glm::vec3{0.0F};
move += input.moveUp ? worldUp : glm::vec3{0.0F};
move -= input.moveDown ? worldUp : glm::vec3{0.0F};

if (length(move) > 0.0F)
{
    position += normalize(move) * speed * deltaSeconds;
}
```

项目当前约定：

- 世界坐标：右手系。
- view matrix：`glm::lookAt` / RH 语义。
- projection：RH_ZO，再做 Vulkan Y flip。
- light direction：光线传播方向，从光源射向场景。

first-person 的方向注释也要与这些约定对齐，避免后续相机 / 光源方向混淆。

## Sandbox UI

建议在现有 sandbox UI 增加 Camera 区域：

```text
Camera
  Mode: Orbit / First Person
  Move Speed
  Fast Multiplier
  Mouse Sensitivity
  FOV
  Near / Far
  Reset Camera
```

UI 层只改 sandbox camera settings，不直接改 renderer 内部对象。每帧由 sandbox 把 controller 输出同步到 `RenderView`。

## Preset 策略

本阶段默认仍使用 orbit，避免已有默认画面、frame validation 和 smoke test 产生无关变化。

后续可以考虑：

- 增加 `SandboxCameraPreset::SponzaWalkthrough`。
- 在 shadow validation scene 中默认提供一个适合观察球体阴影的 first-person 起点。
- 为 CSM / SSAO debug 保存常用 camera bookmarks。

这些都不属于本阶段硬目标。

## 分阶段任务

### 0.70.0 文档与范围确认

- 确认 first-person camera 属于 sandbox app layer。
- 确认 `RenderView` 不直接接触输入系统。
- 确认默认 orbit 行为保持不变。
- 结果：本阶段只改 `src/app` 的输入、相机控制和 sandbox UI，不改变 renderer public API。

### 0.70.1 Camera Mode Contract

- 新增 `SandboxCameraMode`。
- 扩展 `SandboxCameraControllerDesc`。
- 补充 yaw / pitch / move speed / fast multiplier。
- 增加必要中文注释，解释坐标约定和方向语义。
- 结果：`SandboxCameraController` 支持 `Orbit` / `FirstPerson`，并暴露 move speed、fast multiplier、mouse sensitivity。

### 0.70.2 Input Snapshot Keyboard Movement

- 扩展 `InputSnapshot`。
- 在 sandbox 主循环中采样 WASD / up / down / shift / reset。
- 处理 ImGui input capture，避免 UI 操作驱动相机。
- 结果：GLFW 采样 `WASD`、`E/Space`、`Q/Ctrl`、`Shift`，UI keyboard capture 会过滤移动输入。

### 0.70.3 First-Person Controller Path

- 实现 first-person look / movement。
- 保留 orbit path。
- 支持 `deltaSeconds`。
- reset 根据当前 mode 回到合理默认值。
- 结果：主循环使用真实 `deltaSeconds` 更新相机；first-person reset 会回到 preset 推导出的起始位置。

### 0.70.4 Sandbox Camera UI

- 在 sandbox UI 增加 camera mode 和参数调节。
- 支持 reset camera。
- 确认 UI 调参不会触发 swapchain / GPU resource 重建。
- 结果：`ARK Debug / Camera` 面板提供 mode、move speed、fast multiplier、mouse sensitivity。

### 0.70.5 Tests

- 扩展 `sandbox_camera_controller_smoke`。
- 覆盖：
  - orbit 默认行为不变。
  - first-person 前进 / 后退 / 横移 / 上下移动。
  - yaw / pitch 更新和 pitch clamp。
  - mode switching 不生成 NaN 矩阵。
  - reset 行为稳定。
- 结果：新增/扩展 `ark_sandbox_camera_controller_smoke`、`ark_sandbox_ui_settings_smoke`、`ark_framework_headers_smoke` 覆盖相机模式和输入过滤。

### 0.70.6 验证与收尾

- Debug build 通过。
- phase relevant tests 通过。
- sandbox hidden smoke 能正常启动。
- 文档同步实际实现结果。
- 结果：
  - `cmake --build --preset msvc-vcpkg-debug` 通过。
  - phase relevant CTest 5/5 通过。
  - 排除既有 golden 差异的 CTest 33/33 通过。
  - sandbox hidden smoke 通过。
  - 全量 CTest 33/34 通过，唯一失败为既有 `ark_frame_validation_smoke` default composite golden diff。

## 风险与应对

### 风险一：UI 和相机输入冲突

应对：

- 严格检查 ImGui capture 状态。
- 右键按住才 look。
- UI focus 时不处理键盘移动。

### 风险二：first-person 破坏默认画面

应对：

- 默认仍使用 orbit。
- first-person 通过 UI 显式切换。
- frame validation 不更新 golden。

### 风险三：Sponza 尺度导致速度不合适

应对：

- UI 暴露 move speed 和 fast multiplier。
- 默认速度选择适中值。
- 后续可基于 scene bounds 自动建议速度，但本阶段不做复杂自动化。

### 风险四：controller 继续膨胀

应对：

- 本阶段只做两种 mode 分支。
- 如果后续加入 editor camera、bookmark、path record，再拆 `OrbitCameraController` / `FlyCameraController`。

## 完成标准

- sandbox 可以在 Orbit / FirstPerson 之间切换。
- FirstPerson 模式下可以在 Sponza 场景中自由移动和观察局部阴影。
- Orbit 模式行为不回归。
- `RenderView` / renderer public API 不新增输入系统依赖。
- UI 捕获输入时相机不会误动。
- tests 通过，文档记录实现结果。

## 后续方向

完成 Sandbox First-Person Camera 后，建议继续：

1. Phase 0.71：Shadow Debug Visualization，包括 cascade color、shadow diagnostics、shadow map preview assessment。
2. Phase 0.72：SSAO effect foundation，落到 `renderer/effects/ssao/`。
3. Phase 0.73：GPU instanced rendering foundation，减少重复 mesh 的 draw call 压力。
4. Phase 0.74：Material / Texture debug views，方便复杂 glTF 场景排查。
