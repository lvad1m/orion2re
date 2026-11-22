#include "pch.h"

#include "game/resolutions.h"

// Configuration globals bound from specs/config/main.json5 (namespace "diplomacy"), kept apart
// from resolutions.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 2.7.2).
namespace resolutions {

    int32_t turns = 100;
    int32_t violation_opinion = -40;
    int32_t fund_percent = 2;

} // namespace resolutions
