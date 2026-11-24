#include "pch.h"

#include "game/incidents.h"

// Configuration globals bound from specs/config/main.json5 (namespace "events"), kept apart from incidents.cpp
// like the other *_config.cpp files (docs/LocalLLM/events-catalog.md 3.2, 3.3 and 3.4).
namespace incidents {

    bool enabled = false;

} // namespace incidents
