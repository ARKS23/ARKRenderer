# Phase 0.11 TextureResource / TextureCache 与 sRGB 最小闭环

## 阶段判断

Phase 0.10 已经完成 `glTF scene/node transform -> ModelResource instance -> RenderQueue -> ForwardPass` 的最小闭环。当前默认渲染路径已经可以绘制一个包含多个 node instance 的 glTF 2.0 fixture：

```text
glTF 2.0 file
    -> asset::ModelData
    -> ModelResource
    -> RenderScene
    -> RenderQueue
    -> ForwardPass
    -> indexed textured multi draw
```

这说明 scene、queue、camera 和 draw 边界已经基本稳定。下一步不应直接进入完整 PBR、RenderGraph 或 bindless，而应先收束 texture resource 的 ownership 和复用边界。

当前 texture/material 侧仍有几个直接影响后续真实资产能力的问题：

- `MaterialResource` 同时负责图片加载、GPU texture/view/sampler 创建、staging upload、descriptor 写入和上传状态管理。
- 多个 material 指向同一 texture path 时会重复创建 GPU texture、view、sampler 和 staging buffer。
- glTF baseColor texture 默认应按 sRGB 采样，但当前 RHI 只有 `RGBA8Unorm` 路径。
- `uploadTextureData()` 仍只覆盖 RGBA8、mip0、array layer 0、tightly packed rows。
- GPU deferred deletion 当前主要覆盖 upload staging buffer，完整 GPU object 延迟销毁尚未系统化。

因此 Phase 0.11 的主线是：在不引入完整 ResourceManager 的前提下，新增最小 `TextureResource` / `TextureCache`，让 material 引用 texture resource，并明确 sRGB 与 mipmap 的后续边界。

## 目标

Phase 0.11 的目标是在保持现有架构边界的前提下完成以下能力：

- 新增 renderer 层 `TextureResource`，集中管理 base color texture 的 GPU 资源和首次 upload 状态。
- 新增最小 `TextureCache`，按规范化 texture path 复用同一张 texture。
- `MaterialResource` 不再直接加载图片或拥有 texture staging buffer，只保存材质参数和 texture 引用。
- RHI 增加 `RGBA8Srgb` 格式，并在 Vulkan 后端映射到 `VK_FORMAT_R8G8B8A8_SRGB`。
- glTF baseColor texture 默认创建为 sRGB texture。
- 保持 upload 仍在 dynamic rendering scope 外执行。
- 保持 descriptor 仍使用 separate sampled image / sampler binding。
- 增加或扩展 fixture / tests，验证两个 material 共享同一 base color texture 时只创建一个 `TextureResource`。
- 记录 mipmap、HDR、压缩纹理、完整 ResourceManager 和完整 deferred destruction 的后续边界。

## 非目标

Phase 0.11 暂不做以下内容：

- 不做完整 PBR material 参数。
- 不做 normal / metallicRoughness / occlusion / emissive texture。
- 不做 texture array、bindless descriptor 或 descriptor indexing。
- 不做完整 ResourceManager handle 系统。
- 不做 hot reload、asset database 或异步 streaming。
- 不做 transfer queue、timeline semaphore 或后台上传线程。
- 不做完整 mipmap 生成。
- 不做 HDR texture、BC/ASTC/ETC 压缩纹理。
- 不做 glTF KHR_texture_transform、KHR_materials_* 扩展。
- 不做完整 GPU object deferred destruction。
- 不清理 `CubePass`，除非它直接阻塞 texture resource 边界。

这些内容应等 texture ownership、color space 和资源复用边界稳定后再进入后续阶段。

## 模块边界

Phase 0.11 继续遵守：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase07.md
docs/phase/phase08.md
docs/phase/phase09.md
docs/phase/phase10.md
```

硬性边界：

- `asset/` 只负责读取外部文件并输出 CPU 数据，不创建 RHI/GPU 资源。
- `TextureLoader` 继续输出 `ImageData`，不判断 Vulkan 格式，不持有 sampler，不参与 descriptor。
- `renderer/` 可以创建和持有 RHI 资源，但不暴露或使用 Vulkan 类型。
- `TextureResource` 属于 renderer 层，不属于 asset 层。
- `TextureCache` 的 key 初期使用规范化后的 texture path，不使用 Vulkan handle 或原生对象。
- `MaterialResource` 可以引用 `TextureResource`，但不应再直接执行文件读取。
- `ForwardPass` 仍只负责 pipeline、descriptor、uniform binding 和 draw，不负责 texture 查找或文件加载。
- upload 仍必须在 dynamic rendering scope 外记录。
- draw 仍必须在 dynamic rendering scope 内发生。

如果实现方向需要打破上述边界，先更新设计文档，再修改代码。

## 推荐数据流

Phase 0.11 目标数据流：

```text
glTF 2.0 file
    -> asset::ModelData
        -> asset::MaterialData(baseColorTexturePath)
    -> renderer::ModelResource::create(...)
        -> TextureCache::getOrCreate(baseColorTexturePath, TextureColorSpace::Srgb)
            -> TextureLoader::loadRgba8(path)
            -> TextureResource::create(...)
                -> RHI Texture / TextureView / Sampler / StagingBuffer
        -> MaterialResource::create(material, textureRef)
    -> ForwardPass::prepare()
        -> MeshResource::upload()
        -> TextureResource::upload()
        -> MaterialResource::updateDescriptorSet()
    -> ForwardPass::execute()
        -> bind descriptor
        -> drawIndexed
```

说明：

- CPU 图片解码仍通过 `asset::TextureLoader` 完成。
- GPU texture 创建和 upload 状态收敛到 `TextureResource`。
- `MaterialResource` 不再知道图片如何从磁盘加载，只知道当前 material 使用哪一个 texture resource。
- `TextureCache` 只负责复用 texture resource，不负责 draw queue 或 pass 调度。
- 初期 cache 可以由 `Renderer` 或 `ModelResource` 持有；不要为了一个 texture cache 直接引入完整 handle 系统。

## 数据结构建议

以下是设计草案，实际实现应贴合现有代码风格。

### renderer::TextureResource

建议新增：

```text
src/renderer/TextureResource.h
src/renderer/TextureResource.cpp
```

建议职责：

- 根据 `asset::ImageData` 和 texture 创建描述创建 RHI texture。
- 持有 texture/view/sampler/staging buffer。
- 记录 texture extent、format、rowPitch、bytesPerPixel。
- 在 `upload()` 中执行首次 texture upload。
- upload 成功后把 staging buffer 交给 frame-local deferred deletion。
- 对外提供 descriptor 所需的 `TextureView` 与 `Sampler`。

建议接口方向：

```cpp
enum class TextureColorSpace {
    Linear,
    Srgb,
};

struct TextureResourceDesc {
    Path path;
    TextureColorSpace colorSpace = TextureColorSpace::Linear;
    std::string debugName;
};

class TextureResource final {
public:
    bool create(rhi::RenderDevice& device, const asset::ImageData& image, const TextureResourceDesc& desc);
    bool upload(rhi::DeviceContext& context);
    bool isReady() const;

    rhi::TextureView* textureView() const;
    rhi::Sampler* sampler() const;
};
```

注意：

- `TextureResource` 可以先只支持 2D RGBA8。
- baseColor 使用 `TextureColorSpace::Srgb`。
- sampler 初期仍使用 linear filter + repeat address。
- mipLevels 初期仍为 1。

### renderer::TextureCache

建议新增：

```text
src/renderer/TextureCache.h
src/renderer/TextureCache.cpp
```

建议职责：

- 维护 `Path -> TextureResource` 的最小映射。
- 统一调用 `TextureLoader` 解码图片。
- 避免同一路径重复创建 GPU texture resource。
- 提供测试可见的 cache size 或 contains 查询。

建议接口方向：

```cpp
class TextureCache final {
public:
    TextureResource* getOrCreate(rhi::RenderDevice& device, const TextureResourceDesc& desc);
    void clear();
    usize size() const;
};
```

key 策略：

- 初期使用 `std::filesystem::weakly_canonical()` 或项目已有 path 规范化工具。
- 如果规范化失败，则退回 normalized lexical path。
- key 需要包含 color space，避免同一路径同时以 sRGB 和 linear 方式创建时发生错误复用。

示例：

```text
TextureCacheKey {
    canonicalPath
    colorSpace
}
```

### renderer::MaterialResource

Phase 0.11 后建议收缩为：

- 保存 material debugName。
- 保存 `TextureResource*` 或稳定引用。
- 在 `upload()` 中触发 texture upload，或由 `ModelResource` 统一触发 texture upload。
- 在 `updateDescriptorSet()` 中写入 texture view / sampler descriptor。

注意：

- 不再直接调用 `asset::loadImageRgba8()`。
- 不再直接持有 `m_TextureStagingBuffer`、`m_Texture`、`m_TextureView`、`m_Sampler`。
- 如果 texture resource 缺失，明确失败，不使用隐藏 fallback。

### renderer::ModelResource

`ModelResource::create()` 需要能访问 texture cache：

```cpp
bool create(rhi::RenderDevice& device, TextureCache& textureCache, const asset::ModelData& model);
```

为了减少一次性重构，也可以先保留旧接口，并新增重载：

```cpp
bool create(rhi::RenderDevice& device, const asset::ModelData& model);
bool create(rhi::RenderDevice& device, TextureCache& textureCache, const asset::ModelData& model);
```

旧接口可内部使用一个 local cache，但默认渲染路径应逐步迁移到显式 cache。

### rhi::Format

建议新增：

```cpp
enum class Format {
    ...
    RGBA8Unorm,
    RGBA8Srgb,
    ...
};
```

Vulkan 后端映射：

```text
RGBA8Unorm -> VK_FORMAT_R8G8B8A8_UNORM
RGBA8Srgb  -> VK_FORMAT_R8G8B8A8_SRGB
```

注意：

- CPU decoded pixels 仍是 8-bit RGBA 数据。
- sRGB 是 GPU sampling/format 语义，不是 CPU loader 输出格式。
- `ImageData::Rgba8Unorm` 可以继续表达 CPU 字节布局；renderer 层根据 material 语义选择 RHI format。

## 实施顺序

### 0.11.0 文档与范围确认

目标：

- 新增 `docs/phase/phase11.md`。
- 明确主线是 `TextureResource`、`TextureCache` 和 `RGBA8Srgb`。
- 明确不进入完整 PBR、RenderGraph、bindless、mipmap 或 ResourceManager。

当前实现状态：

- 已完成。
- 已确认 Phase 0.11 主线聚焦 `TextureResource`、`TextureCache` 和 `RGBA8Srgb`。
- 0.11.0 到 0.11.6 已完成。

### 0.11.1 RHI RGBA8Srgb 格式

目标：

- 在 `rhi::Format` 中新增 `RGBA8Srgb`。
- Vulkan 后端补充 `VkFormat` 映射、format name、reverse mapping。
- 不改变 swapchain 或 depth 路径。

审核检查点：

- RHI 层不暴露 Vulkan 类型。
- `RGBA8Unorm` 现有路径不回退。
- `RGBA8Srgb` 只改变 GPU texture format，不改变 CPU loader。

当前实现状态：

- 已完成。
- `rhi::Format` 新增 `RGBA8Srgb`。
- Vulkan 后端已补充 format name、`toVkFormat()` 和 `fromVkFormat()` 映射。
- `uploadTextureData()` 已允许 RGBA8 Unorm / sRGB 两种 GPU texture format，CPU upload 字节布局仍保持 RGBA8。

### 0.11.2 TextureResource 最小封装

目标：

- 新增 `TextureResource`。
- 从当前 `MaterialResource` 中迁移 texture/view/sampler/staging/upload 相关逻辑。
- 保持 upload 在 `ForwardPass::prepare()` 触发的 render scope 外命令阶段执行。

审核检查点：

- `TextureResource` 不依赖 glTF。
- `TextureResource` 不持有 Vulkan 原生对象。
- staging buffer upload 成功后仍走 frame-local deferred deletion。
- mipLevels 仍为 1，并记录 TODO。

当前实现状态：

- 已完成。
- 新增 `src/renderer/TextureResource.h/.cpp`。
- `TextureResource` 负责创建 texture/view/sampler/staging buffer，并在首次 `upload()` 后延迟释放 staging buffer。
- baseColor sRGB 语义通过 RHI format 表达，CPU `ImageData` 仍保持 `Rgba8Unorm` 字节布局。

### 0.11.3 TextureCache 最小复用

目标：

- 新增 `TextureCache`。
- `getOrCreate()` 通过 path + colorSpace 复用 texture resource。
- `ModelResource` 创建 material 时通过 cache 获取 texture。

审核检查点：

- cache 不属于 asset 层。
- cache key 不使用 raw path 字符串直接比较，应尽量规范化。
- 同一路径同一 colorSpace 只创建一个 `TextureResource`。
- cache lifetime 不短于引用它的 `MaterialResource`。

当前实现状态：

- 已完成。
- 新增 `src/renderer/TextureCache.h/.cpp`。
- cache key 使用规范化 path + `TextureColorSpace`。
- `ModelResource` 新增显式 `create(device, textureCache, model)` 重载，并保留旧接口的 local cache 兼容路径。

### 0.11.4 MaterialResource 职责收缩

目标：

- `MaterialResource` 不再直接加载图片。
- `MaterialResource` 不再直接拥有 texture/view/sampler/staging。
- descriptor 写入仍由 `MaterialResource::updateDescriptorSet()` 完成，保持 ForwardPass 简洁。

审核检查点：

- material 层仍不暴露 Vulkan 类型。
- descriptor binding 继续与 `mesh.frag.hlsl` 一致。
- texture 缺失时明确失败，不隐式生成 fallback。

当前实现状态：

- 已完成。
- `MaterialResource` 不再直接调用 `TextureLoader`。
- `MaterialResource` 不再拥有 texture/view/sampler/staging buffer，只保存 `TextureResource*` 引用。
- descriptor 写入仍保留在 `MaterialResource::updateDescriptorSet()`，ForwardPass 职责未扩张。

### 0.11.5 cache fixture 与测试

目标：

- 增加或扩展 glTF fixture：两个 material 指向同一张 texture。
- 扩展 smoke test，验证 texture cache size 或 resource 复用。
- sandbox 默认路径可以暂时保持 `forward_multinode_fixture.gltf`，除非新 fixture 更适合覆盖默认路径。

建议测试：

- `TextureCache` 同一路径同一 colorSpace 返回同一 resource。
- 同一路径不同 colorSpace 返回不同 resource。
- `ModelResource` 多 material 共用 texture 时 cache size 为 1。
- 现有 glTF loader、model resource、sandbox smoke 继续通过。

当前实现状态：

- 已完成。
- 新增 `assets/models/texture_cache_fixture.gltf`，两个 material 通过不同 texture slot 指向同一个 image。
- `ark_gltf_loader_smoke` 已验证 fixture 解析为两个 material、两个 primitive，并解析到同一个 baseColor texture path。
- `ark_model_resource_smoke` 已验证该 fixture 通过 `TextureCache` 只创建一个 sRGB texture/view/sampler，并保持两个 draw item。

### 0.11.6 Phase 0.11 收尾

目标：

- 更新 `docs/phase/phase11.md` 当前实现状态。
- 按用户要求再决定是否同步 `docs/codex_handoff.md`。
- 记录剩余 TODO：mipmap、HDR、压缩纹理、完整 GPU object deferred destruction、完整 ResourceManager。

当前实现状态：

- 已完成。
- 0.11.0 到 0.11.6 已完成。
- 已同步 `docs/codex_handoff.md` 到 Phase 0.11 完成后的交接状态。
- mipmap、HDR、压缩纹理、完整 GPU object deferred destruction、完整 ResourceManager 仍保留为后续 TODO。

## 审核检查点

- `asset/` 没有创建 RHI/GPU 资源。
- `TextureResource` 只使用 RHI 公共接口，不使用 Vulkan 类型。
- `TextureCache` key 包含 color space。
- baseColor texture 默认使用 `RGBA8Srgb`。
- non-color texture 语义暂不扩展，避免误用 sRGB。
- upload 不在 dynamic rendering scope 内执行。
- descriptor layout 与 shader binding 保持一致。
- 多个 material 共用同一 texture path 时不会重复创建 GPU texture。
- staging buffer 仍延迟到 frame fence 完成后释放。
- 不引入过重 ResourceManager 或 handle 系统。

## 验证计划

代码实现后运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

当前验证记录：

- `cmake --build --preset msvc-vcpkg-local-debug`: passed.
- `ctest --preset msvc-vcpkg-local-debug`: 8/8 passed.
- `sandbox smoke`: passed.

如涉及默认 sandbox 渲染路径，继续运行 sandbox smoke：

```powershell
$stdout = "build\msvc-vcpkg\ark_sandbox_stdout.log"
$stderr = "build\msvc-vcpkg\ark_sandbox_stderr.log"
Remove-Item -LiteralPath $stdout,$stderr -Force -ErrorAction SilentlyContinue
$process = Start-Process -FilePath "build\msvc-vcpkg\Debug\ark_sandbox.exe" -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr
Start-Sleep -Seconds 3
if ($process.HasExited) {
    Write-Output "ark_sandbox exited early with code $($process.ExitCode)"
    Get-Content -Path $stdout,$stderr -ErrorAction SilentlyContinue
    exit $process.ExitCode
} else {
    Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300
    Write-Output "ark_sandbox started successfully and was stopped after smoke check"
    Get-Content -Path $stdout,$stderr -ErrorAction SilentlyContinue
}
```

## 完成标准

Phase 0.11 完成时应满足：

- `TextureResource` 已接管 texture/view/sampler/staging/upload。
- `TextureCache` 可以复用同一路径同一 colorSpace 的 texture resource。
- `MaterialResource` 不再直接读取图片文件。
- glTF baseColor texture 使用 `RGBA8Srgb` RHI format。
- 现有 Phase 0.10 scene/camera/multi draw 行为不回退。
- build、ctest 和必要 sandbox smoke 通过。
- 文档记录清楚 mipmap、HDR、压缩纹理、完整 deferred destruction 的后续边界。

## 后续 Phase 建议

Phase 0.11 完成后，建议按以下顺序继续：

1. 更完整的 GPU object deferred destruction，覆盖 texture/view/sampler/pipeline 等运行期释放对象。
2. mipmap upload / generation 策略，包含 `TransferSrc` usage、per-mip barrier 和 blit 支持。
3. PBR material 最小参数和基础光照。
4. glTF normal / metallicRoughness / emissive texture 支持。
5. 真正的资源/场景加载入口，替代 renderer 内部默认 sandbox scene。
6. RenderGraph 第一版 pass/resource 声明。

不建议在 Phase 0.11 完成前直接进入完整 PBR 或 RenderGraph，因为当前更关键的是 texture resource ownership、color space 和 cache 生命周期。
