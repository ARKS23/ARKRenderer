# Phase 0.63 Renderer Public Scene / Resource API 收口

## 实施状态

已完成。

Phase 0.62 已经建立默认组合场景的视觉回归闭环：`ark_frame_validation_smoke`
可以渲染 Sponza + DamagedHelmet，并用 golden baseline 覆盖 Shadow、Bloom、ACES
ToneMapping 和 IBL。Phase 0.63 的目标不是新增渲染效果，而是把默认场景、渲染质量、
view 参数和初始相机参数收束到更清晰的公共接口，减少 sandbox、renderer 和测试之间的
重复配置。

## 阶段目标

- 明确 renderer-facing 的公共配置入口。
- 让默认组合场景的 scene / quality / view / camera 具备统一 resolved preset 源头。
- 区分资源加载配置、渲染质量配置、每视图后处理配置和应用层交互相机配置。
- 收口 `ApplicationDesc` / `RendererDesc` 中重复的 scene resource 字段。
- 让 sandbox 和 frame validation 复用同一套 preset/profile 结果。
- 保持现有默认 sandbox 画面和 Phase 0.62 golden baseline 不发生无意漂移。

## 非目标

- 不引入 RenderGraph 重构。
- 不引入 ECS、Scene Graph、Asset Database 或 hot reload。
- 不改变 PBR、IBL、Shadow、Bloom、ToneMapping 算法。
- 不引入运行时 UI 或 editor 工具。
- 不做大规模命名迁移。

## 接口分层

### Asset 层

职责：

- 解析 glTF、图片、KTX、shader 字节码。
- 输出 CPU-side asset data。
- 不创建 RHI 资源，不感知 sandbox / preset / camera controller。

代表接口：

```text
asset::loadGltfModel()
asset::loadImageAuto()
asset::ModelData
asset::ImageData
```

### Renderer Resource 层

职责：

- 把 CPU-side asset data 转成 GPU resource。
- 管理 `ModelResource`、`EnvironmentResource`、`RenderScene` 生命周期。
- 暴露 load report、bounds、resolved path 等诊断信息。
- 不解析 CLI，不拥有窗口或输入。

代表接口：

```text
SceneResourceLoadDesc
SceneResourceLoadReport
SceneResource
ModelResource
EnvironmentResource
RenderScene
```

### Renderer Profile 层

职责：

- 把高层 preset resolve 成纯数据配置。
- 不创建 GPU 资源，不直接渲染。
- 作为 sandbox、frame validation 和后续引擎接入的共同入口。

当前输出：

```cpp
struct ResolvedRendererPreset {
    SceneResourceLoadDesc scene;
    RendererQualityDesc quality;
    RenderViewProfileDesc view;
    OrbitCameraProfileDesc camera;
    OrbitCameraProfileDesc captureCamera;
};
```

字段约束：

- `scene`：只描述加载哪些资源、transform、environment 和 lighting。
- `quality`：只描述 renderer bake / quality。
- `view`：只描述 ToneMapping、PostProcessing、Shadow 这类每视图设置。
- `camera`：sandbox 交互相机初始视角。
- `captureCamera`：frame validation / 截图回归使用的稳定视角。

### App / Sandbox 层

职责：

- 解析 CLI。
- 处理窗口、输入、orbit camera controller。
- 把 CLI override 应用到 resolved preset。
- 不复制默认 Sponza / Helmet / Bloom / Shadow 参数。

代表接口：

```text
SandboxLaunchOptions
makeSandboxApplicationDesc()
Application
SandboxCameraController
```

### Renderer Runtime 层

职责：

- 创建 backend / swapchain。
- 维护默认 `SceneResource`。
- 执行 environment bake 和 frame pass。
- 渲染外部传入 scene 或 renderer 持有的默认 scene。

代表接口：

```text
RendererDesc
Renderer
FrameRenderer
FrameContext
```

## 完成内容

### 0.63.0 文档与范围确认

- 新增本阶段文档。
- 明确本阶段只做 public API 边界收口，不新增渲染算法。

### 0.63.1 Interface Audit

已检查：

```text
src/renderer/RendererPreset.h
src/renderer/RendererPreset.cpp
src/renderer/SceneResource.h
src/renderer/SceneResource.cpp
src/renderer/Renderer.h
src/renderer/Renderer.cpp
src/app/Application.h
src/app/Application.cpp
src/app/SandboxLaunchOptions.h
src/app/SandboxLaunchOptions.cpp
tests/frame_validation_smoke.cpp
```

结论：

- `ApplicationDesc` / `RendererDesc` 中重复保存的 model/environment/lighting 字段应收口到 `SceneResourceLoadDesc`。
- 默认 Bloom / ToneMapping / Shadow 应归属 renderer profile 的 view 数据。
- 默认 orbit camera 应放在 renderer profile 的纯数据结构中，sandbox controller 只消费它。
- frame validation 不应维护第二份默认组合相机和后处理参数。

### 0.63.2 View / Camera Profile 数据结构

新增：

```cpp
struct RenderViewProfileDesc {
    ToneMappingSettings toneMapping;
    PostProcessingSettings postProcessing;
    ShadowSettings shadows;
};

struct OrbitCameraProfileDesc {
    glm::vec3 target;
    float distance;
    float yawRadians;
    float pitchRadians;
    float verticalFovRadians;
    float nearPlane;
    float farPlane;
};
```

`ResolvedRendererPreset` 现在输出：

- `scene`
- `quality`
- `view`
- `camera`
- `captureCamera`

同时新增 `applyOrbitCameraProfile()`，供 frame validation 直接把 renderer 层 camera profile 写入 `RenderView`。

### 0.63.3 默认组合 Profile 单一源头

- `RendererPreset` 继续作为默认组合场景的源头：
  - Sponza 主模型。
  - DamagedHelmet additional model。
  - 默认 lighting / environment intensity。
  - 默认 Bloom / ACES / Shadow。
  - sandbox 初始 camera。
  - frame validation capture camera。
- `tests/frame_validation_smoke.cpp` 不再手写默认组合 camera / post-processing / shadow 参数。
- 默认组合 golden 仍通过 `ark_frame_validation_smoke` 校验。

### 0.63.4 ApplicationDesc / RendererDesc 收口

`ApplicationDesc` 收口为：

```cpp
struct ApplicationDesc {
    WindowDesc window;
    SceneResourceLoadDesc defaultScene;
    RendererQualityDesc rendererQuality;
    RenderViewProfileDesc view;
    OrbitCameraProfileDesc camera;
    bool useDebugOrientationEnvironment;
};
```

`RendererDesc` 收口为：

```cpp
struct RendererDesc {
    rhi::NativeWindowHandle nativeWindow;
    rhi::Extent2D extent;
    SceneResourceLoadDesc defaultScene;
    RendererQualityDesc quality;
    bool enableValidation;
};
```

结果：

- `RendererDesc` 不再包含 camera 或 app 输入相关状态。
- `RendererDesc` 不再复制 model/environment/lighting 的散字段。
- `Application` 负责把 `OrbitCameraProfileDesc` 转成 `SandboxCameraControllerDesc`。

### 0.63.5 Sandbox / Frame Validation 迁移

- `SandboxLaunchOptions` 统一保存 `RendererPresetDesc`、CLI view override 值和 override mask。
- `makeSandboxApplicationDesc()` 先 resolve preset，再应用 debug orientation、model/environment path override 和 view override。
- view override 从 `resolved.view` 开始叠加，避免 sandbox 复制另一套 preset 默认值。
- frame validation 默认组合 case 使用 `ResolvedRendererPreset::view` 和 `ResolvedRendererPreset::captureCamera`。
- `renderer_preset_smoke`、`framework_headers_smoke`、`bloom_validation_fixture_smoke`、`post_processing_settings_smoke` 已迁移到新字段。

### 0.63.6 Tests

已通过定向构建：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_renderer_preset_smoke ark_framework_headers_smoke ark_bloom_validation_fixture_smoke ark_post_processing_settings_smoke ark_frame_validation_smoke ark_scene_resource_smoke ark_sandbox_camera_controller_smoke ark_sandbox
```

已通过定向 CTest：

```powershell
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(renderer_preset|framework_headers|bloom_validation_fixture|post_processing_settings|frame_validation|scene_resource|sandbox_camera_controller)_smoke" --output-on-failure
```

结果：

```text
targeted build passed
targeted CTest passed: 7/7
```

### 0.63.7 验证与收尾

已完成：

```powershell
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

Sandbox hidden-window smoke：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset sponza
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset shadow-validation
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation --bloom --tone-mapping aces
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset sponza --shadow-bounds 64 --shadow-strength=1.0
```

结果：

```text
git diff --check: only CRLF warnings, no whitespace errors
full Debug build passed
full CTest passed: 30/30
sandbox hidden-window smoke passed for default, sponza, shadow-validation, bloom-validation, and sponza manual shadow bounds
```

## 完成标准

- 默认组合场景配置具备单一 resolved profile 源头。
- sandbox 和 frame validation 不再复制默认 camera / post / shadow 参数。
- `ApplicationDesc` / `RendererDesc` 中重复 scene load 字段已收口。
- `SceneResourceLoadDesc` 继续只负责资源加载，不混入 view / camera / post-processing。
- renderer 层不依赖 app 层 camera controller。
- Phase 0.62 default composite golden 仍通过。
- targeted build / CTest 通过。
- full build / full CTest 通过。
- sandbox default / sponza / shadow-validation hidden-window smoke 通过。

## 后续方向

Phase 0.63 完成后，建议继续推进：

1. Phase 0.64：Shadow quality pass，优先做 texel snapping 和 PCF quality preset。
2. Phase 0.65：KTX2 / BasisU / 原始 mip chain 支持。
3. Phase 0.66：基础调试 UI，用于运行时调整 Bloom、ToneMapping、Shadow、IBL 和 camera 参数。
4. Phase 0.67：Renderer integration sample，用更接近外部引擎调用方式的小示例验证 public API。
