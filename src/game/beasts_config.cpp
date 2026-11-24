#include "pch.h"

#include "game/beasts.h"

// Configuration globals bound from specs/config/main.json5 (namespace "events"), kept apart from beasts.cpp like
// the other *_config.cpp files (docs/LocalLLM/events-catalog.md 3.7 and 3.12).
namespace beasts {

    bool enabled = false;

} // namespace beasts
