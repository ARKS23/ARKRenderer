# ARKRenderer

ARKRenderer 是一个逐步搭建的 Vulkan 渲染器实验项目，用于验证 RHI、glTF 资源加载、PBR 材质、IBL、阴影、后处理和自动化回归测试。

## 当前能力

- Vulkan RHI 基础封装，使用 dynamic rendering。
- glTF 模型加载，支持 PBR 材质、PNG/JPG/KTX 纹理、sampler、alpha mode、UV set 和 texture transform。
- HDR 环境、equirectangular-to-cubemap、Skybox、Diffuse IBL、Specular IBL 和 BRDF LUT。
- Directional shadow map 基础路径，默认按 scene bounds 自动拟合；显式 `--shadow-bounds` 会切换为手动 bounds。
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

默认启动会加载放大的 Sponza 大场景，并把 DamagedHelmet 作为第二个模型放在中庭附近，同时默认开启更明显的斜向 Shadow、Bloom 和 ACES ToneMapping，用于观察 IBL、后处理和复杂场景组合。

常用 sandbox 参数：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset material-ball
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset specular-validation
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset sponza
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset shadow-validation
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset sponza --shadow-bounds 64 --shadow-strength=1.0
build\msvc-vcpkg\Debug\ark_sandbox.exe --quality low
build\msvc-vcpkg\Debug\ark_sandbox.exe assets\models\material_ball_validation_fixture.gltf
```

说明：默认 shadow 会根据当前 scene bounds 自动拟合单张 directional shadow map。需要手动调试固定 shadow box 时，使用 `--shadow-bounds`；该参数会关闭自动 fitting。

说明：当前已接入 KTX-Software / libktx 的最小路径，Sponza 现有 KTX1 RGBA8 diffuse/baseColor 贴图可以进入 `TextureCache` / `TextureResource`。压缩 KTX、BasisU、完整 KTX2 转码和 Sponza 缺失的完整 PBR 贴图集仍是后续工作；不支持的纹理仍会走 texture load failure fallback。

`--preset sponza` 会显示纯 Sponza；显式传入模型路径时不会自动追加默认 DamagedHelmet。

## 文档

- 阶段文档：`docs/phase/`
- 当前交接文档：`docs/codex_handoff.md`
- 依赖说明：`docs/dependency.md`
