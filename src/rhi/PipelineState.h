#pragma once

#include "rhi/RHICommon.h"

#include <string>
#include <vector>

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

    enum class VertexInputRate {
        PerVertex,
        PerInstance,
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

    struct VertexAttributeDesc {
        u32 location = 0;
        Format format = Format::Unknown;
        u32 offset = 0;
    };

    struct VertexBufferLayoutDesc {
        u32 binding = 0;
        u32 stride = 0;
        VertexInputRate inputRate = VertexInputRate::PerVertex;
        std::vector<VertexAttributeDesc> attributes;
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

        std::vector<VertexBufferLayoutDesc> vertexBuffers;
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
