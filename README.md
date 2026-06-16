# ARKRenderer

ARKRenderer 是一个逐步搭建的 Vulkan 渲染器实验项目，用于验证 RHI、glTF 资源加载、PBR 材质、IBL、阴影、后处理和自动化回归测试。

## 当前能力

- Vulkan RHI 基础封装，使用 dynamic rendering。
- glTF 模型加载，支持 PBR 材质、纹理、sampler、alpha mode、UV set 和 texture transform。
- HDR 环境、equirectangular-to-cubemap、Skybox、Diffuse IBL、Specular IBL 和 BRDF LUT。
- Directional shadow map 基础路径，sandbox 可通过 preset/参数开启。
- HDR scene color、Physically Based Bloom、ToneMapping 和 frame validation smoke tests。
- sandbox 轨道相机、scene preset、quality preset、默认 Sponza + DamagedHelmet 组合场景和资源 fallback。

## 构建

环境要求：

- Windows + Visual Studio 2022。
- CMake 3.25 或更新版本。
- vcpkg manifest mode。
- Vulkan SDK 或兼容 Vulkan 的显卡驱动。

常用 Debug 构建命令：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

## 运行

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

默认启动会加载 Sponza 大场景，并把 DamagedHelmet 作为第二个模型放在中庭附近，用于观察阴影、Bloom、ToneMapping、IBL 和复杂场景组合。

常用 sandbox 参数：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset material-ball
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset specular-validation
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation --bloom --tone-mapping aces
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset sponza
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset shadow-validation
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset sponza --shadows --shadow-bounds 20
build\msvc-vcpkg\Debug\ark_sandbox.exe --quality low
build\msvc-vcpkg\Debug\ark_sandbox.exe assets\models\material_ball_validation_fixture.gltf
```

说明：当前 Sponza 的贴图是 `.ktx`，项目还没有 KTX/KTX2 解码器，因此会走 texture load failure fallback。它适合先验证几何、场景规模、阴影和相机路径，不代表最终材质质量。

`--preset sponza` 会显示纯 Sponza；显式传入模型路径时不会自动追加默认 DamagedHelmet。

## 文档

- 阶段文档：`docs/phase/`
- 当前交接文档：`docs/codex_handoff.md`
- 依赖说明：`docs/dependency.md`
