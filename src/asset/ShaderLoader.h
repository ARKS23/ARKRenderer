#pragma once

#include "core/FileSystem.h"
#include "core/Types.h"

#include <string_view>
#include <vector>

namespace ark::asset {
    Path findCompiledShaderFile(std::string_view fileName);
    std::vector<u32> readSpirvFile(const Path& path);
    std::vector<u32> loadCompiledShader(std::string_view fileName);
} // namespace ark::asset
