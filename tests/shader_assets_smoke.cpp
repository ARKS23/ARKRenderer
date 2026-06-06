#include <cstdint>
#include <filesystem>
#include <iostream>

#ifndef ARK_SHADER_OUTPUT_DIR
#define ARK_SHADER_OUTPUT_DIR "shaders"
#endif

namespace {
    bool validateSpirvFile(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            std::cerr << "Missing shader asset: " << path.string() << '\n';
            return false;
        }

        const std::uintmax_t size = std::filesystem::file_size(path);
        if (size == 0 || size % sizeof(std::uint32_t) != 0) {
            std::cerr << "Invalid shader asset size: " << path.string() << '\n';
            return false;
        }

        return true;
    }
} // namespace

int main() {
    const std::filesystem::path shaderDir = ARK_SHADER_OUTPUT_DIR;
    const bool vertexShaderValid = validateSpirvFile(shaderDir / "triangle.vert.spv");
    const bool fragmentShaderValid = validateSpirvFile(shaderDir / "triangle.frag.spv");

    return vertexShaderValid && fragmentShaderValid ? 0 : 1;
}
