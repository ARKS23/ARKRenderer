# ARKRenderer

ARKRenderer is a small Vulkan renderer playground focused on building graphics-engine systems step by step.

Current renderer features include:

- Vulkan RHI foundation with dynamic rendering.
- glTF model loading with PBR material data, textures, samplers, alpha modes, UV sets, and texture transforms.
- Default sandbox scene loading with model/environment fallback.
- HDR environment loading, equirectangular-to-cubemap conversion, skybox, diffuse IBL, specular IBL, and BRDF LUT.
- HDR scene color, tone mapping, frame validation, and golden image smoke tests.
- Sandbox orbit camera and renderer scene / quality presets.

## Build

Requirements:

- Windows + Visual Studio 2022.
- CMake 3.25 or newer.
- vcpkg manifest mode.
- Vulkan SDK / compatible Vulkan driver.

Typical Debug build:

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

## Run

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

Useful sandbox options:

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset material-ball
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset specular-validation
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset debug-orientation
build\msvc-vcpkg\Debug\ark_sandbox.exe --quality low
build\msvc-vcpkg\Debug\ark_sandbox.exe assets\models\material_ball_validation_fixture.gltf
```

## Docs

- Phase notes: `docs/phase/`
- Current handoff summary: `docs/codex_handoff.md`
- Dependency notes: `docs/dependency.md`
