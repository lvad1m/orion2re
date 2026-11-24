#include "pch.h"

#include "game/covenants.h"

// Configuration globals bound from specs/config/main.json5 (namespace "diplomacy"), kept apart from
// covenants.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.5.3).
namespace covenants {

    bool secret_clauses = false;
    bool marriages = false;

} // namespace covenants
