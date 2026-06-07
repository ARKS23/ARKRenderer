#pragma once

#include "rhi/RHICommon.h"
#include "rhi/VertexInput.h"

#include <string>

namespace ark::rhi {
    class PipelineLayout;
    class Shader;

    enum class PrimitiveTopology {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip,
    };

    enum class PolygonMode {
        Fill,
        Line,
    };

    enum class CullMode {
        None,
        Front,
        Back,
    };

    enum class FrontFace {
        CounterClockwise,
        Clockwise,
    };

    enum class CompareOp {
        Never,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always,
    };

    struct RasterStateDesc {
        PolygonMode polygonMode = PolygonMode::Fill;
        CullMode cullMode = CullMode::None;
        FrontFace frontFace = FrontFace::CounterClockwise;
    };

    struct DepthStencilStateDesc {
        bool enableDepthTest = false;
        bool enableDepthWrite = false;
        CompareOp depthCompareOp = CompareOp::Less;
    };

    struct ColorBlendAttachmentDesc {
        bool enableBlend = false;
    };

    struct BlendStateDesc {
        ColorBlendAttachmentDesc colorAttachment;
    };

    struct GraphicsPipelineDesc {
        std::string debugName;
        Shader* vertexShader = nullptr;
        Shader* fragmentShader = nullptr;
        PipelineLayout* layout = nullptr;

        VertexInputLayoutDesc vertexInput;
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        RasterStateDesc rasterState;
        DepthStencilStateDesc depthStencilState;
        BlendStateDesc blendState;

        // dynamic rendering 需要在 pipeline 创建时指定附件格式。
        Format colorFormat = Format::Unknown;
        Format depthFormat = Format::Unknown;
    };

    class PipelineState {
    public:
        virtual ~PipelineState() = default;

        virtual const GraphicsPipelineDesc& getDesc() const = 0;
    };
} // namespace ark::rhi
