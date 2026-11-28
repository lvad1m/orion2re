#include "pch.h"

#include "game/ambition.h"

// Configuration globals bound from specs/config/main.json5 (namespaces "loyalty" and "diplomacy"), kept
// apart from ambition.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.5.6, 3.5.3).
namespace ambition {

    bool ambitious_governors = false;
    bool council_bribery = false;
    bool council_bribery_exposes_voters = false;

} // namespace ambition
