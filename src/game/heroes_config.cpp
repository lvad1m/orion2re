#include "pch.h"

#include "game/heroes.h"

// Configuration globals bound from specs/config/main.json5 (namespace "events"), kept apart from heroes.cpp like
// the other *_config.cpp files (docs/LocalLLM/events-catalog.md 3.11, 3.12).
namespace heroes {

    bool enabled = false;

} // namespace heroes
