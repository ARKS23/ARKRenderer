# Phase 0.12 GPU Object Deferred Destruction 最小闭环

## 阶段判断

Phase 0.11 已经完成 texture resource ownership 和 cache 复用边界：

```text
asset::ImageData
    -> TextureResource
        -> RHI Texture / TextureView / Sampler / StagingBuffer
    -> TextureCache(path + colorSpace)
    -> MaterialResource(texture reference)
    -> ForwardPass descriptor + draw
```

这让 material 不再直接加载图片，也避免多个 material 指向同一 texture 时重复创建 GPU texture。下一步不应直接进入 mipmap 或 PBR，因为 resource unload / reload 之前还缺少一个更基础的约束：运行期释放 GPU object 必须等待 GPU 不再使用它。

当前已有 `VulkanDeletionQueue`，但它只覆盖 upload staging buffer：

```text
DeviceContext::deferReleaseBuffer()
    -> VulkanDeletionQueue::deferReleaseBuffer()
    -> frame fence signal 后 flush()
```

这对 upload staging 足够，但对 `TextureResource`、`TextureCache`、后续 mipmap、材质替换和场景切换不够。当前 texture/view/sampler 等对象仍主要依赖 `Scope<T>` 析构立即销毁。默认 shutdown 之前会 `waitIdle()`，所以当前 sandbox 路径相对安全；但一旦进入 runtime clear / reload / resource replacement，即时析构就可能早于 GPU 完成上一帧读取。

因此 Phase 0.12 的主线是：在不引入完整 ResourceManager 的前提下，补齐最小 GPU object deferred destruction，让 renderer 资源可以在运行期安全延迟释放。

## 目标

Phase 0.12 的目标是在保持现有架构边界的前提下完成以下能力：

- 扩展 RHI deferred release 接口，使 buffer、texture、texture view、sampler 都可以延迟释放。
- 扩展 Vulkan frame-local deletion queue，保存待释放 GPU object 到对应 frame fence signal 后再析构。
- `TextureResource` 支持运行期 deferred release。
- `TextureCache` 支持运行期 deferred clear，避免 cached texture 被 GPU 使用时立即销毁。
- 保持 shutdown 路径仍可在 `waitIdle()` 后 immediate clear。
- 保持 upload staging buffer 现有延迟释放行为不回退。
- 增加 smoke tests，验证 deferred release 转移 ownership，而不是直接析构。
- 记录 pipeline、shader、descriptor layout、descriptor pool、ResourceManager handle 的后续边界。

## 非目标

Phase 0.12 暂不做以下内容：

- 不做完整 ResourceManager handle 系统。
- 不做引用计数、asset database、hot reload 或异步 streaming。
- 不做 texture cache unload 策略、LRU、内存预算或统计 UI。
- 不做 mipmap generation。
- 不做 PBR material。
- 不做 normal / metallicRoughness / occlusion / emissive texture。
- 不做 bindless descriptor、descriptor indexing 或 global texture array。
- 不做 RenderGraph 自动 barrier、transient resource aliasing 或 async compute。
- 不做 pipeline、shader module、descriptor set layout 的完整 deferred destruction；本阶段只记录后续扩展点。

这些内容应等最小 GPU object delayed destruction 稳定后再进入后续阶段。

## 模块边界

Phase 0.12 继续遵守：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase07.md
docs/phase/phase08.md
docs/phase/phase09.md
docs/phase/phase10.md
docs/phase/phase11.md
```

硬性边界：

- `asset/` 不参与 GPU resource 生命周期。
- `renderer/` 可以请求 deferred release，但不能接触 Vulkan 类型。
- `rhi/` 提供 API 无关的 deferred release 接口。
- `rhi/vulkan/` 持有实际 `VulkanDeletionQueue`，并决定何时 flush。
- deferred release 必须和 frame fence 对齐。
- upload、copy、resource release 语义仍不进入 dynamic rendering scope。
- `TextureCache` 不直接访问 Vulkan queue/fence，只通过 RHI `DeviceContext`。
- shutdown 仍可以在 `device.waitIdle()` 后走 immediate destruction。

如果实现方向需要打破上述边界，先更新设计文档，再修改代码。

## 推荐数据流

Phase 0.12 目标数据流：

```text
Frame N
    -> ForwardPass::prepare()
        -> TextureResource::upload()
            -> DeviceContext::deferReleaseBuffer(staging)
    -> ForwardPass::execute()
        -> descriptor samples texture/view/sampler
    -> submit frame N

Runtime resource clear/reload during Frame N+1
    -> TextureCache::clearDeferred(context)
        -> TextureResource::releaseDeferred(context)
            -> DeviceContext::deferReleaseTextureView(view)
            -> DeviceContext::deferReleaseSampler(sampler)
            -> DeviceContext::deferReleaseTexture(texture)
    -> objects stored in current frame deletion queue

Later frame begin after fence signal
    -> VulkanDeletionQueue::flush()
        -> queued Scope<T> clear()
        -> RAII destructors destroy Vulkan objects
```

注意：

- texture view 必须先于 texture 释放或至少排在 texture 前清理，避免 view 持有 borrowed texture 指针时出现生命周期反序。
- sampler 和 texture 没有直接 ownership 关系，但都需要等 GPU 不再读取 descriptor。
- descriptor set 当前由 descriptor pool 持有，Phase 0.12 不处理 descriptor set free；只保证被 descriptor 引用的 texture/view/sampler 延迟销毁。
- `TextureResource::releaseDeferred()` 应让 `isReady()` 变为 false，避免之后继续 draw。

## 设计建议

以下是设计草案，实际实现应贴合现有代码风格。

### rhi::DeviceContext

当前接口已有：

```cpp
virtual bool deferReleaseBuffer(Scope<Buffer>& buffer) = 0;
```

Phase 0.12 建议扩展：

```cpp
virtual bool deferReleaseTexture(Scope<Texture>& texture) = 0;
virtual bool deferReleaseTextureView(Scope<TextureView>& textureView) = 0;
virtual bool deferReleaseSampler(Scope<Sampler>& sampler) = 0;
```

约束：

- 传入 `Scope<T>&`，函数成功后调用方的 scope 应被置空。
- 如果传入空 scope，建议返回 true，避免 release path 写大量空检查。
- 如果当前没有 active frame recording，应明确失败并打印英文日志。
- 先不做 pipeline/shader/layout deferred release，避免接口一次扩得过大。

### rhi/vulkan::VulkanDeletionQueue

当前只保存：

```cpp
std::vector<Scope<Buffer>> m_Buffers;
```

Phase 0.12 建议扩展为：

```cpp
std::vector<Scope<Buffer>> m_Buffers;
std::vector<Scope<TextureView>> m_TextureViews;
std::vector<Scope<Sampler>> m_Samplers;
std::vector<Scope<Texture>> m_Textures;
```

flush 顺序建议：

```text
buffers
texture views
samplers
textures
```

说明：

- upload staging buffer 与 texture object 没有依赖关系，buffer 可以先清。
- texture view 应在 texture 前清。
- sampler 和 texture 没直接对象依赖，但都可能被 descriptor 引用；只要 flush 在 fence 后即可。
- 如果未来扩展 descriptor set/layout/pipeline，需要重新检查 flush 顺序。

### rhi/vulkan::VulkanCommandContext

新增接口实现：

```cpp
bool deferReleaseTexture(Scope<Texture>& texture) override;
bool deferReleaseTextureView(Scope<TextureView>& textureView) override;
bool deferReleaseSampler(Scope<Sampler>& sampler) override;
```

每个实现应：

- 要求 active frame recording。
- 要求 frame deferred deletion queue 存在。
- 验证对象是对应 Vulkan 后端对象，避免把非 Vulkan RHI 对象塞入 Vulkan deletion queue。
- 成功后 `std::move(scope)` 进入 deletion queue。

注意：

- `TextureView` 和 `Sampler` 也应做 dynamic_cast 校验。
- 如果 scope 为空，直接返回 true。
- 不在 release 接口里调用 `waitIdle()`。

### renderer::TextureResource

建议新增：

```cpp
bool releaseDeferred(rhi::DeviceContext& context);
void resetImmediate();
```

`releaseDeferred()` 语义：

- 用于运行期 resource clear/reload。
- 依次 deferred release view、sampler、texture、staging buffer。
- 成功后内部 scope 全部为空，`isReady()` 返回 false。
- 如果 staging buffer 已经在 upload 后被移走，跳过即可。

`resetImmediate()` 语义：

- 用于 shutdown / create 失败清理。
- 只能在 GPU idle 或对象尚未提交 GPU 使用时调用。
- 可以复用默认析构行为，但显式方法能让 `TextureCache` 意图更清楚。

### renderer::TextureCache

当前：

```cpp
void clear();
```

Phase 0.12 建议新增：

```cpp
bool clearDeferred(rhi::DeviceContext& context);
```

语义：

- `clearDeferred()` 用于运行期 unload/reload。
- 遍历所有 cached `TextureResource`，调用 `releaseDeferred(context)`。
- 所有 release 成功后清空 map。
- 如果任一 texture release 失败，返回 false；是否保留未释放资源需要实现时明确。

建议保留：

```cpp
void clear();
```

用于 shutdown immediate clear。文档和注释必须说明它不适合 GPU still-in-flight 的运行期释放。

### renderer::ModelResource

Phase 0.12 不要求改成全局 ResourceManager，但需要明确：

- `ModelResource::reset()` 当前会 immediate clear mesh/material/local texture cache。
- 若后续在运行期 reset model，应新增 `resetDeferred(context)`。
- 本阶段可以只覆盖 texture cache deferred clear，或同步补最小 `ModelResource::resetDeferred(context)`。

建议优先级：

1. 先让 `TextureResource` / `TextureCache` 有 deferred release。
2. 如果实现量可控，再补 `ModelResource::resetDeferred(context)`。
3. 不要一次性扩展到 mesh buffer、pipeline、descriptor layout，避免范围过大。

## 实施顺序

### 0.12.0 文档与范围确认

目标：

- 新增 `docs/phase/phase12.md`。
- 明确主线是 GPU object deferred destruction。
- 明确不进入 mipmap、PBR、RenderGraph、bindless 或完整 ResourceManager。

当前实现状态：

- 文档已创建，等待审核。

### 0.12.1 RHI deferred release 接口

目标：

- 在 `DeviceContext` 中新增 texture / texture view / sampler deferred release 接口。
- 保持现有 `deferReleaseBuffer()` 行为不变。
- 更新 fake test context。

审核检查点：

- RHI 层不暴露 Vulkan 类型。
- 空 scope 的行为要明确。
- 成功后调用方 scope 被置空。

### 0.12.2 VulkanDeletionQueue 扩展

目标：

- `VulkanDeletionQueue` 新增 texture / texture view / sampler 队列。
- `flush()` 按安全顺序清空。
- `VulkanCommandContext` 实现新 deferred release 接口。

审核检查点：

- release 需要 active frame recording。
- release 不调用 `waitIdle()`。
- Vulkan 后端对象类型校验清晰。
- frame fence signal 后才 flush。

### 0.12.3 TextureResource deferred release

目标：

- `TextureResource` 增加运行期 deferred release 方法。
- 上传后的 staging buffer 已经移走时不报错。
- release 后 `isReady()` 返回 false。

审核检查点：

- view/sampler/texture 的 release 顺序正确。
- 失败路径不假装释放成功。
- 不接触 Vulkan 类型。

### 0.12.4 TextureCache deferred clear

目标：

- `TextureCache` 新增 `clearDeferred(context)`。
- 保留 `clear()` 作为 shutdown immediate clear。
- 文档和注释区分两者用途。

审核检查点：

- runtime clear 使用 deferred clear。
- immediate clear 不在 GPU still-in-flight 的路径中误用。
- cache map 清空后不留下可继续 draw 的 material reference。

### 0.12.5 Smoke tests

目标：

- 扩展 fake RHI context，记录 deferred texture / view / sampler 数量。
- 测试 `TextureResource::releaseDeferred()` 转移 ownership。
- 测试 `TextureCache::clearDeferred()` 会释放所有 cached texture resource。
- 确认现有 upload staging deferred release 计数不回退。

建议测试：

- `TextureResource` create -> upload -> releaseDeferred。
- `TextureCache` 两个 material 共用 texture -> clearDeferred 只释放一个 texture resource。
- release 后 `TextureResource::isReady()` 为 false。
- `ctest` 全量通过。

### 0.12.6 Phase 0.12 收尾

目标：

- 更新 `docs/phase/phase12.md` 当前实现状态。
- 按用户要求再决定是否同步 `docs/codex_handoff.md`。
- 记录剩余 TODO：mipmap、HDR、压缩纹理、完整 ResourceManager、pipeline/shader/layout deferred destruction。

## 审核检查点

- `asset/` 没有参与 GPU resource release。
- `renderer/` 只通过 RHI 请求 deferred release。
- `rhi/` 没有暴露 Vulkan 类型。
- `VulkanDeletionQueue` flush 仍与 frame fence 对齐。
- texture view 早于 texture 释放。
- release 接口不调用 `waitIdle()`。
- shutdown immediate clear 和 runtime deferred clear 的语义明确。
- `ForwardPass` 不接管 resource lifetime。
- 现有 Phase 0.11 texture cache / sRGB / multi draw 行为不回退。

## 验证计划

代码实现后运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

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

Phase 0.12 完成时应满足：

- RHI 支持 buffer / texture / texture view / sampler 的 deferred release。
- Vulkan deletion queue 可以延迟释放 texture/view/sampler。
- `TextureResource` 支持运行期 deferred release。
- `TextureCache` 支持运行期 deferred clear。
- shutdown immediate clear 与 runtime deferred clear 的用途明确。
- 现有 Phase 0.11 texture cache、sRGB 和 multi draw 行为不回退。
- build、ctest 和必要 sandbox smoke 通过。

## 当前实现状态

截至 2026-06-09，0.12.1 ~ 0.12.6 已完成最小闭环：

- 0.12.1：`rhi::DeviceContext` 已新增 texture / texture view / sampler deferred release 接口，空 scope 返回 true，成功后转移 ownership。
- 0.12.2：`VulkanDeletionQueue` 已扩展 buffer / texture view / sampler / texture 队列；`VulkanCommandContext` 已实现对应 release，并要求 active command recording 且不在 dynamic rendering scope 内。
- 0.12.3：`TextureResource` 已新增 `releaseDeferred(context)` 和 `resetImmediate()`；运行期 release 后 `isReady()` 返回 false。
- 0.12.4：`TextureCache` 已新增 `clearDeferred(context)`；`clear()` 保留为 shutdown / GPU idle 的 immediate clear。
- 0.12.5：`ark_model_resource_smoke` 已覆盖 `TextureResource::releaseDeferred()`、`TextureCache::clearDeferred()` 和原有 upload staging deferred release 计数。
- 0.12.6：Phase 0.12 收尾完成，本文已记录实现状态、验证结果和剩余边界；`docs/codex_handoff.md` 等明确要求时再同步。

验证记录：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

结果：build 通过，8/8 ctest 通过。

sandbox smoke：

```text
ark_sandbox started successfully and was stopped after smoke check
```

剩余边界：

- `ModelResource::reset()` 仍是 immediate clear；运行期 model unload 后续需要补 `resetDeferred(context)`，并覆盖 mesh buffer deferred release。
- descriptor set / descriptor layout / pipeline / shader module 的 deferred destruction 仍未纳入本阶段。
- `TextureCache::clearDeferred()` 清空 map 后，调用方必须确保没有后续 draw 继续持有旧 `TextureResource*`。

## 后续 Phase 建议

Phase 0.12 完成后，建议按以下顺序继续：

1. mipmap upload / generation 策略，包含 `TransferSrc` usage、per-mip barrier 和 blit 支持。
2. PBR material 最小参数和基础光照。
3. glTF normal / metallicRoughness / emissive texture 支持。
4. 真正的 renderer 资源/场景加载入口，替代内部默认 scene 过渡方案。
5. pipeline / shader / descriptor layout 的 deferred destruction。
6. RenderGraph 第一版 pass/resource 声明。

不建议在 Phase 0.12 完成前直接进入 mipmap 或 PBR，因为当前更关键的是运行期 GPU object lifetime 语义。
