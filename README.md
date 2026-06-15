# ARKRenderer

ARKRenderer 是一个逐步搭建的 Vulkan 渲染器实验项目，用于验证图形引擎中的 RHI、资源加载、PBR 材质、IBL、后处理和自动化回归测试等系统。

当前主要能力：

- Vulkan RHI 基础封装，使用 dynamic rendering。
- glTF 模型加载，支持 PBR 材质、纹理、sampler、alpha mode、UV set 和 texture transform。
- sandbox 默认场景加载，支持模型和环境 fallback。
- HDR 环境加载、equirectangular-to-cubemap、skybox、diffuse IBL、specular IBL 和 BRDF LUT。
- HDR scene color、Physically Based Bloom、tone mapping、frame validation 和 golden image smoke tests。
- sandbox 轨道相机、scene preset 和 quality preset。

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

常用 sandbox 参数：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset material-ball
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset specular-validation
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset debug-orientation
build\msvc-vcpkg\Debug\ark_sandbox.exe --quality low
build\msvc-vcpkg\Debug\ark_sandbox.exe --tone-mapping aces
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset material-ball --bloom
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation --bloom
build\msvc-vcpkg\Debug\ark_sandbox.exe --preset bloom-validation --bloom --tone-mapping aces
build\msvc-vcpkg\Debug\ark_sandbox.exe --bloom --bloom-intensity 0.08
build\msvc-vcpkg\Debug\ark_sandbox.exe assets\models\material_ball_validation_fixture.gltf
```

## 文档

- 阶段文档：`docs/phase/`
- 当前交接文档：`docs/codex_handoff.md`
- 依赖说明：`docs/dependency.md`
