#include "asset/ShaderLoader.h"

#include "core/Log.h"

#include <array>
#include <cstring>
#include <string>

#ifndef ARK_SHADER_OUTPUT_DIR
#define ARK_SHADER_OUTPUT_DIR "shaders"
#endif

namespace ark::asset {
    namespace {
        constexpr u32 SpirvMagic = 0x07230203;
    } // namespace

    Path findCompiledShaderFile(std::string_view fileName) {
        if (fileName.empty()) {
            ARK_ERROR("Compiled shader file name is empty");
            return {};
        }

        const std::string fileNameString{fileName};
        // 正常路径来自 CMake 注入的 ARK_SHADER_OUTPUT_DIR；后两个候选用于开发期直接运行调试。
        const std::array<Path, 3> candidates{
            Path{ARK_SHADER_OUTPUT_DIR} / fileNameString,
            Path{"shaders"} / fileNameString,
            Path{"build/msvc-vcpkg/shaders"} / fileNameString,
        };

        Path shaderPath = findFirstExistingPath(candidates);
        if (shaderPath.empty()) {
            ARK_ERROR("Failed to find compiled shader: {}", fileNameString);
        }

        return shaderPath;
    }

    std::vector<u32> readSpirvFile(const Path& path) {
        const std::vector<u8> bytes = readBinaryFile(path);
        if (bytes.empty() || bytes.size() % sizeof(u32) != 0) {
            ARK_ERROR("Invalid SPIR-V shader size: {}", path.string());
            return {};
        }

        std::vector<u32> bytecode(bytes.size() / sizeof(u32));
        std::memcpy(bytecode.data(), bytes.data(), bytes.size());

        if (bytecode.front() != SpirvMagic) {
            ARK_ERROR("Invalid SPIR-V shader magic: {}", path.string());
            return {};
        }

        return bytecode;
    }

    std::vector<u32> loadCompiledShader(std::string_view fileName) {
        const Path shaderPath = findCompiledShaderFile(fileName);
        if (shaderPath.empty()) {
            return {};
        }

        return readSpirvFile(shaderPath);
    }
} // namespace ark::asset
