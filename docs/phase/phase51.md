# Phase 0.51 Renderer Quality / Environment Bake Config API

## 实施同步

已完成 0.51.0 文档与范围确认到 0.51.5 验证与收尾。

本阶段新增 renderer quality 配置入口：

- `src/renderer/RendererQuality.h`
- `src/renderer/RendererQuality.cpp`

主要实现内容：

- 新增 `EnvironmentBakeQualityDesc` 和 `RendererQualityDesc`。
- 新增 `sanitizeRendererQualityDesc()`，统一处理默认值、非正方形 extent、非法 extent、sample count clamp、irradiance sample delta fallback/clamp 和 bake 开关依赖。
- `RendererDesc` 新增 `quality` 字段，保持现有 `defaultModelPath`、`defaultEnvironmentPath`、`useDebugOrientationEnvironment` 兼容。
- `DefaultRenderer` 构造时保存 sanitized quality。
- 默认 environment cube、irradiance cube、specular prefilter cube、BRDF LUT target 创建改为读取 `RendererQualityDesc`。
- 默认 irradiance / specular / BRDF LUT bake 参数改为读取 `RendererQualityDesc`。
- bake target 被禁用时，prepare 和 resolve path 会提前返回，让 `FrameContext` 保持 `nullptr` 并继续使用 ForwardPass fallback。
- 默认值保持 Phase 0.50 行为不变：
  - environment cube: 512x512
  - irradiance cube: 32x32
  - specular cube: 256x256
  - BRDF LUT: 256x256
  - irradiance sample delta: 0.1
  - specular sample count: 128
  - BRDF LUT sample count: 1024

测试覆盖：

- 新增 `tests/renderer_quality_smoke.cpp`。
- `ark_renderer_quality_smoke` 覆盖默认值、custom 值、extent sanitize、sample clamp、disable dependency policy。
- `ark_framework_headers_smoke` 覆盖 `RendererQuality.h` public header 和 `RendererDesc::quality` 编译契约。
- `ark_frame_validation_smoke` 保持默认画面 golden diff 为 0，未更新 baseline。

验证结果：

```text
targeted build passed
ark_renderer_quality_smoke passed
ark_framework_headers_smoke passed
ark_frame_validation_smoke passed
golden diff for specular_ibl_validation_fixture: meanAbsError=0 maxChannelError=0 mismatchedPixelRatio=0
golden diff for material_ball_validation_fixture: meanAbsError=0 maxChannelError=0 mismatchedPixelRatio=0
git diff --check: only line-ending warnings, no whitespace errors
full build passed
CTest: 24/24 tests passed
default sandbox hidden-window smoke passed
material ball sandbox hidden-window smoke passed
debug orientation sandbox hidden-window smoke passed
```

## 阶段判断

Phase 0.50 已经把默认场景资源加载从 `DefaultRenderer` 中抽出到 `SceneResource`，模型路径、HDR 路径、debug/procedural environment fallback 和加载 report 都有了独立入口。当前 renderer 的默认画面路径已经比较完整：

- glTF 模型加载。
- PBR 材质、纹理、sampler、UV transform。
- HDR environment resource。
- equirectangular HDR -> cubemap。
- diffuse irradiance IBL。
- prefiltered specular IBL。
- BRDF LUT。
- skybox。
- ForwardPass。
- ToneMappingPass。
- frame validation golden image。
- sandbox 默认模型和环境显示。

但 environment bake 的质量策略仍然硬编码在 `src/renderer/Renderer.cpp`：

```cpp
constexpr rhi::Extent2D DefaultEnvironmentCubeFaceExtent{512, 512};
constexpr rhi::Extent2D DefaultIrradianceCubeFaceExtent{32, 32};
constexpr rhi::Extent2D DefaultSpecularCubeFaceExtent{256, 256};
constexpr rhi::Extent2D DefaultBrdfLutExtent{256, 256};
constexpr float DefaultIrradianceSampleDelta = 0.1f;
constexpr u32 DefaultSpecularPrefilterSampleCount = 128;
constexpr u32 DefaultBrdfLutSampleCount = 1024;
```

这些常量决定了默认 IBL 的显存占用、启动 bake 成本、specular IBL 质量、BRDF LUT 质量和后续 sandbox/validation 的运行成本。继续把它们藏在 `DefaultRenderer` 内部，会让后续做质量档位、性能模式、测试 fixture、截图基线和 sandbox preset 时变得不够清晰。

Phase 0.51 的核心目标是建立一个小而稳定的 renderer quality config API，把默认 environment bake 质量从硬编码常量收口为可描述、可校验、可测试的配置结构。它是 Phase 0.50 后的自然延伸：0.50 解决“加载什么资源”，0.51 解决“以什么质量准备默认渲染资源”。

## 目标

- 新增 renderer-facing 的 quality/bake 配置结构。
- 保持当前默认画面和默认质量完全兼容。
- 将 `DefaultRenderer` 里的默认 IBL bake target 创建参数改为来自配置结构。
- 对外保留 `RendererDesc` 的现有字段，同时新增 quality 配置入口。
- 对非法或极端配置做明确 clamp/fallback，避免创建 0 尺寸 texture 或过大的 bake target。
- 增加 smoke test 覆盖默认值、custom value、clamp 行为和配置传递。
- 继续使用 Phase 0.49 golden image validation 保护默认画面不发生意外变化。

## 非目标

- 不引入 Bloom、auto exposure、ACES 或新的后处理效果。
- 不改变 shader 的 PBR/IBL 数学。
- 不改变 `SceneResource` 的职责边界，不把 bake target 生命周期塞进 `SceneResource`。
- 不做 runtime scene switching UI。
- 不做 asset hot reload。
- 不做完整 RenderGraph 重构。
- 不做复杂质量 preset UI，只先提供底层描述结构。

## 推荐 API 形态

### EnvironmentBakeQualityDesc

建议新增一个小的 renderer quality header，例如：

```text
src/renderer/RendererQuality.h
src/renderer/RendererQuality.cpp
```

第一版结构可以聚焦 environment bake：

```cpp
namespace ark {
    struct EnvironmentBakeQualityDesc {
        rhi::Extent2D environmentCubeFaceExtent{512, 512};
        rhi::Extent2D irradianceCubeFaceExtent{32, 32};
        rhi::Extent2D specularCubeFaceExtent{256, 256};
        rhi::Extent2D brdfLutExtent{256, 256};

        float irradianceSampleDelta = 0.1f;
        u32 specularPrefilterSampleCount = 128;
        u32 brdfLutSampleCount = 1024;

        bool enableEnvironmentCube = true;
        bool enableIrradiance = true;
        bool enableSpecularPrefilter = true;
        bool enableBrdfLut = true;
    };

    struct RendererQualityDesc {
        EnvironmentBakeQualityDesc environmentBake;
    };
}
```

命名可以在实现时微调，但建议保留两个层级：

- `RendererQualityDesc`：未来可以容纳 shadow、post-processing、MSAA、anisotropy 等质量项。
- `EnvironmentBakeQualityDesc`：本阶段只管默认 IBL bake 相关参数。

### RendererDesc 接入

在 `RendererDesc` 中新增：

```cpp
RendererQualityDesc quality;
```

现有字段继续保留：

```cpp
Path defaultModelPath;
Path defaultEnvironmentPath;
bool useDebugOrientationEnvironment = false;
```

这样外部代码不需要立刻改，但后续可以通过 `RendererDesc.quality` 调整默认 bake 质量。

### Sanitized Quality

建议提供一个纯函数做配置规整：

```cpp
RendererQualityDesc sanitizeRendererQualityDesc(const RendererQualityDesc& desc);
```

它应该处理：

- extent 为 0 时回退到默认值。
- 非正方形 cubemap face extent 可以回退或取较小维度形成正方形。
- sample count clamp 到合理范围。
- `irradianceSampleDelta <= 0` 时回退默认值。
- 如果 `enableEnvironmentCube == false`，则 irradiance/specular 默认也无法生成。
- 如果 `enableSpecularPrefilter == false`，ForwardPass 仍应走现有 fallback specular path。
- 如果 `enableBrdfLut == false`，ForwardPass 仍应走现有 fallback BRDF LUT path。

建议第一版 clamp 范围保守一些：

```text
environment cube face: 16..2048
irradiance cube face: 8..256
specular cube face: 16..2048
BRDF LUT extent: 16..1024
specular sample count: 1..4096
BRDF LUT sample count: 1..4096
irradiance sample delta: 0.005..1.0
```

具体范围可以按现有测试成本微调。关键是行为必须明确，并由 smoke test 覆盖。

## 实施拆分

### 0.51.0 文档与范围确认

- 确认本阶段只做 renderer quality / environment bake config API。
- 确认默认值保持 Phase 0.50 行为。
- 确认不引入新的视觉效果，不更新 golden baseline。
- 确认 `SceneResource` 边界不变。

### 0.51.1 Renderer Quality API Foundation

新增：

```text
src/renderer/RendererQuality.h
src/renderer/RendererQuality.cpp
```

提供：

- `EnvironmentBakeQualityDesc`
- `RendererQualityDesc`
- `sanitizeRendererQualityDesc()`
- 必要的默认常量或 helper

默认值必须与当前 `Renderer.cpp` 常量一致：

```text
environment cube: 512x512
irradiance cube: 32x32
specular cube: 256x256
BRDF LUT: 256x256
irradiance sample delta: 0.1
specular sample count: 128
BRDF LUT sample count: 1024
```

### 0.51.2 RendererDesc Quality Plumbing

修改：

```text
src/renderer/Renderer.h
src/renderer/Renderer.cpp
```

目标：

- `RendererDesc` 新增 `RendererQualityDesc quality`。
- `DefaultRenderer` 构造时保存 sanitized quality。
- `createDefaultEnvironmentBakeTargets()` 使用 sanitized quality 创建：
  - `EnvironmentCubeResourceDesc`
  - `EnvironmentCubeResourceDesc` for irradiance
  - `EnvironmentCubeResourceDesc` for specular
  - `EnvironmentBrdfLutResourceDesc`
- `prepareDefaultIrradianceCube()` 使用配置里的 `irradianceSampleDelta`。
- `prepareDefaultSpecularCube()` 使用配置里的 `specularPrefilterSampleCount`。
- `prepareDefaultBrdfLut()` 使用配置里的 `brdfLutSampleCount`。

实现上要注意：

- 如果某个 target 被禁用，相关 prepare function 应该快速 return。
- 如果 environment cube 被禁用，irradiance/specular 不能尝试生成。
- 如果 specular 或 BRDF LUT 被禁用，`FrameContext` 对应指针保持 `nullptr`，让 ForwardPass fallback 继续工作。

### 0.51.3 Tests

建议新增：

```text
tests/renderer_quality_smoke.cpp
```

覆盖内容：

- 默认 `RendererQualityDesc` sanitize 后值与当前默认常量一致。
- custom extents 和 sample counts 能保留。
- 0 尺寸 extent 会 fallback 到默认值。
- 非正方形 cubemap face extent 行为明确。
- sample count 过小/过大时会 clamp。
- disable flags 的组合逻辑明确：
  - disable environment cube 后，dependent bake targets 不应被视为可用。
  - disable specular 后，specular sample count 不影响最终配置。
  - disable BRDF LUT 后，BRDF LUT sample count 不影响最终配置。

同时更新：

```text
tests/framework_headers_smoke.cpp
```

让 public header 编译契约覆盖 `RendererQuality.h`。

如果实现中抽出 resource desc builder helper，可以在 smoke test 中直接检查 builder 输出，避免为了测试配置传递而创建真实 Vulkan renderer。

### 0.51.4 默认画面回归验证

必须继续执行：

```powershell
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe
```

预期：

- `specular_ibl_validation_fixture.png` golden diff 仍为 0。
- `material_ball_validation_fixture.png` golden diff 仍为 0。

如果默认值保持一致，不应该更新 golden baseline。

### 0.51.5 验证与收尾

建议执行：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_renderer_quality_smoke ark_framework_headers_smoke ark_frame_validation_smoke ark_sandbox
build\msvc-vcpkg\Debug\ark_renderer_quality_smoke.exe
build\msvc-vcpkg\Debug\ark_framework_headers_smoke.exe
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

sandbox smoke：

```powershell
ark_sandbox hidden-window smoke
ark_sandbox hidden-window smoke with assets\models\material_ball_validation_fixture.gltf
ark_sandbox hidden-window smoke with --debug-orientation
```

收尾：

- 更新 `docs/codex_handoff.md`。
- 在本文补充实施同步和验证结果。
- 确认 worktree 只包含 Phase 0.51 相关改动。

## 完成标准

- 新增 renderer quality config API。
- 默认质量配置与 Phase 0.50 完全兼容。
- `DefaultRenderer` 不再直接依赖散落的 environment bake 常量。
- 自定义 environment bake 参数有明确入口。
- 非法配置有明确 sanitize/clamp 行为。
- disable flags 不会触发无效 bake 或无效 resource resolve。
- 新 smoke test 覆盖默认值、custom 值和 clamp 行为。
- `ark_frame_validation_smoke` golden diff 仍通过且不需要更新 baseline。
- full build 和 CTest 通过。
- sandbox 默认打开仍显示模型、skybox 和 IBL 效果。

## 风险与注意事项

- 不要因为引入配置结构而改变默认值，否则 golden image 会变化。
- cubemap face extent 必须保持正方形，否则 cube resource/view 语义会变复杂。
- specular prefilter mip count 由 specular cube face extent 推导，改变默认 extent 会改变 mip chain 和视觉结果。
- disabling BRDF LUT 或 specular prefilter 时，ForwardPass fallback 路径必须保持可用。
- 配置结构不要过早膨胀到 post-processing、shadow、MSAA 等领域。本阶段只做 environment bake。
- `RendererDesc` 可以新增字段，但不要移除现有字段，避免破坏 sandbox 和测试入口。

## 后续方向

Phase 0.51 完成后，下一阶段可以考虑：

1. Scene preset / sandbox loading path：用 `SceneResource` 和 `RendererQualityDesc` 组织 default、material ball、specular validation、debug orientation 等 preset。
2. Post-processing foundation：在 golden validation 稳定保护下推进 Bloom、auto exposure 或 ACES tone mapping。
3. Transparent sorting：基于 camera position 和 bounds 做 Blend bucket back-to-front 排序。
4. Resource lifetime cleanup：整理 pipeline、descriptor、texture view 等 GPU resource 的 deferred destruction。
5. RenderGraph 深化：把 pass resource dependency 从手写顺序逐步收口为更明确的声明式关系。
