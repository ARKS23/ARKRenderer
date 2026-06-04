#pragma once

#include "rhi/Buffer.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DescriptorSetLayout.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/Sampler.h"
#include "rhi/Shader.h"
#include "rhi/SwapChain.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <memory>

namespace ark::rhi {
class RenderDevice {
public:
    virtual ~RenderDevice() = default;

    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual void waitIdle() = 0;
};

using RenderDevicePtr = std::unique_ptr<RenderDevice>;
} // namespace ark::rhi
