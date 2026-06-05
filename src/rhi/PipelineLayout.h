#pragma once

#include <string>

namespace ark::rhi {
    struct PipelineLayoutDesc {
        std::string debugName;
    };

    class PipelineLayout {
    public:
        virtual ~PipelineLayout() = default;

        virtual const PipelineLayoutDesc& getDesc() const = 0;
    };
} // namespace ark::rhi
