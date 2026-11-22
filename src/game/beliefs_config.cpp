#include "pch.h"

#include "game/beliefs.h"

// Configuration globals bound from specs/config/main.json5 (namespaces "diplomacy" and "spy"),
// kept apart from beliefs.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.5.10).
namespace beliefs {

    bool neighbour_model = false;
    bool disinformation = false;
    int32_t disinformation_cost = 30;

} // namespace beliefs
