#include "pch.h"

#include "game/vassals.h"

// Configuration globals bound from specs/config/main.json5 (namespace "diplomacy"), kept apart from
// vassals.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.1).
namespace vassals {

    bool enabled = false;

} // namespace vassals
