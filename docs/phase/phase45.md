# Phase 0.45 Frame Color Readback and Minimal Visual Validation Foundation

## 阶段判断

Phase 0.44 已完成 specular IBL validation / quality pass：

```text
assets/models/specular_ibl_validation_fixture.gltf
    -> GltfLoader material factors
    -> ModelResource material resources
    -> RenderQueue draw items
    -> ForwardPass material uniforms
    -> mesh.frag.hlsl Cook-Torrance + diffuse/specular IBL
```

这意味着当前 renderer 的主要问题已经从“功能是否接上”转向“画面是否可验证”。现在默认 sandbox 可以显示模型、skybox、HDR environment、diffuse irradiance IBL、prefiltered specular IBL 和 tone mapping，但自动化验证仍主要停留在 fake RHI、resource smoke 和启动不崩溃 smoke。它们能证明数据链路和资源绑定没有明显断裂，却不能证明最终 frame color 中真的出现了合理的天空、模型、材质差异和 tone-mapped 输出。

Phase 0.45 建议优先做 **frame-level validation foundation**，不要急着继续扩展 bloom、shadow、多光源或更多 glTF material extensions。视觉回归保护一旦建立，后续每个效果阶段都会更稳。

当前关键缺口：

1. **没有 final frame / screenshot 级验证**
   - Phase 0.38 已有最小 texture readback 和 cubemap orientation pixel validation。
   - 但它只覆盖 cubemap resource，不覆盖 `FrameRenderer` 的最终场景输出。
   - runtime smoke 只能证明 sandbox 不会启动即退出，不能证明画面不是黑屏、模型不是缺失、tone mapping 没有崩坏。

2. **scene color 格式与现有 readback 能力不完全匹配**
   - 当前 `FrameRenderer` scene color 是 `RGBA16Float`。
   - 现有 readback helper 已覆盖 `RGBA8Unorm`、`RGBA8Srgb`、`RGBA32Float` 等路径。
   - 如果直接读取 HDR scene color，需要补齐 `RGBA16Float` half-float readback 与统计逻辑；如果读取 tone-mapped target，需要明确可读目标和 pass 边界。

3. **缺少稳定的验证相机与场景入口**
   - sandbox 已有 orbit camera controller，但交互相机不是自动验证的稳定输入。
   - `specular_ibl_validation_fixture.gltf` 是很好的固定材质网格，但还需要固定 view/projection、固定 environment 和固定输出尺寸。
   - 后续 glTF camera / scene camera selection 也应建立在这个验证路径之上。

4. **没有 golden image / 统计验证策略**
   - 直接做完整 golden image system 会牵涉 PNG writer、baseline asset 管理、平台差异、CI artifact 和 image diff 策略。
   - Phase 0.45 更适合先做统计型 pixel validation：非黑像素比例、亮度范围、颜色均值、天空/模型贡献等。

## 目标

Phase 0.45 的目标是建立最小 frame-level visual validation foundation：

- 明确 frame color readback 的技术路线。
- 为 validation 场景提供固定尺寸、固定相机、固定 environment 和固定模型入口。
- 增加一个最小 frame validation smoke，能读取渲染后的颜色数据。
- 先采用统计型验证，不要求完整 screenshot/golden image。
- 覆盖至少一个可提交 fixture：
  - `assets/models/specular_ibl_validation_fixture.gltf`
- 尽量覆盖默认 renderer 真实路径：
  - environment loading / procedural fallback
  - equirectangular -> cubemap
  - irradiance generation
  - specular prefilter
  - BRDF LUT
  - SkyboxPass
  - ForwardPass
  - ToneMappingPass
- 文档化当前不是完整 screenshot system 的边界。
- 保持 full build、CTest 和 sandbox smoke 通过。

## 非目标

Phase 0.45 暂不做：

- 不做完整截图系统。
- 不引入 PNG/JPEG writer。
- 不提交 golden baseline 图片。
- 不做跨 GPU / 跨驱动的 perceptual image diff。
- 不做 CI artifact 上传或截图报告。
- 不做 runtime async capture API。
- 不做 RenderDoc 集成。
- 不扩展 bloom、auto exposure、ACES、color grading。
- 不扩展 shadow、多光源或 `KHR_lights_punctual`。
- 不做 glTF camera 完整接入；如果需要固定验证视角，可以先使用测试内硬编码 `RenderView`。
- 不重写 `FrameRenderer` 为完整 RenderGraph。
- 不把 sandbox orbit camera 做成 editor camera。

## 当前基线

### Readback 能力

Phase 0.38 已提供：

```cpp
struct TextureReadbackDesc {
    Texture* texture = nullptr;
    Buffer* destinationBuffer = nullptr;
    Extent2D extent{};
    u32 bytesPerPixel = 0;
    u32 mipLevel = 0;
    u32 arrayLayer = 0;
    u64 destinationOffset = 0;
    u64 rowPitch = 0;
};

bool DeviceContext::copyTextureToBuffer(const TextureReadbackDesc&);
bool Buffer::readData(void* destination, u64 size, u64 offset = 0) const;
```

Vulkan readback 目前已经覆盖 image-to-buffer copy、layout transition、readback buffer 和 CPU mapping。已有 `ark_readback_api_smoke` 与 `ark_cubemap_orientation_pixel_smoke`，证明 resource-level pixel validation 是可行的。

Phase 0.45 不应重新设计这套 RHI API，而应在它上面补最小 frame validation 用例。

### FrameRenderer 输出路径

当前 frame path：

```text
RenderScene + RenderView
    -> FrameRenderer
        -> RGBA16Float scene color
        -> ClearPass
        -> SkyboxPass
        -> ForwardPass
        -> ToneMappingPass
            -> swapchain backbuffer
```

其中：

- scene color 是 HDR `RGBA16Float`，作为 render target 和 shader resource。
- tone mapping pass 输出到 swapchain backbuffer。
- swapchain image 通常不适合作为稳定测试 readback target。
- 因此 Phase 0.45 需要在“读 HDR scene color”与“读测试用 LDR/tone-mapped target”之间做取舍。

### 当前格式限制

现有 readback helper 的 bytes-per-pixel 格式覆盖应以当前代码为准。Phase 0.44 文档记录过当前重点限制：

```text
RGBA8Unorm  -> 4 bytes
RGBA8Srgb   -> 4 bytes
RGBA32Float -> 16 bytes
```

而 frame scene color 是：

```text
RGBA16Float -> 8 bytes
```

如果 Phase 0.45 选择读取 HDR scene color，则需要新增：

- `RGBA16Float` readback bytes-per-pixel 支持。
- half-float 到 float 的 CPU decode helper。
- 统计逻辑避免 NaN/Inf 干扰。
- 对 HDR value range 的合理阈值。

如果 Phase 0.45 选择读取 LDR validation target，则需要新增：

- 可被 readback 的 validation color target。
- 或让 `ToneMappingPass` 在测试中输出到 readback-friendly texture。
- 明确这条路径不等同于读取真实 swapchain。

## 推荐路线

Phase 0.45 建议采用两步保守路线：

1. **先补 `RGBA16Float` readback 支持**
   - 当前 renderer 主 scene color 就是 `RGBA16Float`。
   - 支持这个格式后，后续 HDR scene validation、bloom、auto exposure 都能复用。
   - `RGBA16Float` 每像素 8 bytes，Vulkan copy 本身不复杂，主要新增 CPU half decode 和统计逻辑。

2. **先做统计型 frame validation，不做 golden**
   - 统计型验证对 GPU、driver 和 floating point 差异更宽容。
   - 能优先捕获黑屏、pass 没跑、资源没绑定、tone/lighting 完全异常等大问题。
   - 后续如果要做 golden image，可以在这套 readback/harness 上继续扩展。

如果实现中发现 `FrameRenderer` 当前无法直接暴露 scene color 或无法在测试中稳定创建 offscreen frame，可以降级为：

- 新增一个专用 validation render path，只渲染到 readback-friendly offscreen texture。
- 或先只做 `ToneMappingPass` output texture readback smoke。

降级方案需要在 Phase 0.45 收尾文档里明确，不要误称已经有完整 frame screenshot system。

## 设计方案

### 0.45.1 RGBA16Float Readback

建议改动点：

```text
src/rhi/RHICommon.h
src/rhi/vulkan/VulkanCommandContext.cpp
tests/readback_api_smoke.cpp
```

需要确认或新增：

- `rhi::Format::RGBA16Float` readback bytes-per-pixel = 8。
- `copyTextureToBuffer()` 对 `RGBA16Float` 不再拒绝。
- `ark_readback_api_smoke` 增加格式覆盖。
- 如果已有 texture upload / clear / render target path 能产生可控数据，优先测试真实 copy。
- 如果不方便直接写入 half-float texture，可先验证 format acceptance 和 row pitch，再由 frame validation 覆盖真实数据读取。

CPU 统计 helper 可以使用 `glm::unpackHalf1x16` 或本地 half decode helper，按 repo 依赖和已有 include 风格决定。不要引入新第三方库。

### 0.45.2 Frame Validation Harness

建议新增一个测试：

```text
tests/frame_validation_smoke.cpp
```

建议 CMake target：

```text
ark_frame_validation_smoke
```

测试职责：

- 创建真实 Vulkan backend / device / context。
- 创建固定尺寸 offscreen target，例如：

```text
width  = 256
height = 144
format = RGBA16Float
```

- 构建 `RenderScene`：
  - 加载 `assets/models/specular_ibl_validation_fixture.gltf`。
  - 使用默认或程序化 environment。
  - 使用固定 directional light。
- 构建 `RenderView`：
  - 固定 camera position。
  - 固定 look-at target。
  - 固定 projection。
- 运行最小 frame render path。
- 将 frame color copy 到 readback buffer。
- CPU 读取像素并计算统计信息。

如果当前 `FrameRenderer` 仍强绑定 swapchain/backbuffer，Phase 0.45 可以先抽出一个最小 offscreen validation entry：

```text
FrameRenderer::renderToTexture(...)
```

或新增测试内部 helper，但要避免把测试需求泄露成不成熟 public API。优先保持局部和可回退。

### 0.45.3 Pixel Statistics

建议新增轻量统计结构：

```cpp
struct FrameColorStats {
    u32 width = 0;
    u32 height = 0;
    u64 pixelCount = 0;
    u64 finitePixelCount = 0;
    u64 nonBlackPixelCount = 0;
    glm::vec3 minRgb{0.0f};
    glm::vec3 maxRgb{0.0f};
    glm::vec3 meanRgb{0.0f};
    float meanLuminance = 0.0f;
    float maxLuminance = 0.0f;
};
```

最小验证建议：

- 所有读取值必须 finite。
- `nonBlackPixelCount / pixelCount` 大于一个保守阈值。
- `maxLuminance` 大于一个保守阈值，避免全黑。
- `meanLuminance` 在合理范围内，避免全白/爆亮。
- RGB 三通道至少有一定变化，避免单色 clear 误判为成功。

建议第一版阈值保守，不要追求图像级精确：

```text
finite ratio       >= 1.0
non-black ratio    >= 0.05
max luminance      >= 0.01
mean luminance     >  0.001
mean luminance     <  20.0
```

这些阈值需要以真实测试输出为准，最终实现时应根据 fixture、camera、environment 调整。

### 0.45.4 Validation Scene 固定化

建议使用 `specular_ibl_validation_fixture.gltf` 作为第一版可提交验证场景。

固定相机建议：

```text
position = (0.0, 1.2, -7.0)
target   = (0.0, 0.0,  0.0)
fov      = 45 or 60 degrees
near/far = 0.1 / 100.0
```

固定 environment 建议优先级：

1. 程序化 environment：
   - 可提交。
   - 不依赖本地 HDR。
   - 更适合 CI 和 smoke。

2. `assets/HDR/2k.hdr`：
   - 如果 repo 中有小型默认 HDR，可作为第二选择。
   - 需要确认它是提交资产，而不是本地 ignored 资源。

3. `assets/HDR/warm_restaurant_8k.hdr`：
   - 仅用于人工验证。
   - 当前体积约 98MB，受 `.gitignore` 保护，不应作为测试依赖。

### 0.45.5 Tests

建议测试组合：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_readback_api_smoke ark_frame_validation_smoke ark_sandbox
build\msvc-vcpkg\Debug\ark_readback_api_smoke.exe
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

如果新增 offscreen frame renderer target，也建议保留：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
build\msvc-vcpkg\Debug\ark_sandbox.exe assets\models\specular_ibl_validation_fixture.gltf
```

runtime smoke 仍只证明启动路径，不替代 frame validation。

## 实施拆分

### 0.45.0 文档与范围确认

- 阅读 `docs/codex_handoff.md`、`docs/phase/phase44.md` 和本文件。
- 明确 Phase 0.45 只做最小 frame validation foundation。
- 明确不是完整 screenshot/golden image system。
- 先提交或记录当前 sandbox camera horizontal orbit 反转小改动，避免与 Phase 0.45 混淆。

### 0.45.1 Readback Format 补齐

- 确认 `RGBA16Float` 在 RHI/Vulkan format mapping 中已有。
- 为 readback bytes-per-pixel helper 增加 `RGBA16Float`。
- 增加或扩展 readback smoke 覆盖。
- 增加 CPU half-float decode helper，优先放在测试局部或 renderer validation helper 中。

### 0.45.2 Offscreen Frame Validation 入口

- 调研 `FrameRenderer` 是否能在测试中对 offscreen texture 渲染。
- 如果可以，新增最小测试 harness。
- 如果不可以，新增局部最小 API 或测试 helper，不扩大为完整 public capture API。
- 确保 validation path 能固定 extent、view、scene 和 environment。

### 0.45.3 Pixel Statistics Validation

- 新增 `FrameColorStats` 或测试局部等价结构。
- 从 readback buffer 解码 `RGBA16Float`。
- 计算 finite、non-black、min/max/mean/luminance。
- 使用保守阈值验证画面不是黑屏、不是全 clear、不是 NaN/Inf。

### 0.45.4 Scene Fixture Coverage

- 使用 `specular_ibl_validation_fixture.gltf` 跑 frame validation。
- 固定 camera 和 lighting。
- 优先使用程序化 environment，避免依赖 ignored HDR。
- 如果时间允许，增加默认 sandbox model 的 frame validation path。

### 0.45.5 Tests

- 运行新增 target build。
- 运行新增 smoke。
- 运行相关 readback smoke。
- 运行完整 CTest。
- 运行 sandbox runtime smoke。

### 0.45.6 验证与收尾

- 更新 `docs/codex_handoff.md`。
- 在本文件追加实施结果。
- 记录 readback 格式、validation scene、统计阈值和测试命令。
- 明确仍没有完整 screenshot/golden image system。
- 提交并推送。

## 实施结果

Phase 0.45 已按最小 frame-level validation foundation 落地：

- `VulkanCommandContext` 的 texture copy / upload bytes-per-pixel helper 已补齐 `RGBA16Float = 8 bytes`，HDR scene color 可以进入现有 `TextureReadbackDesc` 路径。
- `ark_readback_api_smoke` 已把 fake RHI contract 覆盖切到 `RGBA16Float`，约束 readback descriptor 的 `bytesPerPixel = 8`。
- 新增 `ark_frame_validation_smoke`：
  - 创建真实 Vulkan backend 与隐藏 GLFW window。
  - 创建固定 `256x144 RGBA16Float` offscreen scene color、`D32Float` depth 和 `GpuToCpu` readback buffer。
  - 加载 `assets/models/specular_ibl_validation_fixture.gltf`。
  - 使用程序化 HDR environment，经 `EnvironmentCubeConverter` 转成 skybox cubemap。
  - 以固定 `RenderScene`、`RenderView`、lighting 和 camera 执行 `SkyboxPass + ForwardPass`。
  - 将 HDR scene color copy 到 readback buffer，并在 CPU 端解码 half-float。
  - 统计 finite pixel、non-black ratio、min/max/mean RGB、mean/max luminance，用保守阈值拦截黑屏、全 clear、NaN/Inf 和异常亮度。
- `ark_frame_validation_smoke` 使用测试内局部 harness，不把临时 readback / capture 能力暴露成稳定 public API。
- 本轮也收尾了 sandbox camera 水平 orbit 方向反转的小改动，并由 `ark_sandbox_camera_controller_smoke` 覆盖。

当前边界：

- 仍不是完整 screenshot / golden image system。
- 仍不生成 PNG/JPEG，也不做 image diff 或 CI artifact。
- 当前验证读的是 offscreen HDR scene color，不是 swapchain backbuffer。
- 当前 frame smoke 覆盖 skybox + validation fixture + ForwardPass 真实绘制路径，但还没有 tone-mapped LDR target readback。

验证记录：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_readback_api_smoke ark_frame_validation_smoke ark_sandbox_camera_controller_smoke
build\msvc-vcpkg\Debug\ark_readback_api_smoke.exe
build\msvc-vcpkg\Debug\ark_sandbox_camera_controller_smoke.exe
build\msvc-vcpkg\Debug\ark_frame_validation_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

结果：

```text
targeted readback/frame-validation/camera smoke build passed
ark_readback_api_smoke passed
ark_sandbox_camera_controller_smoke passed
ark_frame_validation_smoke passed
full build passed
CTest: 22/22 passed
default sandbox smoke passed
```

## 完成标准

Phase 0.45 完成时应满足：

- `RGBA16Float` frame color 可以被 readback 或有明确替代验证路径。
- 至少一个 frame validation smoke 可以读取真实渲染输出。
- `specular_ibl_validation_fixture.gltf` 至少被一个 frame-level smoke 覆盖。
- pixel statistics 能发现全黑、全 clear、NaN/Inf 等重大失败。
- full build 通过。
- CTest 通过。
- sandbox 默认启动 smoke 通过。
- 文档明确当前仍不是完整 screenshot/golden image system。

## 风险与注意事项

- `RGBA16Float` readback 需要正确处理 half-float，不能把原始 16-bit 值当作 normalized integer。
- HDR scene color 的数值范围可能远高于 LDR，阈值要保守。
- 不同 GPU 的浮点结果可能有轻微差异，统计验证不要使用过窄阈值。
- 如果读取 swapchain backbuffer，可能受 presentation layout、surface format 和平台差异影响，不建议第一版这么做。
- 如果新增 offscreen render path，不要把临时测试入口设计成稳定 public API。
- `warm_restaurant_8k.hdr` 是本地人工验证资源，不要提交。
- 当前 sandbox camera 刚调整了水平 orbit 方向；如该改动未提交，Phase 0.45 开工前应先单独收尾。

## 后续 Phase 建议

Phase 0.45 之后可以按验证能力的成熟度选择：

1. **Phase 0.46 glTF Camera / Scene Camera Selection**
   - 读取 glTF camera。
   - sandbox 支持使用模型内 camera。
   - validation fixture 可拥有稳定相机入口。

2. **Phase 0.47 Material Ball Visual Fixture**
   - 用低面数 sphere grid 替代 quad grid 或作为补充。
   - 更直观验证 roughness、metallic、normal 和 IBL reflection。

3. **Phase 0.48 Screenshot / Golden Image System**
   - 在统计型 frame validation 稳定后，再考虑 PNG writer、golden baseline、image diff 和 CI artifact。

4. **后处理与曝光**
   - bloom、auto exposure、ACES/filmic、exposure UI/config。
   - 前提是已有最小 frame validation 能保护画面不崩。

5. **Lighting 扩展**
   - shadow、多光源、`KHR_lights_punctual`。
   - 建议等 frame validation 和 camera selection 更稳后再做。
