#pragma once

namespace ark {
    struct FrameContext;

    namespace rhi {
        class RenderDevice;
    } // namespace rhi

    // RenderPass 表达 renderer 层的一个可执行渲染步骤；接口只接触公共 RHI，不暴露后端对象。
    class RenderPass {
    public:
        virtual ~RenderPass() = default;

        // setup 用于创建 pass 私有 GPU 资源；默认空实现方便早期占位 pass 渐进落地。
        virtual void setup(rhi::RenderDevice& device);
        virtual bool prepare(FrameContext& frameContext);
        virtual bool execute(FrameContext& frameContext) = 0;
    };
} // namespace ark
