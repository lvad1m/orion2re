#pragma once

#include <cstdint>
#include <string>

struct s_colony;

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Colony stability (docs/LocalLLM/mechanics.md 3.2.5 and 3.2.5.1; README.md 10.9, tier 2): every
// turn each colony's stability is worked out from the owner's war weariness, famine, taxes and
// deficit, peace, recent victories, growth, barracks, entertainment and the capitol, scaled by the
// government. Negative stability lowers the colony's morale (and with it output and taxes); a
// colony at or below the rebellion threshold for three turns may secede to the neighbour its
// people lean toward. Off by default.
namespace stability {

    extern bool enabled;                // loyalty colony_stability
    extern int32_t rebellion_threshold; // loyalty rebellion_threshold: stability at or below which unrest grows
    extern int32_t rebellion_chance;    // loyalty rebellion_chance: per mille per point below the threshold

    constexpr int32_t MIN_STABILITY = -30;
    constexpr int32_t MAX_STABILITY = 20;
    constexpr int32_t UNREST_TURNS = 3;        // turns at or below the threshold before secession is possible
    constexpr int32_t VICTORY_TURNS = 10;      // a victory counts this long
    constexpr int32_t MORALE_EFFECT_MIN = -10; // morale points (5 % each)

    // The colony's stability as worked out this turn (0 with the flag off or for an invalid colony).
    int32_t Of(int16_t colony);

    // The stability the factors give the colony now.
    int32_t Compute(int16_t colony);

    // What Colony_Morale adds: half of a negative stability, at most MORALE_EFFECT_MIN; 0 otherwise.
    int32_t Morale_Effect(const s_colony* colony);

    // `player` won a battle as the attacker (combfind Do_1_Combat).
    void Note_Victory(int16_t player);

    // The empire a colony's people lean toward: the race living there, a democracy, peace with the
    // owner; among alive empires in contact with the owner. -1: none.
    int16_t Leaning_Neighbour(int16_t colony);

    // Once a turn, before the classic rebellion check (nextturn.cpp): works out every colony's
    // stability, counts unrest turns and lets colonies long in unrest secede (game_random draws
    // only then; the capitol never secedes). Logs "stability: ...".
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'STAB' (game/rules_sections.h): payload version 1, the turn of each player's
    // last victory (int32 x MAX_PLAYERS), then per colony slot (MAX_COLONIES) its stability (int8)
    // and unrest turns (uint8).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

    // State access for the behaviour code (stability_state.cpp).
    int8_t* Stability_Slot(int16_t colony);
    uint8_t* Unrest_Slot(int16_t colony);
    int32_t* Last_Victory_Slot(int16_t player);

} // namespace stability
