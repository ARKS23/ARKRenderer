#pragma once

#include <glm/vec2.hpp>

namespace ark {
    struct InputSnapshot {
        glm::vec2 cursorPosition{0.0f};
        glm::vec2 cursorDelta{0.0f};
        glm::vec2 scrollDelta{0.0f};
        bool leftMouseDown = false;
        bool rightMouseDown = false;
        bool middleMouseDown = false;
        bool shiftDown = false;
        bool resetPressed = false;
        bool debugUiTogglePressed = false;
    };
} // namespace ark
