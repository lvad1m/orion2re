#include "pch.h"

#include "game/hostages.h"

// Configuration globals bound from specs/config/main.json5 (namespace "diplomacy"), kept apart from
// hostages.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.5.3).
namespace hostages {

    bool hostage_swaps = false;
    bool prisoners = false;
    bool spy_prison = false;

} // namespace hostages
