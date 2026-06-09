# Phase 0.14 Texture Mipmap Upload / Generation 最小闭环

## 阶段判断

Phase 0.13 已经把运行期 model 资源 reset 推进到最小安全路径：

```text
ModelResource::resetDeferred(context)
    -> MaterialResource references removed
    -> MeshResource vertex/index buffer deferred release
    -> local TextureCache deferred clear
    -> VulkanDeletionQueue flush after frame fence
```

这说明当前 renderer 层的资源生命周期边界已经比 Phase 0.11 / 0.12 更稳定，可以继续推进 texture sampling 质量。但当前 texture 路径仍停留在最小 mip0 方案：

- `TextureResource` 创建 texture 时固定 `mipLevels = 1`。
- `TextureResource` 的 sampler `mipFilter` 仍是 `Nearest`。
- `DeviceContext::uploadTextureData()` 只支持 mip0 / array layer 0。
- Vulkan upload 路径是 `Undefined -> CopyDst -> ShaderResource`，没有 per-mip barrier。
- glTF material 和 shader 仍只采样 base color texture，没有 PBR 或多 texture 语义。

因此 Phase 0.14 不应直接进入完整 PBR、normal map 或 RenderGraph，而应先补齐 mipmap upload / generation 的最小闭环。目标是让真实 2D texture 可以创建完整 mip chain，上传 mip0 后由 GPU 生成后续 mip，并在 fragment shader 中按 sampler mip filter 正常采样。

## 目标

Phase 0.14 的目标是在不扩大材质系统复杂度的前提下完成以下能力：

- `TextureResource` 支持按 texture 尺寸计算完整 mip count。
- RHI 暴露最小 `generateTextureMips()` 或等价接口，保持 API 无关。
- Vulkan 后端支持对 2D RGBA8 / RGBA8 sRGB texture 执行 GPU mipmap generation。
- mip generation 必须记录在 dynamic rendering scope 外。
- 需要生成 mip 的 texture usage 包含 `TransferSrc | TransferDst | ShaderResource`。
- mip0 仍通过现有 staging upload 路径上传。
- mip0 upload 后不立即把整张 texture 固定为 shader read，而是按 mip generation 流程完成最终 layout。
- sampler 切换到合理的 mip filter，默认使用 linear mip filtering。
- tests 覆盖 mip count 计算、mip generation 调用、staging deferred release 和现有 texture cache 行为不回退。
- 文档记录剩余限制：HDR、压缩纹理、离线 mip、row pitch 扩展、array/cubemap、PBR material 仍留给后续 phase。

## 非目标

Phase 0.14 暂不做以下内容：

- 不做 PBR shader。
- 不做 glTF normal / metallicRoughness / occlusion / emissive texture。
- 不做 glTF sampler 参数解析。
- 不做 HDR texture。
- 不做 BC / ASTC / ETC 压缩纹理。
- 不做离线 mip 数据读取。
- 不做 cubemap、texture array 或 3D texture。
- 不做 transfer queue、异步上传线程或 timeline semaphore。
- 不做 bindless、descriptor indexing 或 global texture array。
- 不做 RenderGraph。
- 不把 `assets/models/DamagedHelmet/` 直接切成默认 sandbox 资源。

`DamagedHelmet` 可以作为后续真实资产验证对象，但在当前 baseColor-only shader 下不应作为默认正确性标准。Phase 0.14 只保证 mipmap 采样链路正确。

## 模块边界

Phase 0.14 继续遵守：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase11.md
docs/phase/phase12.md
docs/phase/phase13.md
```

硬性边界：

- `asset/` 继续只输出 CPU image / mesh / material 数据，不创建 GPU mipmap。
- `TextureLoader` 不判断 GPU mip count，不参与 sampler 或 Vulkan layout。
- `renderer/` 可以决定是否为 `TextureResource` 生成 mip，但不接触 Vulkan 类型。
- `rhi/` 提供 API 无关的 mip generation 命令接口。
- `rhi/vulkan/` 实现实际 `vkCmdBlitImage`、image barrier 和 format capability 检查。
- upload / mip generation / deferred release 都必须在 command recording 阶段、dynamic rendering scope 外发生。
- `ForwardPass` 不接管 texture lifetime，只在 `prepare()` 间接触发 resource upload。
- 当前继续使用 Vulkan Dynamic Rendering，不引入传统 `VkRenderPass` / `VkFramebuffer`。

## 推荐数据流

Phase 0.14 目标数据流：

```text
TextureResource::create(device, image, desc)
    -> calculate mipLevels from image extent
    -> create staging buffer for mip0 pixels
    -> create texture with mipLevels and TransferSrc/TransferDst/ShaderResource usage
    -> create texture view covering all mip levels
    -> create sampler with mip filtering

ForwardPass::prepare()
    -> MaterialResource::upload()
        -> TextureResource::upload(context)
            -> uploadTextureData(mip0)
                -> transition mip0 to CopyDst
                -> copy staging buffer to mip0
            -> generateTextureMips(texture)
                -> for each src/dst mip:
                    -> src mip ShaderResource/CopyDst -> TransferSrc
                    -> dst mip Undefined/CopyDst -> TransferDst
                    -> vkCmdBlitImage
                    -> src mip -> ShaderResource
                -> final mip -> ShaderResource
            -> deferReleaseBuffer(staging)

ForwardPass::execute()
    -> descriptor samples texture view with full mip chain
    -> shader uses regular Texture2D.Sample()
```

实现时要避免把 mip generation 设计成 `TextureResource` 私有 Vulkan 逻辑。`TextureResource` 只提出 RHI 命令请求，具体 barrier / blit 由 Vulkan 后端处理。

## 设计建议

### rhi::TextureDesc / TextureUsage

当前 `TextureDesc` 已有 `mipLevels` 字段，Phase 0.14 应继续沿用，不新增重复字段。

建议补齐 helper：

```cpp
u32 calculateMipLevelCount(rhi::Extent2D extent);
```

语义：
- `1x1` 返回 1。
- 非 2 次幂尺寸也支持，逐级 `max(1, size / 2)`。
- extent 无效时返回 0 或由调用方拒绝创建。

需要 mip generation 的 texture usage：

```text
TransferSrc | TransferDst | ShaderResource
```

原因：
- `TransferDst` 用于 mip0 upload 和后续 dst mip blit。
- `TransferSrc` 用于上一层 mip 作为 blit source。
- `ShaderResource` 用于最终 fragment sampling。

### rhi::DeviceContext

建议新增最小接口：

```cpp
virtual bool generateTextureMips(Texture& texture) = 0;
```

接口语义：
- 只负责 GPU 端从已有 mip0 生成剩余 mip。
- 调用前 mip0 必须已经有有效像素数据。
- texture 必须有 `mipLevels > 1`。
- texture 必须支持 `TransferSrc | TransferDst | ShaderResource`。
- 命令必须记录在 dynamic rendering scope 外。
- 成功后 texture 整体语义为 `ShaderResource`。

如果 `mipLevels == 1`，可以返回 true 并保持当前单 mip 行为。

不建议把 mip generation 塞进 `uploadTextureData()`。原因是后续会存在离线 mip upload、partial subresource upload、streaming mip 等路径，`uploadTextureData()` 保持“上传一个 subresource”的语义更清晰。

### VulkanCommandContext

Vulkan 后端建议实现：

```text
validate texture handle / desc / usage / format
validate command recording active
reject inside dynamic rendering scope
check format supports linear blit if using vkCmdBlitImage
for level in 1..mipLevels-1:
    transition level-1 to TransferSrc
    transition level to TransferDst
    vkCmdBlitImage(level-1 -> level)
    transition level-1 to ShaderResource
transition last level to ShaderResource
```

需要注意：
- `VK_FILTER_LINEAR` blit 需要检查 format feature。RGBA8 和 RGBA8 sRGB 通常可用，但不能假设所有格式都支持。
- 当前只支持 color 2D texture，aspect 固定 color。
- 当前不支持 depth/stencil、compressed format、array layer、cubemap。
- 如果后端 `pipelineBarrier()` 目前只能处理整张 texture 状态，Phase 0.14 需要谨慎设计 per-mip barrier。可以新增 Vulkan 内部 helper，不一定要把 per-subresource barrier 暴露给公共 RHI。
- texture 内部当前若只记录一个整体 `ResourceState`，mip generation 过程中可以在 Vulkan 内部处理 subresource transition，最后把整体 state 设置为 `ShaderResource`。

### TextureResource

建议扩展 `TextureResourceDesc`：

```cpp
bool generateMips = true;
```

创建逻辑：
- 根据 `generateMips` 和 image extent 计算 `mipLevels`。
- `generateMips == false` 时保持 `mipLevels = 1`。
- 使用 full mip view。
- sampler `mipFilter` 建议改为 `Linear`。

上传逻辑：
- mip0 使用现有 staging upload。
- `mipLevels > 1` 时调用 `context.generateTextureMips(*m_Texture)`。
- staging buffer 的 deferred release 仍在 upload / mip generation 命令记录完成后执行。
- upload 成功后 `m_Uploaded = true`。

建议保留默认生成 mip，原因是真实 glTF texture 大多需要 mipmap；但 tests 中可显式关闭，用于覆盖单 mip 路径。

### TextureCache / MaterialResource

Phase 0.14 不建议扩大 `TextureCache` key。默认所有 glTF baseColor texture 都走同一种 mip 策略即可：

```text
key = normalized path + colorSpace
```

如果后续 sampler 参数、mip bias、anisotropy 或 texture transform 接入，再评估 key 是否需要包含 sampler desc。当前不应提前把 cache 复杂化。

`MaterialResource` 不需要直接知道 mipmap，只继续引用 `TextureResource*` 并写 descriptor。

### shader

当前 HLSL：

```hlsl
g_BaseColorTexture.Sample(g_BaseColorSampler, input.uv0)
```

这已经会走 sampler mip selection。Phase 0.14 不需要改 shader，除非为了 debug 增加临时 mip visualization；如果加 debug shader，不应替换默认渲染路径。

## 实施顺序

### 0.14.0 文档与范围确认

目标：
- 新增 `docs/phase/phase14.md`。
- 明确主线是 mipmap upload / generation。
- 明确不进入 PBR、normal map、RenderGraph 或完整 ResourceManager。

当前实现状态：

- 已创建。
- 0.14.1 已开始补齐 mip count 与 `TextureResource` 描述。

### 0.14.1 Mip count 与 TextureResource 描述补齐

目标：
- 新增 mip count helper。
- `TextureResourceDesc` 增加最小 mip 策略字段。
- `TextureResource` 创建 texture 时使用正确 mipLevels 和 usage。

审核检查点：
- 单 mip 路径不回退。
- 非 2 次幂尺寸 mip count 正确。
- `generateMips == true` 时 texture usage 包含 `TransferSrc`。
- renderer 层不出现 Vulkan 类型。

当前实现状态：

- 已新增 `rhi::calculateMipLevelCount(extent)`。
- `TextureResourceDesc` 已新增 `generateMips`，当前默认值保持 `false`，避免 mip generation 命令落地前默认路径采样未初始化 mip。
- `TextureResource` 已能按描述创建 full mip texture、full mip view，并在多 mip 时启用 `TransferSrc` usage 和 linear mip filter。
- `TextureResource::upload()` 当前会明确拒绝多 mip texture upload；真正 upload 后生成 mip 的路径留给 0.14.2 ~ 0.14.4。
- `ark_model_resource_smoke` 已覆盖 mip count、full mip texture/view/sampler 描述，以及多 mip upload 在 generation 支持前不会假装成功。

### 0.14.2 RHI generateTextureMips 接口

目标：
- `DeviceContext` 增加 API 无关接口。
- fake test context 补齐实现与统计。
- VulkanCommandContext 增加声明与实现入口。

审核检查点：
- `mipLevels == 1` 行为明确。
- 调用时机要求写清楚：active command recording，dynamic rendering scope 外。
- 不把 mip generation 混入 `uploadTextureData()`。

### 0.14.3 Vulkan mip generation

目标：
- Vulkan 后端实现 per-mip blit。
- 增加 format feature 检查。
- 完成必要 layout transition。
- 成功后 texture state 为 `ShaderResource`。

审核检查点：
- `vkCmdBlitImage` source/destination layout 正确。
- 每个 mip level 的 extent 使用 `max(1, previous / 2)`。
- 不在 rendering scope 内执行。
- unsupported format 明确报错，不假装生成成功。

### 0.14.4 TextureResource upload 接入

目标：
- mip0 upload 后触发 mip generation。
- sampler 切换到 mip filtering。
- staging buffer 仍 deferred release。

审核检查点：
- upload 命令、mip generation 命令、staging deferred release 顺序正确。
- `m_Uploaded` 只在完整流程成功后置 true。
- `TextureResource::releaseDeferred()` 仍覆盖 texture/view/sampler/staging。

### 0.14.5 Tests 与 sandbox smoke

目标：
- smoke tests 覆盖 mip count。
- fake context 验证 mip generation 调用次数。
- 现有 texture cache / model resource / render queue tests 不回退。
- 默认 sandbox smoke 继续通过。

建议运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

如涉及默认渲染路径，继续运行 sandbox smoke。

### 0.14.6 Phase 0.14 收尾

目标：
- 更新本文档当前实现状态。
- 按需同步 `docs/codex_handoff.md`。
- 记录剩余 TODO：PBR material、多 texture、HDR、压缩纹理、离线 mip、texture streaming。

## 审核检查点

- `asset/` 不参与 mip generation。
- `TextureLoader` 仍只输出 CPU RGBA8 image data。
- `renderer/` 只通过 RHI 接口请求 mip generation。
- `rhi/` 不暴露 Vulkan 类型。
- Vulkan mip generation 不进入 dynamic rendering scope。
- format capability 检查清晰。
- texture usage 包含 mip generation 所需 usage。
- texture view 覆盖完整 mip range。
- sampler mip filter 与 mip chain 匹配。
- mip generation 失败时不会把 texture 标记为 uploaded。
- staging buffer 生命周期仍与 frame fence deferred deletion 对齐。
- 现有 baseColor-only material 行为不回退。

## 验证计划

实现后运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

涉及默认渲染路径时运行 sandbox smoke：

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

Phase 0.14 完成时应满足：

- `TextureResource` 可以创建完整 mip chain。
- RHI 有明确 mip generation 命令接口。
- Vulkan 后端可以通过 GPU blit 生成 2D RGBA8 / RGBA8 sRGB mipmaps。
- upload / mip generation 均发生在 dynamic rendering scope 外。
- descriptor sampling 可以使用完整 mip view。
- sampler mip filter 不再停留在无意义的单 mip 配置。
- build、ctest 和必要 sandbox smoke 通过。
- 文档记录剩余限制和后续阶段建议。

## 后续 Phase 建议

Phase 0.14 完成后，建议继续：

1. glTF material 参数扩展：`baseColorFactor`、`metallicFactor`、`roughnessFactor`。
2. glTF 多 texture 支持：normal、metallicRoughness、emissive，明确 sRGB / linear 语义。
3. 最小基础光照或 PBR shader。
4. 使用 `assets/models/DamagedHelmet/` 做真实 glTF 2.0 资产验证。
5. renderer 级资源 / scene 加载入口，替代内部默认 scene 过渡方案。
6. pipeline / shader / descriptor layout 的 deferred destruction。
7. RenderGraph 第一版 pass/resource 声明。
