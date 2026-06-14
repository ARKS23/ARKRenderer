# Phase 0.37 Cubemap Face Orientation Debug Foundation

## 阶段判断

Phase 0.36 已经补齐 sandbox orbit camera controller。现在默认 sandbox 可以加载模型、HDR environment、转换 cubemap、绘制 skybox、生成 diffuse irradiance cubemap，并通过 `ForwardPass` 使用 irradiance cubemap 做 diffuse ambient IBL：

```text
EnvironmentResource
    -> EnvironmentCubeConverter
        -> EnvironmentCubeResource
            -> SkyboxPass
    -> EnvironmentIrradianceGenerator
        -> DefaultSandboxIrradianceCube
    -> FrameContext::irradianceCube
    -> ForwardPass diffuse ambient IBL
Application
    -> SandboxCameraController
        -> RenderView
```

下一步不要急着进入 prefiltered specular、BRDF LUT 或完整 specular IBL。当前更关键的基础风险是 cubemap face order、face UV、world direction 和 skybox sampling 是否完全一致。如果这个约定不稳，后续 specular prefilter、roughness mip sampling 和 BRDF LUT 即使代码正确，也可能在视觉上表现为方向错、反射错或环境光不可信。

Phase 0.37 先做 cubemap face orientation 的 debug foundation：把 face order / direction mapping 固化为文档和可测试的代码约束，准备一个小型 debug environment 路径，并让 sandbox 可以更明确地进入 orientation debug 模式。真正的 GPU texture readback / automated pixel validation 放到后续独立阶段。

## 目标

Phase 0.37 目标：

- 明确 ARKRenderer 的 cubemap face order 和 face direction 约定。
- 将 equirectangular -> cubemap shader、skybox shader、irradiance shader 的方向约定收口到同一套规则。
- 新增小型 debug orientation environment 输入，避免依赖大型真实 HDRI 判断方向。
- 给 sandbox 提供明确的 orientation debug environment 路径。
- 扩展 smoke tests，验证 face index、face view layer、shader token、debug fixture 和文档约定。
- 让下一阶段可以在此基础上安全进入 readback pixel validation 或 specular IBL 前置资源。

## 非目标

Phase 0.37 暂不做：

- 不做 GPU texture readback。
- 不做 `copyTextureToBuffer()`。
- 不新增 `MemoryUsage::GpuToCpu` 或 buffer map/read API。
- 不做真正 automated cubemap pixel validation。
- 不做 prefiltered specular environment map。
- 不做 BRDF LUT。
- 不做 `ForwardPass` specular IBL。
- 不做 roughness mip sampling。
- 不改 `ForwardPass` descriptor layout。
- 不改 `FrameRenderer` pass graph。
- 不改 diffuse irradiance 积分算法。
- 不引入 RenderGraph、bindless 或 compute pipeline。
- 不提交大型 HDRI。真实 HDR 仍用于本地人工验证。

## 当前基线

### Cubemap Resource

`EnvironmentCubeResource` 当前已经支持：

```text
TextureType::Cube
TextureViewType::Cube
arrayLayers = 6
mipLevels >= 1
cube texture view
6 个 Texture2D face render target view
```

当前 face view 创建顺序是：

```text
faceIndex 0 -> baseArrayLayer 0
faceIndex 1 -> baseArrayLayer 1
faceIndex 2 -> baseArrayLayer 2
faceIndex 3 -> baseArrayLayer 3
faceIndex 4 -> baseArrayLayer 4
faceIndex 5 -> baseArrayLayer 5
```

这一顺序需要在 Phase 0.37 明确命名为：

```text
0: +X
1: -X
2: +Y
3: -Y
4: +Z
5: -Z
```

### Equirectangular To Cube Shader

当前 `shaders/equirect_to_cube.frag.hlsl` 已有 face order 注释：

```hlsl
// Face order: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z.
```

并使用 `faceUvToDirection()` 将 face index 和 UV 转为 world direction，再用 `directionToEquirectUv()` 采样 2D HDR environment。

Phase 0.37 要把这段约定变成测试明确依赖的 contract，而不是只留在注释里。

### Skybox Shader

`SkyboxPass` 当前通过 inverse projection 和 inverse view rotation 重建 world direction，然后直接采样 `TextureCube`：

```hlsl
const float3 worldDirection = reconstructWorldDirection(input.uv);
const float3 color = g_SkyboxCube.Sample(g_SkyboxSampler, worldDirection).rgb;
```

这意味着 skybox 视觉正确性直接依赖：

- conversion shader 写入 cubemap face 的方向正确。
- Vulkan cube view 的 layer order 与 shader face order 一致。
- skybox 重建的 world direction 与 `ForwardPass` / irradiance sampling 使用同一世界坐标系。

### Diffuse IBL Shader

`mesh.frag.hlsl` 当前使用 world normal 采样 irradiance cubemap：

```hlsl
float3 sampleIrradiance(float3 normal) {
    return g_IrradianceCube.Sample(g_IrradianceSampler, normalize(normal)).rgb;
}
```

如果 cubemap face orientation 错误，diffuse ambient IBL 会立刻继承错误方向。Phase 0.37 不改 lighting，只确认 debug foundation。

## 建议设计

### Face Orientation Contract

新增一个文档化 contract，建议写入 `docs/phase/phase37.md`，并同步补充到 shader source tests：

```text
ARK cubemap face order:
0 = +X
1 = -X
2 = +Y
3 = -Y
4 = +Z
5 = -Z

World axes:
+X = right
+Y = up
+Z = forward from default camera target toward camera opposite direction basis currently used by cubemap conversion
```

注意：这里不需要把项目改成某个外部引擎的约定；关键是 ARK 内部 conversion、skybox、irradiance 和 ForwardPass 采样必须自洽。

### Debug Orientation Environment

推荐新增一个程序化 debug environment helper，而不是提交大型 HDR：

```text
src/asset 或 src/renderer 中的小型 helper
```

可选命名：

```cpp
asset::ImageData makeDebugOrientationEnvironmentImage();
```

建议输出 `Rgba32Float` equirectangular image，例如 `64x32` 或 `128x64`。每个主方向使用明显颜色：

```text
+X: red
-X: cyan
+Y: white
-Y: black / dark gray
+Z: blue
-Z: yellow
```

debug image 不追求真实 HDR 光照，只用于 orientation 判断。可以保留适度亮度，例如 1.0 ~ 4.0，避免 tone mapping 后过曝成白片。

实现方式有两种：

1. CPU 程序化生成 equirectangular image。
2. 提交小型 `assets/HDR/debug_orientation.hdr` fixture。

第一版更推荐程序化生成，原因：

- 不增加二进制资源管理负担。
- 不需要新外部工具生成 HDR。
- smoke test 可以直接验证像素生成规则。
- 未来仍可补一个人工可读的 `.hdr` fixture。

### Sandbox Debug Path

建议给 sandbox 一个明确入口，让用户可以启动 orientation debug environment：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
```

或者更保守地先利用现有第二参数路径机制，允许默认 renderer 在找不到真实 HDR 时回退到程序化 debug environment。考虑当前 `ApplicationDesc` 已经有 `defaultEnvironmentPath`，如果不想扩 CLI，可以先做：

```text
assets/HDR/debug_orientation.hdr
```

但如果选择程序化 helper，最好新增一个小的 app/sandbox flag，语义更清楚。

Phase 0.37 推荐优先级：

1. 程序化 debug orientation image helper。
2. sandbox flag 或 renderer desc flag。
3. 文档记录如何人工启动验证。

### Shader Source Tests

扩展 `tests/shader_assets_smoke.cpp`：

- `equirect_to_cube.frag.hlsl` 必须包含 face order 注释或 face mapping token。
- 必须包含 6 个 face case。
- 必须包含 `directionToEquirectUv()`。
- `skybox.frag.hlsl` 必须包含 `TextureCube` 和 `reconstructWorldDirection()`。
- `irradiance_convolve.frag.hlsl` 必须包含同一 face order / face direction helper 或等价 token。
- `mesh.frag.hlsl` 必须继续使用 `TextureCube` irradiance 和 normalized world normal。

这些不是 GPU 像素验证，但能避免后续重构时把方向 contract 无声改坏。

### Resource / Converter Smoke Tests

扩展或新增 smoke test：

```text
tests/cubemap_orientation_contract_smoke.cpp
```

覆盖：

- `EnvironmentCubeResource` 6 个 face views 的 `baseArrayLayer` 与 face index 一致。
- cube view 覆盖 6 layers。
- debug orientation image 不为空，格式为 `Rgba32Float`。
- debug orientation image 的若干方向采样或 CPU helper 结果符合预期颜色。
- `EnvironmentCubeConverter` 仍按 face index 0~5 更新 uniform 并 draw 6 次。

如果暂时不新增 helper，可以把这些检查合并到：

```text
tests/environment_cube_resource_smoke.cpp
tests/equirectangular_to_cube_smoke.cpp
tests/shader_assets_smoke.cpp
```

但 Phase 0.37 推荐新增独立 smoke，名字更清楚。

### 为什么不在本阶段做 GPU Readback

当前 RHI 已有：

```text
TextureUsage::TransferSrc / TransferDst
BufferUsage::TransferSrc / TransferDst
DeviceContext::uploadTextureData()
DeviceContext::uploadBufferData()
DeviceContext::generateTextureMips()
```

但还没有：

```text
MemoryUsage::GpuToCpu
Buffer::read / map read API
DeviceContext::copyTextureToBuffer()
Vulkan vkCmdCopyImageToBuffer wrapper
row pitch / texel block layout policy
GPU completion + CPU invalidate path
```

把这些和 orientation contract 放在同一阶段，会让 Phase 0.37 从“验证基础”膨胀成 RHI readback 子系统。建议后续单独做 Phase 0.38。

## 实施顺序

### 0.37.0 文档与范围确认

目标：

- 新增 `docs/phase/phase37.md`。
- 明确本阶段只做 cubemap face orientation debug foundation。
- 明确不做 GPU readback、pixel validation、specular IBL 或 BRDF LUT。
- 明确 face order contract。

审核点：

- 不改 pass graph。
- 不改 ForwardPass descriptor layout。
- 不引入 readback API。

### 0.37.1 Orientation Contract Tests

目标：

- 扩展 `shader_assets_smoke` 或新增 `cubemap_orientation_contract_smoke`。
- 检查 face order / face direction token。
- 检查 skybox / irradiance / mesh shader 的 TextureCube sampling contract。

审核点：

- 测试不依赖真实 GPU readback。
- 测试能在 fake/source 层防止 shader contract 被无意改掉。

### 0.37.2 Debug Environment Helper

目标：

- 新增程序化 debug orientation environment helper。
- 输出小型 `Rgba32Float` equirectangular image。
- 每个主方向有明显颜色。
- 提供 CPU 层 smoke test 验证尺寸、格式、颜色方向。

审核点：

- 不提交大型 HDR。
- 不影响默认真实 HDR environment path。
- helper 不依赖 RHI。

### 0.37.3 Sandbox Debug Path

目标：

- 给 sandbox 提供明确 orientation debug environment 入口。
- 记录启动方式。
- 默认 sandbox 行为不被破坏，仍优先使用 `assets/HDR/2k.hdr` 或程序化普通 environment。

审核点：

- 不把 debug mode 写成默认产品路径。
- 不要求用户每次启动都指定大型 HDR。

### 0.37.4 Tests

目标：

- 新增/扩展 smoke tests。
- targeted build。
- 新 smoke 通过。
- `shader_assets_smoke` 通过。
- `equirectangular_to_cube_smoke` 通过。
- `skybox_pass_smoke` 通过。

审核点：

- 测试覆盖 contract，而不是只检查文件存在。
- 不让 runtime smoke 替代 contract tests。

### 0.37.5 验证与收尾

目标：

- full build。
- CTest 全量通过。
- default sandbox runtime smoke。
- debug orientation sandbox runtime smoke。
- DamagedHelmet + 本地 HDR runtime smoke。
- 更新 `docs/codex_handoff.md`。

审核点：

- 文档明确 Phase 0.37 仍不是 automated pixel validation。
- 下一阶段建议转向 RHI readback + pixel validation，或 cubemap mip/prefilter foundation。

## 测试策略

建议至少覆盖：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_cubemap_orientation_contract_smoke ark_shader_assets_smoke ark_equirectangular_to_cube_smoke ark_skybox_pass_smoke
build/msvc-vcpkg/Debug/ark_cubemap_orientation_contract_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_equirectangular_to_cube_smoke.exe
build/msvc-vcpkg/Debug/ark_skybox_pass_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

如果本阶段最终没有新增 `--debug-orientation` CLI，则把对应 runtime smoke 改成实际采用的 debug environment 启动方式。

## 实施结果

Phase 0.37 已完成 `0.37.0` ~ `0.37.5`：

- 新增 `src/renderer/CubemapOrientation.h`，把 face order 固化为 `0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z`，并提供 debug color contract。
- 新增 `src/renderer/SandboxEnvironment.h/.cpp`，把原默认程序化 HDR environment 移出 `Renderer.cpp`，并新增 64x32 `Rgba32Float` debug orientation environment。
- `ApplicationDesc` / `RendererDesc` 新增 `useDebugOrientationEnvironment`，`apps/sandbox/main.cpp` 新增 `--debug-orientation`。
- `Renderer` 在 debug flag 开启时优先使用程序化 orientation environment；默认真实 HDR path 和普通程序化 fallback 保持不变。
- 新增 `ark_cubemap_orientation_contract_smoke`，覆盖 face order、`EnvironmentCubeResource` cube/face view layer、debug environment 像素方向和普通 procedural environment 可用性。
- 扩展 `shader_assets_smoke`，约束 equirectangular conversion、irradiance convolution 和 mesh diffuse IBL 的关键方向 token。
- 扩展 `framework_headers_smoke`，覆盖新增 public contract/helper 和 desc flag。

本阶段仍然没有引入 GPU readback、texture-to-buffer copy、CPU-visible readback buffer 或 automated pixel validation。当前验证属于 CPU contract/source smoke + runtime smoke，严格像素级方向验证仍留给后续阶段。

## 验证记录

Phase 0.37 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_cubemap_orientation_contract_smoke ark_shader_assets_smoke ark_equirectangular_to_cube_smoke ark_skybox_pass_smoke ark_framework_headers_smoke ark_sandbox
build/msvc-vcpkg/Debug/ark_cubemap_orientation_contract_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_equirectangular_to_cube_smoke.exe
build/msvc-vcpkg/Debug/ark_skybox_pass_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
targeted smoke build passed
ark_cubemap_orientation_contract_smoke passed
ark_shader_assets_smoke passed
ark_equirectangular_to_cube_smoke passed
ark_skybox_pass_smoke passed
ark_framework_headers_smoke passed
full build passed
CTest: 17/17 passed
default sandbox smoke passed
debug orientation sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

## 完成标准

Phase 0.37 完成时应满足：

- face order contract 被文档和 tests 明确约束。
- equirectangular conversion、skybox、irradiance 和 mesh diffuse IBL 对 cubemap direction 的使用保持自洽。
- 存在小型 debug orientation environment 路径。
- sandbox 能进入 debug orientation environment。
- 不需要大型 HDRI 即可做方向人工验证。
- full build / CTest / runtime smoke 通过。
- handoff 明确下一步仍需 GPU readback / automated pixel validation 才能做到严格像素级验证。

## 后续 Phase 建议

Phase 0.37 后建议：

1. **RHI Texture Readback + Automated Pixel Validation**
   - 新增 `GpuToCpu` readback buffer、texture to buffer copy 和 cubemap face pixel validation。
2. **Cubemap Mip / Face-Mip View Foundation**
   - 为 specular prefilter 准备多 mip face render target view。
3. **Prefiltered Specular Environment**
   - 从 environment cubemap 生成 roughness mip chain。
4. **BRDF LUT**
   - 新增 split-sum BRDF integration LUT resource。
5. **ForwardPass Specular IBL**
   - 接入 prefiltered environment、BRDF LUT 和 roughness mip sampling。
