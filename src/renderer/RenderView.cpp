#include "renderer/RenderView.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace ark {
    namespace {
        std::string normalizeToneMappingOperatorName(std::string_view name) {
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
        const std::string normalized = normalizeToneMappingOperatorName(name);
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
} // namespace ark
