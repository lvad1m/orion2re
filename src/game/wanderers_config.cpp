#include "pch.h"

#include "game/wanderers.h"

// Configuration globals bound from specs/config/main.json5 (namespace "events"), kept apart from wanderers.cpp
// like the other *_config.cpp files (docs/LocalLLM/events-catalog.md 3.1 and 3.3).
namespace wanderers {

    bool enabled = false;

} // namespace wanderers
