#include "pch.h"

#include "game/overtures.h"

// Configuration globals bound from specs/config/main.json5 (namespace "events"), kept apart from overtures.cpp
// like the other *_config.cpp files (docs/LocalLLM/events-catalog.md 3.5 and 3.12).
namespace overtures {

    bool enabled = false;

} // namespace overtures
