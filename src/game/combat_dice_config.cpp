#include "pch.h"

#include "game/combat_dice.h"

// Configuration globals bound from specs/config/main.json5 (namespace "dice"), kept apart from
// combat_dice.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.6).
namespace combat_dice {

    bool combat_log = false;
    bool combat_crits = false;

} // namespace combat_dice
