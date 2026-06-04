#pragma once

#include <string>
#include <utility>
#include <variant>

namespace ark {
struct Error {
    std::string message;
};

template <typename T>
class Result {
public:
    Result(T value)
        : m_Value(std::move(value)) {
    }

    Result(Error error)
        : m_Value(std::move(error)) {
    }

    [[nodiscard]] bool hasValue() const {
        return std::holds_alternative<T>(m_Value);
    }

    [[nodiscard]] explicit operator bool() const {
        return hasValue();
    }

    [[nodiscard]] T& value() {
        return std::get<T>(m_Value);
    }

    [[nodiscard]] const T& value() const {
        return std::get<T>(m_Value);
    }

    [[nodiscard]] const Error& error() const {
        return std::get<Error>(m_Value);
    }

private:
    std::variant<T, Error> m_Value;
};
} // namespace ark
