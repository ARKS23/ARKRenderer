# Phase 0.38 RHI Texture Readback and Cubemap Pixel Validation

## 阶段判断

Phase 0.37 已经把 cubemap face order 从注释推进到可测试 contract：

```text
Face order: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z

Sandbox debug path:
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
```

当前 renderer 主线已经可以：

```text
HDR equirectangular environment
    -> EnvironmentResource
    -> EnvironmentCubeConverter
        -> EnvironmentCubeResource
        -> SkyboxPass
    -> EnvironmentIrradianceGenerator
        -> diffuse irradiance cubemap
    -> ForwardPass diffuse ambient IBL
```

但 Phase 0.37 的验证仍停留在 CPU contract、shader source token、resource desc smoke 和人工可视化入口。也就是说，项目现在知道“应该是什么方向”，也能运行 debug orientation environment，但还不能自动证明 GPU conversion 后的 cubemap face 像素确实落在正确 layer、正确方向、正确颜色上。

如果此时直接进入 prefiltered specular environment、BRDF LUT 和 `ForwardPass` specular IBL，一旦出现方向反转、face 交换、mip view 错位或 shader 采样错误，问题会混进 roughness mip、BRDF integration 和 tone mapping 里，定位成本会明显升高。

Phase 0.38 建议先补最小 RHI texture readback 和 cubemap orientation pixel validation。目标不是做完整截图系统，而是建立一个非常窄、可验证、后续可扩展的 GPU readback 基础。

## 目标

Phase 0.38 目标：

- 新增 CPU 可读 readback buffer 能力。
- 新增最小 `copyTextureToBuffer()` RHI API。
- 在 Vulkan 后端实现 image -> buffer copy。
- 支持等待 GPU 完成后从 readback buffer 读取数据。
- 新增 cubemap orientation pixel smoke，使用 Phase 0.37 的 debug orientation environment 验证 GPU conversion 后的 face center colors。
- 明确该 readback 路径第一版只服务测试和验证，不作为高性能 runtime capture API。
- 让后续 cubemap mip、prefiltered specular、BRDF LUT 和 screenshot/pixel tests 能在这个基础上继续演进。

## 非目标

Phase 0.38 暂不做：

- 不做完整 screenshot 系统。
- 不做 async readback queue 或多帧延迟 readback。
- 不做通用 texture dump 工具。
- 不做 arbitrary compressed / depth / stencil / MSAA texture readback。
- 不做 row pitch 自动重排的通用图像导出。
- 不做 cubemap mip chain 生成。
- 不做 face-mip render target views。
- 不做 prefiltered specular environment map。
- 不做 BRDF LUT。
- 不做 `ForwardPass` specular IBL。
- 不做 roughness mip sampling。
- 不改 `ForwardPass` descriptor layout。
- 不改 `FrameRenderer` pass graph。
- 不引入 RenderGraph、bindless 或 compute pipeline。
- 不提交大型 HDRI。

## 当前基线

### Buffer

当前 `src/rhi/Buffer.h` 已有：

```cpp
enum class BufferUsage : u32 {
    None = 0,
    Vertex = 1 << 0,
    Index = 1 << 1,
    Uniform = 1 << 2,
    Storage = 1 << 3,
    TransferSrc = 1 << 4,
    TransferDst = 1 << 5,
};

enum class MemoryUsage {
    CpuToGpu,
    GpuOnly,
};
```

已有 `CpuToGpu` 可写 staging / uniform 路径，但没有 CPU 可读内存语义，也没有 `Buffer::readData()` / `mapRead()`。

Vulkan `VulkanBuffer::updateData()` 当前只支持 `CpuToGpu`：

```text
CpuToGpu -> host visible write + flush
GpuOnly  -> device local
```

Phase 0.38 需要新增读回语义，例如：

```cpp
enum class MemoryUsage {
    CpuToGpu,
    GpuOnly,
    GpuToCpu,
};
```

第一版建议让 `GpuToCpu` 表示 host visible readback buffer，Vulkan 侧使用 VMA host access random / mapped 或可 map 的 allocation。

### Texture

当前 `src/rhi/Texture.h` 已有：

```cpp
TextureUsage::TransferSrc
TextureUsage::TransferDst
TextureUsage::ShaderResource
TextureUsage::RenderTarget
```

`EnvironmentResource` 2D HDR upload 已用 `TransferDst`。`TextureResource` 2D color texture 支持 GPU mip generation，并使用 `TransferSrc | TransferDst | ShaderResource`。

`EnvironmentCubeResource` 当前创建 cubemap 时 usage 是：

```cpp
rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource
```

如果要 readback GPU-generated cubemap，需要让测试目标 cubemap 或 `EnvironmentCubeResource` 支持 `TransferSrc`。建议第一版在 `EnvironmentCubeResourceDesc` 中新增一个小范围 flag：

```cpp
bool allowReadback = false;
```

当 `allowReadback == true` 时追加 `TextureUsage::TransferSrc`。默认 runtime cubemap 不必全部带 TransferSrc，避免无意扩大资源 usage。

### DeviceContext

当前 `src/rhi/DeviceContext.h` 已有：

```cpp
bool uploadTextureData(const TextureUploadDesc& desc);
bool uploadBufferData(const BufferUploadDesc& desc);
bool generateTextureMips(Texture& texture);
```

但还没有：

```cpp
bool copyTextureToBuffer(const TextureReadbackDesc& desc);
```

Phase 0.38 建议新增最小描述：

```cpp
struct TextureReadbackDesc {
    Texture* texture = nullptr;
    Buffer* destinationBuffer = nullptr;
    u64 destinationOffset = 0;
    Extent2D extent{};
    u32 rowPitch = 0;
    u32 bytesPerPixel = 16;
    u32 mipLevel = 0;
    u32 arrayLayer = 0;
};
```

第一版约束：

- 必须在 active command buffer 中录制。
- 必须在 dynamic rendering scope 外录制。
- `texture` 必须支持 `TextureUsage::TransferSrc`。
- `destinationBuffer` 必须支持 `BufferUsage::TransferDst`。
- 只支持 color texture。
- 只支持 tightly packed rows，即 `rowPitch == width * bytesPerPixel` 或 `rowPitch == 0`。
- 只支持指定 mip / 指定 array layer。
- 只支持 `RGBA16Float` / `RGBA32Float`，如果实现成本要进一步压低，Phase 0.38 第一版可以只支持 `RGBA32Float`。

### GPU Completion

当前 `DeviceContext::submit()` 是异步提交；返回成功只表示提交进入 queue，不表示 GPU 已完成。读回测试必须显式等待 GPU 执行完成。

可选方案：

1. **使用现有 frame fence**
   - `beginFrame()` 复用 frame resource fence。
   - `submit()` signal fence。
   - 下一次 `beginFrame()` 会等待同一 frame slot。
   - 缺点：测试要绕 frame slot，语义不够直接。

2. **新增 `DeviceContext::waitIdle()` 或 `RenderDevice::waitIdle()` 测试路径**
   - `RenderDevice` 已有 `waitIdle()`。
   - smoke test 可以在 submit 后调用 device wait idle，再读取 readback buffer。
   - 简单、同步、适合第一版 smoke。

3. **新增专用 readback fence API**
   - 更完整，但 Phase 0.38 没必要。

建议 Phase 0.38 采用方案 2：submit 后用 `RenderDevice::waitIdle()` 保证读回数据可见。后续如果要做 runtime async capture，再设计更细的 fence/timeline API。

## 建议设计

### Readback Buffer API

建议在 `Buffer` 增加只读接口：

```cpp
virtual bool readData(void* destination, u64 size, u64 offset = 0) const = 0;
```

或更明确地放在 `DeviceContext`：

```cpp
virtual bool readBufferData(Buffer& buffer, void* destination, u64 size, u64 offset = 0) = 0;
```

第一版更推荐放在 `Buffer`：

- 读取行为绑定在 buffer allocation 上，类似当前 `VulkanBuffer::updateData()`。
- fake RHI tests 更容易直接实现。
- `DeviceContext` 继续表达命令录制，不把 CPU 读内存混进 command API。

约束：

- `readData()` 只允许 `MemoryUsage::GpuToCpu`。
- 检查目标指针非空、size 非零、range 不越界。
- Vulkan read path 需要 `vmaInvalidateAllocation()` 后 memcpy。
- 如果 allocation 未保持 mapped，则临时 `vmaMapMemory()` / `vmaUnmapMemory()`。

### Texture To Buffer Copy

建议在 `DeviceContext` 增加：

```cpp
virtual bool copyTextureToBuffer(const TextureReadbackDesc& desc) = 0;
```

Vulkan 实现核心：

```cpp
VkBufferImageCopy region{};
region.bufferOffset = desc.destinationOffset;
region.bufferRowLength = 0; // tightly packed texels
region.bufferImageHeight = 0;
region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
region.imageSubresource.mipLevel = desc.mipLevel;
region.imageSubresource.baseArrayLayer = desc.arrayLayer;
region.imageSubresource.layerCount = 1;
region.imageOffset = {0, 0, 0};
region.imageExtent = {desc.extent.width, desc.extent.height, 1};

vkCmdCopyImageToBuffer(
    commandBuffer,
    image,
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    buffer,
    1,
    &region);
```

状态转换建议由调用方显式走 `pipelineBarrier()`，保持与当前 renderer 风格一致：

```text
ShaderResource -> CopySrc
copyTextureToBuffer()
CopySrc -> ShaderResource
submit
waitIdle
readData()
```

也可以在 `copyTextureToBuffer()` 内部要求 texture 已经是 `CopySrc`，不隐式改状态。推荐第一版采用显式 barrier，测试能更清楚地覆盖资源状态。

### Format / Pixel Helper

Phase 0.38 的 pixel validation 需要知道读取数据布局。建议新增小工具函数，位置可选：

```text
tests/ 内部 helper
```

先不要提升到公共 API。第一版只读 `RGBA32Float`，读取中心像素：

```cpp
struct ReadbackColor {
    float r;
    float g;
    float b;
    float a;
};
```

如果需要支持 `RGBA16Float`，需要 half -> float 转换；这会增加额外复杂度。为了让 Phase 0.38 聚焦在 readback 基础设施，建议 orientation pixel smoke 使用 `RGBA32Float` cubemap target。

### Cubemap Orientation Pixel Smoke

建议新增：

```text
tests/cubemap_orientation_pixel_smoke.cpp
```

测试流程：

1. 创建 Vulkan backend / device / context。
2. 创建 debug orientation `EnvironmentResource`，输入来自 `makeDebugOrientationEnvironmentImage()`。
3. 创建 `EnvironmentCubeResource`：

```cpp
EnvironmentCubeResourceDesc desc{};
desc.faceExtent = {16, 16};
desc.format = rhi::Format::RGBA32Float;
desc.mipLevels = 1;
desc.allowReadback = true;
```

4. 使用 `EnvironmentCubeConverter` 生成 cubemap。
5. 对 cubemap 每个 face 执行：

```text
ShaderResource -> CopySrc
copyTextureToBuffer(face layer)
CopySrc -> ShaderResource
submit + waitIdle
readback buffer readData
check center pixel color
```

6. 期望颜色来自 Phase 0.37 contract：

```text
+X -> red
-X -> cyan
+Y -> white
-Y -> dark
+Z -> blue
-Z -> yellow
```

建议第一版只验证每个 face 中心像素。原因：

- 中心像素最不容易受 edge sampling / seam 影响。
- 能直接发现 face index、layer、major axis、direction sign 错误。
- 不把 seam correctness、filtering 和 edge fixup 混入 Phase 0.38。

### Fake RHI Tests

除真实 Vulkan pixel smoke 外，还建议补一个 fake/source 层测试或扩展现有 smoke：

- `copyTextureToBuffer()` desc validation。
- `GpuToCpu` buffer read API range validation。
- `EnvironmentCubeResourceDesc::allowReadback` 会追加 `TransferSrc`。

是否单独新建 smoke 取决于实现体量。建议：

```text
tests/readback_api_smoke.cpp
tests/cubemap_orientation_pixel_smoke.cpp
```

如果希望少增测试 target，可以把 API contract 合并进 `cubemap_orientation_pixel_smoke` 的 fake helper，但更清晰的做法是拆开。

## 实施顺序

### 0.38.0 文档与范围确认

目标：

- 新增 `docs/phase/phase38.md`。
- 明确本阶段只做最小 readback 和 cubemap pixel validation。
- 明确不做 screenshot、async readback、specular IBL 或 cubemap mip chain。

审核点：

- 不改 `ForwardPass` descriptor layout。
- 不改 `FrameRenderer` pass graph。
- 不引入 RenderGraph / compute。
- 不把 readback path 设计成完整产品 capture API。

### 0.38.1 Readback Buffer Foundation

目标：

- `MemoryUsage` 新增 `GpuToCpu`。
- `Buffer` 新增 CPU read API。
- Vulkan buffer allocation 支持 host visible readback。
- Vulkan read path 支持 invalidate + memcpy。
- fake tests 覆盖 read range、memory usage 拒绝和 empty input。

审核点：

- `CpuToGpu` 写路径不被破坏。
- `GpuOnly` 仍拒绝 CPU read/write。
- `readData()` 不能越界读取。
- 不把 mapped pointer 暴露给 renderer 层长期持有。

### 0.38.2 Texture To Buffer Copy

目标：

- 新增 `TextureReadbackDesc`。
- `DeviceContext` 新增 `copyTextureToBuffer()`。
- Vulkan 使用 `vkCmdCopyImageToBuffer`。
- 校验 texture/buffer usage、format、extent、mip/layer、row pitch、dynamic rendering scope。
- 调用方显式使用 `pipelineBarrier()` 进行 `ShaderResource <-> CopySrc`。

审核点：

- copy 命令必须在 rendering scope 外。
- 只支持 tightly packed rows。
- 不隐式支持 depth/stencil/MSAA/compressed formats。
- 不改变现有 upload/mip generation 行为。

### 0.38.3 Cubemap Readback Resource Flag

目标：

- `EnvironmentCubeResourceDesc` 新增 `allowReadback`。
- `EnvironmentCubeResource` 在该 flag 开启时追加 `TextureUsage::TransferSrc`。
- 更新 resource smoke 验证默认不带 `TransferSrc`、开启后带 `TransferSrc`。

审核点：

- 默认 runtime cubemap usage 不扩大。
- readback flag 只影响 texture usage，不改变 sampler/view 行为。
- face view layer contract 不变。

### 0.38.4 Cubemap Orientation Pixel Smoke

目标：

- 新增真实 Vulkan smoke：

```text
ark_cubemap_orientation_pixel_smoke
```

- 使用 `makeDebugOrientationEnvironmentImage()`。
- 创建 `RGBA32Float` readback-enabled cubemap。
- 跑 `EnvironmentCubeConverter`。
- readback 6 个 face center pixels。
- 与 `CubemapFaceContracts` debug color 做近似比较。

审核点：

- 测试确实从 GPU cubemap readback，不是重新读 CPU debug image。
- 只读中心像素，不把 seam/filtering 纳入本阶段。
- 用 `RenderDevice::waitIdle()` 明确同步。
- 测试失败信息包含 face index / face name / expected / actual。

### 0.38.5 Tests

目标：

- targeted build。
- readback API smoke 通过。
- cubemap orientation pixel smoke 通过。
- 原有 cubemap/resource/shader/framework smoke 通过。
- CTest 全量通过。

建议命令：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_readback_api_smoke ark_cubemap_orientation_pixel_smoke ark_cubemap_orientation_contract_smoke ark_environment_cube_resource_smoke ark_equirectangular_to_cube_smoke ark_shader_assets_smoke ark_framework_headers_smoke
build/msvc-vcpkg/Debug/ark_readback_api_smoke.exe
build/msvc-vcpkg/Debug/ark_cubemap_orientation_pixel_smoke.exe
build/msvc-vcpkg/Debug/ark_cubemap_orientation_contract_smoke.exe
build/msvc-vcpkg/Debug/ark_environment_cube_resource_smoke.exe
build/msvc-vcpkg/Debug/ark_equirectangular_to_cube_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
```

如果最终没有拆出 `ark_readback_api_smoke`，则从命令中移除该 target，并在验证记录说明 readback API 覆盖合并到了实际采用的 smoke。

### 0.38.6 验证与收尾

目标：

- full build。
- CTest 全量通过。
- default sandbox runtime smoke。
- debug orientation sandbox runtime smoke。
- DamagedHelmet + 本地 HDR runtime smoke。
- 更新 `docs/codex_handoff.md`。

建议命令：

```powershell
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

审核点：

- handoff 明确 Phase 0.38 只完成最小 readback / pixel validation，不代表完整 screenshot/capture system。
- 下一阶段建议转向 cubemap mip / face-mip view foundation，或 prefiltered specular environment 前置。

## 关键风险

### GPU 同步

readback 必须在 GPU copy 完成后读取。第一版 smoke 应强制：

```text
submit -> RenderDevice::waitIdle() -> readData()
```

不要依赖 `submit()` 返回值代表完成。

### Layout / State

`copyTextureToBuffer()` 需要 texture 处于 `CopySrc`。建议测试显式使用：

```text
ShaderResource -> CopySrc
copyTextureToBuffer
CopySrc -> ShaderResource
```

不要在 Vulkan 后端偷偷假设当前 layout。

### Row Pitch

第一版只支持 tightly packed rows。`VkBufferImageCopy::bufferRowLength = 0` 表示紧密排列 texel。不要尝试同时处理 arbitrary row pitch、imageHeight 或 CPU-side repack。

### Format

如果支持 `RGBA16Float`，测试需要 half-float 解码。为避免范围膨胀，Phase 0.38 orientation pixel smoke 建议使用 `RGBA32Float` target。

### Resource Usage

默认 cubemap 不应无条件增加 `TransferSrc`。建议用 `EnvironmentCubeResourceDesc::allowReadback` 显式开启，测试资源开启，默认 renderer 可以保持关闭。

## 完成标准

Phase 0.38 完成时应满足：

- RHI 有最小 CPU readback buffer 能力。
- Vulkan 支持 `GpuToCpu` readback buffer allocation 和 `readData()`。
- RHI / Vulkan 支持最小 `copyTextureToBuffer()`。
- `EnvironmentCubeResource` 可以按需创建 readback-enabled cubemap。
- 新增 GPU pixel smoke 能验证 debug orientation environment 转换后的 6 个 cubemap face center colors。
- 测试能证明 face order / layer / major-axis direction 与 `CubemapOrientation` contract 一致。
- full build / CTest / runtime smoke 通过。
- handoff 明确后续仍需 cubemap mip / prefilter / BRDF LUT / specular IBL。

## 实施结果

Phase 0.38 已完成 0.38.0 ~ 0.38.6：

- `MemoryUsage` 新增 `GpuToCpu`，`Buffer` 新增 `readData()` 默认失败接口。
- Vulkan buffer allocation 支持 host-visible readback memory，`VulkanBuffer::readData()` 使用 invalidate + memcpy 读取 CPU 数据。
- RHI 新增 `TextureReadbackDesc` 与 `DeviceContext::copyTextureToBuffer()` 最小 API。
- Vulkan command context 新增 `vkCmdCopyImageToBuffer` 路径，显式要求 active command buffer、rendering scope 外、`CopySrc` texture state、`TransferSrc` texture usage、`TransferDst` + `GpuToCpu` buffer、format bytes-per-pixel 匹配和紧密 row pitch。
- `EnvironmentCubeResourceDesc` 新增 `allowReadback`，默认不扩大 cubemap usage，开启后追加 `TextureUsage::TransferSrc`。
- 新增 `ark_readback_api_smoke`，覆盖 readback buffer contract、texture-to-buffer copy contract 和 cubemap readback usage flag。
- 新增 `ark_cubemap_orientation_pixel_smoke`，通过真实 Vulkan backend 将 debug orientation environment 转换为 cubemap，并读回 6 个 face center pixels 与 `CubemapOrientation` debug color contract 比对。

验证记录：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_readback_api_smoke ark_environment_cube_resource_smoke ark_framework_headers_smoke ark_cubemap_orientation_pixel_smoke
build/msvc-vcpkg/Debug/ark_readback_api_smoke.exe
build/msvc-vcpkg/Debug/ark_environment_cube_resource_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
build/msvc-vcpkg/Debug/ark_cubemap_orientation_pixel_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
targeted smoke build passed
ark_readback_api_smoke passed
ark_environment_cube_resource_smoke passed
ark_framework_headers_smoke passed
ark_cubemap_orientation_pixel_smoke passed
full build passed
CTest: 19/19 passed
default sandbox smoke passed
debug orientation sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

注意：本阶段完成的是验证用最小 readback / pixel smoke 基础，不是完整 screenshot、async capture 或 golden image system。

## 后续 Phase 建议

Phase 0.38 后建议：

1. **Cubemap Mip / Face-Mip View Foundation**
   - 为 prefiltered specular 准备多 mip face render target view。
2. **Prefiltered Specular Environment**
   - 从 environment cubemap 生成 roughness mip chain。
3. **BRDF LUT**
   - 新增 split-sum BRDF integration LUT resource。
4. **ForwardPass Specular IBL**
   - 接入 prefiltered environment、BRDF LUT 和 roughness mip sampling。
5. **Screenshot / Pixel Test Infrastructure**
   - 在最小 readback 基础上扩展 frame color screenshot、golden image 或统计型 pixel smoke。
