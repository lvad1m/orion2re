#include "pch.h"

#include "game/dice.h"

// Configuration globals bound from specs/config/main.json5 (namespace "dice"), kept apart from
// dice.cpp like the other *_config.cpp files.
namespace dice {

    uint8_t roll_kind = ROLL_TWO_D10;
    int32_t brilliant_margin = 5;
    int32_t exposed_margin = -5;

} // namespace dice
