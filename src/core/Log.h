#pragma once

#include <spdlog/spdlog.h>

namespace ark {
    class Log {
    public:
        static void initialize();
        static void shutdown();
    };
} // namespace ark

#define ARK_TRACE(...) ::spdlog::trace(__VA_ARGS__)
#define ARK_DEBUG(...) ::spdlog::debug(__VA_ARGS__)
#define ARK_INFO(...) ::spdlog::info(__VA_ARGS__)
#define ARK_WARN(...) ::spdlog::warn(__VA_ARGS__)
#define ARK_ERROR(...) ::spdlog::error(__VA_ARGS__)
