#include "pch.h"

#include "game/spy_ranks.h"

// Configuration globals bound from specs/config/main.json5 (namespace "spy"), kept apart from
// spy_ranks.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.2.5.4).
namespace spy_ranks {

    bool counter_agents = false;
    bool ranks = false;
    int32_t counter_agent_cost = 35;
    int32_t saboteur_cost = 25;
    int32_t resident_cost = 250;

} // namespace spy_ranks
