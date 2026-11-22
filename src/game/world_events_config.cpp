#include "pch.h"

#include "game/world_events.h"

// Configuration globals bound from specs/config/main.json5 (namespace "events"), kept apart from
// world_events.cpp like the other *_config.cpp files (docs/LocalLLM/events-catalog.md 6).
namespace world_events {

    bool enabled = false;
    int32_t galaxy_chance = 15;
    int32_t empire_chance = 4;

} // namespace world_events
