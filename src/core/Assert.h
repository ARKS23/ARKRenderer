#pragma once

#include <cassert>

#define ARK_ASSERT(condition) assert(condition)
#define ARK_ASSERT_MSG(condition, message) assert((condition) && (message))
