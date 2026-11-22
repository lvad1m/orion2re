#include "pch.h"

#include "game/combat_dice.h"

// The battle dice log and critical hits are not under test in the beam and bomb tests.
namespace combat_dice {
    void Log_Beam_Shot(int16_t, int16_t, int16_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) {}
    void Log_Ordnance_Hit(int16_t, int16_t, int16_t, int32_t, int32_t) {}
    e_system On_Critical_Hit(int16_t, int16_t) { return SYSTEM_COUNT; }
} // namespace combat_dice
