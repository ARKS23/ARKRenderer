#pragma once

#include <chrono>

namespace ark {
    class Timer {
    public:
        using Clock = std::chrono::steady_clock;

        void reset() {
            m_Start = Clock::now();
        }

        [[nodiscard]] double elapsedSeconds() const {
            return std::chrono::duration<double>(Clock::now() - m_Start).count();
        }

    private:
        Clock::time_point m_Start = Clock::now();
    };
} // namespace ark
