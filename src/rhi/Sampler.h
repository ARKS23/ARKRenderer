#pragma once

#include <string>

namespace ark::rhi {
    enum class FilterMode {
        Nearest,
        Linear,
    };

    enum class AddressMode {
        Repeat,
        ClampToEdge,
        MirroredRepeat,
    };

    struct SamplerDesc {
        std::string debugName;
        FilterMode minFilter = FilterMode::Linear;
        FilterMode magFilter = FilterMode::Linear;
        FilterMode mipFilter = FilterMode::Linear;
        AddressMode addressU = AddressMode::Repeat;
        AddressMode addressV = AddressMode::Repeat;
        AddressMode addressW = AddressMode::Repeat;
    };

    class Sampler {
    public:
        virtual ~Sampler() = default;

        virtual const SamplerDesc& getDesc() const = 0;
    };
} // namespace ark::rhi
