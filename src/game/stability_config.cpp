#include "pch.h"

#include "game/stability.h"

// Configuration globals bound from specs/config/main.json5 (namespace "loyalty"), kept apart from
// stability.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.2.5.1).
namespace stability {

    bool enabled = false;
    int32_t rebellion_threshold = -12;
    int32_t rebellion_chance = 20;

} // namespace stability
