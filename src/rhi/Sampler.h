#pragma once

namespace ark::rhi {
    enum class FilterMode {
        Nearest,
        Linear,
    };

    struct SamplerDesc {
        FilterMode minFilter = FilterMode::Linear;
        FilterMode magFilter = FilterMode::Linear;
    };

    class Sampler {
    public:
        virtual ~Sampler() = default;
    };
} // namespace ark::rhi
