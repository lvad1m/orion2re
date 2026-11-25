#include "pch.h"

#include "game/discoveries.h"

// Configuration globals bound from specs/config/main.json5 (namespace "events"), kept apart from discoveries.cpp
// like the other *_config.cpp files (docs/LocalLLM/events-catalog.md 3.8).
namespace discoveries {

    bool enabled = false;

} // namespace discoveries
