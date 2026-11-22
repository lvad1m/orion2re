#include "pch.h"

#include "game/opinion.h"

// Configuration globals bound from specs/config/main.json5 (namespace "diplomacy"), kept apart
// from opinion.cpp like the other *_config.cpp files. Values from docs/LocalLLM/mechanics.md 3.1.
namespace opinion {

    bool enabled = false;
    int32_t decay_period = 2;
    int32_t sneak_attack = -40;
    int32_t helped_in_war = 25;

} // namespace opinion
