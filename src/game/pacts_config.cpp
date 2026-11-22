#include "pch.h"

#include "game/pacts.h"

// Configuration globals bound from specs/config/main.json5 (namespace "diplomacy"), kept apart
// from pacts.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.1.4).
namespace pacts {

    bool enabled = false;
    int32_t period = 10;
    int32_t opinion_min = 20;
    int32_t defended_opinion = 25;
    int32_t abandoned_opinion = -40;
    int32_t guarantees_max = 2;

} // namespace pacts
