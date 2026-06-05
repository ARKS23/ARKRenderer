#pragma once

#include <type_traits>
#include <utility>

namespace ark {
    // 作用域退出时执行清理逻辑；用于替代构造过程中的 try/catch 清理代码。
    template <typename Function>
    class ScopeExit final {
    public:
        explicit ScopeExit(Function function) noexcept(std::is_nothrow_move_constructible_v<Function>)
            : m_Function(std::move(function)) {
        }

        ~ScopeExit() noexcept(noexcept(std::declval<Function&>()())) {
            if (m_Active) {
                m_Function();
            }
        }

        ScopeExit(const ScopeExit&) = delete;
        ScopeExit& operator=(const ScopeExit&) = delete;

        ScopeExit(ScopeExit&& other) noexcept(std::is_nothrow_move_constructible_v<Function>)
            : m_Function(std::move(other.m_Function)), m_Active(other.m_Active) {
            other.release();
        }

        ScopeExit& operator=(ScopeExit&& other) = delete;

        void release() noexcept {
            m_Active = false;
        }

    private:
        Function m_Function;
        bool m_Active = true;
    };

    template <typename Function>
    ScopeExit<std::decay_t<Function>> makeScopeExit(Function&& function) {
        return ScopeExit<std::decay_t<Function>>(std::forward<Function>(function));
    }
} // namespace ark
