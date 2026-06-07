#pragma once

#include "core/Types.h"

#include <filesystem>
#include <span>
#include <vector>

namespace ark {
    using Path = std::filesystem::path;

    bool fileExists(const Path& path);
    std::vector<u8> readBinaryFile(const Path& path);
    Path findFirstExistingPath(std::span<const Path> candidates);
} // namespace ark
