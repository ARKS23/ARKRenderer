# Phase 0.65 Sandbox Debug UI / Runtime Tuning Foundation

## 实施状态

已完成 0.65.0 文档与范围确认 和 0.65.1 Dependency / Backend 评估。

Phase 0.58 ~ 0.64 已经让默认 sandbox 具备 Sponza + DamagedHelmet、Shadow、Bloom、ACES ToneMapping、IBL、KTX 贴图读取、scene bounds shadow fitting、texel snapping 和 PCF shadow filtering。当前渲染器已经进入“需要反复看画面、调参数、比较效果”的阶段，继续只靠 CLI 参数会拖慢后续迭代。

本阶段建议先引入最小 sandbox debug UI。它不是 editor，也不是正式引擎 UI 系统，而是一个面向图形算法实验的运行时调参入口。

## 阶段目标

- 在 sandbox 中提供一个可开关的 debug UI 层。
- 能运行时调整 Shadow、Bloom、ToneMapping、Environment/IBL 和 Camera 的关键参数。
- 让后续 CSM、shadow debug view、auto exposure、材质验证等工作有更快的观察入口。
- 保持 renderer core、frame validation 和 public preset API 的边界清晰。
- 默认不影响 golden/frame validation 输出。

## 非目标

- 不做完整 editor。
- 不做场景层级、资源浏览器、材质编辑器或 gizmo。
- 不把 UI 控件直接塞进 renderer core。
- 不重写现有 preset / CLI / frame validation 机制。
- 不改变 Bloom、ToneMapping、Shadow、IBL 的算法本身。
- 不要求 UI 出现在 offscreen frame validation 中。

## 是否需要 UI 层

结论：需要，但当前只做 **debug UI**，不做正式产品 UI。

理由：

- Shadow 已经有 strength、bias、map extent、fit bounds、texel snapping、filter mode、filter radius 等参数，靠 CLI 调试反馈太慢。
- Bloom / ToneMapping / IBL 都是视觉敏感参数，需要边看边调。
- Sponza + DamagedHelmet 默认场景已经足够复杂，适合成为算法实验视口。
- 后续如果做 CSM 或 debug visualization，没有 UI 会很难定位 split、projection、bias 和 filtering 问题。

UI 应该服务于“更快观察和调参”，而不是提前扩张成 editor 架构。

## 推荐技术方案

优先考虑 Dear ImGui。

理由：

- 适合图形实验和运行时参数调试。
- 控件成本低，能快速覆盖 checkbox、slider、combo、text stats。
- 后续可扩展到 texture debug view、shadow map preview、render pass stats。

接入策略：

- UI backend 放在 app/sandbox 层，不进入 renderer core 公共 API。
- Vulkan/Window 接入如果需要 backend 细节，先封装成 sandbox-only helper。
- Renderer 只继续暴露和消费已有数据结构，例如 `RenderView`、`ShadowSettings`、`PostProcessingSettings`、`ToneMappingSettings`。
- Frame validation 默认不启用 UI，不让 UI 污染 readback / golden。

如果 Dear ImGui 依赖接入成本过高，可以先做一个更小的 immediate debug overlay 桩，但不建议长期维护自研 UI。

## 接口边界

### App / Sandbox 层

负责：

- 创建和销毁 UI context。
- 处理 UI 输入和窗口事件。
- 把 UI 控件改动写回 sandbox 当前 `RenderView` 或 `ApplicationDesc` 持有的 runtime settings。
- 提供 UI 开关，例如 `--ui`、`--no-ui` 或默认开启后按键隐藏。
- 不复制 renderer preset 默认值。

### Renderer 层

负责：

- 保持现有 settings 数据结构稳定。
- 继续通过 `RenderView` 和 `FrameContext` 消费设置。
- 不感知 UI 控件。
- 不依赖 ImGui 类型。

### Frame Validation

负责：

- 继续使用 resolved preset 和固定 capture camera。
- 默认不绘制 UI。
- 如后续需要验证 UI，应单独新增 UI smoke，不混入视觉 golden。

## 解耦审核结论

当前文档方向是对的，但如果实现时不加硬约束，仍然有几处容易让代码变乱：

- `src/renderer/passes/ImGuiPass.h` 已存在占位，容易诱导把 ImGui 当成普通 renderer pass 接进 core。
- `src\rhi\vulkan\VulkanImGuiBackend.h` 已存在占位，容易把 Vulkan native handle 泄漏到通用 RHI 或 renderer 公共接口。
- `Application::run()` 当前每帧从 `m_Desc.view` 重写 `RenderView`，UI 对 runtime settings 的修改会被下一帧覆盖。
- `GlfwWindow` 已有 scroll callback 和 input snapshot，ImGui callback install 需要确认 callback chaining，不然会打断相机输入。
- CMake 当前把 `imgui::imgui` 链接进 `ark_renderer`，因此更需要靠 include 边界避免 renderer core 直接包含 ImGui 头文件。

因此，Phase 0.65 的实现必须遵守下面的硬性边界。

## 硬性边界

- `imgui.h`、`imgui_impl_glfw.h`、`imgui_impl_vulkan.h` 只能出现在 sandbox/app UI 实现和 Vulkan backend 私有实现中。
- `src/renderer/*` 的公共头文件不得暴露 ImGui 类型。
- `RenderView`、`RenderScene`、`RendererPreset`、`FrameContext` 不持有 UI 控件状态。
- 不给通用 `rhi::DeviceContext` 增加 `VkCommandBuffer` getter。
- 不给通用 `rhi::RenderDevice` 增加 `VkDevice` / `VkQueue` getter。
- 不让 UI 直接修改 immutable `ApplicationDesc`。
- 不让 UI 参与 frame validation 默认渲染路径。
- 不启用 ImGui multi-viewport；docking feature 可以存在于依赖中，但第一阶段不打开多平台窗口。

## 推荐落地结构

建议把 UI 拆成三层，避免一个类既画控件、又管 Vulkan、又改 renderer 状态：

### Sandbox Runtime State

建议新增 sandbox/app 层运行时状态，例如：

```cpp
struct SandboxRuntimeSettings {
    RenderViewProfileDesc view;
    OrbitCameraProfileDesc camera;
    bool uiVisible = true;
};
```

职责：

- 从 `ApplicationDesc` 初始化一次。
- UI 和 camera controller 都改 runtime state。
- 每帧由 runtime state 写入 `RenderView`。
- `ApplicationDesc` 保持启动配置语义，不再作为每帧运行时状态源。

### Sandbox Debug UI

建议新增 `SandboxDebugUi`，只负责：

- ImGui context 生命周期。
- 构建 Shadow / Bloom / ToneMapping / Environment / Camera 面板。
- 根据控件修改 `SandboxRuntimeSettings`。
- 暴露 `wantsCaptureMouse()` / `wantsCaptureKeyboard()`，用于阻止相机在 UI 操作时响应输入。

它不负责：

- 创建 Vulkan pipeline。
- 访问 `VkCommandBuffer`。
- 直接操作 `FrameContext`。
- 直接改 `Renderer` 内部对象。

### Frame Overlay / Backend

建议不要直接把 `ImGuiPass` 做成普通 renderer pass。更稳的方式是：

- 在 renderer 层定义一个不含 ImGui 类型的最小 overlay seam，例如 `FrameOverlay`。
- `Renderer` / `FrameRenderer` 只知道“最后有一个 overlay 要画”，不知道它是 ImGui。
- overlay 绘制点固定在 tone mapping 输出到 backbuffer 之后、Present barrier 之前。
- Vulkan 细节封装在 `VulkanImGuiBackend` 或 app/sandbox 的 Vulkan-only helper 中。

如果本阶段为了速度使用现有 `ImGuiPass` 占位，也必须保持：

- `ImGuiPass` 不拥有 UI 面板逻辑。
- `ImGuiPass` 不读取或修改 `RenderViewProfileDesc`。
- `ImGuiPass` 只负责把已生成的 ImGui draw data 提交到当前 backbuffer。

## 建议面板内容

### Scene / Preset

- 当前 scene preset 名称。
- 当前 quality preset 名称。
- 主模型路径、附加模型数量。
- `RenderScene::hasBounds()` 与 bounds center / extent。

### Camera

- target、distance、yaw、pitch。
- near / far / FOV。
- Reset to preset camera。
- Reset to capture camera。

### Shadow

- Enabled。
- Strength。
- Bias。
- Map extent。
- Fit scene bounds。
- Stabilize projection。
- Filter mode：Hard / PCF 3x3 / PCF 5x5。
- Filter radius texels。
- Light direction / main light color 可只读或后续开放。

### Bloom

- Enabled。
- Intensity。
- Scatter。
- Threshold。
- Soft knee。
- Max mip count。

### ToneMapping

- Operator：Linear / Reinhard / ACES。
- Exposure。
- Output gamma。

### Environment / IBL

- Environment enabled 状态。
- Environment intensity。
- Skybox enabled 状态。
- Diffuse irradiance ready。
- Specular IBL ready。
- BRDF LUT ready。

### Diagnostics

- 当前 frame size。
- 当前 shadow map 是否绑定。
- 当前 bloom 是否 active。
- 当前 tone mapping operator。
- 可选：CPU frame time / GPU frame time 后续再接。

## 分阶段任务

### 0.65.0 文档与范围确认

- 明确本阶段是 sandbox debug UI foundation。
- 明确不做 editor。
- 明确 UI 不进入 renderer core。
- 明确 frame validation 默认不绘制 UI。

当前确认结果：

- Phase 0.65 的目标保持为 sandbox debug UI，不扩张为 editor。
- UI 第一阶段只服务运行时调参和调试状态观察。
- Renderer core 不直接依赖 ImGui 类型，避免污染后续引擎接入边界。
- Frame validation / offscreen readback 默认不启用 UI，避免 golden 被 UI 覆盖层污染。
- 现有 CLI / preset 继续保留，UI 是交互调参入口，不替代自动化测试入口。

### 0.65.1 Dependency / Backend 评估

- 检查 vcpkg 是否已有 Dear ImGui / imgui backend 可用。
- 决定依赖方式：
  - 优先 vcpkg package。
  - 如 package 不满足 Vulkan backend，可 vendor 最小 backend 文件，但要记录原因。
- 明确 Win32/GLFW/Vulkan backend 需要哪些句柄和生命周期。
- 不急着接复杂 docking / viewport。

当前评估结果：

- `vcpkg.json` 已经包含 `imgui`，并启用：
  - `docking-experimental`
  - `glfw-binding`
  - `vulkan-binding`
- `CMakeLists.txt` 已经 `find_package(imgui CONFIG REQUIRED)`，并把 `imgui::imgui` 链接进 `ark_renderer`。
- 本地 vcpkg install tree 中已存在：
  - `imgui_impl_glfw.h`
  - `imgui_impl_vulkan.h`
- `tests/dependency_smoke.cpp` 已补充 backend include 和函数符号引用，确认 GLFW/Vulkan backend 能编译和链接。
- `ark_dependency_smoke` targeted build / CTest 已通过。

Backend 关键要求：

- GLFW backend 需要 `GLFWwindow*`，当前可从 `GlfwWindow::getNativeWindowHandle()` 获得。
- Vulkan backend 需要：
  - `VkInstance`
  - `VkPhysicalDevice`
  - `VkDevice`
  - graphics queue family
  - `VkQueue`
  - image count / min image count
  - descriptor pool 或 `DescriptorPoolSize`
  - dynamic rendering pipeline info
  - 当前 frame `VkCommandBuffer`
- `VulkanDevice` 已暴露 instance / physical device / device / queue / queue family。
- `VulkanFrameResource` 已有 `getCommandBuffer()`，但当前 `DeviceContext` 抽象没有公开 native command buffer。
- `VulkanSwapChain` 当前没有公开 image count / native swapchain 信息，但 RHI 层已有 `getBackBufferCount()`。
- `FrameRenderer` 当前在 tone mapping 后直接 transition backbuffer 到 Present；UI 若绘制到 swapchain，应插在 tone mapping render scope 之后、Present barrier 之前。

建议的接入边界：

- 第一版做 sandbox-only Vulkan ImGui backend，不进入 renderer core 公共 API。
- 可以新增一个小的 native backend seam，例如仅在 Vulkan 后端提供 ImGui rendering helper，避免把 `VkCommandBuffer` 加到通用 RHI public API。
- UI 绘制应该作为 swapchain/backbuffer 最后一层 overlay，使用 dynamic rendering，color format 使用当前 swapchain color format。
- 禁用 ImGui docking/multi-viewport 的运行时功能，虽然依赖已包含 docking feature，但第一阶段不启用多窗口，减少 swapchain 和平台窗口复杂度。
- GLFW callback 可先让 ImGui backend install callbacks，并依赖其 callback chaining；如果和当前 scroll callback 冲突，再改成手动转发。

当前缺口：

- 已补 `VulkanImGuiBackend` 最小实现，封装 GLFW/Vulkan backend 初始化、new frame、draw data 提交和 shutdown。
- `src\renderer\passes\ImGuiPass.h` 继续保留为历史占位，本阶段没有把 UI 面板逻辑塞进 renderer pass。
- 已新增 `SandboxRuntimeSettings`，`Application::run()` 不再每帧用 immutable `ApplicationDesc` 覆盖 runtime 调参结果。
- 已新增 UI input capture 过滤，UI 捕获鼠标/键盘时 orbit camera 不消费对应输入。

### 0.65.2 Sandbox UI Lifecycle

- 新增 sandbox-only UI wrapper。
- 初始化 / new frame / render / shutdown 生命周期清晰。
- UI 可通过 CLI 或按键开关。
- 默认不影响没有 UI 的 headless smoke。
- 引入 `SandboxRuntimeSettings` 或等价结构，避免 UI 修改被 `ApplicationDesc` 每帧覆盖。
- 定义 UI input capture 与 camera controller 的优先级。
- 定义 overlay 绘制 seam，但 renderer 公共头不暴露 ImGui 类型。

实现结果：

- 新增 `SandboxDebugUi`，负责 ImGui context、GLFW/Vulkan backend 生命周期、每帧面板构建与 draw data 提交。
- 新增 `FrameOverlay` 最小接口，`Renderer` / `FrameRenderer` 只接收 overlay 指针，不依赖 ImGui 类型。
- `FrameRenderer` 在 ToneMapping 输出到 swapchain 后、Present barrier 前打开 overlay rendering scope，并以 Load/Store 方式叠加 UI。
- `SandboxLaunchOptions` 新增 `--ui` / `--no-ui`，默认启用 sandbox debug UI。
- `GlfwWindow` 新增 F1 按下沿输入，sandbox 运行时可用 F1 显示/隐藏 UI。
- `Application::run()` 接入 UI begin/build/end/render 生命周期，并在 renderer render 调用中传入 overlay。

### 0.65.3 Runtime Settings Bridge

- 把 UI 控件写回当前 runtime `RenderView`。
- 对写回数据复用现有 sanitize：
  - `RenderView::setShadowSettings()`
  - `RenderView::setToneMappingSettings()`
  - `RenderView::setPostProcessingSettings()`
- 不绕过已有 parser / clamp / preset 边界。
- UI 只修改 runtime settings，再由 runtime settings 写入 `RenderView`。
- CLI / preset 仍然只负责初始值。
- Frame validation 继续使用 resolved preset，不读取 sandbox UI runtime state。

实现结果：

- 新增 `SandboxRuntimeSettings`，从 `ApplicationDesc` 初始化 `RenderViewProfileDesc` / camera / UI 可见状态。
- 新增 `applySandboxRuntimeSettings()`，每帧把 runtime view settings 写入 `RenderView`，复用现有 sanitize 路径。
- 新增 `filterSandboxInputForUiCapture()`，UI 捕获鼠标时清空相机鼠标/滚轮输入，捕获键盘时屏蔽 reset 输入。
- `ApplicationDesc` 保持启动配置语义，UI 不直接修改它。

### 0.65.4 Debug Panels

- 实现 Shadow / Bloom / ToneMapping / Environment / Camera 基础面板。
- UI 文案保持短小，优先控件本身，不写教程式说明。
- 控件值范围使用保守上下限，避免拖到明显非法状态。

实现结果：

- 已实现 Tone Mapping 面板：Operator / Exposure / Gamma。
- 已实现 Bloom 面板：Enabled / Intensity / Scatter / Threshold / Soft Knee / Max Mips。
- 已实现 Shadow 面板：Enabled / Strength / Bias / Map Extent / Fit Bounds / Stabilize / Filter / Filter Radius / Manual Bounds。
- 已实现 Diagnostics 基础状态：当前 tone operator、Bloom on/off、Shadow on/off 与 filter mode。
- Environment / Camera 面板暂不在本阶段展开，避免在 runtime camera controller 和 scene resource API 尚未完全公共化前提前把 UI 做乱。

### 0.65.5 Tests

建议覆盖：

- `ark_framework_headers_smoke`
- `ark_renderer_preset_smoke`
- `ark_post_processing_settings_smoke`
- `ark_shadow_pass_smoke`
- 新增 sandbox UI settings bridge smoke，如果 UI wrapper 能在 fake/noop backend 下测试。
- `ark_frame_validation_smoke` 确认默认不受 UI 影响。

如果 ImGui backend 很难在单测里实例化，第一阶段至少测试 settings bridge 纯数据层，不强测真实 UI 绘制。

实现结果：

- 新增 `ark_sandbox_ui_settings_smoke`，覆盖 runtime settings bridge、sanitize 写回、UI input capture 过滤、`--ui` / `--no-ui`。
- 更新 `ark_dependency_smoke`，确认 ImGui GLFW/Vulkan backend 头与符号可编译链接。
- 更新 `ark_framework_headers_smoke`，覆盖 `SandboxRuntimeSettings`、`FrameOverlay` 和 `debugUiEnabled` 配置。
- Targeted build / CTest 已通过：
  - `ark_sandbox_ui_settings_smoke`
  - `ark_dependency_smoke`
  - `ark_framework_headers_smoke`
  - `ark_renderer_preset_smoke`

### 0.65.6 验证与收尾

- 更新 `docs/phase/phase65.md` 状态。
- 更新 `docs/codex_handoff.md`。
- targeted build / CTest。
- full Debug build / full CTest。
- sandbox hidden-window smoke：
  - default。
  - sponza。
  - shadow-validation。
  - UI enabled。
  - UI disabled。
- 提交并推送。

当前收尾状态：

- 已完成 0.65.2 ~ 0.65.5 编码与 targeted tests。
- 已完成 full Debug build / full CTest / sandbox hidden-window smoke。

验证记录：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_sandbox_ui_settings_smoke ark_dependency_smoke ark_framework_headers_smoke ark_renderer_preset_smoke ark_sandbox
ctest --test-dir build\msvc-vcpkg -C Debug -R "ark_(sandbox_ui_settings_smoke|dependency_smoke|framework_headers_smoke|renderer_preset_smoke)$" --output-on-failure
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build\msvc-vcpkg -C Debug --output-on-failure
ark_sandbox hidden-window smoke: default, --no-ui, --preset shadow-validation
```

结果：

```text
targeted build passed
targeted CTest passed: 4/4
full Debug build passed
full CTest passed: 31/31
sandbox hidden-window smoke passed for default, --no-ui, and shadow-validation
```

交互说明：

- sandbox 默认显示 debug UI。
- `--no-ui` 可禁用 UI。
- 运行时按 F1 可显示/隐藏 UI。

Hotfix 记录：

- 用户反馈默认打开 sandbox 会闪退。
- WER 报告显示 `0xc0000005`，异常偏移落在 volk 全局函数指针变量 `vkGetDeviceProcAddr`。
- 根因判断：0.65 初版在 vcpkg ImGui Vulkan backend 已经链接 `vulkan-1` 的情况下，又手动调用 `ImGui_ImplVulkan_LoadFunctions()` 并走 volk 函数指针加载，存在把函数指针变量地址当成函数执行的风险。
- 修复：`VulkanImGuiBackend` 不再调用 `ImGui_ImplVulkan_LoadFunctions()`，改为使用 vcpkg backend 默认的 Vulkan loader 链接路径。
- 复验：
  - full Debug build passed
  - full CTest passed: 31/31
  - sandbox default double-click-like smoke stayed alive for 12s
  - sandbox `--no-ui` stayed alive for 8s
  - sandbox `--preset shadow-validation` stayed alive for 8s
- 用户随后反馈 sandbox 在默认启动日志到 environment bake 后仍会闪退，并出现 Vulkan validation：`VUID-VkShaderModuleCreateInfo-pCode-08740` / `DemoteToHelperInvocation`。
- 根因判断：`mesh.frag.hlsl` 的 alpha mask `discard` 会编译成 SPIR-V `DemoteToHelperInvocation` capability，而 `VulkanDevice` 只启用了 dynamic rendering，没有显式启用 `shaderDemoteToHelperInvocation` device feature。
- 修复：`VulkanDevice::createDevice()` 查询 `VkPhysicalDeviceVulkan13Features`，在不支持时提前报错，并在支持设备上启用 `shaderDemoteToHelperInvocation`。
- 复验：
  - targeted build passed: `ark_sandbox`, `ark_frame_validation_smoke`, `ark_forward_pass_pipeline_smoke`, `ark_dependency_smoke`
  - targeted CTest passed: 3/3
  - full Debug build passed
  - full CTest passed: 31/31
  - sandbox default / `--no-ui` / `--preset shadow-validation` hidden-window smoke stayed alive for 8s without demote validation error
- 用户再次反馈默认 sandbox 仍在 environment bake 后闪退，但 `--no-ui` 路径可稳定运行。
- 根因判断：项目使用 `VK_NO_PROTOTYPES + volk`，同时 vcpkg 的 `imgui::imgui` Vulkan backend 静态库按默认 Vulkan prototype 链接 `vulkan-1`。两种 loader 模式混在同一可执行文件时，`vkGetDeviceProcAddr` 名称会解析到 volk 全局函数指针变量，ImGui backend 在 dynamic rendering 初始化里调用 `vkGetDeviceProcAddr()` 时等价于跳到数据地址执行，WER 偏移仍落在 `vkGetDeviceProcAddr`。
- 修复：不再链接 vcpkg 预编译的 `volk::volk` 静态库，改为链接 `volk::volk_headers`，并新增项目内 `src/rhi/vulkan/Volk.cpp` 以 `VOLK_NAMESPACE` 编译 volk 实现，避免全局 `vk*` 函数指针符号污染 vcpkg ImGui backend 的 Vulkan prototype 路径。
- 复验：
  - `--no-ui` hidden-window smoke stayed alive for 8s
  - default UI hidden-window smoke stayed alive for 8s
  - targeted CTest passed: `ark_dependency_smoke`, `ark_framework_headers_smoke`, `ark_sandbox_ui_settings_smoke`
  - full Debug build passed
  - full CTest passed: 31/31

## 风险与约束

- ImGui Vulkan backend 可能需要访问 Vulkan instance/device/queue/render pass 或 dynamic rendering 信息；如果当前 RHI 没有暴露这些句柄，要避免大面积打穿 RHI。
- UI 输入可能和 orbit camera 输入冲突，需要定义 capture mouse/keyboard 后 camera 不响应。
- UI 绘制顺序应在 tone mapping 后或最终 swapchain render target 上，避免污染 HDR scene color。
- frame validation 和 offscreen rendering 默认必须绕开 UI。
- 如果 UI 依赖引入导致构建链路不稳定，应优先保证 renderer tests 不受影响。

## 反模式清单

下面这些实现方式应避免：

- 在 `RenderView` 或 `RenderScene` 里加入 ImGui 字段。
- 在 `FrameContext` 里加入 `ImDrawData*`。
- 在通用 RHI 接口里暴露 Vulkan handle，只为了 ImGui backend。
- 让 `ApplicationDesc` 同时承担启动配置和运行时可变状态。
- 让 UI 面板直接调用 pass 内部对象或 renderer private API。
- 让 frame validation 默认绘制 UI。
- 为了 UI 改掉现有 preset / CLI 的职责边界。

## 解耦验收点

- `rg "imgui|ImGui" src/renderer src/rhi -g "*.h"` 不应在 renderer core 公共头中出现 ImGui 类型；Vulkan backend 私有头除外。
- UI enabled / disabled 都能启动 sandbox。
- UI 操作时相机不会同时旋转或缩放。
- 关闭 UI 后，CLI / preset / frame validation 行为与 0.64 保持一致。
- Debug UI settings bridge 可以用纯数据 smoke 测试，不依赖真实 Vulkan UI 绘制。

## 完成标准

- sandbox 可以启动并显示/隐藏 debug UI。
- Shadow / Bloom / ToneMapping 的核心参数可以运行时调整并立即影响画面。
- UI 不影响默认 frame validation。
- renderer core 不依赖 ImGui 类型。
- targeted build / CTest 通过。
- full Debug build / full CTest 通过。
- sandbox UI enabled / disabled smoke 通过。

## 后续方向

0.65 完成后，后续可以根据视觉调参效率选择：

1. Phase 0.66：CSM / Cascaded Shadow Map 设计与最小实现。
2. Phase 0.67：Shadow debug visualization，包括 shadow map preview、cascade split overlay、light frustum debug。
3. Phase 0.68：Auto exposure / histogram / tone mapping debug controls。
4. Phase 0.69：Material / texture debug view，用于复杂 glTF 资产验证。
