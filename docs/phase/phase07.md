# Phase 0.7 Upload System、TextureLoader 与资源生命周期收口

## 阶段判断

Phase 0.6 已经完成 texture sampling 的最小闭环：

```text
CubePass
    -> checkerboard CPU data
    -> staging buffer
    -> GPU texture + texture view + sampler
    -> sampled image descriptor + sampler descriptor
    -> textured cube shader
```

这说明 RHI descriptor、sampler、texture view、shader binding 和 `vkCmdCopyBufferToImage` 路径已经可用。接下来不应该直接跳到 glTF、PBR 或完整材质系统，因为当前资源上传和生命周期仍然是阶段性实现：

- texture upload 已有 `TextureUploadDesc` 路径；upload staging buffer 已可交给 frame-level deferred deletion，尚未迁移到统一 upload allocator。
- `TextureUploadDesc` 已在 0.7.2 显式表达 `rowPitch` / `bytesPerPixel`，但实现仍只覆盖 tightly packed RGBA8、2D、mip0、array layer 0。
- `VulkanBuffer` 对 `GpuOnly initialData` 仍然显式报错；0.7.3 已补齐 staging + `uploadBufferData()` 的最小上传闭环。
- `TextureLoader` 已在 0.7.1 输出 CPU 侧 `ImageData` 和 LDR RGBA8 加载路径；HDR 路径仍保留 TODO。
- `VulkanDeletionQueue` 已用于 upload staging buffer 的延迟释放；完整 Vulkan 对象 deferred destruction 仍未覆盖。
- `VulkanDescriptorManager` 已在 0.7.6 支持简单多 pool 增长；单个 descriptor set 超出单 pool 配额、pool reset/free 策略仍留到后续阶段。

因此 Phase 0.7 的主线应是资源上传与生命周期收口，为后续真实纹理、mesh、material 和 glTF 做准备。

## 目标

Phase 0.7 目标是在不引入重型 ResourceManager / RenderGraph 的前提下，完成以下能力：

- 新增轻量 `TextureLoader`，从文件读取图片并输出 CPU 侧 `ImageData`。
- 建立更可复用的 GPU upload 路径，覆盖 texture upload，并补齐 GPU-only buffer upload 的最小能力。
- 明确 staging buffer 的生命周期策略，避免 GPU 异步执行期间提前释放上传资源。
- 将 `CubePass` 从 Phase 0.6 的 pass-local 一次性 upload 逐步迁移到通用 upload 接口。
- 让 descriptor pool 在 sampled texture 数量增长时更可维护，至少记录固定 pool 风险，优先实现简单 growable pool。
- 继续保持 renderer/RHI/Vulkan 边界清晰，不让 Vulkan 类型泄漏到公共 RHI 或 renderer 层。

## 非目标

Phase 0.7 暂不做以下内容：

- 不做完整 `ResourceManager` handle 系统。
- 不做完整 RenderGraph、自动 barrier、transient resource aliasing。
- 不做 bindless texture / global descriptor array。
- 不做完整 material system / PBR shading。
- 不做完整 glTF scene import。
- 不做异步 transfer queue、多 queue ownership transfer 或 timeline semaphore upload。
- 不做 mipmap 生成、block compressed texture、cubemap、texture array 的完整支持。
- 不做 HDR texture / IBL 的完整链路；Phase 0.7 默认图片加载路径只处理 LDR RGBA8。
- 不做 shader hot reload 或完整 asset root / virtual file system。

这些内容应等 upload、lifetime 和基础 asset loader 稳定后再进入后续阶段。

## 模块边界

Phase 0.7 必须继续遵守现有设计文档：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
```

### asset/

`asset/TextureLoader` 只负责 CPU 侧图片读取和格式整理，不创建 RHI 资源，不依赖 `renderer/` 或 `rhi/`。

Phase 0.7 的默认加载接口只面向 LDR 8-bit 图片。HDR 文件不能静默走 `RGBA8Unorm` 路径，否则会丢失动态范围，并且会让后续 IBL / PBR 调试变得不可信。实现时应通过 `stbi_is_hdr_from_memory()` 或等价检测先识别 HDR；如果调用的是 `loadImageRgba8()`，遇到 HDR 应返回失败并输出英文日志。HDR 支持应通过单独接口显式表达，例如后续 `loadImageHdrRgba32F()`。

建议输出：

```cpp
namespace ark::asset {
    enum class ImageFormat {
        Rgba8Unorm,
        Rgba32Float, // TODO: HDR path, not required by Phase 0.7 minimum.
    };

    struct ImageData {
        u32 width = 0;
        u32 height = 0;
        ImageFormat format = ImageFormat::Rgba8Unorm;
        u32 bytesPerPixel = 4;
        std::vector<u8> pixels;
        std::string debugName;
    };

    // LDR-only path. HDR input must fail explicitly instead of being quantized to 8-bit.
    ImageData loadImageRgba8(const Path& path);
}
```

实现上优先通过 `core::readBinaryFile()` 读取文件，再用 `stbi_load_from_memory()` 解码 LDR 图片。这样文件路径策略仍留在 core / asset 层，不进入 renderer 或 RHI。

HDR 后续扩展方向：

```text
stbi_is_hdr_from_memory()
    -> stbi_loadf_from_memory()
    -> ImageData::format = Rgba32Float
    -> rhi::Format::R32G32B32A32Float 或显式转换到 RGBA16Float
    -> bytesPerPixel = 16 或 8
```

这个路径不纳入 Phase 0.7 最小完成标准，除非 texture upload 已经能可靠表达 format、bytesPerPixel 和 rowPitch。

### renderer/

`renderer/` 负责把 CPU asset 数据转成 RHI 资源创建和上传命令：

```text
asset::ImageData
    -> RenderDevice::createBuffer(staging)
    -> RenderDevice::createTexture(gpu texture)
    -> DeviceContext::uploadTextureData()
    -> RenderDevice::createTextureView()
    -> RenderDevice::createSampler()
    -> DescriptorSet update
```

`CubePass` 可以继续作为阶段验证 pass，但它不应该长期承载图片解码、上传资源回收策略或全局资源管理。

### rhi/

RHI 公共层表达 API 无关的上传语义，不暴露 Vulkan layout、access mask、pipeline stage、`VkBufferImageCopy` 或 `VkImageLayout`。

推荐扩展方向：

```cpp
struct BufferUploadDesc {
    Buffer* sourceBuffer = nullptr;
    Buffer* destinationBuffer = nullptr;
    u64 sourceOffset = 0;
    u64 destinationOffset = 0;
    u64 size = 0;
};

struct TextureUploadDesc {
    Buffer* sourceBuffer = nullptr;
    Texture* texture = nullptr;
    u64 sourceOffset = 0;
    Extent2D extent{};
    u32 rowPitch = 0;
    u32 bytesPerPixel = 4;
    u32 mipLevel = 0;
    u32 arrayLayer = 0;
};
```

`rowPitch` 和 `bytesPerPixel` 可以先只支持 tightly packed RGBA8，但接口需要明确表达约束，避免后续扩展时反复改语义。

### rhi/vulkan/

Vulkan 后端负责把 RHI upload 语义落地为：

- staging buffer 到 GPU buffer 的 `vkCmdCopyBuffer`。
- staging buffer 到 image 的 `vkCmdCopyBufferToImage`。
- `Undefined -> CopyDst -> ShaderResource` texture barrier。
- buffer copy 前后的最小 buffer barrier。
- staging resource 的延迟释放或 frame-safe 保活。

Phase 0.7 可以继续使用 graphics queue 记录 upload，不引入独立 transfer queue。

## 实施顺序

### 0.7.0 文档与现状确认

目标：

- 新增 `docs/phase/phase07.md`。
- 确认 Phase 0.6 已完成，不重复实现 sampler、sampled image descriptor、textured cube shader。
- 记录当前风险：pass-local staging、GPU-only buffer initial data、deferred deletion、fixed descriptor pool。

验收：

- 文档和既有设计边界一致。
- 没有把 Phase 0.7 范围扩大到 ResourceManager / RenderGraph。

### 0.7.1 TextureLoader 与 ImageData

工作内容：

- 在 `src/asset/TextureLoader.h/.cpp` 中定义 CPU 侧 `ImageData` 和 `loadImageRgba8()`。
- 使用 `stb_image` 解码，强制输出 RGBA8。
- 使用 HDR 检测阻止 `.hdr` / float image 静默进入 RGBA8 路径；Phase 0.7 最小实现可以直接失败并记录 TODO。
- 通过 `core::readBinaryFile()` 读取文件内容。
- 错误日志使用英文，并包含文件路径。
- 增加轻量 smoke test，覆盖无效路径和一个有效图片样例。

注意：

- 当前仓库没有 `assets/` 根目录。若要测试真实图片，可以新增一个很小的测试 fixture，或先只做 API 与失败路径测试，并在文档中记录缺口。
- `TextureLoader` 不要创建 `rhi::Texture`，否则会破坏 asset 与 RHI 的边界。

验收：

- `asset/TextureLoader` 不依赖 `renderer/` 或 `rhi/`。
- 成功路径返回 width / height / RGBA8 pixels。
- HDR 输入不会被隐式量化成 RGBA8。
- 失败路径不崩溃，日志清晰。

当前实现状态：

- 已在 `src/asset/TextureLoader.h/.cpp` 实现 `ImageFormat`、`ImageData`、`TextureLoader::loadRgba8()` 和 `loadImageRgba8()`。
- `loadImageRgba8()` 使用 `core::readBinaryFile()` 读取文件，再通过 `stbi_is_hdr_from_memory()` 拒绝 HDR 输入，最后用 `stbi_load_from_memory(..., 4)` 解码为 RGBA8。
- `ImageData` 当前携带 `width`、`height`、`format`、`bytesPerPixel`、`pixels` 和 `debugName`，保持纯 CPU 数据，不依赖 RHI。
- 已新增 `tests/texture_loader_smoke.cpp`，覆盖临时 LDR PPM 成功加载、HDR header 拒绝、缺失文件失败路径，以及 `TextureLoader` class wrapper。
- 已在 `CMakeLists.txt` 接入 `ark_texture_loader_smoke`，并在 `framework_headers_smoke` 中触碰 `ImageData` 类型。
- 已通过：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

### 0.7.2 通用 texture upload 描述补齐

工作内容：

- 扩展 `TextureUploadDesc`，补充 `rowPitch`、`bytesPerPixel` 或等价字段。
- 明确 Phase 0.7 仍只支持 2D、mip0、array layer 0、RGBA8。
- Vulkan 后端继续使用 `vkCmdCopyBufferToImage`，但根据 upload desc 校验范围。
- 保持 upload 命令只能在 dynamic rendering scope 外调用。

验收：

- RHI 公共层仍无 Vulkan 类型。
- upload API 能表达当前限制，不隐含 magic layout。
- `CubePass` 的 texture upload 使用新描述仍能正常工作。

当前实现状态：

- 已扩展 `rhi::TextureUploadDesc`，新增 `rowPitch` 和 `bytesPerPixel`。
- `rowPitch = 0` 表示 tightly packed，即 `extent.width * bytesPerPixel`。
- `CubePass` 上传 checkerboard 时已显式填写 `rowPitch` 和 `bytesPerPixel`。
- `VulkanCommandContext::uploadTextureData()` 已校验：
  - upload 必须在 dynamic rendering scope 外记录。
  - source buffer 必须是 Vulkan buffer 且包含 `TransferSrc` usage。
  - target texture 必须是 Vulkan texture 且包含 `TransferDst` usage。
  - 当前只支持 `Format::RGBA8Unorm`、`bytesPerPixel = 4`。
  - 当前只支持 mip 0 和 array layer 0。
  - 当前只支持 tightly packed rows；非紧密 row pitch 后续再扩展。
  - upload 范围不能越过 source buffer 大小。
- `framework_headers_smoke` 已触碰新增字段。
- 仍保留 `Undefined -> CopyDst -> ShaderResource` texture barrier 语义。

限制：

- 当前还没有实现真正的 padded row upload；`rowPitch` 只是先进入 RHI 描述和校验。
- 当前仍不支持 HDR / RGBA16F / RGBA32F texture upload。
- 当前仍不支持 mipmap、array texture 或 image offset。

### 0.7.3 GPU-only buffer upload 最小闭环

工作内容：

- 新增 `DeviceContext::uploadBufferData()` 或等价 API。
- Vulkan 后端实现 `vkCmdCopyBuffer`。
- 为 GPU-only vertex/index buffer initial data 建立可用路径。
- 不要求 `RenderDevice::createBuffer(initialData + GpuOnly)` 自动上传，因为 `RenderDevice` 不负责命令录制。

建议策略：

```text
RenderDevice::createBuffer(GpuOnly | TransferDst)
RenderDevice::createBuffer(CpuToGpu | TransferSrc staging)
DeviceContext::uploadBufferData(staging -> gpu buffer)
```

验收：

- 不再依赖 CPU-visible GPU resource 来初始化大块静态数据。
- `VulkanBuffer` 可以继续拒绝 `GpuOnly initialData`，但文档和调用路径必须说明正确做法。
- 后续 mesh upload 可以复用该接口。

当前实现状态：

- 已新增 `rhi::BufferUploadDesc`，显式表达 source/destination buffer、source/destination offset 和 upload size。
- 已新增 `DeviceContext::uploadBufferData()`，资源创建仍由 `RenderDevice` 负责，buffer copy 命令仍由 `DeviceContext` 录制。
- `VulkanCommandContext::uploadBufferData()` 已实现：
  - upload 必须在 active command buffer 中录制。
  - upload 必须在 dynamic rendering scope 外录制。
  - source buffer 必须是 Vulkan buffer 且包含 `TransferSrc` usage。
  - destination buffer 必须是 Vulkan buffer 且包含 `TransferDst` usage。
  - source/destination offset + size 必须在 buffer 范围内。
  - 通过 `vkCmdCopyBuffer` 执行 copy。
  - copy 后根据 destination buffer usage 插入最小 buffer barrier，使 transfer write 对后续 vertex/index/uniform/storage 等读取可见。
- `CubePass` 的 vertex/index buffer 已迁移为 `GpuOnly | TransferDst` 目标 buffer，初始数据通过 `CpuToGpu | TransferSrc` staging buffer 在 `prepare()` 中首次上传。
- mesh staging buffer 在 0.7.4 中交给当前 frame 的 deferred deletion queue；统一 upload allocator 仍留到后续阶段。
- `VulkanBuffer` 仍显式拒绝 `GpuOnly initialData`，这是当前正确行为；调用方必须走 staging + `uploadBufferData()`。

### 0.7.4 staging 生命周期与 deferred deletion

工作内容：

- 明确 staging buffer 的释放策略。
- 优先实现最小 `VulkanDeletionQueue`，由 frame resource 持有并在对应 frame fence 完成后 flush。
- 如果暂不实现完整 delayed destruction，则保留 pass-local staging 但把风险记录到文档，并避免释放时机早于 GPU 完成。

推荐最小方向：

```text
Frame N records upload using staging resource
staging resource enters current frame deferred deletion list
Frame N fence signaled
current frame begins reuse
flush deferred deletion list
```

注意：

- deletion queue 不应依赖 renderer 高层对象。
- 如果对象析构仍可能发生在 GPU 使用期间，应优先由 owner 在安全时机释放，或调用 `device.waitIdle()` 作为阶段性保守策略。

验收：

- upload 资源不会在 GPU copy 执行前释放。
- resize / shutdown 不会留下明显 lifetime 风险。
- 文档明确哪些资源已经走 deferred deletion，哪些仍是阶段性保守策略。

当前实现状态：

- `VulkanDeletionQueue` 已从占位类变成最小延迟释放队列，当前只持有 `Scope<rhi::Buffer>`。
- `VulkanFrameResource` 继续持有 per-frame `deferredDeletion`，释放时机和 frame slot fence 对齐。
- `VulkanCommandContext::beginFrame()` 在等待当前 frame slot 的 in-flight fence signal 后 flush `deferredDeletion`，确保上一轮提交引用的 staging buffer 不会提前析构。
- 已新增 `DeviceContext::deferReleaseBuffer(Scope<Buffer>&)`，调用成功后当前 frame 的 deletion queue 接管 buffer 所有权，调用方的 `Scope` 置空。
- `CubePass` 的 vertex/index staging buffer 和 texture staging buffer 在首次 upload 成功后都会交给 `deferReleaseBuffer()`，不再由 pass 长期持有。
- sandbox validation 暴露了 swapchain binary semaphore 复用风险；已改为每个 swapchain image 持有独立 render-finished semaphore，并在 acquire 成功后交给当前 frame submit/present 使用。
- 当前 deferred deletion 只覆盖 upload staging buffer；texture、texture view、sampler、pipeline 等一般 GPU 对象仍依赖 owner 生命周期和 shutdown / resize 前的 `waitIdle()` 保守策略。

### 0.7.5 CubePass 迁移与纹理来源整理

工作内容：

- 保留 textured cube 作为默认验证路径。
- 将 `CubePass` 中的 checkerboard 数据整理成 `ImageData` 或同语义 CPU 数据结构。
- 让 `CubePass` 使用通用 texture upload 路径，而不是依赖 Phase 0.6 的特殊假设。
- 如果引入真实图片文件，使用 `asset::loadImageRgba8()`，并保留程序生成 checkerboard 作为 fallback。

验收：

- 默认 sandbox 仍能显示 textured cube。
- shader binding 仍为：

```text
set 0 binding 0: UniformBuffer
set 0 binding 1: SampledImage
set 0 binding 2: Sampler
```

- descriptor layout、descriptor writes 和 HLSL binding 完全一致。

当前实现状态：

- `CubePass` 的程序生成 checkerboard 已整理为 `asset::ImageData`，保持 CPU 数据语义。
- `CubePass` 优先通过 `asset::loadImageRgba8()` 加载 `assets/textures/xiaowei.png`，成功后按图片实际尺寸创建 RGBA8 texture 和 staging buffer。
- 如果真实图片不存在或解码失败，`CubePass` 会记录英文日志并 fallback 到程序生成 checkerboard，默认 sandbox 仍可运行。
- texture upload 继续使用 `TextureUploadDesc`，row pitch / bytes per pixel 由 `ImageData` 推导。
- texture staging buffer 在首次 upload 成功后继续交给 `deferReleaseBuffer()`，保持 0.7.4 的 deferred deletion 策略。
- `ark_sandbox` 构建后会把 `assets/` 复制到可执行文件目录，支持直接从 build output 运行 exe。
- 当前真实图片路径仍是阶段性硬编码；后续 asset root / resource handle 进入更完整资源系统时再统一整理。

### 0.7.6 DescriptorManager 可增长 pool

工作内容：

- 将固定容量 descriptor pool 演进为简单多 pool 策略。
- 当 `vkAllocateDescriptorSets` 因 pool 容量不足失败时，创建新 pool 并重试。
- 继续由 `VulkanDescriptorManager` 统一持有 pools。
- 暂不做 descriptor set 单独 free，也不做 frame-local reset。

验收：

- 普通 descriptor set 分配不再被单个固定 256 pool 卡死。
- pool 生命周期仍集中在 `VulkanDescriptorManager`。
- renderer/RHI 不知道 pool 增长细节。

当前实现状态：

- `VulkanDescriptorManager` 已从单个 `VkDescriptorPool` 改为内部持有 `std::vector<VkDescriptorPool>`。
- 构造时创建第一个 pool；普通分配优先使用当前最后一个 pool。
- 当 `vkAllocateDescriptorSets` 返回 `VK_ERROR_OUT_OF_POOL_MEMORY` 或 `VK_ERROR_FRAGMENTED_POOL` 时，创建新 pool 并重试一次。
- 每个 pool 仍沿用当前阶段固定容量：256 sets、256 uniform buffer、256 sampled image、256 sampler。
- descriptor set 不单独 free，descriptor pool 销毁时统一释放其分配出的 sets。
- 该策略只解决“set 数量增长导致当前 pool 耗尽”；如果单个 descriptor set 本身超过单 pool 配额，仍会明确失败。

## 代码阅读建议

Phase 0.7 审核时建议按下面顺序阅读：

1. `docs/design/framework.md` 与 `docs/design/module_responsibility.md`

   先确认职责边界：asset 只输出 CPU 数据，RenderDevice 创建资源，DeviceContext 记录 upload 命令，Vulkan 后端处理真实 barrier 和 copy。

2. `src/asset/TextureLoader.h/.cpp`

   看 `ImageData` 是否纯 CPU 数据，是否没有 RHI/Vulkan 依赖，失败路径日志是否足够清晰。

3. `src/rhi/DeviceContext.h`

   看 `TextureUploadDesc` / `BufferUploadDesc` 是否只表达 API 无关语义，是否把 row pitch、offset、extent 等关键约束说清楚。

4. `src/renderer/passes/CubePass.cpp`

   看 `prepare()` 是否仍在 `beginRendering()` 前执行 upload；看 staging buffer 是否通过通用路径保活；看 descriptor set 是否仍写入 binding 0/1/2。

5. `src/rhi/vulkan/VulkanCommandContext.cpp`

   看 `uploadTextureData()` 和 `uploadBufferData()` 的 copy、barrier、rendering scope 校验是否正确。重点确认 upload 不能发生在 dynamic rendering scope 内。

6. `src/rhi/vulkan/VulkanDeletionQueue.h/.cpp` 与 `VulkanFrameResource.h`

   看 deferred deletion 是否和 frame fence 对齐，是否会提前释放 GPU 仍在使用的资源。

7. `src/rhi/vulkan/VulkanDescriptorManager.cpp`

   看 descriptor pool 增长策略是否集中在后端，是否没有污染 RHI 公共接口。

8. `shaders/textured_cube.vert.hlsl` 与 `shaders/textured_cube.frag.hlsl`

   最后核对 shader binding 与 descriptor layout / descriptor write 是否一致。

## 审核检查点

- `asset/` 不依赖 `renderer/`、`rhi/` 或 `rhi/vulkan/`。
- `renderer/` 和 RHI 公共层不包含 Vulkan 头文件，不出现 `Vk*` 类型。
- LDR/HDR 加载路径必须显式区分，不能把 HDR 静默压到 RGBA8。
- upload 命令只在 render scope 外记录。
- texture upload barrier 仍保持 `Undefined -> CopyDst -> ShaderResource` 的语义。
- buffer upload 有明确 copy src / copy dst usage 和必要同步。
- staging buffer 生命周期长于 GPU copy 执行窗口。
- descriptor layout、descriptor write、shader binding 三者一致。
- `CubePass` 仍是阶段验证 pass，不扩展成材质系统或资源管理器。
- 新增日志使用英文。
- 不确定或暂不支持的能力写 TODO 或文档，不假装完成。

## 验证计划

每个子阶段完成后运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

涉及默认渲染路径变化后运行 sandbox smoke：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

建议至少做一次 RenderDoc 或 validation 检查：

- texture image 最终 layout 是否为 shader read。
- fragment shader 是否读取 binding 1 / binding 2。
- descriptor set 中 sampled image 和 sampler 是否有效。
- staging buffer 是否没有在 copy 完成前释放。
- GPU-only buffer copy 后 vertex/index draw 是否读取正确数据。

## 完成标准

Phase 0.7 完成时应满足：

- `TextureLoader` 能输出 LDR RGBA8 CPU image data，且不破坏 asset 层边界。
- HDR 输入会被显式拒绝或走独立 HDR TODO 路径，不会静默损失动态范围。
- texture upload API 比 Phase 0.6 更明确，至少表达 row pitch / pixel size / mip / array 的当前限制。
- GPU-only buffer upload 有最小可用闭环，后续 mesh upload 不再依赖 CPU-visible static buffers。
- staging upload 资源有明确生命周期策略，或以文档形式记录仍未完成的 deferred deletion 缺口。
- `CubePass` 继续渲染 textured cube，并使用更通用的 upload 路径。
- descriptor pool 固定容量风险被缓解或明确记录为后续 TODO。
- build、CTest 和 sandbox smoke 通过。

## 后续 Phase 建议

Phase 0.8 再考虑进入真实资产和材质方向：

- mesh CPU data / GPU buffer upload。
- 最小 material 描述。
- glTF 单 mesh + 单材质加载。
- ForwardPass 替代阶段性的 CubePass。

在 Phase 0.7 完成前，不建议直接进入完整 glTF / PBR，因为那会把 upload、descriptor、lifetime 和 asset 解析问题混在一起，调试成本会明显上升。
