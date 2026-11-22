#include "pch.h"

#include "game/loyalty.h"

// Configuration globals bound from specs/config/main.json5 (namespace "loyalty"), kept apart from
// loyalty.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.2.5.1).
namespace loyalty {

    bool enabled = false;
    int32_t period = 5;

} // namespace loyalty
