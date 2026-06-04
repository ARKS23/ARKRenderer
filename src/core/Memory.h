#pragma once

#include <memory>
#include <utility>

namespace ark {
    // Scope 表示唯一所有权，默认用于 Renderer、RHI 对象和后端内部 RAII 对象。
    template <typename T>
    using Scope = std::unique_ptr<T>;

    template <typename T, typename... Args>
    Scope<T> makeScope(Args&&... args) {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    // Ref 表示共享所有权，只在资源确实需要跨系统共享生命周期时使用。
    template <typename T>
    using Ref = std::shared_ptr<T>;

    template <typename T, typename... Args>
    Ref<T> makeRef(Args&&... args) {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }
} // namespace ark
