# Phase 0.60 KTX/KTX2 Texture Loader / Sponza Material Recovery

## 实施状态

已完成 0.60.0 文档与范围确认 ~ 0.60.6 验证与收尾。

本阶段目标是补上 Sponza `.ktx` 贴图的最小读取路径，让默认 Sponza + DamagedHelmet 组合场景不再只能依赖 texture load failure fallback 观察几何和阴影，而是能看到 Sponza 现有 diffuse/baseColor 纹理。

## 完成内容

- 新增 vcpkg `ktx` 依赖，并通过 CMake `find_package(Ktx CONFIG REQUIRED)` 链接 `KTX::ktx`。
- `tests/dependency_smoke.cpp` 增加 `<ktx.h>` 和 `ktxTexture_CreateFromMemory()` 链接探针。
- `TextureLoader` 新增：
  - `loadImageKtx()`
  - `loadImageAuto()`
  - `TextureLoader::loadKtx()`
  - `TextureLoader::loadAuto()`
- `TextureCache` 改为通过 `loadImageAuto()` 按扩展名分流：
  - `.hdr` -> `loadImageHdrRgba32F()`
  - `.ktx` / `.ktx2` -> `loadImageKtx()`
  - 其他 -> `loadImageRgba8()`
- KTX 第一版读取 base mip，继续沿用现有 `TextureResource` 单 mip upload + GPU mip generation 路径。
- KTX 解码失败、格式不支持或数据越界时保持失败返回，由 `ModelResource` 继续走已有 per-slot fallback。
- Sponza scene smoke 从“fallback 下能加载”升级为“确实创建 1024x1024 KTX-backed texture”。
- README、dependency 文档和 handoff 同步当前 KTX 支持范围。

## 支持范围

当前 KTX 路径优先服务 Sponza 现有资源：

- 支持 KTX1 2D texture。
- 支持非压缩 RGBA8 / SRGB8_ALPHA8。
- 支持非压缩 KTX2 R8G8B8A8 UNORM / SRGB 的基础入口。
- 只读取 base mip，后续 mip 仍由 GPU generation 产生。
- 不使用 libktx 的 Vulkan upload helper，仍通过项目 RHI / `TextureResource` 上传。

明确非目标：

- 不手写完整 KTX/KTX2 parser。
- 不支持 compressed KTX、BasisU、UASTC、ETC1S 或 supercompression 转码。
- 不上传 KTX 原始 mip chain。
- 不恢复 Sponza 资源包中不存在的完整 PBR texture set。
- 不重构 material、sampler、RHI upload 或 RenderGraph。

## 验证

已执行：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug --target ark_dependency_smoke
ctest --test-dir build/msvc-vcpkg -C Debug -R ark_dependency_smoke --output-on-failure
cmake --build --preset msvc-vcpkg-debug --target ark_texture_loader_smoke ark_scene_resource_smoke
ctest --test-dir build/msvc-vcpkg -C Debug -R "ark_(texture_loader|scene_resource)_smoke" --output-on-failure
cmake --build --preset msvc-vcpkg-debug --target ark_model_resource_smoke
ctest --test-dir build/msvc-vcpkg -C Debug -R ark_model_resource_smoke --output-on-failure
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

结果：

```text
dependency smoke passed
texture loader + scene resource targeted CTest passed: 2/2
model resource CTest passed: 1/1
full build passed
full CTest passed: 29/29
```

`ark_texture_loader_smoke` 覆盖：

- 临时 PPM -> RGBA8。
- HDR 输入被 RGBA8 loader 拒绝。
- HDR -> RGBA32F。
- `assets/models/sponza/white.ktx` -> 4x4 RGBA8。
- `assets/models/sponza/10381718147657362067.ktx` -> 1024x1024 RGBA8。
- `loadImageAuto()` 对 KTX 的扩展名分流。

`ark_model_resource_smoke` 覆盖：

- `TextureCache` 通过 `.ktx` path 创建 sRGB `TextureResource`。
- 坏 `.ktx` 文件仍触发 per-slot texture fallback。

`ark_scene_resource_smoke` 覆盖：

- Sponza SceneResource 仍能正常加载。
- Fake RHI 观察到 1024x1024 texture desc，证明 Sponza KTX 纹理进入 renderer resource path。
- Sponza + DamagedHelmet 组合场景仍能正常加载。

## 后续方向

Phase 0.60 之后建议继续推进：

1. Shadow bounds fitting / CSM prelude，让 Sponza 大场景阴影覆盖更自动。
2. 默认组合场景 screenshot baseline，用实际图像回归锁住 Bloom、ToneMapping、Shadow、IBL 和 KTX texture 恢复效果。
3. Renderer public scene/resource API，开始为后续接入引擎做边界整理。
4. KTX2 / BasisU 压缩纹理完善，包括转码策略、原始 mip chain 上传和 GPU format policy。
