#include "core/FileSystem.h"

#include "core/Log.h"

#include <fstream>
#include <system_error>

namespace ark {
    bool fileExists(const Path& path) {
        std::error_code errorCode;
        return std::filesystem::is_regular_file(path, errorCode);
    }

    std::vector<u8> readBinaryFile(const Path& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            ARK_ERROR("Failed to open binary file: {}", path.string());
            return {};
        }

        const std::streamsize fileSize = file.tellg();
        if (fileSize < 0) {
            ARK_ERROR("Failed to get binary file size: {}", path.string());
            return {};
        }

        std::vector<u8> data(static_cast<usize>(fileSize));
        if (data.empty()) {
            return data;
        }

        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(data.data()), fileSize);
        if (!file) {
            ARK_ERROR("Failed to read binary file: {}", path.string());
            return {};
        }

        return data;
    }

    Path findFirstExistingPath(std::span<const Path> candidates) {
        for (const Path& path : candidates) {
            if (fileExists(path)) {
                return path;
            }
        }

        return {};
    }
} // namespace ark
