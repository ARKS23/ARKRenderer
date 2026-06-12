# Phase 0.36 Sandbox Orbit Camera Controller

## 阶段判断

Phase 0.35 已经把默认 renderer 生成的 diffuse irradiance cubemap 接入 `ForwardPass`：

```text
EnvironmentResource
    -> EnvironmentCubeConverter
        -> DefaultSandboxEnvironmentCube
    -> EnvironmentIrradianceGenerator
        -> DefaultSandboxIrradianceCube
    -> FrameContext::irradianceCube
    -> ForwardPass binding 16/17
    -> mesh.frag.hlsl diffuse ambient IBL
```

现在默认 sandbox 已经能显示模型、skybox、HDR scene color、tone mapping 和 diffuse irradiance IBL。下一个最值得补齐的能力不是继续堆 specular IBL，而是先让使用者能围绕模型观察当前效果。

当前 `Application::run()` 的相机路径仍然是固定默认视角：

```cpp
RenderView view;
view.setDefaultPerspective(currentExtent);

while (!m_Window->shouldClose()) {
    m_Window->pollEvents();
    ...
    m_Renderer->render(scene, view);
}
```

`RenderView` 已经具备 view/projection/camera position 数据接口，但没有交互控制器；`Window` 也只暴露 `pollEvents()` 和 `getExtent()`，没有输入快照。Phase 0.36 应该补一个 sandbox/app 层的 orbit camera controller，让后续 IBL、skybox orientation、normal/tangent、透明排序和材质响应都能被更直观地观察。

Phase 0.36 仍然不是 editor camera、glTF camera 或完整 input system。它只做 sandbox 调试体验所需的最小 orbit camera。

## 目标

Phase 0.36 目标：

- 在 app/window 层新增最小输入快照，不让 renderer 直接依赖 GLFW。
- `GlfwWindow` 采集 mouse position、mouse delta、scroll delta、mouse buttons 和少量 key state。
- 新增 sandbox orbit camera controller，负责 yaw / pitch / distance / target / pan / projection 参数。
- `Application::run()` 每帧 poll events 后更新 camera controller，并写入 `RenderView`。
- window resize 时只更新 projection/aspect，不重置 orbit camera 状态。
- 默认无输入时保持当前 `(0, 0, -4)` 看向原点的视觉基线。
- 交互支持 orbit、dolly/zoom、pan 和 reset。
- smoke tests 覆盖 controller 数学、输入快照 API、`RenderView` 写入和 runtime 启动稳定性。

## 非目标

Phase 0.36 暂不做：

- 不做 glTF camera 加载或 scene camera 选择。
- 不做 editor camera / 多 viewport / gizmo / selection。
- 不做完整 input manager、action mapping 或事件分发系统。
- 不让 `renderer/`、`ForwardPass`、`FrameRenderer` 依赖 GLFW。
- 不改 `RenderScene` 语义。
- 不改 `ForwardPass` lighting、BRDF、IBL 或 descriptor layout。
- 不做 cubemap face orientation pixel validation。
- 不做 prefiltered specular、BRDF LUT、specular IBL 或 roughness mip sampling。
- 不做 bloom / auto exposure / ACES。
- 不做 ImGui 控件。
- 不做保存/加载相机 preset。

## 当前基线

### Window

当前 `Window` 接口：

```cpp
class Window {
public:
    virtual ~Window() = default;

    virtual bool shouldClose() const = 0;
    virtual void pollEvents() = 0;

    virtual rhi::NativeWindowHandle getNativeWindowHandle() const = 0;
    virtual rhi::Extent2D getExtent() const = 0;
};
```

它没有输入状态。`GlfwWindow` 内部持有 `GLFWwindow*`，但没有暴露 mouse/key/scroll。

### Application

当前 `Application::run()`：

- 创建 `GlfwWindow`。
- 创建 renderer。
- 创建空 `RenderScene` 和一个 `RenderView`。
- 初始和 resize 时调用 `view.setDefaultPerspective(currentExtent)`。
- 每帧直接 `m_Renderer->render(scene, view)`。

这意味着 resize 会重置 camera 到默认位置，用户也没有任何相机交互入口。

### RenderView

当前 `RenderView` 是合适的数据载体：

```cpp
void setMatrices(const glm::mat4& view,
                 const glm::mat4& projection,
                 const glm::vec3& cameraPosition);

void setDefaultPerspective(rhi::Extent2D extent);

const glm::mat4& viewMatrix() const;
const glm::mat4& projectionMatrix() const;
const glm::vec3& cameraPosition() const;
```

Phase 0.36 不需要把 input 放进 `RenderView`。`RenderView` 继续只保存最终相机矩阵和 camera position。

## 建议设计

### InputSnapshot

新增轻量输入结构，建议放在 app 层：

```text
src/app/Input.h
```

建议第一版只覆盖 sandbox camera 需要的输入：

```cpp
namespace ark {
    enum class MouseButton {
        Left,
        Right,
        Middle,
        Count,
    };

    enum class Key {
        LeftShift,
        RightShift,
        R,
        Count,
    };

    struct InputSnapshot {
        glm::vec2 cursorPosition{0.0f};
        glm::vec2 cursorDelta{0.0f};
        glm::vec2 scrollDelta{0.0f};
        std::array<bool, static_cast<usize>(MouseButton::Count)> mouseButtons{};
        std::array<bool, static_cast<usize>(Key::Count)> keys{};
    };
}
```

也可以不用 enum，直接提供明确字段：

```cpp
bool rightMouseDown = false;
bool middleMouseDown = false;
bool shiftDown = false;
bool resetPressed = false;
```

第一版更推荐明确字段，代码少、测试直观；如果后续要扩展 editor/input mapping，再抽象 enum。

### Window API

`Window` 新增：

```cpp
virtual const InputSnapshot& input() const = 0;
```

或者：

```cpp
virtual InputSnapshot getInputSnapshot() const = 0;
```

推荐返回 by value，避免外部长期持有窗口内部状态：

```cpp
virtual InputSnapshot getInputSnapshot() const = 0;
```

语义：

- `cursorPosition`：当前 cursor 位置，窗口坐标。
- `cursorDelta`：本帧相对上一帧变化。
- `scrollDelta`：本帧滚轮 delta。
- mouse/key：poll 后的当前状态。
- `scrollDelta` 和 `cursorDelta` 应是 frame-local 数据，`pollEvents()` 内更新，下一帧重新计算。

### GlfwWindow 输入采集

`GlfwWindow` 可以使用 GLFW polling：

```cpp
glfwGetCursorPos(window, &x, &y);
glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);
glfwGetKey(window, GLFW_KEY_R);
```

滚轮需要 callback：

```cpp
glfwSetScrollCallback(window, scrollCallback);
glfwSetWindowUserPointer(window, this);
```

建议 `GlfwWindow` 内保存：

```cpp
InputSnapshot m_Input;
glm::vec2 m_PreviousCursorPosition{0.0f};
glm::vec2 m_PendingScrollDelta{0.0f};
bool m_HasPreviousCursorPosition = false;
```

`pollEvents()` 顺序建议：

```text
m_Input.scrollDelta = {};
glfwPollEvents()
read cursor/buttons/keys
m_Input.cursorDelta = current - previous
m_Input.scrollDelta = pending scroll accumulated by callback
pending scroll reset after snapshot
```

注意：

- 第一次 poll 不应产生巨大 cursor delta。
- minimized / unfocused 时可以继续记录输入，但 controller update 应能安全处理 extent 0。
- `R` reset 建议做 pressed-edge，而不是 held-state，否则按住 R 会每帧重置。可以在 `GlfwWindow` 里提供 `resetPressed`，或在 controller 中比较上一帧 key 状态。

### SandboxCameraController

建议新增：

```text
src/app/SandboxCameraController.h
src/app/SandboxCameraController.cpp
```

也可以命名为 `OrbitCameraController`，但 Phase 0.36 的语义是 sandbox 工具，所以 `SandboxCameraController` 更贴合当前 app 层。

建议接口：

```cpp
namespace ark {
    struct SandboxCameraControllerDesc {
        glm::vec3 target{0.0f, 0.0f, 0.0f};
        float distance = 4.0f;
        float yaw = 0.0f;
        float pitch = 0.0f;
        float verticalFovRadians = glm::radians(60.0f);
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
    };

    class SandboxCameraController {
    public:
        void reset();
        void setViewportExtent(rhi::Extent2D extent);
        void update(const InputSnapshot& input);
        void writeTo(RenderView& view) const;
    };
}
```

更直接的接口也可以：

```cpp
void update(const InputSnapshot& input, rhi::Extent2D extent, RenderView& view);
```

但推荐拆成 `update()` + `writeTo()`，方便 smoke test 单独验证状态和矩阵。

### Orbit 约定

默认约定要保持当前视觉：

```text
target = (0, 0, 0)
distance = 4
camera position = (0, 0, -4)
up = (0, 1, 0)
vertical fov = 60 degrees
near/far = 0.1 / 100
```

为了让默认位置是 `(0, 0, -4)`，可使用：

```cpp
glm::vec3 direction{
    std::cos(pitch) * std::sin(yaw),
    std::sin(pitch),
    std::cos(pitch) * std::cos(yaw),
};

cameraPosition = target - direction * distance;
view = glm::lookAt(cameraPosition, target, glm::vec3{0.0f, 1.0f, 0.0f});
```

或反过来定义 direction，只要测试固定默认结果即可。

建议交互：

```text
Right mouse drag:
    orbit yaw/pitch

Mouse wheel:
    dolly distance

Middle mouse drag:
    pan target in camera right/up plane

Shift + Right mouse drag:
    pan target in camera right/up plane

R:
    reset target/distance/yaw/pitch
```

建议参数：

```text
orbitSensitivity = 0.005 rad / pixel
panSensitivity = distance * 0.0015 world unit / pixel
zoomSensitivity = 0.15 per wheel notch
minPitch = -89 degrees
maxPitch = +89 degrees
minDistance = 0.25
maxDistance = 100
```

缩放建议使用指数式：

```cpp
distance *= std::exp(-scrollDelta.y * zoomSensitivity);
distance = clamp(distance, minDistance, maxDistance);
```

这样远近距离下 zoom 手感更一致。

### Projection

projection 仍沿用 Vulkan clip space：

```cpp
glm::perspectiveRH_ZO(fov, aspect, nearPlane, farPlane);
projection[1][1] *= -1.0f;
```

resize 时只更新 extent/aspect，不调用 `RenderView::setDefaultPerspective()`，避免重置 orbit state。

### Layering 边界

必须保持：

```text
GlfwWindow / Window
    -> InputSnapshot
Application
    -> SandboxCameraController
        -> RenderView
Renderer / FrameRenderer / ForwardPass
    -> only consumes RenderView matrices and camera position
```

不允许：

```text
renderer/ include GLFW
ForwardPass read keyboard/mouse
RenderView own input state
RenderScene own camera controller
```

## 测试策略

### Sandbox Camera Controller Smoke

建议新增：

```text
tests/sandbox_camera_controller_smoke.cpp
```

覆盖：

- default/reset camera 写入 `RenderView` 后 camera position 等于 `(0, 0, -4)`。
- right mouse drag 后 yaw/pitch 改变，camera position 改变。
- pitch clamp 生效，不翻转到 up vector 奇异状态。
- scroll zoom 后 distance clamp 生效。
- middle mouse drag 或 shift+right drag 后 target 发生 pan。
- resize / extent 变化只改 projection aspect，不重置 target/yaw/pitch/distance。
- `R` pressed edge 触发 reset。

### Framework Headers Smoke

扩展 `tests/framework_headers_smoke.cpp`：

- include `app/Input.h`。
- include `app/SandboxCameraController.h`。
- touch `InputSnapshot` 字段。
- touch controller `reset()` / `setViewportExtent()` / `writeTo()`。

### Runtime Smoke

保持现有三类 runtime smoke：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

runtime smoke 仍然只证明启动链路稳定，不证明交互视觉正确。相机交互需要手动拖拽验证，或后续引入 screenshot/pixel 自动测试。

## 实施顺序

### 0.36.0 文档与范围确认

目标：

- 新增 `docs/phase/phase36.md`。
- 明确本阶段只做 sandbox orbit camera controller。
- 明确 renderer 不依赖 GLFW/input。
- 明确不做 specular IBL、face orientation pixel test 或 editor camera。

审核点：

- 范围不扩大到完整 input system。
- 不改变 renderer pass graph。
- 不改变 lighting / IBL shader。

### 0.36.1 Input Snapshot

目标：

- 新增 `src/app/Input.h`。
- `Window` 新增输入快照访问接口。
- `GlfwWindow` 采集 cursor、mouse buttons、scroll、R、Shift。
- scroll delta 支持 callback 累积和逐帧消费。

审核点：

- 输入 API 保持 app 层。
- `renderer/` 不 include GLFW。
- 第一次 cursor delta 为 0。
- reset 使用 pressed-edge 或可测试语义，避免按住每帧抖动。

### 0.36.2 Sandbox Camera Controller

目标：

- 新增 `SandboxCameraController`。
- 支持 orbit、zoom、pan、reset。
- 支持 extent/aspect 更新。
- 输出 view/projection/cameraPosition 到 `RenderView`。

审核点：

- 默认 camera 与 `RenderView::setDefaultPerspective()` 视觉兼容。
- pitch/distance clamp。
- projection 保持 RH_ZO + Vulkan Y flip。
- 不把 controller 放入 renderer 层。

### 0.36.3 Application 接入

目标：

- `Application::run()` 创建 controller。
- 初始化 controller extent。
- 每帧 poll events 后读取 input 更新 controller。
- resize 时更新 renderer size 和 controller extent，不重置相机。
- render 前写入 `RenderView`。

审核点：

- 默认无输入仍能看到模型。
- window minimized extent 0 时不产生非法 projection。
- renderer 仍只消费 `RenderView`。

### 0.36.4 Tests

目标：

- 新增 `ark_sandbox_camera_controller_smoke`。
- 更新 `ark_framework_headers_smoke`。
- 保持 `ark_forward_pass_pipeline_smoke`、`ark_shader_assets_smoke`、`ark_skybox_pass_smoke` 通过。

审核点：

- controller 数学测试不依赖真实 GLFW。
- 输入快照测试不需要打开窗口。
- runtime smoke 不替代数学测试。

### 0.36.5 验证与收尾

目标：

- targeted build。
- 新增/相关 smoke executables 通过。
- full build。
- CTest 全量通过。
- runtime smoke 通过。
- 更新 `docs/codex_handoff.md`。
- 文档记录 Phase 0.36 后仍未支持：
  - glTF camera
  - editor camera
  - cubemap face orientation pixel validation
  - prefiltered specular
  - BRDF LUT
  - specular IBL
  - bloom / auto exposure / ACES

审核点：

- 不把 sandbox camera 写成 renderer camera system。
- 不把手动交互 smoke 写成自动视觉正确性验证。
- 下一步建议转向 cubemap face orientation fixture 或 specular IBL 前置资源。

## 实施结果

Phase 0.36 已完成 0.36.0 ~ 0.36.5：

- 新增 `src/app/Input.h`，定义 app 层 `InputSnapshot`。
- `Window` 新增 `getInputSnapshot()`，用于按帧读取输入快照。
- `GlfwWindow` 已通过 GLFW polling 和 scroll callback 采集 cursor position、cursor delta、scroll delta、mouse buttons、Shift 和 R reset pressed-edge。
- 新增 `SandboxCameraController`，支持 orbit、zoom、pan、reset、viewport extent 更新和写入 `RenderView`。
- `Application::run()` 已接入 sandbox camera controller；resize 时只更新 renderer extent 和 controller extent，不再重置 orbit 状态。
- 默认无输入时保持 `(0, 0, -4)` 看向原点，与原 `RenderView::setDefaultPerspective()` 视觉基线兼容。
- 新增 `ark_sandbox_camera_controller_smoke`，覆盖默认 camera、orbit、zoom、pan、pitch/distance clamp、reset 和 resize 不重置 camera state。
- `framework_headers_smoke` 已覆盖新增 app 层 API。
- `renderer/`、`FrameRenderer`、`ForwardPass` 没有新增 GLFW/input 依赖。

Phase 0.36 仍不是完整 editor camera 或 glTF camera，也不包含自动视觉验证。

## 验证记录

Windows/MSVC/vcpkg/DXC debug preset 下已完成：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_sandbox_camera_controller_smoke ark_framework_headers_smoke ark_sandbox
build/msvc-vcpkg/Debug/ark_sandbox_camera_controller_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
targeted smoke build passed
ark_sandbox_camera_controller_smoke passed
ark_framework_headers_smoke passed
full build passed
CTest: 16/16 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

runtime smoke 使用隐藏窗口启动后自动停止，用于确认默认场景、DamagedHelmet 和本地 HDR environment 路径不会启动即退出。它不等同于自动交互或视觉正确性验证；orbit / pan / zoom 的行为由 `ark_sandbox_camera_controller_smoke` 覆盖数学路径，最终手感仍建议人工打开 sandbox 验证。

## 完成标准

Phase 0.36 完成时应满足：

- sandbox 默认打开仍能显示模型和环境。
- 用户可以通过鼠标 orbit / zoom / pan 观察模型和 skybox。
- resize 不重置相机状态。
- `Renderer`、`FrameRenderer`、`ForwardPass` 没有 GLFW/input 依赖。
- `RenderView` 仍是纯 view/projection/camera position 数据载体。
- controller 数学 smoke 通过。
- framework headers smoke 覆盖新增 app 层 API。
- full build / CTest / runtime smoke 通过。
- handoff 明确当前只是 sandbox orbit camera，不是完整 editor camera 或 glTF camera。

## 后续 Phase 建议

Phase 0.36 后建议：

1. **Cubemap Face Orientation Fixture / Pixel Validation**
   - 有相机交互后，可以更可靠地验证 skybox 和 IBL 方向；自动化上建议准备小型 debug HDR 或 readback path。
2. **Prefiltered Specular Environment + BRDF LUT**
   - 开始 specular IBL 前置资源。
3. **ForwardPass Specular IBL**
   - 接入 roughness mip sampling 和 split-sum BRDF。
4. **HDR / Cubemap Mip Generation Policy**
   - 为 prefilter 和更高质量 environment sampling 铺路。
5. **Bloom / Auto Exposure / ACES**
   - 后处理质量提升。
