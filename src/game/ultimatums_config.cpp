#include "pch.h"

#include "game/ultimatums.h"

// Configuration globals bound from specs/config/main.json5 (namespace "diplomacy"), kept apart from
// ultimatums.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.5.3).
namespace ultimatums {

    bool enabled = false;

} // namespace ultimatums
