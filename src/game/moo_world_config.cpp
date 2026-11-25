#include "pch.h"

#include "game/moo_world.h"

// Configuration globals bound from specs/config/main.json5 (namespace "events"), kept apart from moo_world.cpp
// like the other *_config.cpp files (docs/LocalLLM/events-catalog.md 4.2).
namespace moo_world {

    bool enabled = false;

} // namespace moo_world
