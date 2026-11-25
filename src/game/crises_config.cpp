#include "pch.h"

#include "game/crises.h"

// Configuration globals bound from specs/config/main.json5 (namespace "events"), kept apart from
// crises.cpp like the other *_config.cpp files (docs/LocalLLM/events-catalog.md 5.1).
namespace crises {

    bool enabled = false;
    int32_t chance = 3;
    bool situations = false;
    bool crisis_front = false;
    bool become_crisis = false;
    int32_t front_strength = 0;

} // namespace crises
