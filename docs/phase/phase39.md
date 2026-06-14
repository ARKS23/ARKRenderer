# Phase 0.39 Cubemap Mip and Face-Mip View Foundation

## 阶段判断

Phase 0.38 已经补齐了最小 RHI texture readback，并用真实 Vulkan pixel smoke 自动验证了 debug orientation environment 转换后的 6 个 cubemap face center colors。也就是说，当前项目已经不只是在 CPU contract 上约定 cubemap face order，而是能从 GPU 生成结果读回像素确认 layer、方向和颜色都符合 `CubemapOrientation` contract。

当前 environment / IBL 主线是：

```text
HDR equirectangular environment
    -> EnvironmentResource
    -> EnvironmentCubeConverter
        -> EnvironmentCubeResource mip0
        -> SkyboxPass
    -> EnvironmentIrradianceGenerator
        -> diffuse irradiance cubemap mip0
    -> ForwardPass diffuse ambient IBL
```

下一步如果要进入 specular IBL，需要生成 prefiltered specular environment。该资源通常是一个多 mip cubemap：每个 mip 对应不同 roughness，shader 运行时按材质 roughness 采样不同 mip。当前 `EnvironmentCubeResource` 虽然允许 `mipLevels > 1`，但只创建了 6 个 mip0 face render target views：

```cpp
rhi::TextureView* faceRenderTargetView(u32 faceIndex) const;
```

这足够服务 equirectangular -> cubemap 和 diffuse irradiance 的 mip0 渲染，但不够服务 prefilter pass 对 `face + mip` 的逐层渲染。因此 Phase 0.39 建议先补齐 cubemap mip / face-mip render target view foundation，让后续 prefiltered specular pass 可以只关注采样、roughness 和 shader 数学，不再同时改资源结构。

## 目标

Phase 0.39 目标：

- 保持现有 `EnvironmentCubeResource` mip0 face view API 兼容。
- 为 `EnvironmentCubeResource` 新增每个 face、每个 mip 的 2D render target view。
- 新增安全查询 API，例如：

```cpp
rhi::TextureView* faceMipRenderTargetView(u32 faceIndex, u32 mipLevel) const;
rhi::Extent2D mipExtent(u32 mipLevel) const;
```

- 明确 `faceRenderTargetView(faceIndex)` 继续返回 mip0 view，现有 converter / irradiance generator 不需要改行为。
- 更新 fake smoke，验证多 mip cubemap 会创建 `1 cube view + FaceCount * mipLevels face-mip views + sampler`。
- 更新 framework headers smoke，覆盖新增 public API。
- 保证 Phase 0.32 / 0.34 / 0.38 既有 equirectangular conversion、irradiance generation 和 cubemap pixel readback 测试继续通过。

## 非目标

Phase 0.39 暂不做：

- 不做 prefiltered specular environment shader。
- 不做 roughness -> mip 的采样策略。
- 不做 BRDF LUT。
- 不做 `ForwardPass` specular IBL。
- 不改 `ForwardPass` descriptor layout。
- 不改 `FrameRenderer` pass graph。
- 不做 cubemap mip 自动生成。
- 不做 compute prefilter。
- 不引入 RenderGraph、bindless 或新的 descriptor manager 抽象。
- 不做 screenshot/golden image 系统。
- 不提交新 HDR 资源。

## 当前基线

### EnvironmentCubeResource

当前 `EnvironmentCubeResourceDesc` 已支持：

```cpp
struct EnvironmentCubeResourceDesc {
    rhi::Extent2D faceExtent{};
    rhi::Format format = rhi::Format::RGBA16Float;
    u32 mipLevels = 1;
    rhi::SamplerDesc sampler;
    bool hasSamplerOverride = false;
    bool allowReadback = false;
    std::string debugName;
};
```

当前创建逻辑：

```text
TextureDesc:
    type = Cube
    arrayLayers = 6
    mipLevels = desc.mipLevels
    usage = RenderTarget | ShaderResource
    optional TransferSrc when allowReadback

TextureView:
    one cube view covering all 6 faces and all mips

Face views:
    6 Texture2D views
    each view covers one face and mip0 only
```

当前限制是：`mipLevels > 1` 时 cube view 和 sampler 已经知道多 mip，但 renderer 层没有暴露 mip1+ 的 face render target views。

### Vulkan TextureView

当前 `TextureViewDesc` 已有：

```cpp
u32 baseMipLevel = 0;
u32 mipLevelCount = 1;
u32 baseArrayLayer = 0;
u32 arrayLayerCount = 1;
TextureViewType type = TextureViewType::Texture2D;
```

`VulkanTextureView` 已校验：

- mip range 不越界。
- array range 不越界。
- 2D view 必须 exactly one array layer。
- cube view 必须来自 cube texture 且 exactly 6 array layers。

因此 Phase 0.39 不需要新 RHI view 类型，只需要在 `EnvironmentCubeResource` 中为每个 face+mip 创建已有的 `Texture2D` view。

## 建议设计

### Face-Mip View 存储

建议把当前固定 6 个 face views 扩展为一维数组或 vector：

```cpp
std::vector<Scope<rhi::TextureView>> m_FaceMipViews;
```

索引建议固定为：

```cpp
index = mipLevel * FaceCount + faceIndex;
```

原因：

- 遍历释放简单。
- 测试中可以直接推导期望数量。
- prefilter pass 通常外层 mip、内层 face 或外层 face、内层 mip 都能快速查询。

保留旧 API：

```cpp
rhi::TextureView* faceRenderTargetView(u32 faceIndex) const {
    return faceMipRenderTargetView(faceIndex, 0);
}
```

新增 API：

```cpp
rhi::TextureView* faceMipRenderTargetView(u32 faceIndex, u32 mipLevel) const;
```

查询越界时返回 `nullptr`，与现有 `faceRenderTargetView()` 越界返回 `nullptr` 的风格一致。

### Mip Extent Helper

建议新增：

```cpp
rhi::Extent2D mipExtent(u32 mipLevel) const;
```

计算：

```cpp
width  = max(1, faceExtent.width  >> mipLevel)
height = max(1, faceExtent.height >> mipLevel)
```

越界时返回 `{}`。后续 prefilter pass 可以直接用它设置 viewport、scissor 和 render target size。

### View 创建规则

对于每个 mip、每个 face：

```cpp
rhi::TextureViewDesc faceMipViewDesc{};
faceMipViewDesc.format = textureDesc.format;
faceMipViewDesc.baseMipLevel = mipLevel;
faceMipViewDesc.mipLevelCount = 1;
faceMipViewDesc.baseArrayLayer = faceIndex;
faceMipViewDesc.arrayLayerCount = 1;
faceMipViewDesc.type = rhi::TextureViewType::Texture2D;
```

debugName 目前 `TextureViewDesc` 没有名字字段，Phase 0.39 不需要扩展 RHI debug naming。若未来需要 GPU object name，可以单独阶段处理。

### isValid 语义

`isValid()` 应要求：

- cube texture 存在。
- cube texture view 存在。
- sampler 存在。
- `m_FaceMipViews.size() == FaceCount * m_MipLevels`。
- 每个 face-mip view 都存在。

### Release / Reset

`releaseDeferred()` 和 `resetImmediate()` 需要遍历全部 face-mip views，而不是只处理 6 个 mip0 face views。

释放成功后：

```text
m_FaceMipViews.clear()
m_TextureView.reset()
m_Sampler.reset()
m_Texture.reset()
m_FaceExtent = {}
m_Format = Unknown
m_MipLevels = 1
```

### 兼容现有 Pass

现有 pass 使用：

```cpp
target.faceRenderTargetView(faceIndex)
```

Phase 0.39 不应要求它们改用新 API。`EnvironmentCubeConverter` 和 `EnvironmentIrradianceGenerator` 继续渲染 mip0，行为不变。

后续 prefilter pass 才使用：

```cpp
target.faceMipRenderTargetView(faceIndex, mipLevel)
target.mipExtent(mipLevel)
```

## 实施顺序

### 0.39.0 文档与范围确认

目标：

- 新增 `docs/phase/phase39.md`。
- 明确本阶段只做 cubemap mip / face-mip view foundation。
- 明确不做 prefilter shader、BRDF LUT 和 ForwardPass specular IBL。

审核点：

- 不扩大到完整 specular IBL。
- 不改 renderer pass graph。
- 不改 descriptor layout。

### 0.39.1 EnvironmentCubeResource Face-Mip API

目标：

- `EnvironmentCubeResource` 新增 `faceMipRenderTargetView(faceIndex, mipLevel)`。
- 新增 `mipExtent(mipLevel)`。
- 保留 `faceRenderTargetView(faceIndex)` 作为 mip0 alias。
- 内部 storage 从固定 6 face views 扩展为 `FaceCount * mipLevels` views。

审核点：

- `faceRenderTargetView()` 行为与旧版本一致。
- 越界查询返回 `nullptr` 或 `{}`，不崩溃。
- mip extent 最小值 clamp 到 1。

### 0.39.2 Face-Mip View Creation and Lifetime

目标：

- 创建 cubemap 时为每个 face+mip 创建 `Texture2D` view。
- `isValid()` 检查所有 face-mip views。
- `releaseDeferred()` 和 `resetImmediate()` 清理所有 face-mip views。

审核点：

- 创建失败时完整 reset。
- deferred release 数量与 view 数量一致。
- cube view 仍覆盖所有 mips 和 6 faces。
- sampler policy 不变：多 mip 默认 linear mip filter，单 mip nearest mip filter。

### 0.39.3 Tests

目标：

- 更新 `ark_environment_cube_resource_smoke`：
  - `mipLevels = 3` 时 view 数量应为 `1 + FaceCount * 3`。
  - 每个 face+mip view 的 `baseMipLevel`、`mipLevelCount`、`baseArrayLayer`、`arrayLayerCount` 和 type 正确。
  - `faceRenderTargetView(face)` 等于 mip0 view 语义。
  - `faceMipRenderTargetView(face, mip)` 可查询 mip1/mip2。
  - 越界 face/mip 返回 `nullptr`。
  - `mipExtent(0/1/2)` 正确。
  - deferred release 数量正确。
- 更新 `ark_framework_headers_smoke` 覆盖新增 API。
- 保持现有 conversion / irradiance / pixel smoke 通过。

建议命令：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_environment_cube_resource_smoke ark_framework_headers_smoke ark_equirectangular_to_cube_smoke ark_environment_irradiance_smoke ark_cubemap_orientation_pixel_smoke
build/msvc-vcpkg/Debug/ark_environment_cube_resource_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
build/msvc-vcpkg/Debug/ark_equirectangular_to_cube_smoke.exe
build/msvc-vcpkg/Debug/ark_environment_irradiance_smoke.exe
build/msvc-vcpkg/Debug/ark_cubemap_orientation_pixel_smoke.exe
```

### 0.39.4 验证与收尾

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
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

## 关键风险

### 现有 Pass 兼容性

`EnvironmentCubeConverter` 和 `EnvironmentIrradianceGenerator` 当前只需要 mip0 face view。Phase 0.39 必须保留 `faceRenderTargetView(faceIndex)` 语义，避免为了新 API 让旧 pass 跟着改动。

### View 数量增长

多 mip cubemap view 数量是：

```text
1 cube view + 6 * mipLevels face-mip views
```

对于常见 128/256/512 prefilter cubemap，这个数量很小，可以接受。Phase 0.39 不需要做 lazy view creation。

### Mip Extent

mip extent 必须 clamp 到 1。如果直接右移到 0，会导致 viewport/scissor/rendering desc 无效。测试应覆盖较小尺寸，例如 4x4 的 mip chain。

### Sampler Policy

当前 `EnvironmentCubeResource` 多 mip 默认 `mipFilter = Linear`。Phase 0.39 不应改变 sampler 默认策略。后续 prefilter/specular IBL 如果需要 LOD bias 或 max LOD，再单独设计。

### Readback Pixel Smoke

Phase 0.38 的 pixel smoke 使用 `mipLevels = 1`，理论上不应受 Phase 0.39 影响。若影响，通常说明 mip0 face view alias 或 view 创建顺序破坏了旧 contract。

## 完成标准

Phase 0.39 完成时应满足：

- `EnvironmentCubeResource` 能为多 mip cubemap 创建完整 face-mip render target views。
- `faceRenderTargetView(face)` 继续返回 mip0 view。
- `faceMipRenderTargetView(face, mip)` 能查询任意有效 face+mip。
- `mipExtent(mip)` 返回正确 clamped extent。
- `isValid()`、`releaseDeferred()` 和 `resetImmediate()` 覆盖全部 face-mip views。
- fake smoke 覆盖 view 数量、view desc、越界访问、mip extent 和 deferred release。
- equirectangular conversion、irradiance generation、cubemap orientation pixel readback 继续通过。
- full build / CTest / runtime smoke 通过。
- handoff 明确下一阶段可进入 prefiltered specular environment。

## 实施结果

Phase 0.39 已完成 0.39.0 ~ 0.39.4：

- `EnvironmentCubeResource` 内部 face view storage 从固定 6 个 mip0 views 扩展为 `FaceCount * mipLevels` 个 face-mip views。
- 新增 `faceMipRenderTargetView(faceIndex, mipLevel)`，越界返回 `nullptr`。
- `faceRenderTargetView(faceIndex)` 保持兼容，继续作为 mip0 view alias。
- 新增 `mipExtent(mipLevel)`，按 mip level 计算并 clamp 到 1，越界或无效资源返回 `{}`。
- `isValid()`、`releaseDeferred()`、`resetImmediate()` 已覆盖全部 face-mip views。
- 多 mip cubemap 仍保留一个 cube sampled view，覆盖全部 6 faces 和全部 mips。
- 现有 `EnvironmentCubeConverter` / `EnvironmentIrradianceGenerator` 继续使用 mip0 face view，行为不变。
- `ark_environment_cube_resource_smoke` 已覆盖 face-mip view 数量、view desc、mip0 alias、越界查询、mip extent 和 deferred release 数量。
- `ark_framework_headers_smoke` 已覆盖新增 public API。

验证记录：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_environment_cube_resource_smoke ark_framework_headers_smoke ark_equirectangular_to_cube_smoke ark_environment_irradiance_smoke ark_cubemap_orientation_pixel_smoke
build/msvc-vcpkg/Debug/ark_environment_cube_resource_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
build/msvc-vcpkg/Debug/ark_equirectangular_to_cube_smoke.exe
build/msvc-vcpkg/Debug/ark_environment_irradiance_smoke.exe
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
ark_environment_cube_resource_smoke passed
ark_framework_headers_smoke passed
ark_equirectangular_to_cube_smoke passed
ark_environment_irradiance_smoke passed
ark_cubemap_orientation_pixel_smoke passed
full build passed
CTest: 19/19 passed
default sandbox smoke passed
debug orientation sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

注意：本阶段只完成 face-mip render target view foundation，不代表已经有 prefiltered specular environment、BRDF LUT 或 ForwardPass specular IBL。

## 后续 Phase 建议

Phase 0.39 后建议：

1. **Prefiltered Specular Environment**
   - 基于 source environment cubemap，按 roughness 渲染 target cubemap 的 mip chain。
2. **BRDF LUT**
   - 新增 split-sum BRDF integration LUT resource。
3. **ForwardPass Specular IBL**
   - 接入 prefiltered environment、BRDF LUT 和 roughness mip sampling。
4. **Specular IBL Validation**
   - 使用金属/粗糙度测试球、HDR environment 和 Phase 0.38 readback 基础做更强验证。
5. **Screenshot / Pixel Test Infrastructure**
   - 在 readback 基础上扩展 frame color screenshot、golden image 或统计型 pixel smoke。
