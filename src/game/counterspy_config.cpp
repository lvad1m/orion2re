#include "pch.h"

#include "game/counterspy.h"

// Configuration globals bound from specs/config/main.json5 (namespace "spy"), kept apart from
// counterspy.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.5.5).
namespace counterspy {

    bool mole_hunt = false;
    bool canary_trap = false;
    bool interrogation = false;
    bool guards = false;
    int32_t guard_fee = 5;

} // namespace counterspy
