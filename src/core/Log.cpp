#include "core/Log.h"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <utility>

namespace ark {
    namespace {
        bool g_LogInitialized = false;
    } // namespace

    void Log::initialize() {
        if (g_LogInitialized) {
            return;
        }

        auto logger = spdlog::stdout_color_mt("ARKRenderer");
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        logger->set_level(spdlog::level::trace);
        spdlog::set_default_logger(std::move(logger));
        g_LogInitialized = true;
    }

    void Log::shutdown() {
        if (!g_LogInitialized) {
            return;
        }

        spdlog::shutdown();
        g_LogInitialized = false;
    }
} // namespace ark
