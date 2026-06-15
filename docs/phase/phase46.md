# Phase 0.46 glTF Camera and Scene Camera Selection

## 阶段判断

Phase 0.45 已经补上 frame color validation smoke，说明当前渲染链路已经具备最小的自动化画面检查能力。但这条验证路径仍然依赖测试侧手写相机，sandbox 侧也主要依赖交互相机初始化。也就是说，模型、材质、IBL、tone mapping、frame readback 已经能串起来，但“默认从哪个视角观察场景”还不是资产驱动的稳定输入。

下一阶段应优先补齐 glTF camera 与场景相机选择，而不是立刻进入阴影、Bloom、RenderGraph 或 golden image。原因很简单：截图基准、材质球夹具、默认 sandbox 展示效果都需要一个稳定、可复现、可由资产携带的相机定义。

## 目标

- 在资产层表示 glTF camera 数据，保持纯 CPU 数据结构，不引入 RHI/renderer 依赖。
- 在 glTF loader 中解析 `cameras` 与 node camera instance，不再把节点相机简单忽略。
- 提供从 glTF perspective camera 构建 `RenderView` 矩阵的稳定路径。
- 为 frame validation 与后续 visual fixture 提供资产驱动的默认观察视角。
- 在不打乱现有 sandbox 交互相机的前提下，探索默认相机选择路径，让首次打开不依赖硬编码测试视角。

## 非目标

- 不做 glTF animation、skin、morph target。
- 不接入 `KHR_lights_punctual`。
- 不做编辑器式多相机切换 UI。
- 不做完整 public scene/resource loading API。
- 不做 RenderGraph、shadow map、Bloom、auto exposure 或 ACES。
- 不把 screenshot/golden image 系统放进本阶段。
- Orthographic camera 可以先解析与保留数据，但不要求完整接入渲染路径。

## 当前基线

- `ModelData` 已包含 mesh、material、instance 等数据，但还没有 camera 数据。
- `GltfLoader` 当前遇到 `node.camera` 时会记录 warning，并明确忽略相机。
- `RenderView` 已支持直接设置 view/projection/cameraPosition，也有默认 perspective helper。
- `SandboxCameraController` 是当前 sandbox 交互相机的主要入口。
- `frame_validation_smoke` 已经能读回 framebuffer 并做基础颜色检查，但相机由测试硬编码。

这说明 Phase 0.46 不需要重写渲染主链路，只需要补齐资产相机数据、矩阵构建 helper、以及验证侧使用方式。

## 推荐路线

本阶段建议按“资产数据先行，渲染消费最小接入”的路线推进：

1. 先在 asset 层加入 camera 数据结构。
2. 再让 glTF loader 真实解析 camera 与 node camera transform。
3. 然后提供 `RenderView` 从 scene camera 构建矩阵的 helper。
4. 最后把测试或 sandbox 默认路径接到这条 helper 上。

这样可以避免过早把 renderer、application、sandbox controller 绑死在一个尚未成熟的 scene loading API 上。

## 设计方案

### Asset Camera Data

建议在 `src/asset/MeshData.h` 或相邻资产头文件中加入 camera 数据。第一版以 glTF 语义为准：

- `CameraProjectionType`
  - `Perspective`
  - `Orthographic`
- `PerspectiveCameraData`
  - `yfov`
  - `aspectRatio`
  - `znear`
  - `zfar`
  - `hasZfar` 或约定 `zfar <= 0` 表示无限远平面
- `OrthographicCameraData`
  - `xmag`
  - `ymag`
  - `znear`
  - `zfar`
- `CameraData`
  - `type`
  - `perspective`
  - `orthographic`
  - `debugName`
- `SceneCameraData`
  - `cameraIndex`
  - `worldTransform`
  - `debugName`

`ModelData` 可追加：

- `std::vector<CameraData> cameras`
- `std::vector<SceneCameraData> sceneCameras`

这里的 `SceneCameraData` 表示“某个 glTF node 实例化了某个 camera，并带有节点世界变换”。它比只记录 `CameraData` 更适合后续默认视角选择。

### GltfLoader Camera Parsing

loader 需要补两层：

- 解析 `model.cameras`，保存 perspective / orthographic 参数。
- 遍历 scene node 时，如果 node 带 `camera`，记录一个 `SceneCameraData`，使用该 node 的 world transform。

需要注意 glTF 约定：

- camera local forward 是 `-Z`。
- camera local up 是 `+Y`。
- node transform 应继续沿用当前 loader 的 matrix/TRS 处理规则。
- perspective `yfov` 与 `znear` 是必需项。
- perspective `zfar` 是可选项。
- `aspectRatio` 可选；缺省时应使用当前 viewport aspect。

如果本阶段不实现无限远投影，可以把缺省 `zfar` 映射到一个保守默认值，并在文档/代码中明确记录。更好的做法是资产层保留 optional 语义，渲染 helper 再决定 fallback。

### RenderView Camera Construction

建议补一个轻量 helper，而不是让每个测试或调用点重复写矩阵：

- 输入：camera data、camera world transform、viewport extent。
- 输出：写入 `RenderView` 的 view、projection、cameraPosition。
- view：使用 camera world transform 的 inverse。
- camera position：取 world transform translation。
- projection：使用 `glm::perspectiveRH_ZO(yfov, aspect, znear, zfar)`。
- Vulkan clip 修正：继续执行 `projection[1][1] *= -1.0f`。
- aspect 优先级：camera `aspectRatio > 0` 时使用资产值，否则使用 viewport aspect。

Orthographic 可先只保留数据，不进入默认选择。若实现成本很小，也可以同时补一个 `glm::orthoRH_ZO` helper，但不应因此扩大测试面太多。

### Sandbox / Default Camera Path

当前 sandbox 的交互相机仍应保留为用户操作入口。Phase 0.46 的目标不是替换交互控制器，而是让“首次视角”可以来自资产或验证 fixture。

推荐优先级：

1. 如果加载的模型存在有效 perspective scene camera，则使用它作为初始视角。
2. 如果没有 scene camera，则继续使用现有 sandbox 默认 orbit camera。
3. 用户一旦交互，后续帧仍由 `SandboxCameraController` 驱动。

这里要谨慎处理 Application / Renderer 边界。当前 renderer 的默认模型加载路径如果只在 renderer 内部可见，不应为了相机强行暴露整个 asset loading API。第一版可以选择较小接入点：

- 在 validation fixture 中先使用 glTF camera。
- 给 `SandboxCameraController` 增加初始化 preset 能力。
- 如果 renderer/application 边界允许，再把默认模型的首个 scene camera 传给 controller。

如果边界不干净，本阶段可以把 sandbox 默认相机接入标记为后续 public scene loading API 的前置需求，但 asset 与 validation 路径必须先完成。

## 实施拆分

### 0.46.0 文档与范围确认

- 确认本阶段只解决 camera 数据、解析、矩阵构建与最小消费路径。
- 明确不进入 golden image、后处理和完整场景 API。

### 0.46.1 Asset Camera Data

- 添加 camera 相关数据结构。
- 扩展 `ModelData`。
- 保持已有 mesh/material/instance 调用点兼容。
- 增加必要的默认值，确保空模型和旧 fixture 不受影响。

### 0.46.2 GltfLoader Camera Parsing

- 解析 glTF `cameras`。
- 遍历 node 时记录 camera instance。
- 移除或替换“camera ignored”的旧 warning。
- 对非法 camera 参数做保守跳过，并保留日志。

### 0.46.3 RenderView Camera Helper

- 增加从 perspective camera + world transform + extent 写入 `RenderView` 的 helper。
- 统一 aspect fallback 与 Vulkan Y flip。
- 覆盖 camera position 推导，避免 shader 侧 view vector 错误。

### 0.46.4 Minimal Default Camera Integration

- 优先让 frame validation 使用资产相机。
- 视 Application / Renderer 边界情况，最小接入 sandbox 初始相机。
- 保留用户交互相机控制逻辑。
- 无 camera 资产时保持当前默认行为。

### 0.46.5 Tests

- 扩展 glTF loader smoke，覆盖 camera count、projection 参数和 scene camera transform。
- 增加或扩展 RenderView helper 测试，确认 view/projection/cameraPosition 稳定。
- 如果改动 sandbox controller，补 controller preset smoke。
- frame validation smoke 尽量切换到 fixture camera；若 fixture 缺相机，则保留明确 fallback。
- 运行完整 CTest。

### 0.46.6 验证与收尾

- 全量构建。
- 全量 CTest。
- sandbox smoke，确认默认打开不黑屏、不崩溃。
- 更新 handoff，记录 camera 支持状态与下一阶段建议。

## 实施结果

本阶段已完成 0.46.0 ~ 0.46.6 的最小闭环：

- `ModelData` 新增 `CameraData` 与 `SceneCameraData`，可以保存 glTF perspective / orthographic camera 以及 node camera instance 的 world transform。
- `GltfLoader` 已解析 glTF `cameras`，遍历 scene node 时记录 scene camera，不再把 `node.camera` 作为无条件忽略项。
- `RenderView` 新增从 perspective `CameraData` + camera world transform + viewport extent 构建 view/projection/cameraPosition 的 helper，统一 viewport aspect fallback、Vulkan projection Y flip 和 camera position 推导。
- `assets/models/specular_ibl_validation_fixture.gltf` 已加入稳定 perspective camera node，作为后续 frame validation 与视觉 fixture 的资产相机入口。
- `ark_frame_validation_smoke` 已改为使用 fixture 中的 scene camera 渲染，不再依赖测试侧硬编码 view/projection。
- `ark_gltf_loader_smoke`、`ark_mesh_data_smoke`、`ark_forward_pass_pipeline_smoke`、`ark_framework_headers_smoke` 已覆盖 camera 数据结构、loader 解析、RenderView helper 和 public header 组合路径。

边界说明：

- orthographic camera 当前只解析和保存数据，尚未进入默认 `RenderView` helper 消费路径。
- sandbox 的交互相机仍是主要运行时控制入口；本阶段已通过带 camera fixture 的 sandbox smoke 验证资源可加载和启动稳定，但没有强行引入完整 public scene loading API 或 editor-style camera switching。
- screenshot/golden image、tone-mapped LDR readback、材质球视觉 fixture、Bloom/auto exposure 仍留给后续阶段。

本阶段验证结果：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_gltf_loader_smoke ark_mesh_data_smoke ark_forward_pass_pipeline_smoke ark_frame_validation_smoke ark_framework_headers_smoke
build\msvc-vcpkg\Debug\ark_gltf_loader_smoke.exe
build\msvc-vcpkg\Debug\ark_mesh_data_smoke.exe
build\msvc-vcpkg\Debug\ark_forward_pass_pipeline_smoke.exe
build\msvc-vcpkg\Debug\ark_framework_headers_smoke.exe
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

结果：targeted build 与 targeted smoke 全部通过；full build 通过；CTest 22/22 通过；`git diff --check` 仅有既有 CRLF line-ending warning，无 whitespace error。

额外 sandbox smoke：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
build\msvc-vcpkg\Debug\ark_sandbox.exe assets/models/specular_ibl_validation_fixture.gltf
```

结果：默认 sandbox 与带 camera fixture 的 sandbox 均可启动并在短时间 smoke 后正常停止。

## Tests

建议至少覆盖：

- `ark_gltf_loader_smoke`
  - glTF camera 被解析。
  - node camera 被记录为 scene camera。
  - perspective 参数与 transform 符合 fixture。
- `ark_forward_pass_pipeline_smoke` 或新增小型 RenderView 测试
  - perspective camera 构建后的 projection 符合 Vulkan Y flip。
  - camera position 与 world transform 一致。
- `ark_frame_validation_smoke`
  - 使用稳定相机路径后仍能通过颜色读回检查。
- sandbox smoke
  - 程序启动后短时间运行正常。

推荐命令：

```powershell
cmake --build build/msvc-vcpkg --config Debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

## 完成标准

- glTF camera 不再被 loader 无条件忽略。
- `ModelData` 能携带 camera 与 scene camera instance。
- `RenderView` 有统一 helper 能从 perspective scene camera 构建矩阵。
- 至少一个测试 fixture 覆盖 camera 解析。
- frame validation 或 sandbox 默认路径至少有一个使用资产相机。
- 无 camera 资产仍保持现有默认相机行为。
- 全量构建、CTest、sandbox smoke 均通过。
- handoff 记录 Phase 0.46 完成情况与后续建议。

## 风险与注意事项

- glTF camera 朝向是 local `-Z`，不要误用 `+Z`。
- Vulkan 投影仍需要 Y flip，否则画面会上下翻转。
- `aspectRatio` 缺省时必须使用 viewport aspect，不能写死 1.0。
- `zfar` 可选，不能在 asset 层丢失这个信息。
- 如果用 world transform inverse 构建 view，要确认矩阵布局与现有 `TransformData` 一致。
- sandbox controller 如果每帧重写 `RenderView`，renderer 内部默认相机可能不会生效。
- 不要为了本阶段过早引入完整 scene editor 或 public resource manager。

## 后续 Phase 建议

Phase 0.46 完成后，推荐继续：

- Phase 0.47 Material Ball Visual Fixture：使用稳定 camera + HDR environment 建立材质球验证场景。
- Phase 0.48 Screenshot / Golden Image System：在稳定相机与 fixture 之上做截图基准。
- Phase 0.49 Public Scene Loading API：把默认模型、相机、环境和材质资源加载从 sandbox 特例整理成公开 API。
- Phase 0.50 Post Processing Baseline：在已有 tone mapping 基础上进入 Bloom、auto exposure 或 ACES。
