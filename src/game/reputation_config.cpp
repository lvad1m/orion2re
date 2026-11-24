#include "pch.h"

#include "game/reputation.h"

// Configuration globals bound from specs/config/main.json5 (namespace "diplomacy"), kept apart from
// reputation.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.5.6).
namespace reputation {

    bool enabled = false;

} // namespace reputation
