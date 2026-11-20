#pragma once

#include <cstdint>

// Diplomatic memory (docs/LocalLLM/mechanics.md, section 4, step 1): trust heals after a betrayal
// and grows while treaties hold, the grievance of broken treaties fades, and war losses make an
// empire weary of the war. Built only on fields the save already has (s_player
// trust_worthiness[], broken_treaties_modifier[], ship_war_losses[], colony_war_losses[]), so a
// save made with it loads in any build. Off by default: the classic game never forgives.
namespace diplomemory {

    extern bool enabled;            // diplomacy memory
    extern int32_t period;          // diplomacy memory_period: turns per step of healing and fading
    extern int32_t trust_cap;       // diplomacy memory_trust_cap: trust kept treaties earn at most
    extern int32_t weariness_cap;   // diplomacy memory_weariness_cap: largest war weariness

    // Trust never falls below this with memory on (classic trust wraps around the int8 range).
    constexpr int32_t TRUST_FLOOR = -100;

    // Once a turn, after the classic end-of-turn diplomacy adjustments. Every `period` turns, for
    // each pair in contact and not at war: negative trust heals by 1, trust under an active treaty
    // grows by 1 up to `trust_cap`, and the broken-treaties modifier moves 1 toward zero.
    void End_Of_Turn();

    // Lowers `player`'s trust in `target` by `amount`. Classic: plain int8 arithmetic, which wraps
    // (seven betrayals turned a traitor into a trusted friend). With memory on: stops at TRUST_FLOOR.
    void Lower_Trust(int16_t player, int16_t target, int32_t amount);

    // How tired `player` is of its war with `enemy`, 0..weariness_cap (0 with memory off or at
    // peace): half the share of its fleet lost to this enemy plus the share of its population
    // lost to it, in percent. Fleet in the units the combat loss counter uses (2^(size + 1)).
    int32_t War_Weariness(int16_t player, int16_t enemy);

} // namespace diplomemory
