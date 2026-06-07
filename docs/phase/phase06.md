# Phase 0.6 Texture Sampling 设计方案

## 目标

Phase 0.6 的目标是在不引入过重资源系统的前提下，完成 texture sampling 最小闭环：

- RHI 增加 `Sampler` 的真实创建路径。
- RHI descriptor 支持 sampled image 和 sampler 绑定。
- Vulkan 后端支持 texture 数据上传到 GPU image。
- 默认 cube 渲染路径从 vertex color 演进为 textured cube。

本阶段继续沿用 Vulkan Dynamic Rendering，不引入传统 `VkRenderPass` / `VkFramebuffer`。

## 当前基础

Phase 0.5 已经完成：

- `CubePass`
- uniform buffer
- descriptor set / descriptor set layout
- indexed draw
- default depth attachment
- dynamic rendering color + depth path

Phase 0.6 不重复这些工作，只在现有 cube pass 上补 texture sampling。

## 模块边界

- `renderer/passes/CubePass`
  - 持有阶段性 pass-local checkerboard texture、texture view、sampler、descriptor set。
  - 只使用 RHI 类型，不包含 Vulkan 头文件。
  - 在 render scope 之前触发一次 texture upload，之后只做 sampled read。

- `rhi/`
  - 增加 API 无关的 sampler desc、sampled image descriptor 和 texture upload 描述。
  - 不暴露 `VkImageLayout`、`VkDescriptorImageInfo`、`VkBufferImageCopy`。

- `rhi/vulkan/`
  - 实现 `VulkanSampler` RAII。
  - 将 `DescriptorType::SampledImage` 和 `DescriptorType::Sampler` 映射到 Vulkan descriptor。
  - 用 staging buffer + `vkCmdCopyBufferToImage` 完成最小 texture upload。

## Descriptor 设计

本阶段使用 separate descriptors：

```text
set 0 binding 0: UniformBuffer, vertex shader
set 0 binding 1: SampledImage, fragment shader
set 0 binding 2: Sampler, fragment shader
```

选择 separate sampled image + sampler 的原因：

- 符合“Sampler”和“sampled image descriptor”分别落地的目标。
- 后续可自然演进到 sampler 复用、material texture array 或 bindless。
- 不需要在 RHI 层提前绑定 combined image sampler 的实现细节。

## Texture Upload 策略

本阶段采用最小可验证路径：

1. `CubePass::prepare()` 在 dynamic rendering scope 之前调用一次 `DeviceContext::uploadTextureData()`。
2. Vulkan 后端创建 pass 提供的 CPU-visible staging buffer。
3. command buffer 内执行：
   - texture `Undefined -> CopyDst`
   - `vkCmdCopyBufferToImage`
   - texture `CopyDst -> ShaderResource`
4. `CubePass` 保持 staging buffer 存活，避免 GPU 异步执行时提前释放。

限制：

- 只支持 2D、mip0、array layer 0 的 tightly packed RGBA8 数据。
- 暂不做 mipmap 生成。
- 暂不做异步 upload queue、ring upload allocator 或 deferred deletion。

TODO：

- 后续将 staging buffer 生命周期迁移到 frame-local upload allocator / deferred deletion。
- 后续扩展 row pitch、mip levels、array layers 和 block compressed texture。
- 后续补 buffer barrier 和更完整的 synchronization2 路径。

## Shader 设计

新增 shader：

- `shaders/textured_cube.vert.hlsl`
- `shaders/textured_cube.frag.hlsl`

vertex 输入从 `position + color` 改为 `position + uv`。fragment shader 使用 binding 1 的 `Texture2D` 和 binding 2 的 `SamplerState` 采样 checkerboard。

## 验收标准

- `cmake --build --preset msvc-vcpkg-local-debug` 通过。
- `ctest --preset msvc-vcpkg-local-debug` 通过。
- `ark_shader_assets_smoke` 覆盖 textured cube shader 产物。
- sandbox smoke 能启动并稳定运行数秒。

## 已知风险

- 当前 upload 路径是阶段性实现，不代表最终资源上传系统。
- `VulkanDescriptorManager` 仍是固定容量 pool，只是增加了 sampled image / sampler 容量。
- `CubePass::FramesInFlight = 2` 仍与 command context 的 frame slot 数量分离，后续应统一。
- 未使用 RenderDoc 验证实际 fragment sampling 和 descriptor 绑定内容。

## 代码阅读指南

建议按“公共语义 -> 一帧调度 -> pass 资源 -> Vulkan 落地 -> shader 绑定”的顺序阅读，能最快看清 Phase 0.6 的 texture sampling 闭环。

1. 先看 RHI 公共接口：
   - `rhi/Sampler.h` 定义 `SamplerDesc`、filter 和 address mode。
   - `rhi/DescriptorSetLayout.h` 增加 `SampledImage` 和 `Sampler` descriptor 类型。
   - `rhi/DescriptorSet.h` 增加 sampled image / sampler 写入接口。
   - `rhi/DeviceContext.h` 增加 `TextureUploadDesc` 和 `uploadTextureData()`，上层只表达“上传 texture 数据”，不暴露 Vulkan copy 细节。

2. 再看一帧调度：
   - `FrameRenderer::render()` 仍负责 backbuffer/depth barrier、`beginRendering()`、pass 执行和 present 前 barrier。
   - `RenderPass::prepare()` 会在 `beginRendering()` 之前调用，用于记录 upload / copy 类命令。
   - 这一点很重要：`vkCmdCopyBufferToImage` 不能放在 dynamic rendering scope 内。

3. 然后看 `CubePass`：
   - 顶点从 `position + color` 改为 `position + uv`。
   - cube 使用 24 个顶点，让每个面有独立 UV，避免 8 顶点共享导致 UV 接缝错误。
   - `createTextureResources()` 生成 checkerboard，创建 staging buffer、GPU texture、texture view 和 sampler。
   - 每个 frame slot 仍有独立 camera uniform buffer / descriptor set，descriptor set 同时写入 binding 0/1/2。
   - `prepare()` 首帧上传 texture，之后 texture 保持 `ShaderResource` 状态，只在 fragment shader 中采样。
   - staging buffer 目前由 `CubePass` 持有，避免 GPU 异步执行 copy 时被提前释放。

4. 接着看 Vulkan 后端：
   - `VulkanSampler` 是最小 RAII 封装，负责创建和销毁 `VkSampler`。
   - `VulkanDescriptorSetLayout` 把 `SampledImage` / `Sampler` 映射为 Vulkan descriptor type。
   - `VulkanDescriptorManager` 扩大固定 descriptor pool，加入 sampled image / sampler 容量。
   - `VulkanDescriptorSet` 分别写入 `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE` 和 `VK_DESCRIPTOR_TYPE_SAMPLER`，当前没有使用 combined image sampler。
   - `VulkanCommandContext::uploadTextureData()` 记录 `Undefined -> CopyDst -> vkCmdCopyBufferToImage -> ShaderResource`。

5. 最后看 shader：
   - `textured_cube.vert.hlsl` 读取 set 0 / binding 0 的 camera uniform，并输出 UV。
   - `textured_cube.frag.hlsl` 读取 set 0 / binding 1 的 `Texture2D` 和 binding 2 的 `SamplerState`。
   - shader binding 必须和 `CubePass` 的 descriptor layout 完全一致。

## 审核检查点

- renderer / RHI 公共层没有包含 Vulkan 头文件，也没有暴露 `Vk*` 类型。
- texture upload 发生在 `beginRendering()` 之前，不在 dynamic rendering scope 内。
- descriptor layout、descriptor set 写入和 HLSL `[[vk::binding(..., 0)]]` 一致。
- staging buffer 生命周期长于首次 upload command 的 GPU 执行窗口。
- 当前 upload 是 Phase 0.6 最小实现；后续应迁移到 upload allocator / deferred deletion，并补完整 buffer barrier / synchronization2。
