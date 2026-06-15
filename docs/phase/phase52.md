# Phase 0.52 Scene / Quality Preset Foundation

## Implementation Sync

Phase 0.52 has completed 0.52.0 scope confirmation through 0.52.5 validation and closeout.

This phase adds a small renderer-facing preset layer on top of the Phase 0.50 `SceneResource` boundary and the Phase 0.51 `RendererQualityDesc` boundary. It does not change the default rendering path, shader math, golden baselines, or resource ownership model.

Implemented files:

- `src/renderer/RendererPreset.h`
- `src/renderer/RendererPreset.cpp`
- `src/app/SandboxLaunchOptions.h`
- `src/app/SandboxLaunchOptions.cpp`
- `tests/renderer_preset_smoke.cpp`

Updated files:

- `src/app/Application.h`
- `src/app/Application.cpp`
- `apps/sandbox/main.cpp`
- `tests/framework_headers_smoke.cpp`
- `CMakeLists.txt`

## Goals

- Add a renderer-facing scene / quality preset API.
- Resolve presets into `SceneResourceLoadDesc` and `RendererQualityDesc`.
- Keep existing sandbox usage compatible:
  - `ark_sandbox.exe`
  - `ark_sandbox.exe [model]`
  - `ark_sandbox.exe [model] [environment.hdr]`
  - `ark_sandbox.exe --debug-orientation`
- Add lightweight sandbox options:
  - `--preset default`
  - `--preset material-ball`
  - `--preset specular-validation`
  - `--preset debug-orientation`
  - `--quality low`
  - `--quality default`
  - `--quality high`
- Preserve default sandbox behavior and default golden validation output.

## Non-Goals

- No Bloom, auto exposure, ACES, or new post-processing pass.
- No PBR / IBL shader math changes.
- No runtime editor scene switching UI.
- No JSON / YAML preset file system.
- No RenderGraph rewrite.
- No golden baseline update.

## Renderer Preset API

`RendererPreset` is intentionally pure data and does not depend on a Vulkan device.

```cpp
enum class RendererScenePreset {
    Default,
    MaterialBall,
    SpecularValidation,
    DebugOrientation,
};

enum class RendererQualityPreset {
    Low,
    Default,
    High,
};

struct RendererPresetDesc {
    RendererScenePreset scene = RendererScenePreset::Default;
    RendererQualityPreset quality = RendererQualityPreset::Default;
};

struct ResolvedRendererPreset {
    SceneResourceLoadDesc scene;
    RendererQualityDesc quality;
};
```

The first version exposes:

- `resolveRendererPreset()`
- `parseRendererScenePreset()`
- `parseRendererQualityPreset()`

All quality preset output is passed through `sanitizeRendererQualityDesc()`.

## Scene Presets

### Default

Keeps current default sandbox behavior:

- Empty model path.
- `SceneModelFallbackPolicy::DefaultSandboxModel`.
- Empty environment path.
- `SceneEnvironmentFallbackPolicy::DefaultHdrThenProcedural`.
- Scene names remain `DefaultSandboxScene`, `DefaultSandboxModel`, and `DefaultSandboxEnvironment`.

### MaterialBall

Uses:

```text
assets/models/material_ball_validation_fixture.gltf
```

This preset is intended for material / texture / IBL visual inspection.

### SpecularValidation

Uses:

```text
assets/models/specular_ibl_validation_fixture.gltf
```

This preset is intended for roughness / metallic / specular IBL validation.

### DebugOrientation

Keeps the default model fallback and switches the environment fallback to:

```cpp
SceneEnvironmentFallbackPolicy::DebugOrientation
```

## Quality Presets

### Default

Matches Phase 0.51 defaults:

- Environment cube: 512x512.
- Irradiance cube: 32x32.
- Specular cube: 256x256.
- BRDF LUT: 256x256.
- Irradiance sample delta: 0.1.
- Specular prefilter sample count: 128.
- BRDF LUT sample count: 1024.

### Low

For faster smoke runs and weaker machines:

- Environment cube: 256x256.
- Irradiance cube: 16x16.
- Specular cube: 128x128.
- BRDF LUT: 128x128.
- Irradiance sample delta: 0.2.
- Specular prefilter sample count: 64.
- BRDF LUT sample count: 512.

Feature flags stay enabled, so the preset changes quality and cost without changing renderer feature behavior.

### High

For manual high-quality inspection:

- Environment cube: 1024x1024.
- Irradiance cube: 64x64.
- Specular cube: 512x512.
- BRDF LUT: 512x512.
- Irradiance sample delta: 0.05.
- Specular prefilter sample count: 256.
- BRDF LUT sample count: 2048.

High is not the default to avoid changing startup cost and validation cost.

## Sandbox Plumbing

`ApplicationDesc` now has:

```cpp
RendererQualityDesc rendererQuality;
```

`Application::run()` forwards this into:

```cpp
RendererDesc::quality
```

Sandbox command-line parsing now lives in `SandboxLaunchOptions` so the behavior can be tested without launching the app.

Supported forms:

```powershell
ark_sandbox.exe
ark_sandbox.exe assets\models\material_ball_validation_fixture.gltf
ark_sandbox.exe assets\models\material_ball_validation_fixture.gltf assets\HDR\custom.hdr
ark_sandbox.exe --debug-orientation
ark_sandbox.exe --preset material-ball
ark_sandbox.exe --preset=specular-validation
ark_sandbox.exe --quality low
ark_sandbox.exe --quality=high
ark_sandbox.exe --preset material-ball --debug-orientation
```

Override rules:

- Explicit positional model path overrides the preset model path.
- Explicit positional environment path overrides the preset environment path.
- `--debug-orientation` only overrides the environment fallback and does not change the selected model preset.
- Missing `--preset` or `--quality` values fall back conservatively instead of crashing.

## Tests

Added:

```text
ark_renderer_preset_smoke
```

Coverage:

- Default scene preset.
- Material ball scene preset.
- Specular validation scene preset.
- Debug orientation scene preset.
- Low/default/high quality preset values.
- String parse fallback behavior.
- Sandbox CLI preset and quality mapping.
- Explicit path override policy.
- `--debug-orientation` override policy.
- Missing option value fallback behavior.

Updated:

```text
ark_framework_headers_smoke
```

Coverage:

- `RendererPreset.h` public API.
- `SandboxLaunchOptions.h` public API.
- `ApplicationDesc::rendererQuality`.

## Validation

Executed:

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_renderer_preset_smoke ark_framework_headers_smoke ark_frame_validation_smoke ark_sandbox
build\msvc-vcpkg\Debug\ark_renderer_preset_smoke.exe
build\msvc-vcpkg\Debug\ark_framework_headers_smoke.exe
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe
git diff --check
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
ark_sandbox hidden-window smoke
ark_sandbox hidden-window smoke with --preset material-ball
ark_sandbox hidden-window smoke with --preset specular-validation
ark_sandbox hidden-window smoke with --preset debug-orientation
ark_sandbox hidden-window smoke with --quality low
ark_sandbox hidden-window smoke with --quality high
ark_sandbox hidden-window smoke with assets\models\material_ball_validation_fixture.gltf
ark_sandbox hidden-window smoke with --preset material-ball --debug-orientation
```

Results:

```text
targeted build passed
ark_renderer_preset_smoke passed
ark_framework_headers_smoke passed
ark_frame_validation_smoke passed
golden diff for specular_ibl_validation_fixture: meanAbsError=0 maxChannelError=0 mismatchedPixelRatio=0
golden diff for material_ball_validation_fixture: meanAbsError=0 maxChannelError=0 mismatchedPixelRatio=0
git diff --check: only line-ending warnings, no whitespace errors
full build passed
CTest: 25/25 tests passed
all sandbox hidden-window smoke cases passed
```

## Completion Criteria

- Renderer preset API exists and is device-independent.
- Presets resolve to `SceneResourceLoadDesc` and `RendererQualityDesc`.
- Sandbox supports preset and quality options.
- Old sandbox command-line behavior remains compatible.
- Default sandbox and default validation image behavior remain unchanged.
- New smoke coverage protects preset mapping and sandbox override rules.
- Full Debug build and CTest pass.

## Next Directions

Phase 0.52 gives the renderer a stable named-entry-point layer for manual visual checks and future post-processing validation. Good follow-up candidates:

1. Bloom foundation: threshold, downsample, upsample, and composite path.
2. Tone-mapping operator presets: keep current behavior as default, then add ACES-style operator selection.
3. Auto exposure prelude: luminance mip or histogram resource foundation before adaptive exposure.
4. Screenshot preset routing: reuse scene / quality presets in frame validation fixtures.
5. Transparent sorting: back-to-front sorting for the Blend bucket using camera position and bounds.
