#include "pch.h"

#include "game/embargo.h"

// Configuration globals bound from specs/config/main.json5 (namespace "diplomacy"), kept apart
// from embargo.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 2.7.1).
namespace embargo {

    bool enabled = false;
    bool council_resolutions = false;
    int32_t period = 10;
    int32_t opinion_value = -30;
    int32_t penalty_percent = 15;
    int32_t min_turns = 20;
    int32_t council_turns = 50;
    int32_t opinion_min = -40;
    int32_t join_opinion = -20;

} // namespace embargo
