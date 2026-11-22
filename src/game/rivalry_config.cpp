#include "pch.h"

#include "game/rivalry.h"

// Configuration globals bound from specs/config/main.json5 (namespace "diplomacy"), kept apart
// from rivalry.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.1.3).
namespace rivalry {

    bool enabled = false;
    int32_t period = 10;
    int32_t opinion_value = -20;
    int32_t strength_percent = 110;
    int32_t hatred = -50;

} // namespace rivalry
