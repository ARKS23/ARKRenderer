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
        bool moveForward = false;
        bool moveBackward = false;
        bool moveLeft = false;
        bool moveRight = false;
        bool moveUp = false;
        bool moveDown = false;
        bool fastMove = false;
        bool resetPressed = false;
        bool debugUiTogglePressed = false;
    };
} // namespace ark
