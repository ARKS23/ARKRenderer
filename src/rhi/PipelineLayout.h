#pragma once

#include <string>
#include <vector>

namespace ark::rhi {
    class DescriptorSetLayout;

    struct PipelineLayoutDesc {
        std::string debugName;
        std::vector<DescriptorSetLayout*> descriptorSetLayouts;
    };

    class PipelineLayout {
    public:
        virtual ~PipelineLayout() = default;

        virtual const PipelineLayoutDesc& getDesc() const = 0;
    };
} // namespace ark::rhi
