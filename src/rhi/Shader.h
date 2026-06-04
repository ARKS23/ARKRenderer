#pragma once

#include <string>

namespace ark::rhi {
enum class ShaderStage {
    Vertex,
    Fragment,
    Compute,
};

struct ShaderDesc {
    ShaderStage stage = ShaderStage::Vertex;
    std::string debugName;
};

class Shader {
public:
    virtual ~Shader() = default;
};
} // namespace ark::rhi
