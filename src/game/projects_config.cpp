#include "pch.h"

#include "game/projects.h"

// Configuration globals bound from specs/config/main.json5 (namespace "events"), kept apart from projects.cpp
// like the other *_config.cpp files (docs/LocalLLM/events-catalog.md 3.8).
namespace projects {

    bool enabled = false;

} // namespace projects
