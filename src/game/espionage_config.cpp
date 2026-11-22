#include "pch.h"

#include "game/espionage.h"

// Configuration globals bound from specs/config/main.json5 (namespace "spy"), kept apart from
// espionage.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.2.5.2).
namespace espionage {

    bool network = false;
    int32_t points_per_spy = 1;
    int32_t network_cap = 30;
    int32_t free_spy_turns = 20;
    bool counterintelligence = false;
    int32_t witch_hunt_accusation = 20;
    int32_t caught_opinion = -15;

} // namespace espionage
