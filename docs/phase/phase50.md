# Phase 0.50 Public Scene / Resource Loading API Foundation

## 实施同步

Phase 0.50 已按本文范围落地：

- 新增 `src/renderer/SceneResource.h/.cpp`，提供 `SceneResourceLoadDesc`、fallback/source enums、`SceneResourceLoadReport` 和拥有资源生命周期的 `SceneResource`。
- `SceneResource` 现在统一处理 model path resolution、DamagedHelmet / committed fixture fallback、HDR / debug orientation / procedural environment fallback，并把 `ModelResource`、`EnvironmentResource` 和 `RenderScene` 组织成可渲染场景。
- `DefaultRenderer` 的默认模型和默认 environment 创建逻辑已迁移到 `SceneResource m_DefaultSceneResource`；renderer 仍保留 default environment cube、irradiance cube、prefiltered specular cube 和 BRDF LUT 的 bake target 生命周期。
- `RendererDesc.defaultModelPath`、`RendererDesc.defaultEnvironmentPath`、`RendererDesc.useDebugOrientationEnvironment` 外部行为保持兼容，sandbox 默认打开仍走默认模型和 IBL 路径。
- 新增 `ark_scene_resource_smoke`，覆盖 explicit fixture load、missing model fallback、debug orientation environment、missing HDR fallback、report/source/path 和 `RenderQueue` flatten。
- `ark_framework_headers_smoke` 已纳入 `renderer/SceneResource.h`，确保 public header 编译契约被覆盖。
- Phase 0.49 的 `ark_frame_validation_smoke` golden diff 继续作为默认画面迁移保护网。

验证命令：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_scene_resource_smoke ark_framework_headers_smoke ark_sandbox
build\msvc-vcpkg\Debug\ark_scene_resource_smoke.exe
build\msvc-vcpkg\Debug\ark_framework_headers_smoke.exe
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
ark_sandbox hidden-window smoke
ark_sandbox hidden-window smoke with assets\models\material_ball_validation_fixture.gltf
ark_sandbox hidden-window smoke with --debug-orientation
```

验证结果：

- targeted build passed。
- `ark_scene_resource_smoke` passed。
- `ark_framework_headers_smoke` passed。
- `ark_frame_validation_smoke` passed，两个 golden PNG 的 mean/max/mismatch diff 均为 0。
- `git diff --check` 仅有 CRLF 提示，无 whitespace error。
- full Debug build passed。
- CTest passed：23/23。
- sandbox hidden-window smoke passed：default、material ball override、`--debug-orientation`。

## 阶段判断

Phase 0.49 已经把最终 LDR 画面接入 screenshot artifact 和 golden image diff。现在项目具备了比较可靠的视觉回归兜底：如果默认场景、IBL、tone mapping 或材质路径被重构影响，`ark_frame_validation_smoke` 能通过 PNG golden 及时发现明显画面变化。

因此下一阶段最适合做工程边界收口，而不是继续堆新效果。当前 renderer 的基础功能已经可以跑通：

- glTF 模型加载。
- PBR 材质、纹理、sampler、UV transform。
- direct Cook-Torrance BRDF。
- HDR environment、equirectangular -> cubemap。
- diffuse irradiance IBL。
- prefiltered specular IBL + BRDF LUT。
- skybox、ForwardPass、ToneMappingPass。
- sandbox orbit camera。
- frame readback、LDR artifact、golden validation。

但默认模型、默认 HDR、debug/procedural environment、fallback path 和默认 scene 资源生命周期仍然主要写在 `src/renderer/Renderer.cpp` 的 `DefaultRenderer` 内部，并带有明显 sandbox 语义。`Application` 只把路径转交给 `RendererDesc`，tests 又有自己的 fixture 加载逻辑。长期看，这会让后续增加 scene presets、quality config、runtime scene switching、资源热重载或更多 validation fixture 都变得分散。

Phase 0.50 的核心目标是建立一个最小但清晰的 **public scene/resource loading API foundation**：把“加载一个可渲染场景需要哪些输入、会得到哪些资源、fallback 发生了什么”从 sandbox 特例整理成 renderer 层可复用入口。

## 目标

- 新增 renderer-facing 的 scene/resource loading 描述结构，统一表达：
  - 模型路径。
  - 模型 fallback 策略。
  - environment HDR 路径。
  - debug/procedural environment 策略。
  - scene/model debug name。
  - environment intensity。
- 新增一个拥有资源生命周期的 scene resource 对象，负责持有：
  - `asset::ModelData`。
  - `ModelResource`。
  - `EnvironmentResource`。
  - `RenderScene`。
  - 加载结果/诊断信息。
- 将 `DefaultRenderer` 里的默认模型和默认 environment 创建逻辑迁移到该 scene resource API。
- 保持 sandbox 默认打开仍然能显示模型和 IBL 效果。
- 保持现有 `RendererDesc.defaultModelPath`、`RendererDesc.defaultEnvironmentPath`、`useDebugOrientationEnvironment` 对外行为不变，避免破坏 app 入口。
- 让后续 tests 或 sandbox 能复用同一套 scene loading/fallback 逻辑，而不是各自复制路径查找和资源创建。
- 用 Phase 0.49 golden validation 保护迁移后的默认渲染路径。

## 非目标

- 不做 runtime scene switching UI。
- 不做 asset hot reload。
- 不做完整 RenderGraph。
- 不做 async resource streaming。
- 不做 glTF 多 scene 选择 UI。
- 不在本阶段引入 Bloom、auto exposure、ACES 或新的后处理。
- 不把 renderer quality config 全部展开；本阶段只为后续 quality config 留位置。
- 不重写 `GltfLoader`、`ModelResource`、`TextureCache` 或 material system。
- 不改变 `RenderScene` 当前“引用 renderer resources”的基本模型。

## 当前基线

当前默认路径大致是：

```text
apps/sandbox/main.cpp
    -> ApplicationDesc.defaultModelPath / defaultEnvironmentPath / debug flag
    -> Application::run()
        -> RendererDesc
        -> createRenderer()
            -> DefaultRenderer
                -> createDefaultScene()
                    -> findSandboxModelFile()
                    -> asset::loadGltfModel()
                    -> ModelResource::create()
                    -> m_DefaultScene.addModel()
                -> createDefaultEnvironment()
                    -> findSandboxEnvironmentFile()
                    -> asset::loadImageHdrRgba32F()
                    -> fallback procedural/debug environment
                    -> EnvironmentResource::create()
                    -> m_DefaultScene.setEnvironment()
                    -> create default cubemap / irradiance / specular / BRDF LUT targets
```

这条路径能工作，但问题是：

- `Renderer.cpp` 里有 sandbox 命名和路径策略。
- 默认 scene 的资源拥有关系不够独立，`DefaultRenderer` 同时负责 renderer frame flow、default scene loading、environment bake resources。
- 加载诊断信息只通过日志散落输出，外部无法结构化知道实际用了哪个 model/environment。
- tests 暂时很难复用 renderer 默认加载策略。
- 后续如果加入 renderer quality config 或 scene preset，`RendererDesc` 会继续膨胀。

## 推荐 API 形态

### SceneResourceLoadDesc

建议新增 `src/renderer/SceneResource.h/.cpp`，提供最小描述结构：

```cpp
namespace ark {
    enum class SceneModelFallbackPolicy {
        None,
        DefaultSandboxModel,
    };

    enum class SceneEnvironmentFallbackPolicy {
        None,
        DefaultHdrThenProcedural,
        ProceduralOnly,
        DebugOrientation,
    };

    struct SceneResourceLoadDesc {
        Path modelPath;
        SceneModelFallbackPolicy modelFallback = SceneModelFallbackPolicy::DefaultSandboxModel;

        Path environmentPath;
        SceneEnvironmentFallbackPolicy environmentFallback =
            SceneEnvironmentFallbackPolicy::DefaultHdrThenProcedural;

        std::string sceneName = "DefaultScene";
        std::string modelName = "DefaultModel";
        std::string environmentName = "DefaultEnvironment";
        float environmentIntensity = 1.0f;
    };
}
```

初版不追求完美命名，但要保证语义清晰：desc 描述“想加载什么”和“失败时怎么 fallback”。

### SceneResourceLoadReport

建议同时提供结构化加载结果：

```cpp
namespace ark {
    enum class SceneModelSource {
        None,
        RequestedPath,
        DefaultFallback,
    };

    enum class SceneEnvironmentSource {
        None,
        RequestedHdr,
        DefaultHdr,
        Procedural,
        DebugOrientation,
    };

    struct SceneResourceLoadReport {
        bool modelLoaded = false;
        bool environmentLoaded = false;
        Path resolvedModelPath;
        Path resolvedEnvironmentPath;
        SceneModelSource modelSource = SceneModelSource::None;
        SceneEnvironmentSource environmentSource = SceneEnvironmentSource::None;
    };
}
```

这样 renderer、sandbox、tests 都可以知道“实际加载了什么”，而不是只能从日志里猜。

### SceneResource

建议新增拥有资源生命周期的类：

```cpp
namespace ark {
    class SceneResource {
    public:
        bool load(rhi::RenderDevice& device, const SceneResourceLoadDesc& desc);
        void resetImmediate();

        RenderScene& scene();
        const RenderScene& scene() const;

        const asset::ModelData& modelData() const;
        ModelResource* model();
        EnvironmentResource* environment();

        const SceneResourceLoadReport& report() const;
        bool hasScene() const;
    };
}
```

职责边界：

- `SceneResource` 负责 CPU asset 数据和 GPU scene resources。
- `SceneResource` 可以构建一个 `RenderScene`，但不负责 per-frame rendering。
- `SceneResource` 不负责 prefiltered specular / irradiance / BRDF LUT bake target 生命周期；这些仍然暂时由 `DefaultRenderer` 管，因为它们和 renderer device/frame command flow 更紧。
- `SceneResource` 不创建 `RenderView`，但保留 `asset::ModelData`，让后续可复用 glTF scene camera。

## 推荐实施拆分

### 0.50.0 文档与范围确认

- 确认本阶段目标是 public scene/resource loading API foundation。
- 明确不做 runtime scene switching、不做 quality config 大改、不做后处理。
- 明确 sandbox 默认显示模型和 IBL 的行为必须保持。
- 明确 Phase 0.49 golden validation 是迁移保护网。

### 0.50.1 SceneResource API Foundation

新增：

```text
src/renderer/SceneResource.h
src/renderer/SceneResource.cpp
```

第一步只放结构和最小类骨架：

- `SceneResourceLoadDesc`
- `SceneResourceLoadReport`
- fallback/source enums
- `SceneResource::load()`
- `SceneResource::resetImmediate()`
- `SceneResource::scene()`
- `SceneResource::report()`

实现时先复用现有能力：

- `asset::loadGltfModel()`
- `ModelResource::create()`
- `asset::loadImageHdrRgba32F()`
- `makeProceduralSandboxEnvironmentImage()`
- `makeDebugOrientationEnvironmentImage()`
- `EnvironmentResource::create()`

### 0.50.2 Path Resolution And Fallback Policy

把当前 `Renderer.cpp` 内的默认路径策略迁移到 SceneResource 或一个小的 renderer-private helper：

```text
assets/models/DamagedHelmet/DamagedHelmet.gltf
assets/models/forward_multinode_fixture.gltf
assets/HDR/2k.hdr
```

建议保留多级相对路径查找，但不要让函数名继续叫 `findSandbox...`。可以改成：

- `findRendererAssetFile()`
- `findDefaultModelFile()`
- `findDefaultEnvironmentFile()`

fallback 行为建议：

- 有 explicit model path：
  - 找到则使用 requested path。
  - 找不到则按 `modelFallback` 决定是否退到 default model。
- 没有 explicit model path：
  - `DefaultSandboxModel` 策略下优先 DamagedHelmet，缺失时退到 committed fixture。
- 有 explicit HDR path：
  - 找到并成功解码则使用 requested HDR。
  - 找不到或解码失败则按 environment fallback 决定是否退到 default/procedural。
- debug orientation：
  - 直接使用 debug orientation environment，不再尝试 HDR。

### 0.50.3 DefaultRenderer Migration

把 `DefaultRenderer` 的默认 scene 持有方式从散落成员迁移为：

```cpp
SceneResource m_DefaultSceneResource;
```

迁移后：

- `createDefaultScene()` 和 `createDefaultEnvironment()` 可以合并成 `createDefaultSceneResource()`。
- `render()` 中的默认 scene 选择改为：

```cpp
RenderScene& renderScene =
    scene.empty() && m_DefaultSceneResource.hasScene()
        ? m_DefaultSceneResource.scene()
        : scene;
```

- environment bake 判断从 `m_DefaultEnvironment` 改为 `m_DefaultSceneResource.environment()`。
- default environment cube / irradiance / specular / BRDF LUT target 仍由 `DefaultRenderer` 持有。
- `RendererDesc` 先保持兼容，不强制改外部应用入口。

### 0.50.4 Sandbox / Test Reuse Path

本阶段最小目标是 renderer 默认路径先复用 `SceneResource`。如果实现量可控，再增加一个 smoke test 直接验证 `SceneResource`：

```text
tests/scene_resource_smoke.cpp
```

建议覆盖：

- explicit committed fixture model load。
- missing explicit model fallback 到 committed default fixture。
- missing HDR fallback 到 procedural environment。
- debug orientation environment source。
- report 中 resolved path/source 正确。
- `RenderScene` 非空，且 environment 已设置。

如果测试创建真实 Vulkan device 成本较高，可以先用现有 renderer smoke/frame validation 兜底，不强行引入复杂 fake RHI。

### 0.50.5 Tests

建议至少执行：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_frame_validation_smoke ark_dependency_smoke
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe
build\msvc-vcpkg\Debug\ark_dependency_smoke.exe
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

如果新增 `ark_scene_resource_smoke`，补充：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_scene_resource_smoke
build\msvc-vcpkg\Debug\ark_scene_resource_smoke.exe
```

迁移涉及默认 renderer/sandbox path，建议继续执行：

```powershell
ark_sandbox hidden-window smoke
build\msvc-vcpkg\Debug\ark_sandbox.exe assets\models\material_ball_validation_fixture.gltf
build\msvc-vcpkg\Debug\ark_sandbox.exe --debug-orientation
```

### 0.50.6 验证与收尾

- 更新 `docs/codex_handoff.md`。
- 在本文件补充实施同步。
- 确认 default sandbox 仍能自动显示模型、skybox 和 IBL 效果。
- 确认 `ark_frame_validation_smoke` golden diff 通过，避免默认资源迁移造成画面回归。

## 完成标准

- 新增清晰的 scene/resource loading API foundation。
- 默认 model/environment path resolution 不再只藏在 `DefaultRenderer` 的 sandbox 命名 helper 中。
- `DefaultRenderer` 通过 `SceneResource` 持有默认场景资源。
- 现有 `RendererDesc` 外部行为保持兼容。
- sandbox 默认打开仍显示模型和环境效果。
- debug orientation environment 路径继续可用。
- Phase 0.49 golden image validation 继续通过。
- 全量 build 和 CTest 通过。
- 文档和 handoff 同步。

## 风险与注意事项

- `RenderScene` 当前持有的是 renderer resource 指针，`SceneResource` 必须比它内部的 `RenderScene` 使用期更长。
- `ModelData` 如果后续要用于 glTF scene camera，需要在 `SceneResource` 中保留，不要加载后立即丢弃。
- environment bake resources 暂时不要塞进 `SceneResource`，否则它会同时承担 scene loading 和 renderer quality/bake lifecycle，边界会变厚。
- fallback 发生时必须记录 report 和日志，避免用户指定路径失败却静默看到另一个资源。
- 默认资源路径查找要兼容从 repo root、build 目录、Debug 输出目录启动。
- 不要因为本阶段抽 API 就改变现有 PBR/IBL shader 行为；画面应该保持稳定。

## 后续方向

Phase 0.50 完成后，下一步建议按这个顺序推进：

1. Renderer quality config API：把 environment cube size、irradiance size、specular prefilter size、BRDF LUT size、sample count 等配置从常量收口到描述结构。
2. Runtime scene preset / loading command：让 sandbox 可以更明确地切换 default fixture、material ball、debug orientation、外部模型。
3. Post-processing stack：在 scene loading 和 screenshot/golden 稳定后，再推进 Bloom、auto exposure、ACES。
4. Blend bucket sorting：基于 camera position 和 bounds 做透明物体 back-to-front 排序。
5. Deferred destruction：整理 pipeline、descriptor、texture view 等 GPU resource lifetime。
