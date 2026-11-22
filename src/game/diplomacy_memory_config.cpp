#include "pch.h"

#include "game/diplomacy_memory.h"

// Configuration globals bound from specs/config/main.json5 (namespace "diplomacy"), kept apart
// from diplomacy_memory.cpp like the other *_config.cpp files.
namespace diplomemory {

    bool enabled = false;
    int32_t period = 5;
    int32_t trust_cap = 30;
    int32_t weariness_cap = 60;
    int32_t weary_limit = 0;

} // namespace diplomemory
