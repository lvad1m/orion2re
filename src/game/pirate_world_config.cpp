#include "pch.h"

#include "game/pirate_world.h"

// Configuration globals bound from specs/config/main.json5 (namespace "events"), kept apart from pirate_world.cpp
// like the other *_config.cpp files (docs/LocalLLM/events-catalog.md 3.6 and 3.12).
namespace pirate_world {

    bool enabled = false;

} // namespace pirate_world
