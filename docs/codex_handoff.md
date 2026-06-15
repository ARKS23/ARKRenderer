# Codex Handoff Summary

更新时间：2026-06-15

## 1. 当前状态

ARKRenderer 当前代码实现已完成 Phase 0.43。`KHR_texture_transform` 最小闭环已经从 asset/glTF loader 一直打通到 `MaterialResource`、`ForwardPass` material uniform、mesh fragment shader、fixture 和 smoke tests；`RenderQueue` 已完成最小 alpha bucket ordering，保证 Opaque / Mask draw items 在 Blend draw items 前绘制；`ForwardPass` 已按 glTF `doubleSided` 精确设置 raster culling；`RenderScene` / `RenderView` 已提供可配置 scene lighting 和 camera position；mesh fragment shader 已把 direct lighting 从旧 specular power 路径升级到 Cook-Torrance direct BRDF；`FrameRenderer` 现在通过 `RGBA16Float` HDR scene color 和 `ToneMappingPass` 输出到 swapchain backbuffer；Phase 0.29 新增了 HDR environment texture 前置链路；Phase 0.30 已把 environment resource 接入 ForwardPass 与 mesh shader 的最小 equirectangular ambient lighting 路径；Phase 0.31 已补齐 cubemap resource foundation 和 HDR asset baseline；Phase 0.32 已新增 equirectangular -> cubemap GPU conversion foundation；Phase 0.33 已新增 cubemap debug skybox path，并让默认 sandbox 优先加载 `assets/HDR/2k.hdr`，缺失时回退到程序化 HDR environment，避免默认打开只有空背景；Phase 0.34 已新增 diffuse irradiance cubemap generation foundation；Phase 0.35 已把默认生成的 irradiance cubemap 通过 `FrameContext::irradianceCube` 接入 `ForwardPass`，mesh shader 现在优先使用 `TextureCube` irradiance 做 diffuse ambient IBL；Phase 0.36 已新增 sandbox orbit camera controller，让默认 sandbox 支持 orbit、zoom、pan 和 reset；Phase 0.37 已新增 cubemap face orientation contract、程序化 debug orientation environment 和 `ark_sandbox --debug-orientation` 路径；Phase 0.38 已新增最小 RHI texture readback、Vulkan image-to-buffer copy 和 automated cubemap orientation pixel validation；Phase 0.39 已新增 cubemap mip / face-mip render target view foundation；Phase 0.40 已新增 prefiltered specular environment generation foundation，能从 `ShaderResource` source cubemap 渲染 target cubemap 的完整 roughness mip chain；Phase 0.41 已新增 BRDF integration LUT resource / generator foundation；Phase 0.42 已把默认 renderer 的 specular bake path 接起来，默认启动会生成 prefiltered specular cubemap 和 BRDF LUT，并通过 `FrameContext` 传递给后续 pass；Phase 0.43 已把 prefiltered specular cubemap 和 BRDF LUT 接入 `ForwardPass` binding 18-21 与 mesh shader split-sum specular IBL。

Phase 0.28 已把 `ToneMappingPass` 从 hardcoded exposure 改为 `RenderView` 持有 `ark::ToneMappingSettings` -> per-frame uniform buffer -> `tonemap.frag.hlsl` constant buffer 的数据流，并新增 fake RHI `ark_tone_mapping_pass_smoke` 覆盖 uniform 数据流、descriptor layout、per-frame resources、pipeline state 和 fullscreen triangle draw。Phase 0.29 已新增 `loadImageHdrRgba32F()`、`rhi::Format::RGBA32Float`、Vulkan `RGBA32Float` upload、`EnvironmentResource` 和 `RenderScene` environment API。Phase 0.30 已新增 ForwardPass environment bindings 14/15、fallback environment、lighting uniform environment intensity/enabled、mesh shader equirectangular sampling 和 sandbox environment path override。Phase 0.31 已新增 `rhi::TextureType::Cube` / `rhi::TextureViewType::Cube`、Vulkan cube-compatible image/view mapping、`EnvironmentCubeResource` 和 `ark_environment_cube_resource_smoke`。Phase 0.32 已扩展 `EnvironmentCubeResource` face render target views、新增 equirectangular-to-cube shaders、`EnvironmentCubeConverter`、默认 renderer minimal conversion path 和 `ark_equirectangular_to_cube_smoke`。Phase 0.33 已新增 `FrameContext::environmentCube`、`skybox.vert/frag.hlsl`、`SkyboxPass`、`ark_skybox_pass_smoke`，并把 scene pass 顺序调整为 `ClearPass -> SkyboxPass -> ForwardPass`。Phase 0.34 已新增 `irradiance_convolve.vert/frag.hlsl`、`EnvironmentIrradianceGenerator`、默认 renderer irradiance bake path 和 `ark_environment_irradiance_smoke`。Phase 0.35 已新增 `FrameContext::irradianceCube`、ForwardPass irradiance binding 16/17、fallback irradiance cubemap、`LightingUniform.environment.z` irradiance flag 和 mesh shader diffuse irradiance IBL path。Phase 0.36 已新增 `InputSnapshot`、`Window::getInputSnapshot()`、`GlfwWindow` 输入采集、`SandboxCameraController` 和 `ark_sandbox_camera_controller_smoke`。Phase 0.37 已新增 `CubemapOrientation` face contract、`SandboxEnvironment` procedural/debug environment helpers、sandbox `--debug-orientation` flag 和 `ark_cubemap_orientation_contract_smoke`。Phase 0.38 已新增 `MemoryUsage::GpuToCpu`、`Buffer::readData()`、`TextureReadbackDesc`、`DeviceContext::copyTextureToBuffer()`、Vulkan readback buffer/image-to-buffer copy、`EnvironmentCubeResourceDesc::allowReadback`、`ark_readback_api_smoke` 和 `ark_cubemap_orientation_pixel_smoke`。Phase 0.39 已新增 `EnvironmentCubeResource::faceMipRenderTargetView()`、`EnvironmentCubeResource::mipExtent()` 和完整 face-mip render target view 生命周期，为 prefiltered specular environment 铺好资源基础。Phase 0.40 已新增 `EnvironmentSpecularPrefilterGenerator`、`specular_prefilter` shaders 和 `ark_specular_prefilter_smoke`，能按 face+mip 渲染 roughness mip chain。Phase 0.41 已新增 `EnvironmentBrdfLutResource`、`EnvironmentBrdfLutGenerator`、`brdf_lut` shaders 和 `ark_brdf_lut_smoke`。Phase 0.42 已新增默认 renderer specular cubemap / BRDF LUT resource lifetime、one-shot bake path 和 `FrameContext` specular resource plumbing。Phase 0.43 已新增 `ForwardPass` specular IBL descriptor/fallback path、`LightingUniform.environmentSpecular`、`mesh.frag.hlsl` split-sum specular IBL 和 smoke coverage。Windows/MSVC/vcpkg/DXC debug preset 下 full build、CTest 21/21、default sandbox smoke、debug orientation sandbox smoke 和 DamagedHelmet + 本地 HDR environment smoke 均已通过。

当前默认渲染主线：

```text
Vulkan Dynamic Rendering
    -> Renderer
        -> RenderScene / 默认 sandbox scene
            -> SceneLighting main directional light + ambient
            -> SceneEnvironment slot (resource pointer + intensity)
        -> optional default EnvironmentResource upload
        -> optional one-shot EnvironmentCubeConverter
            -> equirectangular HDR 2D texture
            -> 6 cubemap face render target views
            -> EnvironmentCubeResource cube texture
        -> optional one-shot EnvironmentIrradianceGenerator
            -> source environment cubemap
            -> 32x32 RGBA16F diffuse irradiance cubemap
        -> optional one-shot EnvironmentSpecularPrefilterGenerator
            -> source environment cubemap
            -> 256x256 RGBA16F prefiltered specular cubemap full mip chain
        -> optional one-shot EnvironmentBrdfLutGenerator
            -> 256x256 RGBA16F BRDF integration LUT
        -> FrameContext
            -> environmentCube for SkyboxPass
            -> irradianceCube for ForwardPass diffuse ambient IBL
            -> prefilteredSpecularCube / brdfLut for ForwardPass specular IBL
        -> default environment fallback
            -> assets/HDR/2k.hdr when present
            -> procedural 64x32 RGBA32F HDR sky gradient otherwise
            -> debug 64x32 RGBA32F face-color environment when --debug-orientation is used
        -> cubemap orientation contract
            -> Face order: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z
        -> Application sandbox camera controller
            -> InputSnapshot from Window / GlfwWindow
            -> orbit / zoom / pan / reset
            -> writes RenderView matrices and camera position
        -> RenderQueue
            -> Opaque bucket
            -> Mask bucket
            -> Blend bucket
    -> FrameRenderer
        -> prepare() upload stage
            -> MeshResource GPU-only vertex/index upload
            -> TextureResource RGBA8/sRGB upload + GPU mipmap generation
            -> TextureCache path + colorSpace + fallback + sampler key reuse
            -> MaterialResource texture references + factors + render state + texCoord selectors
        -> beginRendering(RGBA16Float scene color + depth)
        -> ClearPass
        -> SkyboxPass
            -> TextureCube sampled cubemap background
            -> no-op when FrameContext has no environmentCube
            -> linear HDR output before ToneMappingPass
        -> ForwardPass
            -> RenderView camera uniform + camera position
            -> per-draw object uniform + normal matrix
            -> per-draw material uniform
               factors + alpha state + per-slot texCoord selectors + per-slot texture transforms
            -> lighting uniform from RenderScene lighting + RenderView camera position
               environment intensity/enabled from RenderScene environment
            -> baseColor / normal / metallicRoughness / occlusion / emissive sampled images + samplers
            -> environment sampled image/sampler with fallback binding 14/15
            -> irradiance cubemap sampled image/sampler with fallback binding 16/17
            -> prefiltered specular cubemap sampled image/sampler with fallback binding 18/19
            -> BRDF LUT sampled image/sampler with fallback binding 20/21
            -> lighting uniform environment.w specular IBL enabled flag
            -> lighting uniform environmentSpecular.x max prefiltered mip
            -> per-slot selectUv() + transformUv() before sampling
            -> Cook-Torrance direct BRDF
               GGX distribution + Smith geometry + Schlick Fresnel
            -> diffuse irradiance cubemap ambient when available
            -> equirectangular environment ambient fallback when irradiance is unavailable
            -> split-sum specular IBL when prefiltered specular cubemap and BRDF LUT are available
               reflection vector + roughness mip SampleLevel + BRDF LUT scale/bias
            -> alphaMode / doubleSided pipeline variant key
            -> doubleSided culling: None for double-sided, Back for single-sided
            -> indexed textured mesh draw(s) in RenderQueue order
        -> endRendering()
        -> scene color RenderTarget -> ShaderResource
        -> beginRendering(swapchain backbuffer)
        -> ToneMappingPass
            -> fullscreen triangle
            -> sample HDR scene color
            -> RenderView ToneMappingSettings
            -> per-frame ToneMappingUniformBuffer
            -> exposure + Reinhard tone mapping + output gamma encoding
        -> endRendering()
        -> Present
```

当前默认 sandbox 仍加载：

```text
assets/models/DamagedHelmet/DamagedHelmet.gltf  # preferred local asset when present
assets/models/forward_multinode_fixture.gltf    # committed fallback
assets/textures/xiaowei.png
shaders/mesh.vert.hlsl
shaders/mesh.frag.hlsl
```

真实模型验证入口仍是：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

方向调试验证入口：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
```

`assets/models/DamagedHelmet/` 是本地真实模型资产目录，已加入 `.gitignore`；当前默认 sandbox 会在本地存在时优先使用它，缺失时回退到提交内置 fixture，但该目录本身仍不应在未明确要求时提交。

Phase 0.22 已完成的主要改动：

```text
docs/phase/phase22.md
src/asset/MeshData.h
src/asset/GltfLoader.cpp
src/renderer/material/MaterialResource.h/.cpp
src/renderer/passes/ForwardPass.cpp
shaders/mesh.frag.hlsl
assets/models/texture_transform_fixture.gltf
tests/gltf_loader_smoke.cpp
tests/model_resource_smoke.cpp
tests/shader_assets_smoke.cpp
tests/framework_headers_smoke.cpp
```

Phase 0.23 已完成的主要改动：

```text
docs/phase/phase23.md
src/renderer/RenderQueue.cpp
tests/render_scene_queue_smoke.cpp
tests/model_resource_smoke.cpp
docs/codex_handoff.md
```

Phase 0.24 已完成的主要改动：

```text
docs/phase/phase24.md
src/renderer/passes/ForwardPass.cpp
tests/forward_pass_pipeline_smoke.cpp
CMakeLists.txt
docs/codex_handoff.md
```

Phase 0.25 已完成的主要改动：

```text
docs/phase/phase25.md
src/renderer/RenderScene.h/.cpp
src/renderer/RenderView.h
src/renderer/passes/ForwardPass.cpp
tests/forward_pass_pipeline_smoke.cpp
tests/render_scene_queue_smoke.cpp
tests/framework_headers_smoke.cpp
docs/codex_handoff.md
```

Phase 0.26 已完成的主要改动：

```text
docs/phase/phase26.md
shaders/mesh.frag.hlsl
tests/shader_assets_smoke.cpp
docs/codex_handoff.md
```

Phase 0.27 已完成的主要改动：

```text
docs/phase/phase27.md
src/renderer/FrameContext.h
src/renderer/FrameRenderer.cpp
src/renderer/passes/ForwardPass.cpp
src/renderer/passes/ToneMappingPass.h/.cpp
shaders/tonemap.vert.hlsl
shaders/tonemap.frag.hlsl
CMakeLists.txt
tests/forward_pass_pipeline_smoke.cpp
tests/shader_assets_smoke.cpp
docs/codex_handoff.md
```

Phase 0.28 已完成的主要改动：

```text
docs/phase/phase28.md
src/renderer/RenderView.h
src/renderer/passes/ToneMappingPass.h/.cpp
shaders/tonemap.frag.hlsl
CMakeLists.txt
tests/tone_mapping_pass_smoke.cpp
tests/shader_assets_smoke.cpp
tests/framework_headers_smoke.cpp
docs/codex_handoff.md
```

Phase 0.29 已完成的主要改动：

```text
docs/phase/phase29.md
src/asset/TextureLoader.h/.cpp
src/rhi/RHICommon.h
src/rhi/DeviceContext.h
src/rhi/vulkan/VulkanCommon.cpp
src/rhi/vulkan/VulkanCommandContext.cpp
src/renderer/EnvironmentResource.h/.cpp
src/renderer/RenderScene.h/.cpp
CMakeLists.txt
tests/texture_loader_smoke.cpp
tests/environment_resource_smoke.cpp
tests/render_scene_queue_smoke.cpp
tests/framework_headers_smoke.cpp
docs/codex_handoff.md
```

Phase 0.30 已完成的主要改动：

```text
docs/phase/phase30.md
apps/sandbox/main.cpp
shaders/mesh.frag.hlsl
src/app/Application.h/.cpp
src/renderer/Renderer.h/.cpp
src/renderer/passes/ForwardPass.h/.cpp
tests/forward_pass_pipeline_smoke.cpp
tests/framework_headers_smoke.cpp
tests/shader_assets_smoke.cpp
docs/codex_handoff.md
```

Phase 0.31 已完成的主要改动：

```text
.gitignore
assets/HDR/.gitkeep
docs/phase/phase31.md
src/rhi/Texture.h
src/rhi/TextureView.h
src/rhi/vulkan/VulkanTexture.cpp
src/rhi/vulkan/VulkanTextureView.cpp
src/renderer/EnvironmentCubeResource.h/.cpp
CMakeLists.txt
tests/environment_cube_resource_smoke.cpp
tests/framework_headers_smoke.cpp
docs/codex_handoff.md
```

Phase 0.40 已完成的主要改动：

```text
docs/phase/phase40.md
src/renderer/EnvironmentSpecularPrefilterGenerator.h/.cpp
shaders/specular_prefilter.vert.hlsl
shaders/specular_prefilter.frag.hlsl
CMakeLists.txt
tests/specular_prefilter_smoke.cpp
tests/shader_assets_smoke.cpp
tests/framework_headers_smoke.cpp
docs/codex_handoff.md
```

Phase 0.41 已完成的主要改动：

```text
docs/phase/phase41.md
src/renderer/EnvironmentBrdfLutResource.h/.cpp
src/renderer/EnvironmentBrdfLutGenerator.h/.cpp
shaders/brdf_lut.vert.hlsl
shaders/brdf_lut.frag.hlsl
CMakeLists.txt
tests/brdf_lut_smoke.cpp
tests/shader_assets_smoke.cpp
tests/framework_headers_smoke.cpp
docs/codex_handoff.md
```

Phase 0.42 已完成的主要改动：

```text
docs/phase/phase42.md
src/renderer/FrameContext.h
src/renderer/Renderer.h/.cpp
tests/framework_headers_smoke.cpp
docs/codex_handoff.md
```

Phase 0.43 已完成的主要改动：

```text
docs/phase/phase43.md
src/renderer/passes/ForwardPass.h/.cpp
shaders/mesh.frag.hlsl
tests/forward_pass_pipeline_smoke.cpp
tests/shader_assets_smoke.cpp
docs/codex_handoff.md
```

当前支持范围：

- 读取 textureInfo 上的 `KHR_texture_transform`。
- 支持 `offset`、`scale`、`rotation`。
- 支持 extension 内 `texCoord` override。
- 最终 `texCoord` 仍只支持 0/1；超过范围 warning 后 fallback 到 0。
- transform 是 material texture slot 语义，不进入 `TextureResource` 或 `TextureCache` key。
- shader 对 baseColor、normal、metallicRoughness、occlusion、emissive 分别应用自己的 transform。
- 尚不支持 texture transform animation、`TEXCOORD_2+`、完整 glTF extension 系统或 per-UV-set tangent basis。
- `RenderQueue::build()` 会按 material alpha mode 生成稳定 draw order：Opaque bucket、Mask bucket、Blend bucket。
- 每个 bucket 内保持原 scene/model traversal order。
- `ForwardPass` 不理解 bucket，仍只按 `RenderQueue::drawItems()` 顺序绘制。
- 当前不是完整 transparent sorting；Blend bucket 内不做 camera-distance back-to-front 排序。
- `ForwardPass` 根据 `MaterialRenderState::doubleSided` 设置 cull mode：
  - `doubleSided=false` -> `CullMode::Back`
  - `doubleSided=true` -> `CullMode::None`
- Forward mesh pipeline 显式使用 `FrontFace::CounterClockwise`。
- 当前不做 two-sided lighting；双面材质只是关闭背面剔除，shader 没有基于 `gl_FrontFacing` 翻转 normal。
- `RenderScene` 持有最小 `SceneLighting`：一个 directional light 和 ambient color。
- `RenderScene::lighting()` / `setLighting()` 可配置 scene lighting，默认值与 Phase 0.24 hardcoded light 对齐。
- `RenderView` 持有 camera position；`setDefaultPerspective()` 写入默认 `(0, 0, -4)`。
- `ForwardPass::makeLightingUniform()` 从 `FrameContext::scene`、`FrameContext::view` 和 environment/specular frame resources 读取 lighting、camera position、IBL enable flag 和 prefiltered mip 信息。
- `LightingUniform` binding 13 当前包含 light、camera、environment 和 specular IBL 参数；CPU/HLSL size 由 smoke test 约束。
- `mesh.frag.hlsl` 的 direct lighting 已升级为 Cook-Torrance direct BRDF：
  - GGX / Trowbridge-Reitz normal distribution
  - Smith geometry term
  - Schlick Fresnel
  - Lambert diffuse
  - metallic workflow F0
- `FrameContext` 提供当前 render scope 的 `colorFormat` / `depthFormat` 和 post pass 采样用的 `sceneColorView`。
- `ForwardPass` pipeline key 优先使用 `FrameContext::colorFormat` / `FrameContext::depthFormat`，不再强依赖 swapchain color format。
- `FrameRenderer` 创建 `RGBA16Float` scene color，usage 为 `RenderTarget | ShaderResource`。
- `FrameRenderer` 当前是两段 dynamic rendering：Forward scene pass 写 HDR scene color，ToneMappingPass 再写 swapchain backbuffer。
- `RenderView` 持有最小 `ToneMappingSettings`：`exposure` 和 `outputGamma`。
- `ToneMappingPass` 使用 per-frame descriptor set 采样 scene color，并用 per-frame uniform buffer 上传 tone mapping settings。
- `tonemap.frag.hlsl` 通过 binding 2 的 `ToneMappingUniform` 读取 `exposure` / `inverseOutputGamma`。
- 默认 `exposure = 1.0`、`outputGamma = 2.2`，保持 Phase 0.27 默认视觉行为。
- `ark_tone_mapping_pass_smoke` 使用 fake RHI 验证 `ToneMappingPass` uniform 数据流、fallback/clamp、descriptor layout、per-frame resources 和 fullscreen triangle draw。
- `TextureLoader` 支持显式 HDR float loader：`loadImageHdrRgba32F()` / `TextureLoader::loadHdrRgba32F()`。
- `loadImageRgba8()` 仍显式拒绝 HDR 输入，不静默量化到 RGBA8。
- `asset::ImageData(Rgba32Float)` 已作为真实 HDR CPU image output 使用，`bytesPerPixel = 16`。
- RHI/Vulkan 已支持最小 `RGBA32Float` sampled texture upload，Vulkan 映射为 `VK_FORMAT_R32G32B32A32_SFLOAT`。
- `VulkanCommandContext::uploadTextureData()` 支持 `RGBA32Float` tightly packed mip0/layer0 upload；HDR mip generation 仍未接入。
- `EnvironmentResource` 负责把 `ImageData(Rgba32Float)` 创建为 `RGBA32Float` 2D sampled texture，默认 `mipLevels = 1`、usage 为 `ShaderResource | TransferDst`。
- `EnvironmentResource` 默认 sampler 为 `U=Repeat`、`V/W=ClampToEdge`，适配 equirectangular environment map。
- `RenderScene` 持有 `SceneEnvironment`：`EnvironmentResource*` 与 `intensity`，但不拥有 GPU resource。
- `RenderScene::clear()` 保留 lighting/environment policy；显式清空 environment 使用 `clearEnvironment()`。
- `ForwardPass` descriptor layout 已新增 binding 14/15：environment sampled image / sampler。
- `ForwardPass` descriptor layout 已新增 binding 16/17：diffuse irradiance cubemap sampled image / sampler。
- `ForwardPass` descriptor layout 已新增 binding 18/19：prefiltered specular cubemap sampled image / sampler。
- `ForwardPass` descriptor layout 已新增 binding 20/21：BRDF LUT sampled image / sampler。
- `ForwardPass` 持有 1x1 RGBA32F fallback environment，保证 environment disabled 时 descriptor 仍完整。
- `ForwardPass` 持有 1x1 RGBA16F fallback irradiance cubemap，保证 irradiance descriptor 仍完整；缺少真实 irradiance 时通过 `environment.z = 0` 禁用采样。
- `ForwardPass` 持有 1x1 RGBA16F fallback prefiltered specular cubemap 和 1x1 RGBA16F fallback BRDF LUT，保证 specular descriptor 仍完整；缺少真实 specular resources 时通过 `environment.w = 0` 禁用 specular IBL。
- `LightingUniform` environment vector 当前语义：`x = intensity`，`y = environment enabled`，`z = irradiance enabled`，`w = specular IBL enabled`。
- `LightingUniform.environmentSpecular.x` 当前保存最大 prefiltered specular mip level；其余分量保留。
- `mesh.frag.hlsl` 已新增 equirectangular direction -> UV、environment sampling、irradiance cubemap sampling、roughness mip `SampleLevel()`、BRDF LUT sampling 和 `evaluateIndirectLighting()`。
- environment enabled 且 irradiance cube valid 时，mesh shader 用 sampled irradiance cubemap * intensity * albedo 作为 diffuse ambient contribution；没有 irradiance 时 fallback 到 equirectangular environment；prefiltered specular cubemap + BRDF LUT 同时 valid 时启用 split-sum specular IBL；disabled 时保持旧 `ambientColor * albedo`。
- `ApplicationDesc` / `RendererDesc` 已支持 `defaultEnvironmentPath`，`ark_sandbox.exe [model] [environment.hdr]` 可选加载本地 HDR environment。
- 默认 sandbox 无参数时优先加载 `assets/HDR/2k.hdr`，缺失时使用程序化 RGBA32F HDR sky gradient。
- `assets/HDR/` 已作为 HDR environment 资源目录；`assets/HDR/*.hdr` 默认忽略，`assets/HDR/debug_*.hdr` 可作为小型 fixture 入库。
- 用户本地已有 `assets/HDR/warm_restaurant_8k.hdr`，约 98MB，用于手工验证，不应默认提交。
- RHI 已能表达 cube texture / cube texture view：`TextureDesc::type = TextureType::Cube`、`TextureViewDesc::type = TextureViewType::Cube`。
- Vulkan 后端已支持 cube-compatible image 和 cube view；cube texture 必须 square 且 `arrayLayers = 6`，cube view 必须覆盖 6 layers。
- `EnvironmentCubeResource` 是 renderer 层 cubemap GPU resource owner，创建 `RGBA16Float` / `RGBA32Float` cube texture、cube view 和 sampler，并支持 deferred release / immediate reset。
- `EnvironmentCubeConverter` 已支持 equirectangular HDR -> cubemap GPU conversion。
- `SkyboxPass` 已支持采样 converted cubemap 绘制 debug skybox background。
- `EnvironmentIrradianceGenerator` 已支持从 source cubemap 生成 low-res diffuse irradiance cubemap。
- 默认 renderer 已在默认 environment cubemap conversion 后生成 32x32 RGBA16F irradiance cubemap。
- `ForwardPass` 已通过 `FrameContext::irradianceCube` 消费默认 irradiance cubemap，形成 diffuse ambient IBL 闭环。
- `EnvironmentSpecularPrefilterGenerator` 已支持从 source cubemap 生成 prefiltered specular target cubemap 的完整 roughness mip chain，并覆盖 face-mip render path smoke。
- `EnvironmentBrdfLutResource` / `EnvironmentBrdfLutGenerator` 已支持生成 split-sum BRDF integration LUT，并覆盖 resource/generator smoke。
- 默认 renderer 已把 prefiltered specular cubemap 和 BRDF LUT 通过 `FrameContext` 传给 `ForwardPass`；mesh shader 已用 split-sum 公式消费 roughness mip chain 和 BRDF LUT。
- 当前仍不是完整 production IBL；尚缺 screenshot/golden image 验证、roughness/metallic 视觉 fixture、specular quality policy、shadow、多光源、bloom 或 auto exposure。

## 2. 最近提交与工作区

最近提交（接手时仍以 `git log --oneline -n 5` 为准）：

```text
d48006a Add renderer default specular bake path
db315f8 模型加载
ae573b9 Add BRDF LUT generation foundation
705c35d Add specular environment prefilter generator
79be8cb Add cubemap face mip views
```

本轮 0.43.0 ~ 0.43.6 收尾主要工作区改动：

```text
## main...origin/main
 M docs/codex_handoff.md
 M shaders/mesh.frag.hlsl
 M src/renderer/passes/ForwardPass.cpp
 M src/renderer/passes/ForwardPass.h
 M tests/forward_pass_pipeline_smoke.cpp
 M tests/shader_assets_smoke.cpp
?? docs/phase/phase43.md
```

接手时先执行：

```powershell
git status -sb
git diff --stat
git log --oneline -n 5
```

## 3. 已完成能力

### Phase 0.7

- `asset::ImageData` 和 `TextureLoader` 使用 `stb_image` 加载 LDR RGBA8。
- HDR 输入显式失败，不静默量化到 RGBA8。
- `TextureUploadDesc` 补齐 row pitch、slice pitch、offset、mip、array layer 等字段。
- `BufferUploadDesc` 和 `DeviceContext::uploadBufferData()` 已落地。
- GPU-only vertex/index buffer 通过 staging buffer 上传。
- texture upload 使用 `Undefined -> CopyDst -> ShaderResource`。
- staging buffer 通过 frame-local deferred deletion 延迟释放。
- `VulkanDescriptorManager` 支持可增长 descriptor pool。
- CMake post-build 会复制 `assets/` 到 sandbox 输出目录。

### Phase 0.8

- 建立 `MeshVertex`、`MeshPrimitiveData`、`MaterialData`、`ModelData`。
- `MeshResource` 负责 CPU mesh 到 GPU-only vertex/index buffer。
- `MaterialResource` 最初完成 textured mesh indexed draw 所需 descriptor 写入。
- `ForwardPass` 完成 textured mesh indexed draw 闭环。
- `GltfLoader` 建立 glTF 2.0 最小加载路径，图片像素交给 `TextureLoader`。

### Phase 0.9

- `RenderScene` 支持 model 级 `SceneModel` 和 primitive 级 `SceneObject`。
- `RenderQueue` 从 scene 生成 flat `DrawItem` list，并展开 `ModelResource` primitives。
- `ModelResource` 从 `asset::ModelData` 创建多个 `MeshResource` 和 `MaterialResource`。
- `ForwardPass` 消费 `RenderQueue`，`prepare()` 上传 mesh/material 并准备 per-draw descriptor resources。
- `CameraUniform` 和 `ObjectUniform` 拆分，per-draw object uniform 独立分配。

### Phase 0.10

- `ModelData::instances` 表达 glTF node 对 primitive 的实例化。
- `TransformData` 使用 column-major `float[16]`，asset 层不依赖 renderer/RHI/Vulkan。
- `GltfLoader` 支持 glTF 2.0 default scene、scene root node 递归遍历、node `matrix` 和 TRS。
- `RenderQueue::build()` 展开 `ModelResource::instances()`。
- `DrawItem::modelMatrix` 使用 `sceneModel.transform * instance.localTransform`。
- `RenderView` 提供 view/projection matrix，`ForwardPass` 从 `FrameContext::view` 读取 camera uniform。
- 默认 sandbox model 切换为 `assets/models/forward_multinode_fixture.gltf`。

### Phase 0.11

- `rhi::Format` 新增 `RGBA8Srgb`，Vulkan format mapping 已补齐。
- `TextureResource` 接管 texture/view/sampler/staging buffer 和首次 upload 状态。
- `TextureCache` 使用规范化 path + `TextureColorSpace` 作为 key。
- glTF baseColor texture 默认通过 `TextureColorSpace::Srgb` 创建 `RGBA8Srgb` RHI texture。
- `MaterialResource` 不再直接调用 `TextureLoader`，只保存 `TextureResource*` 引用并负责 descriptor 写入。
- `texture_cache_fixture.gltf` 验证共享 texture 只创建一次。

### Phase 0.12

- `DeviceContext` 新增 texture/view/sampler deferred release 接口。
- `VulkanDeletionQueue` 扩展 buffer / texture view / sampler / texture 队列。
- `TextureResource` 新增 `releaseDeferred(context)` 和 `resetImmediate()`。
- `TextureCache` 新增 `clearDeferred(context)`，`clear()` 保留为 shutdown / GPU idle immediate path。
- smoke tests 覆盖 texture resource deferred release 和 texture cache deferred clear。

### Phase 0.13

- `MeshResource` 新增 `releaseDeferred(context)` 和 `resetImmediate()`。
- `ModelResource` 新增 `resetDeferred(context)`，用于运行期 model unload / replacement。
- `ModelResource` 区分 local texture cache 和 external texture cache。
- local cache 会在 model deferred reset 中 `clearDeferred(context)`；external cache 由外部拥有者管理。
- `ModelResource::reset()` 保留为 shutdown / GPU idle immediate path。

### Phase 0.14

- `rhi::calculateMipLevelCount(extent)` 支持非 2 次幂尺寸。
- `TextureResourceDesc::generateMips` 默认开启 mip chain。
- `TextureResource` 创建 texture 时计算 mipLevels。
- 多 mip texture usage 包含 `TransferSrc | TransferDst | ShaderResource`。
- texture view 覆盖完整 mip range。
- sampler 在多 mip texture 上使用 linear mip filtering。
- `DeviceContext::generateTextureMips(Texture&)` 和 Vulkan GPU blit mip generation 已落地。
- upload / mip generation / staging deferred release 仍发生在 dynamic rendering scope 外。

### Phase 0.15

- `MaterialData` 新增 baseColor / metallic / roughness factors，默认值遵循 glTF 2.0。
- `GltfLoader::loadMaterial()` 读取 `pbrMetallicRoughness` core factors。
- renderer 层新增 `MaterialFactors`。
- `MaterialResource` 保存 factors，`ForwardPass` 创建 per-draw material uniform buffer。
- descriptor layout 新增 `set 0 binding 4: MaterialUniformBuffer`。
- `mesh.frag.hlsl` 使用 `baseColorTexture * baseColorFactor`。
- metallic / roughness factors 已进入 uniform。

### Phase 0.16

- `MaterialData` 新增 normal、metallicRoughness、occlusion、emissive texture slots。
- `GltfLoader` 读取 glTF core material texture slots。
- `TextureCache` 支持 color / non-color texture 语义。
- fallback textures 最小实现已落地。
- `MaterialResource` 与 `ForwardPass` 接入多 texture descriptors。
- shader 最小接入 baseColor、normal、metallicRoughness、occlusion、emissive 输入。

### Phase 0.17

- `MeshVertex` 增加 tangent。
- `GltfLoader` 支持读取 glTF `TANGENT` attribute。
- `ForwardPass` 增加 `LightingUniform`。
- shader 最小 direct-light-only PBR 输入解释已落地。
- normal map 通过 TBN 进入 world normal。
- 当前仍不是完整 PBR，没有 IBL/HDR/tone mapping。

### Phase 0.18

- `asset::generateTangents(MeshPrimitiveData&)` 实现 indexed triangle CPU tangent generation。
- glTF 显式 `TANGENT` 保持优先；缺失 `TANGENT` 时在 primitive 索引读取完成后自动生成。
- 退化 UV / 退化 triangle 会跳过；无有效累计 tangent 的 vertex 使用与 normal 正交的 fallback tangent。
- `ApplicationDesc` / `RendererDesc` 新增 `defaultModelPath`。
- `apps/sandbox/main.cpp` 支持 `ark_sandbox.exe [optional_model_path]`。
- 默认 sandbox fixture 保持 `assets/models/forward_multinode_fixture.gltf`，不绑定 DamagedHelmet。
- `ObjectUniform` 增加 `normalMatrix`。
- `mesh.vert.hlsl` 使用 normal matrix 变换 normal，tangent 使用 model 线性部分变换后相对 world normal 正交化。
- tests 覆盖 generated tangent、explicit tangent、degenerate fallback、shader normal matrix source smoke、DamagedHelmet optional load。
- `.gitignore` 已忽略 `assets/models/DamagedHelmet/`。

### Phase 0.19

- `MaterialTextureSlotData` 表达 path、`texCoord`、sampler 和 sampler 是否显式存在。
- `GltfLoader` 已读取 glTF `textureInfo.index` / `texCoord` 和 `samplers` 的 filter / wrap。
- asset 层有自己的 texture filter / address mode 枚举，不依赖 RHI。
- RHI 已补齐 `AddressMode::MirroredRepeat`，Vulkan sampler 已映射。
- renderer 层把 asset sampler 转换为 `rhi::SamplerDesc`。
- `TextureCache` key 已包含 sampler override，避免同一路径不同 sampler 错误复用。
- 新增 `sampler_fixture.gltf` 覆盖 default sampler、explicit sampler、同 image 不同 sampler 和 `texCoord=1` 路径。

### Phase 0.20

- `MaterialData` 新增 `AlphaMode`、`alphaCutoff`、`doubleSided`，默认值对齐 glTF 2.0。
- `GltfLoader` 读取 `alphaMode`、`alphaCutoff`、`doubleSided`，未知 alphaMode warning 并 fallback 到 Opaque。
- RHI 补齐最小 `BlendFactor` / `BlendOp`，Vulkan pipeline creation 已映射。
- `MaterialResource` 缓存 material render state。
- `ForwardPass` 按 color/depth format、alpha mode、doubleSided 建立 pipeline variant。
- Blend material 使用标准 alpha blending，并关闭 depth write；Opaque / Mask 保持 depth write。
- `mesh.frag.hlsl` 支持 Mask discard，并让 Blend 输出 base color alpha。
- 新增 `alpha_modes_fixture.gltf`，覆盖 Opaque / Mask / Blend / doubleSided loader 与 material resource 路径。

### Phase 0.21

- `MeshVertex` 新增 `uv1`。
- `GltfLoader` 读取可选 `TEXCOORD_1`，缺失时复制 `uv0` 到 `uv1`。
- `MaterialResource` 缓存 baseColor、normal、metallicRoughness、occlusion、emissive 的 per-slot texCoord。
- `ForwardPass` vertex layout 增加 `uv1`，tangent location 调整到 4。
- `MaterialUniform` 增加 per-slot texCoord selector。
- `mesh.vert.hlsl` 传递 `uv1`。
- `mesh.frag.hlsl` 按每个 texture slot 选择 `uv0` / `uv1`。
- 新增 `texcoord1_fixture.gltf`，覆盖 `TEXCOORD_1` 和多 texture slot texCoord。
- `gltf_loader_smoke`、`model_resource_smoke`、`shader_assets_smoke`、`framework_headers_smoke`、`mesh_data_smoke` 覆盖 Phase 0.21 关键路径。

### Phase 0.22

- `asset::TextureTransformData` 表达 glTF texture transform，默认 identity。
- `MaterialTextureSlotData` 持有 per-slot transform。
- `GltfLoader` 读取 textureInfo 上的 `KHR_texture_transform`，支持 `offset`、`scale`、`rotation` 和 extension 内 `texCoord` override。
- malformed optional transform field warning 后保留默认值，不中断 glTF 加载。
- extension 内 `texCoord` override 后仍沿用 0/1/fallback 规则。
- `MaterialResource` 缓存 baseColor、normal、metallicRoughness、occlusion、emissive 的 per-slot transform，并暴露 `textureTransforms()`。
- `ForwardPass::MaterialUniform` 携带 per-slot texture transform，binding/layout 保持不变。
- `mesh.frag.hlsl` 对每个 texture slot 执行 `selectUv()` 后再执行 `transformUv()`。
- baseColor alpha mask 使用 transformed baseColor UV。
- 新增 `texture_transform_fixture.gltf` 覆盖 `TEXCOORD_0/1`、extension texCoord override 和五个 texture slot 的 transform。
- `gltf_loader_smoke`、`model_resource_smoke`、`shader_assets_smoke`、`framework_headers_smoke` 覆盖 Phase 0.22 关键路径。

### Phase 0.23

- 新增 `docs/phase/phase23.md`，明确本阶段只做 RenderQueue alpha 分桶，不做完整透明排序、OIT、HDR、IBL 或 RenderGraph。
- `RenderQueue::build()` 按 `MaterialResource::renderState().alphaMode` 将 draw item 分入 Opaque / Mask / Blend bucket。
- 最终 `drawItems()` 输出顺序为 Opaque、Mask、Blend。
- bucket 内保持原 scene/model traversal order；Blend bucket 内仍不做 back-to-front sorting。
- `ForwardPass`、descriptor layout、pipeline layout、shader 均未修改，继续按 queue 顺序绘制。
- `render_scene_queue_smoke` 覆盖 scene object 的 alpha bucket ordering 和 bucket 内稳定顺序。
- `model_resource_smoke` 覆盖 model primitive instance 展开后的 alpha bucket ordering 和 bucket 内稳定顺序。

### Phase 0.24

- 新增 `docs/phase/phase24.md`，明确本阶段只做 `doubleSided` culling 精确化，不做 two-sided lighting、透明排序、HDR、IBL 或 RenderGraph。
- `ForwardPass` 已把 `MaterialRenderState::doubleSided` 映射到 raster cull mode。
- `doubleSided=false` material 使用 `rhi::CullMode::Back`。
- `doubleSided=true` material 使用 `rhi::CullMode::None`。
- Forward mesh pipeline 显式设置 `rhi::FrontFace::CounterClockwise`。
- `ForwardPipelineKey::doubleSided` 与实际 raster state 已对齐。
- 新增 `ark_forward_pass_pipeline_smoke`，使用 fake RHI 捕获 `GraphicsPipelineDesc`，覆盖 single-sided opaque、double-sided mask、single-sided blend 的 cull/depth/blend state。

### Phase 0.25

- 新增 `docs/phase/phase25.md`，明确本阶段只做可配置 scene light / camera，不做 BRDF、HDR、IBL、RenderGraph、glTF lights/cameras 或透明排序。
- `RenderScene` 新增最小 lighting 语义：
  - `DirectionalLight`
  - `SceneLighting`
  - `RenderScene::lighting()`
  - `RenderScene::setLighting()`
- 默认 lighting 保持 Phase 0.24 hardcoded values：direction `(-0.35, -0.8, -0.45)`，color `(1.0, 0.96, 0.88)`，ambient `(0.08, 0.09, 0.11)`。
- `RenderView` 新增 camera position：
  - `setDefaultPerspective()` 写入默认 camera position `(0, 0, -4)`
  - `setMatrices(view, projection, cameraPosition)` 支持显式 camera position
  - 旧 `setMatrices(view, projection)` 保持可用，并在 `RenderView` 内部反推 camera position
- `ForwardPass::makeLightingUniform()` 已从 `FrameContext::scene` 读取 scene lighting，从 `FrameContext::view` 读取 camera position。
- `LightingUniform` size、binding 13、descriptor layout、pipeline layout 和 mesh shaders 均未修改。
- `ark_forward_pass_pipeline_smoke` 已扩展 fake context 捕获 `ForwardLightingUniformBuffer`，覆盖 scene lighting / view camera position 到 ForwardPass uniform 的数据流，并继续覆盖 Phase 0.24 pipeline state。
- `ark_render_scene_queue_smoke` 覆盖 `RenderScene` lighting 默认值和 set/get。
- `ark_framework_headers_smoke` 覆盖新增 public structs 能编译。

### Phase 0.26

- 新增 `docs/phase/phase26.md`，明确本阶段只做 direct lighting BRDF shader 升级，不做 HDR、tone mapping、IBL、shadow、多光源、RenderGraph 或 glTF material extensions。
- `mesh.frag.hlsl` 新增 direct lighting BRDF helper：
  - `PI`
  - `distributionGGX()`
  - `geometrySchlickGGX()`
  - `geometrySmith()`
  - `fresnelSchlick()`
- `evaluateDirectLighting()` 已从旧的 `specularPower` / `pow(nDotH, specularPower)` 高光模型升级为 Cook-Torrance direct BRDF。
- direct light 公式现在使用 GGX distribution、Smith geometry、Schlick Fresnel、Lambert diffuse 和 metallic workflow F0。
- `readPbrInputs()` 的 texture sampling、UV selection、texture transform、alpha mask/blend 路径未修改。
- `LightingUniform` binding 13、descriptor layout、pipeline layout、RHI/Vulkan 均未修改。
- `shader_assets_smoke` 已扩展 source smoke，覆盖 BRDF helper 和关键变量，并继续验证 mesh SPIR-V 能加载。

### Phase 0.27

- 新增 `docs/phase/phase27.md`，明确本阶段只做 HDR scene color / tone mapping 最小闭环，不做 IBL、bloom、auto exposure、shadow、多光源或 RenderGraph。
- `FrameContext` 新增：
  - `sceneColorView`
  - `colorFormat`
  - `depthFormat`
- `ForwardPass` pipeline format 已从 swapchain 解耦，优先使用 `FrameContext::colorFormat` / `FrameContext::depthFormat`，并保留 swapchain fallback。
- `DefaultFrameRenderer` 新增 `RGBA16Float` scene color texture/view，usage 为 `RenderTarget | ShaderResource`。
- `FrameRenderer` 从单 render scope 改为两段：
  - scene pass：`ClearPass` + `ForwardPass` 写 HDR scene color + swapchain depth。
  - post pass：`ToneMappingPass` 采样 scene color 后写 swapchain backbuffer。
- `ToneMappingPass` 新增完整 C++ 实现：
  - descriptor layout：binding 0 sampled image，binding 1 sampler
  - per-frame descriptor set，避免多帧并行时改写同一 descriptor set
  - clamp-to-edge linear sampler
  - fullscreen triangle draw
  - pipeline 按当前 backbuffer color format 创建
- 新增 `shaders/tonemap.vert.hlsl`，使用 `SV_VertexID` 生成 fullscreen triangle。
- Phase 0.27 当时新增 `shaders/tonemap.frag.hlsl`，采样 HDR scene color，执行固定 exposure + Reinhard tone mapping + linear-to-sRGB；当前 Phase 0.28 工作区已把 exposure / output gamma 参数化。
- CMake 已编译 `tonemap.vert.spv` / `tonemap.frag.spv`。
- `shader_assets_smoke` 已覆盖 tonemap SPIR-V 和关键源码 token。
- `ark_forward_pass_pipeline_smoke` 已覆盖 `FrameContext::colorFormat = RGBA16Float` 时 ForwardPass pipeline 使用 HDR color format。

### Phase 0.28（0.28.0 ~ 0.28.6 已完成并验证）

- 新增 `docs/phase/phase28.md`，明确本阶段只做 tone mapping settings / color pipeline 收口，不做 IBL、bloom、auto exposure、ACES/filmic、sRGB swapchain 切换或完整 post-process stack。
- `RenderView` 新增 `ToneMappingSettings`：
  - `exposure`
  - `outputGamma`
  - `toneMappingSettings()`
  - `setToneMappingSettings()`
- `RenderView::setDefaultPerspective()` 不重置用户设置的 tone mapping settings。
- `ToneMappingPass` 新增 `ToneMappingUniform`，每个 frame slot 创建一个 `ToneMappingUniformBuffer`。
- `ToneMappingPass` descriptor layout 新增 binding 2 `UniformBuffer`，fragment stage 可见。
- `ToneMappingPass::execute()` 从 `frameContext.view` 读取 `ToneMappingSettings`，并在 draw fullscreen triangle 前更新当前 frame slot uniform。
- CPU 侧对 settings 做最小防御：`exposure < 0` clamp 到 `0`，`outputGamma <= 0` fallback 到 `2.2`。
- `shaders/tonemap.frag.hlsl` 删除 hardcoded `Exposure`，改用 binding 2 `ConstantBuffer<ToneMappingUniform>`。
- shader 现在使用 `g_ToneMapping.exposure` 和 `g_ToneMapping.inverseOutputGamma`，output encoding helper 命名为 `linearToOutput()`。
- `shader_assets_smoke` 已更新 source token，覆盖 tone mapping uniform、binding 2、exposure、inverse gamma 和 `linearToOutput()`。
- `framework_headers_smoke` 已覆盖 `ToneMappingSettings` public API 编译路径。
- 新增 `tests/tone_mapping_pass_smoke.cpp`，使用 fake RHI / fake context 捕获 `ToneMappingUniformBuffer` 的实际上传数据。
- `ark_tone_mapping_pass_smoke` 覆盖正常 exposure/gamma、非法参数 clamp/fallback、无 view 默认 settings、descriptor layout、per-frame resources、pipeline state 和 fullscreen triangle draw。
- `CMakeLists.txt` 已在 `ARK_DXC_SUPPORTED` 测试块中接入 `ark_tone_mapping_pass_smoke`，并添加 `ark_shaders` 依赖。

### Phase 0.29（0.29.0 ~ 0.29.6 已完成并验证）

- 新增 `docs/phase/phase29.md`，明确本阶段是 HDR environment texture / IBL prelude，不做完整 IBL、cubemap、irradiance、prefilter、BRDF LUT、skybox、bloom 或 auto exposure。
- `TextureLoader` 新增 `loadImageHdrRgba32F()` 和 `TextureLoader::loadHdrRgba32F()`，使用 stb float path 显式读取 HDR。
- `loadImageRgba8()` 继续拒绝 HDR 输入，避免隐式量化到 RGBA8。
- `ImageData(Rgba32Float)` 现在使用 `bytesPerPixel = 16`，`pixels` 保存 RGBA float bytes。
- `rhi::Format` 新增 `RGBA32Float`，Vulkan 映射到 `VK_FORMAT_R32G32B32A32_SFLOAT`。
- `VulkanCommandContext::uploadTextureData()` 支持 `RGBA32Float` 16 bytes per pixel tightly packed upload。
- 新增 `EnvironmentResource`，从 `ImageData(Rgba32Float)` 创建 `RGBA32Float` 2D sampled environment texture、view、sampler 和 staging buffer。
- `EnvironmentResource` 默认 mipLevels 为 1，usage 为 `ShaderResource | TransferDst`，不调用 `generateTextureMips()`。
- `EnvironmentResource` 支持 `releaseDeferred()` 与 `resetImmediate()`，生命周期风格对齐现有 renderer resource。
- `RenderScene` 新增 `SceneEnvironment`、`environment()`、`setEnvironment()` 和 `clearEnvironment()`；scene 只保存 resource pointer 与 intensity，不拥有 GPU resource。
- `framework_headers_smoke`、`texture_loader_smoke`、`render_scene_queue_smoke` 和新增 `ark_environment_resource_smoke` 已覆盖 public API、loader 边界、resource desc/upload desc、sampler policy、deferred release 和 scene environment policy。

### Phase 0.30（0.30.0 ~ 0.30.6 已完成并验证）

- 新增 `docs/phase/phase30.md`，明确本阶段只做最小 environment lighting，不做完整 IBL、cubemap、irradiance、prefilter、BRDF LUT、skybox、bloom 或 auto exposure。
- `ForwardPass` descriptor layout 新增 binding 14/15，用于 environment sampled image / sampler。
- `ForwardPass` 持有 1x1 RGBA32F fallback environment，保证 scene 没有 environment 时 descriptor 仍完整。
- `LightingUniform` 扩展 `environment` 字段，`x = intensity`，`y = enabled`。
- `ForwardPass::prepare()` 会上传 fallback environment，并在 scene environment 启用时上传 scene environment。
- `mesh.frag.hlsl` 新增 equirectangular direction -> UV、environment sampling 和 `evaluateAmbientLighting()`。
- environment enabled 时用 sampled HDR environment * intensity * albedo 替换旧常量 ambient；disabled 时保持 `ambientColor * albedo`。
- `ApplicationDesc` / `RendererDesc` 新增 `defaultEnvironmentPath`。
- `apps/sandbox/main.cpp` 支持 `ark_sandbox.exe [optional_model_path] [optional_environment_hdr_path]`。
- Renderer 默认 scene 可选加载本地 HDR environment 并设置 `SceneEnvironment`。
- `forward_pass_pipeline_smoke` 覆盖 environment descriptor layout、fallback binding、scene environment binding、lighting uniform 和 upload 行为。
- `shader_assets_smoke` 覆盖 mesh shader environment binding / sampling token。
- `framework_headers_smoke` 覆盖 `ApplicationDesc` / `RendererDesc` environment path public API。

### Phase 0.31（0.31.0 ~ 0.31.6 已完成并验证）

- 新增 `docs/phase/phase31.md`，明确本阶段是 cubemap foundation + HDR asset baseline，不做 equirectangular -> cubemap conversion 或完整 IBL。
- 新增 `assets/HDR/.gitkeep`，建立 HDR environment 资源目录。
- `.gitignore` 默认忽略 `assets/HDR/*.hdr`，但允许 `assets/HDR/debug_*.hdr` 小型 fixture 入库。
- 用户本地 `assets/HDR/warm_restaurant_8k.hdr` 可用于手工验证，但因体积约 98MB 且为真实 HDRI，不提交为默认依赖。
- `TextureDesc` 新增 `TextureType type`，默认 `Texture2D`，可显式表达 `Cube`。
- `TextureViewDesc` 新增 `TextureViewType type`，默认 `Texture2D`，可显式表达 `Cube`。
- Vulkan texture 创建支持 cube-compatible image：`TextureType::Cube` 映射 `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`。
- Vulkan texture view 支持 cube view：`TextureViewType::Cube` 映射 `VK_IMAGE_VIEW_TYPE_CUBE`。
- Vulkan 后端新增 cube 校验：cubemap 必须 square、6 array layers；cube view 必须覆盖 6 layers，且 mip/layer range 不能越界。
- 新增 `EnvironmentCubeResource`，创建 `RGBA16Float` / `RGBA32Float` cubemap texture、cube view 和 sampler，并支持 `releaseDeferred()` / `resetImmediate()`。
- 新增 `ark_environment_cube_resource_smoke`，覆盖 cubemap desc、view desc、sampler policy、invalid desc rejection、sampler override 和 deferred/immediate release。
- `framework_headers_smoke` 覆盖 cubemap public API 和 `EnvironmentCubeResource` 头文件。

### Phase 0.32（0.32.0 ~ 0.32.6 已完成并验证）

- 新增 `docs/phase/phase32.md`，明确本阶段只做 equirectangular -> cubemap conversion foundation，不做完整 IBL。
- `EnvironmentCubeResource` 已新增 6 个 per-face render target views，cubemap texture usage 变为 `RenderTarget | ShaderResource`。
- Vulkan `Texture2D` view 校验已明确单 layer 语义，用于 cube-compatible image 的 face render target view。
- 新增 `shaders/equirect_to_cube.vert.hlsl` / `shaders/equirect_to_cube.frag.hlsl`，使用 fullscreen triangle 和 face index uniform 采样 equirectangular HDR。
- 新增 `EnvironmentCubeConverter`，通过 RHI descriptor layout、per-face uniform buffers、graphics pipeline、dynamic rendering 和 barriers 把 `EnvironmentResource` 转换到 `EnvironmentCubeResource`。
- 默认 renderer 在默认 HDR environment path 中创建 `DefaultSandboxEnvironmentCube`，并在 command recording 开始后触发一次 conversion；失败时保留 ForwardPass 既有 equirectangular ambient path。
- `ForwardPass` 仍然采样 equirectangular environment，尚未切换到 cubemap-based IBL。
- 新增 `ark_equirectangular_to_cube_smoke`，覆盖 converter descriptor layout、pipeline state、six-face rendering、uniform data、barrier 和 draw count。
- `ark_environment_cube_resource_smoke` 已扩展覆盖 face views；`ark_shader_assets_smoke` 覆盖 conversion shaders；`ark_framework_headers_smoke` 覆盖 converter public API。

### Phase 0.33（0.33.0 ~ 0.33.6 已完成并验证）

- 新增 `docs/phase/phase33.md`，明确本阶段只做 cubemap debug skybox / face orientation validation，不做完整 IBL。
- `FrameContext` 新增非拥有 `EnvironmentCubeResource* environmentCube`。
- `DefaultRenderer` 在默认 cubemap conversion 成功后把 cubemap 传入 frame context。
- 默认 sandbox environment 策略更新：优先加载 `assets/HDR/2k.hdr`，缺失时使用程序化 64x32 RGBA32F HDR sky gradient，避免默认打开只有空背景。
- 新增 `shaders/skybox.vert.hlsl` / `shaders/skybox.frag.hlsl`，使用 fullscreen triangle、inverse projection / inverse view rotation 和 `TextureCube<float4>` 采样 cubemap。
- `SkyboxPass` 已从占位头文件实现为真实 pass，支持 no-cubemap no-op、per-frame uniform、cube sampled image / sampler binding、pipeline cache 和 fullscreen draw。
- `FrameRenderer` scene pass 顺序已调整为 `ClearPass -> SkyboxPass -> ForwardPass`。
- 新增 `ark_skybox_pass_smoke`，覆盖 no-cubemap path、cubemap draw path、descriptor layout、pipeline state、uniform update、cube binding 和 draw count。
- `shader_assets_smoke` 已覆盖 skybox SPIR-V 和 shader source token。
- `framework_headers_smoke` 已覆盖 `FrameContext::environmentCube`。
- `ForwardPass` 仍然采样 equirectangular environment ambient，尚未切换到 cubemap-based IBL。

### Phase 0.34（0.34.0 ~ 0.34.5 已完成并验证）

- 新增 `docs/phase/phase34.md`，明确本阶段只做 diffuse irradiance cubemap generation foundation，不把结果接入 `ForwardPass`。
- 新增 `shaders/irradiance_convolve.vert.hlsl` / `shaders/irradiance_convolve.frag.hlsl`，使用 fullscreen triangle、face index uniform 和 `TextureCube<float4>` 做 diffuse irradiance convolution。
- 新增 `EnvironmentIrradianceGenerator`，沿用 `EnvironmentCubeConverter` 的 per-face dynamic rendering 模式，支持 source cubemap -> target irradiance cubemap。
- `EnvironmentIrradianceGenerator::generate()` 覆盖 source/target 校验、source != target、per-face uniform、descriptor update、6 face draw 和 target ShaderResource barrier。
- 默认 renderer 已新增 `DefaultSandboxIrradianceCube`，并在默认 environment cubemap conversion 成功后生成 32x32 RGBA16F irradiance cubemap。
- 默认 irradiance generation 失败只记录 warning，不阻断 skybox / ForwardPass / ToneMappingPass。
- `FrameContext` 暂未新增 irradiance cube pointer；`ForwardPass` 仍然采样 equirectangular environment ambient。
- 新增 `ark_environment_irradiance_smoke`，覆盖 generator descriptor layout、pipeline state、six-face rendering、uniform data、source cubemap binding、target barrier 和 source==target 拒绝行为。
- `shader_assets_smoke` 已覆盖 irradiance SPIR-V 和 shader source token。
- `framework_headers_smoke` 已覆盖 `EnvironmentIrradianceGenerator` public API。

### Phase 0.35（0.35.0 ~ 0.35.5 已完成并验证）

- 新增 `docs/phase/phase35.md`，明确本阶段只做 ForwardPass diffuse irradiance IBL，不做完整 specular IBL。
- `FrameContext` 新增非拥有 `EnvironmentCubeResource* irradianceCube`，与 `environmentCube` 区分：前者用于 mesh diffuse IBL，后者用于 skybox background。
- 默认 renderer 新增 `resolveFrameIrradianceCube()`，在默认 irradiance generation 成功且当前 scene 使用默认 environment 时把 `DefaultSandboxIrradianceCube` 传入 frame context。
- `ForwardPass` descriptor layout 新增 binding 16/17，用于 irradiance cubemap sampled image / sampler；binding 14/15 equirectangular environment fallback 保留。
- `ForwardPass` 新增 1x1 `RGBA16Float` fallback irradiance cubemap，保证 descriptor set 完整；缺失真实 irradiance 时 `LightingUniform.environment.z = 0`，shader 不采样 fallback cube。
- `LightingUniform.environment` 语义更新为 `x = intensity`、`y = environment enabled`、`z = irradiance enabled`、`w = reserved`。
- `mesh.frag.hlsl` 新增 `TextureCube<float4> g_IrradianceCube`、`SamplerState g_IrradianceSampler` 和 `sampleIrradiance()`。
- `evaluateAmbientLighting()` 现在优先使用 irradiance cubemap 做 diffuse ambient IBL；没有 irradiance cube 时 fallback 到 equirectangular environment；没有 environment 时 fallback 到 scene ambient color。
- `ark_forward_pass_pipeline_smoke` 已覆盖 descriptor layout 16/17、fallback irradiance binding、valid irradiance binding 和 `environment.z` flag。
- `shader_assets_smoke` 已覆盖 mesh fragment shader irradiance binding/source token。
- `framework_headers_smoke` 已覆盖 `FrameContext::irradianceCube`。

### Phase 0.36（0.36.0 ~ 0.36.5 已完成并验证）

- 新增 `docs/phase/phase36.md`，明确本阶段只做 sandbox orbit camera controller，不做 editor camera、glTF camera 或完整 input system。
- 新增 `src/app/Input.h`，定义 app 层 `InputSnapshot`。
- `Window` 新增 `getInputSnapshot()`；`GlfwWindow` 采集 cursor position、cursor delta、scroll delta、mouse buttons、Shift 和 R reset pressed-edge。
- 新增 `SandboxCameraController`，支持 orbit、zoom、pan、reset、viewport extent 更新和写入 `RenderView`。
- `Application::run()` 已接入 controller；resize 时只更新 renderer extent 和 controller extent，不再重置 camera state。
- 默认无输入时保持 `(0, 0, -4)` 看向原点，与旧 `RenderView::setDefaultPerspective()` 视觉基线兼容。
- 新增 `ark_sandbox_camera_controller_smoke`，覆盖默认 camera、orbit、zoom、pan、pitch/distance clamp、reset 和 resize 不重置 camera state。
- `framework_headers_smoke` 已覆盖新增 app 层 API。
- `renderer/`、`FrameRenderer`、`ForwardPass` 没有 GLFW/input 依赖。

### Phase 0.37（0.37.0 ~ 0.37.5 已完成并验证）

- 新增 `docs/phase/phase37.md`，明确本阶段只做 cubemap face orientation debug foundation，不做 GPU readback、automated pixel validation、specular IBL 或 BRDF LUT。
- 新增 `src/renderer/CubemapOrientation.h`，把 ARK cubemap face order 固化为 `0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z`，并提供 debug color contract。
- 新增 `src/renderer/SandboxEnvironment.h/.cpp`，把默认程序化 HDR environment helper 从 `Renderer.cpp` 拆出，并新增 64x32 `Rgba32Float` debug orientation environment。
- `ApplicationDesc` / `RendererDesc` 新增 `useDebugOrientationEnvironment`，`apps/sandbox/main.cpp` 新增 `--debug-orientation`。
- `Renderer` 在 debug flag 开启时优先使用程序化 orientation environment；默认真实 HDR path 和普通程序化 fallback 保持不变。
- 新增 `ark_cubemap_orientation_contract_smoke`，覆盖 face order、`EnvironmentCubeResource` cube/face view layer、debug orientation environment 像素方向和普通 procedural environment 可用性。
- `shader_assets_smoke` 已扩展 cubemap face mapping、irradiance face mapping 和 mesh diffuse irradiance normal sampling token。
- `framework_headers_smoke` 已覆盖新增 public contract/helper 和 desc flag。
- 当前仍没有 GPU texture readback、texture-to-buffer copy、CPU-visible readback buffer 或 automated cubemap pixel validation。

### Phase 0.38（0.38.0 ~ 0.38.6 已完成并验证）

- 新增 `docs/phase/phase38.md`，明确本阶段只做最小 RHI texture readback 和 cubemap pixel validation，不做 screenshot system、async readback、specular IBL、BRDF LUT 或 cubemap mip chain。
- `MemoryUsage` 新增 `GpuToCpu`，`Buffer` 新增 `readData()` 默认失败接口。
- Vulkan buffer allocation 支持 readback memory，`VulkanBuffer::readData()` 使用 `vmaInvalidateAllocation()` 后 memcpy 到 CPU destination。
- RHI 新增 `TextureReadbackDesc`，`DeviceContext` 新增 `copyTextureToBuffer()` 默认失败接口。
- `VulkanCommandContext::copyTextureToBuffer()` 已接入 `vkCmdCopyImageToBuffer`，要求命令录制中、dynamic rendering scope 外、source texture 处于 `CopySrc`、texture usage 带 `TransferSrc`、destination buffer 带 `TransferDst` 且 memory usage 为 `GpuToCpu`。
- `EnvironmentCubeResourceDesc` 新增 `allowReadback`；默认 cubemap 不带 `TransferSrc`，测试或验证资源显式开启后才追加 `TransferSrc`。
- 新增 `ark_readback_api_smoke`，覆盖 fake readback buffer、texture-to-buffer copy contract 和 cubemap readback usage flag。
- 新增 `ark_cubemap_orientation_pixel_smoke`，通过真实 Vulkan backend 将 debug orientation environment 转成 cubemap，逐 face 读回 center pixel，并与 `CubemapOrientation` debug color contract 比对。
- 当前 readback 路径是验证用同步基础：submit 后用 `RenderDevice::waitIdle()` 再 `readData()`，仍不是 runtime async capture 或完整 screenshot/golden image 系统。

### Phase 0.39（0.39.0 ~ 0.39.4 已完成并验证）

- 新增 `docs/phase/phase39.md`，明确本阶段只做 cubemap mip / face-mip render target view foundation，不做 prefiltered specular shader、BRDF LUT 或 ForwardPass specular IBL。
- `EnvironmentCubeResource` 内部 face view storage 已从固定 6 个 mip0 views 扩展为 `FaceCount * mipLevels` 个 face-mip views。
- 新增 `faceMipRenderTargetView(faceIndex, mipLevel)`，用于后续 prefilter pass 查询任意 face+mip render target view；越界返回 `nullptr`。
- `faceRenderTargetView(faceIndex)` 保持兼容，继续作为 mip0 alias，现有 equirectangular conversion 和 irradiance generation 行为不变。
- 新增 `mipExtent(mipLevel)`，返回 clamp 到 1 的 mip 尺寸，越界或无效资源返回 `{}`。
- `isValid()`、`releaseDeferred()` 和 `resetImmediate()` 已覆盖所有 face-mip views。
- `ark_environment_cube_resource_smoke` 已覆盖 view 数量、view desc、mip0 alias、越界查询、mip extent 和 deferred release 数量。
- `ark_framework_headers_smoke` 已覆盖新增 public API。
- 当前已具备 prefiltered specular environment 的资源视图基础，但还没有 prefilter shader、BRDF LUT、ForwardPass specular IBL 或 roughness mip sampling。

### Phase 0.40（0.40.0 ~ 0.40.5 已完成并验证）

- 新增 `docs/phase/phase40.md`，明确本阶段只做 prefiltered specular environment generation foundation，不做 BRDF LUT、ForwardPass specular IBL 或默认 renderer specular bake path。
- 新增 `EnvironmentSpecularPrefilterGenerator` 和 `EnvironmentSpecularPrefilterDesc`，API 风格与 `EnvironmentIrradianceGenerator` 保持一致。
- generator 会校验 source / target 非空、有效、不同资源，并要求 source cubemap 处于 `ShaderResource`。
- generator 会按 `FaceCount * targetMipLevels` 持有独立 uniform buffer / descriptor set，避免同一 command buffer 中重复覆盖 uniform storage。
- `generate()` 遍历全部 target mips 和 faces，使用 `faceMipRenderTargetView(face, mip)`、`mipExtent(mip)`、per-face-mip uniform 和 fullscreen triangle 绘制 roughness mip chain。
- 新增 `specular_prefilter.vert.hlsl` / `specular_prefilter.frag.hlsl`，fragment shader 使用 GGX importance sampling、Hammersley 序列、`TextureCube.SampleLevel()` 和既有 face orientation contract。
- 新增 `ark_specular_prefilter_smoke`，覆盖 invalid desc、source state validation、descriptor layout、shader/pipeline resource、barrier、rendering desc、viewport/scissor、uniform 数据和 draw count。
- `ark_shader_assets_smoke` 已覆盖新增 shader 文件、编译产物和关键 token；`ark_framework_headers_smoke` 已覆盖新增 public API。
- 当前 prefiltered specular cubemap 还没有接入 `Renderer` 默认 bake path、`FrameContext`、`ForwardPass` descriptor layout 或 mesh shader specular IBL。

### Phase 0.41（0.41.0 ~ 0.41.5 已完成并验证）

- 新增 `docs/phase/phase41.md`，明确本阶段只做 BRDF LUT resource / generator / shader / tests，不接 ForwardPass 或默认 renderer bake path。
- 新增 `EnvironmentBrdfLutResource` 和 `EnvironmentBrdfLutResourceDesc`，创建 2D `RenderTarget | ShaderResource` texture、sampled view、render target view 和 clamp sampler。
- 新增 `EnvironmentBrdfLutGenerator` 和 `EnvironmentBrdfLutGenerationDesc`，使用 fullscreen triangle 生成 split-sum BRDF integration LUT。
- 新增 `brdf_lut.vert.hlsl` / `brdf_lut.frag.hlsl`，fragment shader 使用 Hammersley、GGX importance sampling、Smith geometry 和 `IntegrateBRDF()`，输出 `float4(A, B, 0, 1)` linear data。
- generator 使用 16-byte uniform 传入 sampleCount，并在 CPU 侧 clamp 到 `[16, 4096]`。
- generator 会把 target texture 从当前状态 transition 到 `RenderTarget`，渲染后 transition 到 `ShaderResource`。
- 新增 `ark_brdf_lut_smoke`，覆盖 invalid desc、sampler override、deferred release、descriptor layout、uniform、barrier、rendering desc、viewport/scissor、pipeline 和 draw。
- `ark_shader_assets_smoke` 已覆盖新增 shader 文件、编译产物和关键 token；`ark_framework_headers_smoke` 已覆盖新增 public API。
- 当前 BRDF LUT foundation 已在 Phase 0.42 接入默认 renderer bake path 和 `FrameContext`，并在 Phase 0.43 被 `ForwardPass` descriptor layout 与 mesh shader specular IBL 消费。

### Phase 0.42（0.42.0 ~ 0.42.5 已完成并验证）

- 新增 `docs/phase/phase42.md`，明确本阶段只做默认 renderer specular bake path，不接 `ForwardPass` specular IBL。
- `FrameContext` 新增 `prefilteredSpecularCube` 和 `brdfLut` 非拥有指针，用于后续 pass 消费。
- `DefaultRenderer` 新增 `EnvironmentSpecularPrefilterGenerator`、`EnvironmentBrdfLutGenerator`、`m_DefaultSpecularCube` 和 `m_DefaultBrdfLut`。
- 默认 specular cubemap 使用 `256x256 RGBA16Float` full mip chain；默认 BRDF LUT 使用 `256x256 RGBA16Float`。
- 默认 render prepare 阶段在 environment cubemap conversion 后 one-shot 生成 prefiltered specular cubemap，并生成 BRDF LUT。
- `resolveFramePrefilteredSpecularCube()` / `resolveFrameBrdfLut()` 会在资源生成成功后填入 `FrameContext`。
- specular bake / BRDF LUT bake 失败保持非 fatal warning，默认 sandbox 继续使用 skybox、diffuse IBL、equirectangular fallback 和 tone mapping。
- `ark_framework_headers_smoke` 已覆盖新增 `FrameContext` 字段；`ForwardPass` descriptor layout 和 `mesh.frag.hlsl` 本阶段保持不变。

### Phase 0.43（0.43.0 ~ 0.43.6 已完成并验证）

- 新增 `docs/phase/phase43.md`，明确本阶段只做 `ForwardPass` specular IBL 最小接入，不做 screenshot/golden validation、quality policy、bloom、auto exposure、shadow 或多光源。
- `ForwardPass` descriptor layout 新增 binding 18/19，用于 prefiltered specular cubemap sampled image / sampler；新增 binding 20/21，用于 BRDF LUT sampled image / sampler。
- `ForwardPass` 新增 1x1 `RGBA16Float` fallback specular cubemap 和 1x1 `RGBA16Float` fallback BRDF LUT，保证 descriptor set 在无真实资源时仍完整。
- `LightingUniform` 新增 `environmentSpecular`，`environment.w` 作为 specular IBL enable flag，`environmentSpecular.x` 保存 max prefiltered mip level。
- `mesh.frag.hlsl` 新增 `fresnelSchlickRoughness()`、`samplePrefilteredSpecular()`、`sampleBrdfLut()` 和 `evaluateIndirectLighting()`，使用 reflection direction、roughness mip `SampleLevel()` 与 BRDF LUT scale/bias 做 split-sum specular IBL。
- `ark_forward_pass_pipeline_smoke` 已覆盖 descriptor layout 18-21、fallback specular/BRDF LUT binding、valid specular resources binding、uniform flag 和 max mip。
- `ark_shader_assets_smoke` 已覆盖 mesh shader 新 binding、split-sum helper、`SampleLevel()`、`reflect()` 和 specular enable token。
- 当前仍缺 screenshot/golden image validation、roughness/metallic validation fixture、specular quality policy、glTF camera、shadow、多光源、bloom 和 auto exposure。

## 4. 关键代码阅读顺序

建议按以下顺序审核当前 Phase 0.43 完整闭环：

1. `docs/phase/phase21.md`
   - 回看 `TEXCOORD_1` / per-slot UV selection 的前置范围和限制。
2. `docs/phase/phase22.md`
   - 确认 `KHR_texture_transform` 最小闭环、当前限制和验证记录。
3. `docs/phase/phase23.md`
   - 确认 RenderQueue alpha bucket 范围、非目标、验证记录和仍非完整 transparent sorting 的限制。
4. `docs/phase/phase24.md`
   - 确认 doubleSided culling 范围、front-face 约定、测试策略和仍不做 two-sided lighting 的限制。
5. `docs/phase/phase25.md`
   - 确认 scene lighting / camera position 的范围、非目标、测试策略和仍不做 BRDF/HDR/IBL 的限制。
6. `docs/phase/phase26.md`
   - 确认 direct lighting BRDF 升级范围、非目标、测试策略和仍不做 HDR/IBL 的限制。
7. `docs/phase/phase27.md`
   - 确认 HDR scene color / tone mapping 范围、两段 FrameRenderer 调度、测试策略和仍不做 IBL/bloom/auto exposure 的限制。
8. `docs/phase/phase28.md`
   - 确认 tone mapping settings / color pipeline 收口范围、0.28.0 ~ 0.28.6 已完成项和验证记录。
9. `docs/phase/phase29.md`
   - 确认 HDR environment texture 前置链路范围、0.29.0 ~ 0.29.6 已完成项、非目标和验证记录。
10. `docs/phase/phase30.md`
   - 确认最小 environment lighting 范围、descriptor binding、fallback environment、sandbox path 和仍非完整 IBL 的限制。
11. `docs/phase/phase31.md`
   - 确认 cubemap foundation、HDR asset baseline、非目标和验证记录。
12. `docs/phase/phase32.md`
   - 确认 equirectangular -> cubemap conversion 范围、最小接入、非目标和验证记录。
13. `docs/phase/phase33.md`
   - 确认 cubemap debug skybox / face orientation validation 范围、默认 sandbox environment fallback 和非目标。
14. `docs/phase/phase34.md`
   - 确认 diffuse irradiance cubemap foundation 范围、非目标、摄像机交互判断和验证记录。
15. `docs/phase/phase35.md`
   - 确认 ForwardPass diffuse irradiance IBL 范围、descriptor binding 16/17、fallback 策略、非目标和验证记录。
16. `docs/phase/phase36.md`
   - 确认 sandbox orbit camera controller 范围、输入层边界、交互约定、测试策略和非目标。
17. `docs/phase/phase37.md`
   - 确认 cubemap face orientation contract、debug environment、sandbox debug path、非目标和验证记录。
18. `docs/phase/phase38.md`
   - 确认 RHI texture readback、cubemap pixel validation、非目标和验证记录。
19. `docs/phase/phase39.md` / `docs/phase/phase40.md` / `docs/phase/phase41.md` / `docs/phase/phase42.md` / `docs/phase/phase43.md`
   - 确认 cubemap mip / face-mip view foundation、prefiltered specular environment generation foundation、BRDF LUT foundation、renderer default specular bake path、ForwardPass specular IBL 接入、非目标和验证记录。
20. `src/rhi/Buffer.h` / `src/rhi/DeviceContext.h`
   - 看 `MemoryUsage::GpuToCpu`、`Buffer::readData()`、`TextureReadbackDesc` 和 `copyTextureToBuffer()` 的 RHI contract。
21. `src/rhi/vulkan/VulkanBuffer.h/.cpp` / `src/rhi/vulkan/VulkanCommandContext.h/.cpp`
   - 看 readback allocation、CPU read invalidate、`vkCmdCopyImageToBuffer`、usage/state/format/row pitch/range 校验。
22. `tests/readback_api_smoke.cpp` / `tests/cubemap_orientation_pixel_smoke.cpp`
   - 看 fake API contract、readback-enabled cubemap usage 和真实 Vulkan cubemap center pixel validation。
23. `src/app/Input.h` / `src/app/Window.h` / `src/app/GlfwWindow.h/.cpp`
   - 看输入快照、GLFW polling/callback、scroll delta 和 reset pressed-edge。
24. `src/app/SandboxCameraController.h/.cpp`
   - 看 orbit/zoom/pan/reset、clamp、projection Y flip 和 `RenderView` 写入。
25. `src/app/Application.h/.cpp` / `src/renderer/Renderer.h/.cpp` / `apps/sandbox/main.cpp`
   - 看 controller 每帧更新、resize 不重置 camera state、`defaultEnvironmentPath` 传递、`--debug-orientation` 和 renderer debug environment flag。
26. `src/renderer/CubemapOrientation.h`
   - 看 face order contract、dominant direction helper 和 debug color 约定。
27. `src/renderer/SandboxEnvironment.h/.cpp`
   - 看默认程序化 HDR environment 和 debug orientation environment 的 CPU 生成规则。
28. `src/rhi/Texture.h` / `src/rhi/TextureView.h`
   - 看 `TextureType::Cube` / `TextureViewType::Cube` 的 RHI 语义和默认 2D 行为。
29. `src/rhi/vulkan/VulkanTexture.cpp` / `src/rhi/vulkan/VulkanTextureView.cpp`
   - 看 Vulkan cube-compatible image / cube view 映射，以及 square、6 layers、mip/layer range、2D single-layer view 校验。
30. `src/renderer/EnvironmentCubeResource.h/.cpp`
   - 看 renderer 层 cubemap resource 的 texture/cube view/face-mip views/sampler 创建、mip0 alias、mip extent、sampler policy、deferred release 和 reset 行为。
31. `src/renderer/EnvironmentCubeConverter.h/.cpp`
   - 看 equirectangular -> cubemap conversion 的 descriptor layout、per-face uniform buffers、barrier、dynamic rendering 和 draw path。
32. `src/renderer/EnvironmentIrradianceGenerator.h/.cpp` / `src/renderer/EnvironmentSpecularPrefilterGenerator.h/.cpp` / `src/renderer/EnvironmentBrdfLutResource.h/.cpp` / `src/renderer/EnvironmentBrdfLutGenerator.h/.cpp`
   - 看 diffuse irradiance cubemap generation、prefiltered specular cubemap generation 和 BRDF LUT generation 的 resource ownership、descriptor layout、uniform resources、barrier、dynamic rendering 和 draw path。
33. `src/renderer/passes/SkyboxPass.h/.cpp`
   - 看 cubemap background draw 的 descriptor layout、uniform、TextureCube binding、no-op fallback 和 pipeline state。
34. `shaders/equirect_to_cube.vert.hlsl` / `shaders/equirect_to_cube.frag.hlsl`
   - 看 fullscreen triangle、face index、face orientation 和 equirectangular sampling。
35. `shaders/irradiance_convolve.vert.hlsl` / `shaders/irradiance_convolve.frag.hlsl` / `shaders/specular_prefilter.vert.hlsl` / `shaders/specular_prefilter.frag.hlsl` / `shaders/brdf_lut.vert.hlsl` / `shaders/brdf_lut.frag.hlsl`
   - 看 fullscreen triangle、face index、cubemap sampling、半球积分、GGX importance sampling、roughness mip chain、BRDF integration LUT 和 linear HDR/data 输出。
36. `shaders/skybox.vert.hlsl` / `shaders/skybox.frag.hlsl`
   - 看 fullscreen triangle、inverse projection / inverse view rotation 和 cubemap sampling。
37. `src/asset/TextureLoader.h/.cpp`
   - 看 `loadImageRgba8()` 对 HDR 的拒绝语义，以及 `loadImageHdrRgba32F()` 的 float RGBA32F 输出路径。
38. `src/rhi/RHICommon.h` / `src/rhi/vulkan/VulkanCommon.cpp`
   - 看 `RGBA32Float` format 枚举、format name 和 Vulkan format mapping。
39. `src/rhi/vulkan/VulkanCommandContext.cpp`
   - 看 RGBA8 / RGBA32Float upload 的 bytes-per-pixel 约束，以及 HDR mip generation 仍未支持的边界。
40. `src/renderer/EnvironmentResource.h/.cpp`
   - 看 HDR environment texture resource 的创建、upload、sampler policy、deferred release 和 reset 行为。
41. `src/renderer/FrameContext.h`
   - 看 `sceneColorView`、`colorFormat`、`depthFormat`、environment/irradiance/specular/BRDF LUT 指针的 pass 间共享语义。
42. `src/renderer/FrameRenderer.cpp`
   - 看 `RGBA16Float` scene color 创建、scene pass / tone mapping pass 两段 dynamic rendering、barrier 和 viewport/scissor。
43. `src/renderer/RenderView.h`
   - 看 camera position、`setDefaultPerspective()`、显式 `setMatrices()`、旧 `setMatrices()` 兼容路径和 `ToneMappingSettings`。
44. `src/renderer/passes/ToneMappingPass.h/.cpp`
   - 看 per-frame descriptor set、scene color sampled image/sampler binding、per-frame tone mapping uniform buffer、fullscreen triangle draw 和 pipeline format。
45. `shaders/tonemap.vert.hlsl` / `shaders/tonemap.frag.hlsl`
   - 看 `SV_VertexID` fullscreen triangle、HDR scene color sampling、uniform exposure、Reinhard tone mapping 和 output gamma encoding。
46. `src/renderer/RenderScene.h/.cpp`
   - 看 `DirectionalLight`、`SceneLighting`、`SceneEnvironment`、`RenderScene::lighting()`、`RenderScene::setLighting()` 和 environment API。
47. `src/asset/MeshData.h/.cpp`
   - 看 `MeshVertex::uv1`、tangent 字段、`TextureTransformData`、`MaterialTextureSlotData` 和 `generateTangents()`。
48. `src/asset/GltfLoader.cpp`
   - 看 sampler、alpha render state、`TEXCOORD_1`、`KHR_texture_transform`、显式/生成 tangent、scene/node instance 的读取路径。
49. `src/renderer/ModelResource.cpp`
   - 看 asset sampler 到 RHI sampler 的转换、texture cache 获取和 fallback texture。
50. `src/renderer/material/MaterialResource.h/.cpp`
   - 看 material factors、render state、texture references、per-slot texCoord set、per-slot transform set 和 descriptor 写入。
51. `src/renderer/RenderQueue.cpp`
   - 看 scene/model draw item 展开、Opaque / Mask / Blend 分桶和 bucket 合并顺序。
52. `src/renderer/passes/ForwardPass.cpp`
   - 看 descriptor layout、binding 14/15、binding 16/17、binding 18/19、binding 20/21、fallback environment、fallback irradiance cube、fallback specular cube、fallback BRDF LUT、environment upload、irradiance/specular enabled flag、max prefiltered mip、pipeline variant key、FrameContext attachment format 解耦、vertex layout、doubleSided cull mode、camera/object/material/lighting uniform、scene lighting / view camera position / environment/specular resources 读取、per-slot transform 写入和 draw loop。
53. `shaders/mesh.vert.hlsl` / `shaders/mesh.frag.hlsl`
   - 确认 normal matrix、uv1 传递、per-slot `selectUv()` + `transformUv()`、alpha mask/blend、Cook-Torrance direct BRDF、irradiance cubemap ambient、equirectangular fallback、prefiltered specular roughness mip sampling、BRDF LUT sampling 和 split-sum specular IBL 路径。
54. `src/rhi/vulkan/VulkanPipelineState.cpp` / `src/rhi/vulkan/VulkanSampler.cpp`
    - 看 blend/cull/depth state 和 sampler address/filter 映射。
55. `tests/readback_api_smoke.cpp` / `tests/cubemap_orientation_pixel_smoke.cpp` / `tests/cubemap_orientation_contract_smoke.cpp` / `tests/sandbox_camera_controller_smoke.cpp` / `tests/environment_cube_resource_smoke.cpp` / `tests/environment_resource_smoke.cpp` / `tests/texture_loader_smoke.cpp` / `tests/forward_pass_pipeline_smoke.cpp` / `tests/tone_mapping_pass_smoke.cpp` / `tests/render_scene_queue_smoke.cpp` / `tests/model_resource_smoke.cpp` / `tests/gltf_loader_smoke.cpp` / `tests/shader_assets_smoke.cpp` / `tests/framework_headers_smoke.cpp`
   - 看当前 smoke tests 对 readback API、cubemap orientation pixel validation、cubemap orientation contract、sandbox camera controller、cubemap resource、HDR loader、EnvironmentResource、SceneEnvironment、ForwardPass HDR attachment format、cull state、lighting/specular uniform、ForwardPass environment/irradiance/specular descriptors、ToneMappingPass uniform 数据流、queue alpha bucket、sampler、alpha、uv1、texture transform、BRDF/IBL shader source、tone mapping shader source 和 public API 的约束。

## 5. 必须继续遵守的架构边界

后续开发前继续阅读：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase18.md
docs/phase/phase19.md
docs/phase/phase20.md
docs/phase/phase21.md
docs/phase/phase22.md
docs/phase/phase23.md
docs/phase/phase24.md
docs/phase/phase25.md
docs/phase/phase26.md
docs/phase/phase27.md
docs/phase/phase28.md
docs/phase/phase29.md
docs/phase/phase30.md
docs/phase/phase31.md
docs/phase/phase32.md
docs/phase/phase33.md
docs/phase/phase34.md
docs/phase/phase35.md
docs/phase/phase36.md
docs/phase/phase37.md
docs/phase/phase38.md
docs/phase/phase39.md
docs/phase/phase40.md
docs/phase/phase41.md
docs/phase/phase42.md
docs/phase/phase43.md
```

硬性边界：

- 只有 `src/rhi/vulkan/` 可以包含 Vulkan 头文件和 `Vk*` 类型。
- `asset/` 只解析外部文件并输出 CPU 数据，不创建 RHI/GPU 资源。
- `asset/` 不依赖 renderer/RHI/Vulkan；需要新增语义时先放 asset 自有数据结构。
- tangent generation 属于 asset CPU 后处理，不进入 renderer/RHI/Vulkan。
- `TextureLoader` 只输出 CPU `ImageData`，不决定 GPU sRGB/linear 采样语义，也不参与 mip generation。
- HDR loader 必须保持显式入口；不要让 `loadImageRgba8()` 静默量化 HDR。
- texture transform 属于 material slot 语义；不要放进 `TextureResource` 或 `TextureCache` key。
- `renderer/` 可以创建和持有 RHI 资源，但不能接触 Vulkan 类型。
- `RenderScene` 保存 scene 语义，不创建 GPU 资源。
- `RenderScene` 的 environment slot 只保存 `EnvironmentResource*` 和 intensity，不拥有或创建 environment resource。
- `EnvironmentCubeResource` 只是 renderer 层 cubemap resource foundation；当前不接入 `RenderScene`，也不替换 `EnvironmentResource`。
- scene lighting 属于 `RenderScene` 语义，不进入 RHI/Vulkan，也不由 `ForwardPass` 决定默认场景策略。
- camera position 属于 `RenderView` 语义；`ForwardPass` 只读取并写入 uniform。
- tone mapping settings 属于 `RenderView` 语义；`ToneMappingPass` 只通过 `FrameContext::view` 读取并写入自己的 uniform。
- direct lighting BRDF 当前只落在 `shaders/mesh.frag.hlsl`；不为 shader 公式升级改 RHI/Vulkan 或 descriptor layout。
- environment ambient lighting 已接入 `ForwardPass` / `mesh.frag.hlsl`，但只作为 equirectangular ambient contribution，不代表完整 IBL。
- `ForwardPass` 负责 environment descriptor 完整性和 fallback environment；`RenderScene` 仍不拥有 GPU resource。
- `RenderQueue` 是 draw list，不拥有底层 GPU 资源；当前只做 Opaque / Mask / Blend alpha bucket ordering，不做完整 transparent sorting。
- `ModelResource` 是 renderer 层 GPU resource owner，通过 RHI 创建资源。
- `EnvironmentResource` 是 renderer 层 GPU resource owner，不复用 material `TextureResource` 的 sRGB/linear policy，也暂不进入 `TextureCache`。
- local texture cache 可由 `ModelResource` 管理；external texture cache 必须由外部拥有者管理。
- `MaterialResource` 保存 material 语义和 texture 引用，并负责 descriptor 写入。
- `ForwardPass` 负责 pipeline、descriptor、per-frame/per-draw binding、doubleSided culling 和 draw，不负责 asset loading、cache 或 resource lifetime。
- `ForwardPass` 的 pipeline attachment format 应来自当前 render scope 的 `FrameContext::colorFormat` / `depthFormat`，不要重新绑定到 swapchain format。
- `FrameRenderer` 当前拥有 frame-level HDR scene color，不放入 `RenderScene`、`ForwardPass` 或 `ToneMappingPass`。
- `ToneMappingPass` 只负责采样 scene color 并写 backbuffer，不拥有 scene color 生命周期。
- tone mapping 已支持最小 exposure / output gamma 参数化，但仍不代表完整 post-process stack。
- output encoding 当前只应发生在 tone mapping shader；mesh/lighting shader 继续输出 linear HDR。
- upload / mip generation / deferred release 命令必须记录在 dynamic rendering scope 外。
- `RGBA32Float` environment upload 当前只支持 tightly packed mip0/layer0 2D equirectangular texture；不要把它理解为 cubemap CPU upload、array texture 或 HDR mip generation 已完成。
- cubemap RHI/Vulkan 语义、equirectangular -> cubemap conversion、diffuse irradiance generation、ForwardPass diffuse IBL、face orientation debug foundation、最小 GPU readback / pixel validation、face-mip render target view foundation、prefiltered specular generator foundation、BRDF LUT foundation、默认 renderer specular bake path 和 ForwardPass specular IBL 已存在，但当前没有 cubemap CPU upload、cubemap mip generation policy、screenshot/golden validation 或 specular quality policy。
- 当前继续使用 Vulkan Dynamic Rendering，不引入传统 `VkRenderPass` / `VkFramebuffer`。
- 日志输出使用英文。
- 必要注释使用简洁中文。

## 6. 已知风险和 TODO

P0 / 下一阶段优先：

- Blend draw 已通过 RenderQueue alpha bucket 保证位于 Opaque / Mask 之后，但仍未做 back-to-front sorting；当前不声明透明排序完全正确。
- tangent generation 不是 MikkTSpace；与 DCC/baker 的 tangent basis 可能不完全一致。
- tangent generation 仍基于 `uv0`；如果 normal texture 使用 `texCoord=1`，严格 tangent basis 仍是后续改进项。
- `KHR_texture_transform` 已支持 textureInfo 上的 offset / scale / rotation / texCoord override，但不支持 animation、`TEXCOORD_2+` 或 per-UV-set tangent basis。
- `doubleSided=true` 当前只关闭背面剔除，不做 two-sided lighting，也不基于 `gl_FrontFacing` 翻转 normal。
- 当前 direct lighting 已使用 Cook-Torrance BRDF，并通过 `RGBA16Float` scene color + ToneMappingPass 输出；HDR environment ambient、equirectangular -> cubemap conversion、skybox、diffuse irradiance generation、ForwardPass diffuse IBL、face orientation debug foundation、automated cubemap pixel validation、prefiltered specular generator foundation、BRDF LUT foundation、默认 renderer specular bake path 和 ForwardPass specular IBL 已接入，但仍没有 screenshot/golden frame validation、roughness/metallic 视觉 fixture、specular quality policy、shadow 或多光源。
- sandbox 已有 orbit camera controller，但仍不是 editor camera，不支持 glTF camera、camera preset、selection focus、多 viewport 或 action mapping。
- tone mapping 当前只有手动 `exposure` / `outputGamma` + Reinhard，不支持 auto exposure、ACES 参数化、bloom 或 color grading。
- 当前只支持一个 directional light 和 ambient color；不支持 point / spot / area light、shadow、glTF camera 或 `KHR_lights_punctual`。
- `Renderer` 内部默认 scene 仍是 sandbox 过渡方案；真正 renderer 级资源/场景加载入口尚未设计。
- `assets/models/DamagedHelmet/` 可作为本地真实 glTF 2.0 默认优先验证对象，但该目录受 `.gitignore` 保护，不应作为提交依赖。
- descriptor set / descriptor layout / pipeline / shader module 的 deferred destruction 仍未纳入。

P1：

- texture upload 仍只覆盖 RGBA8 / RGBA8 sRGB / RGBA32Float、mip0 upload、array layer 0、tightly packed rows。
- mip generation 只支持 2D color texture、single array layer、GPU blit；不支持离线 mip、array/cubemap、HDR 或压缩纹理。
- `EnvironmentCubeResource` 当前仍不支持 CPU 侧直接填充 cubemap faces，也没有通用 `RenderScene` cubemap/irradiance/specular resource API；默认 renderer 只通过内部 conversion/irradiance/specular prefilter/BRDF LUT generation path 填充默认资源。
- glTF skin、animation、morph target、embedded image、data URI image、`TEXCOORD_2+` 尚未支持。
- glTF extensions 目前只完成 `KHR_texture_transform` 最小支持；`KHR_materials_*` 等仍未支持。
- sampler cache 尚未独立拆分；同 image 不同 sampler 当前会重复创建 texture/view/sampler。
- anisotropy、compare sampler、shadow sampler 尚未接入。
- shader binding 与 descriptor layout 仍靠人工一致，后续可考虑 reflection 或测试校验。
- `CubePass` 仍保留为阶段性 debug pass，后续应决定保留、隐藏还是清理。
- `VulkanDescriptorManager` 有 growable pools，但尚无 free/reset 策略和容量统计。
- per-draw descriptor/object/material uniform 策略简单直接，draw 数量上升时应评估 push constants、dynamic uniform buffer 或 storage buffer。

P2：

- Vulkan debug object names。
- RenderDoc capture / screenshot / pixel smoke。
- ResourceManager handle 系统。
- RenderGraph 第一版 pass/resource 声明。
- IBL 质量策略、bloom、auto exposure。

## 7. 最近验证记录

Phase 0.27 收尾在 Windows/MSVC vcpkg debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_shader_assets_smoke
cmake --build --preset msvc-vcpkg-debug --target ark_forward_pass_pipeline_smoke
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
cmake --build --preset msvc-vcpkg-debug --target ark_sandbox -- /t:Rebuild /m:1
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：

```text
targeted shader assets build passed
targeted forward pass pipeline build passed
ark_shader_assets_smoke passed
ark_forward_pass_pipeline_smoke passed
single-process ark_sandbox rebuild passed
full build passed
CTest: 9/9 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
```

本轮 sandbox smoke 使用隐藏窗口启动 3 秒后自动停止，用于确认默认场景和 DamagedHelmet 路径不会启动即退出。期间曾遇到一次并行 MSBuild 写 PDB/tlog 锁和一次脏 `ToneMappingPass.obj` 链接问题；通过顺序构建和单进程 Rebuild 清理中间产物后，完整 build、CTest 和 runtime smoke 均通过。

Phase 0.28 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_tone_mapping_pass_smoke
build/msvc-vcpkg/Debug/ark_tone_mapping_pass_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：

```text
targeted ark_tone_mapping_pass_smoke build passed
ark_tone_mapping_pass_smoke passed
full build passed
CTest: 10/10 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
```

Phase 0.29 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_environment_resource_smoke ark_texture_loader_smoke ark_render_scene_queue_smoke ark_framework_headers_smoke
build/msvc-vcpkg/Debug/ark_environment_resource_smoke.exe
build/msvc-vcpkg/Debug/ark_texture_loader_smoke.exe
build/msvc-vcpkg/Debug/ark_render_scene_queue_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：

```text
targeted smoke build passed
ark_environment_resource_smoke passed
ark_texture_loader_smoke passed
ark_render_scene_queue_smoke passed
ark_framework_headers_smoke passed
full build passed
CTest: 11/11 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
```

本轮 sandbox smoke 使用隐藏窗口启动 3 秒后自动停止，用于确认默认场景和 DamagedHelmet 路径不会启动即退出。`texture_loader_smoke` 和 `environment_resource_smoke` 中出现的 error log 是测试刻意触发非法输入路径，用于验证拒绝行为，进程退出码为 0。

Phase 0.30 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_forward_pass_pipeline_smoke ark_shader_assets_smoke ark_framework_headers_smoke ark_render_scene_queue_smoke
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
build/msvc-vcpkg/Debug/ark_render_scene_queue_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：

```text
targeted smoke build passed
ark_forward_pass_pipeline_smoke passed
ark_shader_assets_smoke passed
ark_framework_headers_smoke passed
ark_render_scene_queue_smoke passed
full build passed
CTest: 11/11 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
```

本轮 sandbox smoke 使用隐藏窗口启动 3 秒后自动停止，用于确认默认场景和 DamagedHelmet 路径不会启动即退出。

Phase 0.31 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug --target ark_environment_cube_resource_smoke ark_framework_headers_smoke ark_environment_resource_smoke ark_forward_pass_pipeline_smoke
build/msvc-vcpkg/Debug/ark_environment_cube_resource_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
build/msvc-vcpkg/Debug/ark_environment_resource_smoke.exe
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
configure passed
targeted smoke build passed
ark_environment_cube_resource_smoke passed
ark_framework_headers_smoke passed
ark_environment_resource_smoke passed
ark_forward_pass_pipeline_smoke passed
full build passed
CTest: 12/12 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

本轮 sandbox smoke 使用隐藏窗口启动后自动停止，用于确认默认场景、DamagedHelmet 路径和本地 HDR environment 路径不会启动即退出。`ark_environment_cube_resource_smoke` 与 `ark_environment_resource_smoke` 中出现的 error log 是测试刻意触发非法输入路径，用于验证拒绝行为，进程退出码为 0。`assets/HDR/warm_restaurant_8k.hdr` 是本地真实 HDRI，约 98MB，受 `.gitignore` 保护，不随 Phase 0.31 提交。

Phase 0.32 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug --target ark_equirectangular_to_cube_smoke ark_environment_cube_resource_smoke ark_shader_assets_smoke ark_framework_headers_smoke ark_forward_pass_pipeline_smoke
build/msvc-vcpkg/Debug/ark_equirectangular_to_cube_smoke.exe
build/msvc-vcpkg/Debug/ark_environment_cube_resource_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
configure passed
targeted smoke build passed
ark_equirectangular_to_cube_smoke passed
ark_environment_cube_resource_smoke passed
ark_shader_assets_smoke passed
ark_framework_headers_smoke passed
full build passed
CTest: 13/13 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

本轮 sandbox smoke 使用隐藏窗口启动后自动停止，用于确认默认场景、DamagedHelmet 路径和本地 HDR environment conversion path 不会启动即退出。`ark_environment_cube_resource_smoke` 中出现的 error log 是测试刻意触发非法输入路径，用于验证拒绝行为，进程退出码为 0。Phase 0.32 尚未提供 skybox 或 readback，因此 face orientation 仍需后续可视化或像素测试验证。

Phase 0.33 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug --target ark_skybox_pass_smoke ark_shader_assets_smoke ark_framework_headers_smoke ark_equirectangular_to_cube_smoke ark_forward_pass_pipeline_smoke
build/msvc-vcpkg/Debug/ark_skybox_pass_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
build/msvc-vcpkg/Debug/ark_equirectangular_to_cube_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
configure passed
targeted smoke build passed
ark_skybox_pass_smoke passed
ark_shader_assets_smoke passed
ark_framework_headers_smoke passed
ark_equirectangular_to_cube_smoke passed
full build passed
CTest: 14/14 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

本轮 sandbox smoke 使用隐藏窗口启动后自动停止，用于确认默认模型、默认/procedural environment、cubemap conversion、SkyboxPass、ForwardPass 和 ToneMappingPass 串联后不会启动即退出。Phase 0.33 已能显示 cubemap debug skybox，但 face orientation 仍建议后续使用 debug fixture 或 readback/pixel test 严格验证。

Phase 0.34 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug --target ark_environment_irradiance_smoke ark_shader_assets_smoke ark_framework_headers_smoke ark_equirectangular_to_cube_smoke ark_skybox_pass_smoke
build/msvc-vcpkg/Debug/ark_environment_irradiance_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
build/msvc-vcpkg/Debug/ark_equirectangular_to_cube_smoke.exe
build/msvc-vcpkg/Debug/ark_skybox_pass_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
configure passed
targeted smoke build passed
ark_environment_irradiance_smoke passed
ark_shader_assets_smoke passed
ark_framework_headers_smoke passed
ark_equirectangular_to_cube_smoke passed
ark_skybox_pass_smoke passed
full build passed
CTest: 15/15 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

本轮 sandbox smoke 使用隐藏窗口启动后自动停止，用于确认默认 environment conversion、irradiance generation、SkyboxPass、ForwardPass 和 ToneMappingPass 串联后不会启动即退出。`ark_environment_irradiance_smoke` 中的 `InvalidSameCubeIrradiance` error log 是测试刻意触发非法输入路径，用于验证 `source == target` 会被拒绝，进程退出码为 0。Phase 0.34 只生成 irradiance cubemap；mesh lighting 尚未切换到 cubemap irradiance IBL。

Phase 0.35 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_forward_pass_pipeline_smoke ark_shader_assets_smoke ark_framework_headers_smoke ark_environment_irradiance_smoke ark_skybox_pass_smoke
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
build/msvc-vcpkg/Debug/ark_environment_irradiance_smoke.exe
build/msvc-vcpkg/Debug/ark_skybox_pass_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
targeted smoke build passed
ark_forward_pass_pipeline_smoke passed
ark_shader_assets_smoke passed
ark_framework_headers_smoke passed
ark_environment_irradiance_smoke passed
ark_skybox_pass_smoke passed
full build passed
CTest: 15/15 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

本轮 sandbox smoke 使用隐藏窗口启动后自动停止，用于确认默认 environment conversion、irradiance generation、SkyboxPass、ForwardPass diffuse irradiance descriptor path 和 ToneMappingPass 串联后不会启动即退出。`ark_environment_irradiance_smoke` 中的 `InvalidSameCubeIrradiance` error log 是测试刻意触发非法输入路径，用于验证 `source == target` 会被拒绝，进程退出码为 0。Phase 0.35 只完成 diffuse irradiance IBL；仍不是完整 image-based lighting。

Phase 0.36 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_sandbox_camera_controller_smoke ark_framework_headers_smoke ark_sandbox
build/msvc-vcpkg/Debug/ark_sandbox_camera_controller_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
targeted smoke build passed
ark_sandbox_camera_controller_smoke passed
ark_framework_headers_smoke passed
full build passed
CTest: 16/16 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

本轮 sandbox smoke 使用隐藏窗口启动后自动停止，用于确认 Phase 0.36 app input / camera controller 接入后默认、DamagedHelmet 和本地 HDR environment 路径不会启动即退出。runtime smoke 不等同于自动交互或视觉正确性验证；orbit / pan / zoom 的数学路径由 `ark_sandbox_camera_controller_smoke` 覆盖，最终手感仍建议人工打开 sandbox 验证。

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

本轮 sandbox smoke 使用隐藏窗口启动后自动停止，用于确认默认 environment path、debug orientation environment path 和本地真实模型 + HDR environment path 不会启动即退出。Phase 0.37 已把 face order / direction debug foundation 固化为 CPU contract/source smoke 和人工可视化入口，但仍不是 GPU readback 或 automated pixel validation。

Phase 0.38 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

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

本轮新增 readback 验证仍是同步 smoke：GPU copy 提交后用 `RenderDevice::waitIdle()` 等待，再从 `GpuToCpu` buffer 读取。它证明 cubemap face order / layer / major-axis direction 的 GPU conversion 结果与 contract 一致，但不等同于完整 screenshot、async capture 或 golden image system。

Phase 0.39 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

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

本轮只完成 cubemap face-mip render target view foundation：它让 renderer 能按 face+mip 查询 render target view，并保留旧 mip0 face view API。后续 prefiltered specular environment 可以直接在这个基础上逐 mip 渲染 roughness 结果。

Phase 0.40 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_specular_prefilter_smoke ark_shader_assets_smoke ark_framework_headers_smoke
build/msvc-vcpkg/Debug/ark_specular_prefilter_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
targeted smoke build passed
ark_specular_prefilter_smoke passed
ark_shader_assets_smoke passed
ark_framework_headers_smoke passed
full build passed
CTest: 20/20 passed
default sandbox smoke passed
debug orientation sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

本轮只完成 prefiltered specular cubemap generation foundation：generator 可以从 source cubemap 渲染 target cubemap 的完整 face+mip roughness chain，并在 fake RHI smoke 中验证 render path。它还没有接入 renderer 默认 environment bake、`FrameContext`、`ForwardPass` descriptor layout 或 mesh shader specular IBL。

Phase 0.41 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_brdf_lut_smoke ark_shader_assets_smoke ark_framework_headers_smoke
build/msvc-vcpkg/Debug/ark_brdf_lut_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
targeted smoke build passed
ark_brdf_lut_smoke passed
ark_shader_assets_smoke passed
ark_framework_headers_smoke passed
full build passed
CTest: 21/21 passed
default sandbox smoke passed
debug orientation sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

本轮只完成 BRDF integration LUT foundation：resource/generator/shader 已能生成 split-sum BRDF LUT，并在 fake RHI smoke 中验证 resource lifetime 和 render path。它还没有接入 renderer 默认 environment bake、`FrameContext`、`ForwardPass` descriptor layout 或 mesh shader specular IBL。

Phase 0.42 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_renderer ark_framework_headers_smoke
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
targeted renderer/framework headers build passed
full build passed
CTest: 21/21 passed
default sandbox smoke passed
debug orientation sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

本轮完成 renderer default specular bake path：默认 renderer 现在持有并生成 `DefaultSandboxSpecularCube` 和 `DefaultSandboxBrdfLut`，并通过 `FrameContext::prefilteredSpecularCube` / `FrameContext::brdfLut` 传递给后续 pass。它仍没有改 `ForwardPass` descriptor layout，也没有让 mesh shader 消费 specular IBL。

Phase 0.43 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_forward_pass_pipeline_smoke ark_shader_assets_smoke
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
targeted forward pass / shader assets build passed
ark_forward_pass_pipeline_smoke passed
ark_shader_assets_smoke passed
full build passed
CTest: 21/21 passed
default sandbox smoke passed
debug orientation sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

本轮完成 `ForwardPass` specular IBL 最小接入：mesh shader 已消费 default renderer 传入的 prefiltered specular cubemap 和 BRDF LUT，并在缺少真实资源时绑定 fallback descriptor 但通过 `environment.w = 0` 禁用 specular contribution。它仍没有 screenshot/golden image validation、roughness/metallic 视觉 fixture 或 specular quality policy。

## 8. 推荐下一步

Phase 0.43 已完成 `ForwardPass` specular IBL 最小接入。下一阶段建议不要继续扩 descriptor，而是先把验证和质量边界补上：

- 若继续推进 PBR/IBL：增加 roughness/metallic material ball fixture，验证 diffuse/specular IBL 在不同材质参数下的能量和视觉趋势。
- 若继续推进验证基础：基于 Phase 0.38 readback 扩展 frame color screenshot / golden image / statistical pixel smoke，至少覆盖 skybox + DamagedHelmet + specular highlight。

不要直接跳到 RenderGraph、bindless、完整材质扩展或复杂后处理栈。

优先顺序：

1. Roughness / metallic validation fixture or material ball grid。
2. Screenshot / frame color readback / golden image or statistical pixel infrastructure。
3. Specular IBL quality policy：prefilter size、BRDF LUT size、sample count、fallback semantics 和 performance budget。
4. glTF camera / scene camera selection。
5. HDR/cubemap mip generation policy。
6. bloom、auto exposure、ACES/filmic 或 exposure UI/config 可作为后续独立阶段。
7. 真正的 renderer 资源/场景加载入口，替代内部默认 scene 过渡方案。
8. 基于 camera 和 bounds 的 Blend bucket back-to-front sorting。
9. pipeline / shader / descriptor layout 的 deferred destruction。

## 9. 下一次 Codex 启动提示

```text
请先阅读 docs/codex_handoff.md，理解 ARKRenderer 当前已完成 Phase 0.43：KHR_texture_transform 最小闭环已经打通到 asset、GltfLoader、MaterialResource、ForwardPass uniform、mesh.frag.hlsl、fixture 和 smoke tests；RenderQueue alpha bucket ordering 已完成；ForwardPass 已按 glTF doubleSided 设置 raster culling；RenderScene / RenderView 已提供 scene lighting、camera position 和 tone mapping settings；mesh.frag.hlsl 已升级为 Cook-Torrance direct BRDF；FrameRenderer 已接入 RGBA16Float HDR scene color 和 ToneMappingPass；Phase 0.29 已新增 HDR loader、RGBA32Float upload、EnvironmentResource 和 RenderScene environment API；Phase 0.30 已把 environment resource 接入 ForwardPass binding 14/15、LightingUniform environment intensity/enabled、fallback environment 和 mesh.frag.hlsl equirectangular ambient sampling；Phase 0.31 已新增 assets/HDR 资源目录策略、RHI cube texture/view 语义、Vulkan cube-compatible image/view mapping 和 EnvironmentCubeResource；Phase 0.32 已新增 EnvironmentCubeResource face render target views、equirectangular-to-cube shaders、EnvironmentCubeConverter、默认 renderer minimal conversion path 和 ark_equirectangular_to_cube_smoke；Phase 0.33 已新增 FrameContext::environmentCube、skybox.vert/frag.hlsl、SkyboxPass、默认/procedural sandbox environment fallback 和 ark_skybox_pass_smoke；Phase 0.34 已新增 irradiance_convolve.vert/frag.hlsl、EnvironmentIrradianceGenerator、默认 renderer 32x32 RGBA16F irradiance bake path 和 ark_environment_irradiance_smoke；Phase 0.35 已新增 FrameContext::irradianceCube、ForwardPass irradiance binding 16/17、fallback irradiance cubemap、LightingUniform environment.z irradiance flag 和 mesh.frag.hlsl TextureCube diffuse irradiance IBL；Phase 0.36 已新增 app/Input.h、Window::getInputSnapshot()、GlfwWindow 输入采集、SandboxCameraController、Application 接入和 ark_sandbox_camera_controller_smoke；Phase 0.37 已新增 CubemapOrientation face contract、SandboxEnvironment procedural/debug environment helpers、Application/Renderer debug flag、ark_sandbox --debug-orientation 和 ark_cubemap_orientation_contract_smoke；Phase 0.38 已新增 MemoryUsage::GpuToCpu、Buffer::readData()、TextureReadbackDesc、DeviceContext::copyTextureToBuffer()、Vulkan readback buffer/image-to-buffer copy、EnvironmentCubeResourceDesc::allowReadback、ark_readback_api_smoke 和 ark_cubemap_orientation_pixel_smoke；Phase 0.39 已新增 EnvironmentCubeResource face-mip render target views、faceMipRenderTargetView()、mipExtent() 和对应 smoke coverage；Phase 0.40 已新增 EnvironmentSpecularPrefilterGenerator、specular_prefilter shaders 和 ark_specular_prefilter_smoke；Phase 0.41 已新增 EnvironmentBrdfLutResource、EnvironmentBrdfLutGenerator、brdf_lut shaders 和 ark_brdf_lut_smoke；Phase 0.42 已新增默认 renderer specular cubemap / BRDF LUT resource lifetime、one-shot bake path 和 FrameContext specular resource plumbing；Phase 0.43 已新增 ForwardPass specular descriptor/fallback path、LightingUniform environmentSpecular、mesh.frag.hlsl split-sum specular IBL 和相关 smoke coverage。当前已有最小 GPU readback、automated cubemap pixel validation、cubemap face-mip view foundation、prefiltered specular generator foundation、BRDF LUT foundation、默认 renderer specular bake path 和 ForwardPass specular IBL，但仍没有 screenshot/golden validation、roughness/metallic validation fixture、specular quality policy、glTF camera、bloom 或 auto exposure。

重点理解当前默认渲染路径：
Vulkan Dynamic Rendering + Application InputSnapshot + SandboxCameraController + Renderer + RenderScene scene lighting + SceneEnvironment slot + optional one-shot EnvironmentCubeConverter + optional one-shot EnvironmentIrradianceGenerator + optional one-shot EnvironmentSpecularPrefilterGenerator + optional one-shot EnvironmentBrdfLutGenerator + FrameContext::environmentCube + FrameContext::irradianceCube + FrameContext::prefilteredSpecularCube + FrameContext::brdfLut + CubemapOrientation face contract + SandboxEnvironment procedural/debug environment helpers + ark_sandbox --debug-orientation + RenderView camera matrix/camera position + ToneMappingSettings + RenderQueue alpha buckets + FrameRenderer two-stage rendering + RGBA16Float scene color + ClearPass + SkyboxPass TextureCube background + ForwardPass doubleSided culling + ForwardPass fallback environment + ForwardPass fallback irradiance cubemap + ForwardPass fallback specular cubemap + ForwardPass fallback BRDF LUT + ModelResource + MeshResource + MaterialResource + TextureResource + TextureCache + EnvironmentResource + EnvironmentCubeResource face-mip views + EnvironmentSpecularPrefilterGenerator resource foundation and default bake path + EnvironmentBrdfLutResource/Generator resource foundation and default bake path + glTF scene/node primitive instances + RenderView camera uniform + per-draw object/material/lighting uniform + environment texture/sampler descriptors + irradiance cubemap/sampler descriptors + prefiltered specular cubemap/sampler descriptors + BRDF LUT/sampler descriptors + normal matrix + sampled images/samplers + GPU mipmap generation + Cook-Torrance direct BRDF + diffuse irradiance cubemap ambient + split-sum specular IBL + equirectangular environment fallback + generated/explicit tangent + glTF sampler + alpha render states + TEXCOORD_1 / per-slot UV selection + KHR_texture_transform per-slot transform + indexed textured multi draw + depth attachment + ToneMappingPass fullscreen triangle + exposure/Reinhard/output gamma encoding + swapchain backbuffer。

然后阅读：
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase18.md
docs/phase/phase19.md
docs/phase/phase20.md
docs/phase/phase21.md
docs/phase/phase22.md
docs/phase/phase23.md
docs/phase/phase24.md
docs/phase/phase25.md
docs/phase/phase26.md
docs/phase/phase27.md
docs/phase/phase28.md
docs/phase/phase29.md
docs/phase/phase30.md
docs/phase/phase31.md
docs/phase/phase32.md
docs/phase/phase33.md
docs/phase/phase34.md
docs/phase/phase35.md
docs/phase/phase36.md
docs/phase/phase37.md
docs/phase/phase38.md
docs/phase/phase39.md
docs/phase/phase40.md
docs/phase/phase41.md
docs/phase/phase42.md
docs/phase/phase43.md

不要重复 Phase 0.5 ~ 0.43 已完成工作。
不要重复 Phase 0.22 已完成的 KHR_texture_transform 最小闭环，不要重复 Phase 0.23 已完成的 RenderQueue alpha bucket，不要重复 Phase 0.24 已完成的 doubleSided culling，不要重复 Phase 0.25 已完成的 scene light / camera 数据入口，不要重复 Phase 0.26 已完成的 direct lighting BRDF，不要重复 Phase 0.27 已完成的 HDR scene color / ToneMappingPass 最小闭环，不要重复 Phase 0.28 已完成的 tone mapping settings / color pipeline 收口，不要重复 Phase 0.29 已完成的 HDR loader / RGBA32Float upload / EnvironmentResource / RenderScene environment API，不要重复 Phase 0.30 已完成的最小 equirectangular environment ambient 接入，不要重复 Phase 0.31 已完成的 RHI/Vulkan cubemap resource foundation，不要重复 Phase 0.32 已完成的 equirectangular -> cubemap conversion foundation，不要重复 Phase 0.33 已完成的 SkyboxPass / cubemap debug background foundation，不要重复 Phase 0.34 已完成的 diffuse irradiance cubemap generation foundation，不要重复 Phase 0.35 已完成的 ForwardPass diffuse irradiance IBL，不要重复 Phase 0.36 已完成的 sandbox orbit camera controller，不要重复 Phase 0.37 已完成的 cubemap face orientation debug foundation，不要重复 Phase 0.38 已完成的最小 RHI texture readback / automated cubemap pixel validation，不要重复 Phase 0.39 已完成的 cubemap face-mip view foundation，不要重复 Phase 0.40 已完成的 prefiltered specular environment generator foundation，不要重复 Phase 0.41 已完成的 BRDF LUT foundation，不要重复 Phase 0.42 已完成的 renderer default specular bake path / FrameContext specular resource plumbing，也不要重复 Phase 0.43 已完成的 ForwardPass specular IBL descriptor/fallback/shader 接入。下一步优先考虑 roughness/metallic validation fixture、screenshot/frame color readback/golden validation、specular quality policy、glTF camera、bloom/auto exposure 或 renderer 资源/场景加载入口等小步。不要提前引入完整 RenderGraph、bindless、复杂 glTF extensions 或完整材质扩展，除非用户明确改变目标。

如果实现方向与既有设计文档冲突，先说明并更新设计文档，再修改代码。新增代码保持现有风格：左大括号不换行，namespace 内缩进，日志输出用英文，必要注释用简洁中文。不确定的地方写 TODO 或记录到文档，不要假装完成。

先执行并查看：
git status -sb
git diff --stat
git log --oneline -n 5
```
