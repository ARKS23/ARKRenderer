#include "renderer/RenderView.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace ark {
    namespace {
        std::string normalizeOptionName(std::string_view name) {
            std::string normalized{name};
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
                if (ch == '_') {
                    return '-';
                }

                return static_cast<char>(std::tolower(ch));
            });
            return normalized;
        }
    } // namespace

    ToneMappingOperator parseToneMappingOperator(std::string_view name,
                                                 ToneMappingOperator fallback) {
        const std::string normalized = normalizeOptionName(name);
        if (normalized == "reinhard" || normalized == "default") {
            return ToneMappingOperator::Reinhard;
        }

        if (normalized == "linear") {
            return ToneMappingOperator::Linear;
        }

        if (normalized == "aces" ||
            normalized == "aces-fitted" ||
            normalized == "acesfitted" ||
            normalized == "filmic") {
            return ToneMappingOperator::ACES;
        }

        return fallback;
    }

    ShadowFilterMode parseShadowFilterMode(std::string_view name,
                                           ShadowFilterMode fallback) {
        const std::string normalized = normalizeOptionName(name);
        if (normalized == "hard" || normalized == "none" || normalized == "off" || normalized == "default") {
            return ShadowFilterMode::Hard;
        }

        if (normalized == "pcf" || normalized == "pcf3" ||
            normalized == "pcf-3" || normalized == "pcf3x3" ||
            normalized == "pcf-3x3") {
            return ShadowFilterMode::Pcf3x3;
        }

        if (normalized == "pcf5" || normalized == "pcf-5" ||
            normalized == "pcf5x5" || normalized == "pcf-5x5") {
            return ShadowFilterMode::Pcf5x5;
        }

        return fallback;
    }
} // namespace ark
