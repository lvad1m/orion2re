#pragma once

#include <cstdint>

// Dice in battle (docs/LocalLLM/mechanics.md 3.6.2 layers A and C, 3.6.5 items 2 and 4; README.md
// 10.9, tier 7). MOO2's battles already roll d100 for every beam shot and every missile; layer A
// shows those rolls without changing them (a log line per shot, read later by the battle journal of
// the interface step), layer C gives a natural 96-100 on a beam shot an extra effect: one system of
// the struck ship fails for the rest of the turn. Both off by default; layer A never draws from
// game_random, layer C only on a critical hit.
namespace combat_dice {

    extern bool combat_log;     // dice combat_log
    extern bool combat_crits;   // dice combat_crits

    enum e_system : uint8_t {
        SYSTEM_SHIELDS = 0,     // every shield arc drops to zero; it recharges as usual next turn
        SYSTEM_DRIVE = 1,       // no movement left this turn
        SYSTEM_COMPUTER = 2,    // no weapon ready for the rest of this turn
        SYSTEM_COUNT = 3,
    };

    // Layer A: one beam shot as the original rolled it. `natural` is the d100 before modifiers,
    // `bonus` the attacker's hit chance, `threshold` the roll needed (defence and range);
    // `damage` is 0 for a miss.
    void Log_Beam_Shot(int16_t source, int16_t target, int16_t weapon_id, int32_t natural, int32_t bonus,
        int32_t threshold, int32_t damage, int32_t min_damage, int32_t max_damage);
    // Layer A: one missile or fighter bomb (its own d100 roll, a hit when above 0).
    void Log_Ordnance_Hit(int16_t source, int16_t target, int16_t weapon_id, int32_t roll, int32_t damage);

    // Layer C: a natural 96-100 on a beam shot that hit ship `target` (not a planet). Picks a system
    // with game_random and applies the failure; returns the system, or SYSTEM_COUNT when the flag is
    // off or the target is no ship.
    e_system On_Critical_Hit(int16_t source, int16_t target);
    // The failure itself (for the tests).
    void Apply_Failure(int16_t target, e_system system);

    const char* System_Name(e_system system);

} // namespace combat_dice
