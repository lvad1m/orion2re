#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Crises with a choice and event chains (docs/LocalLLM/events-catalog.md 5.1 and 3.1-3.4; README.md
// 10.9, tier 5B). A crisis opens with three options; each has a cost now, a kind of check
// (science, diplomacy, army, espionage) and may leave a mark on the empire's reputation. Three turns
// later the engine rolls the check - 2d10 + the empire's standing in that field + a leader's skill
// against the difficulty - and builds the outcome (success, partial, failure) from the classic
// primitives; some outcomes start a follow-up crisis some turns later. Computer empires choose by
// personality and objective; a human's crises wait for the interface step (none start for a human).
// Off by default; with the flag off nothing here draws from game_random and no state is kept.
namespace crises {

    extern bool enabled;        // events crises
    extern int32_t chance;      // events crisis_chance: per mille a turn for each computer empire

    enum e_crisis : uint8_t {
        CRISIS_NONE = 0,
        CRISIS_ASTEROID = 1,         // an asteroid falls toward the capital
        CRISIS_BRAIN_PARASITES = 2,  // parasites in a colony's people
        CRISIS_REVOLUTIONARIES = 3,  // revolutionaries in a colony
        CRISIS_PARASITES_RETURN = 4, // follow-up: the parasites come back
        CRISIS_REVOLUTION = 5,       // follow-up: the revolutionaries rise
        CRISIS_COUNT = 6,
    };

    enum e_check : uint8_t {
        CHECK_SCIENCE = 0,
        CHECK_DIPLOMACY = 1,
        CHECK_ARMY = 2,
        CHECK_ESPIONAGE = 3,
        CHECK_COUNT = 4,
    };

    enum e_result : uint8_t {
        RESULT_PENDING = 0,
        RESULT_SUCCESS = 1,
        RESULT_PARTIAL = 2,
        RESULT_FAILURE = 3,
    };

    struct s_option {
        const char* name;
        int16_t cost_bc;
        uint8_t check;        // e_check
        bool cruel;           // leaves a mark: the neighbours think it likelier to attack them
    };

    constexpr int32_t OPTION_COUNT = 3;
    constexpr int32_t CHECK_DELAY = 3;        // turns from the choice to the check
    constexpr int32_t DIFFICULTY = 12;
    constexpr int32_t LEADER_BONUS = 2;
    constexpr int32_t MAX_ACTIVE = 16;
    constexpr int32_t MIN_TURN = 30;          // no crisis before this turn of the game
    constexpr int32_t PREFERRED_BONUS = 20;   // percent added to the preferred kind of check

    const char* Name(e_crisis crisis);
    const s_option* Options(e_crisis crisis);  // OPTION_COUNT options

    // The empire's modifier for a check of `kind` (its standing plus a leader's skill).
    int32_t Check_Bonus(int16_t player, e_check kind);
    // The option a computer empire takes: the best chance to succeed among those it can pay for,
    // its personality's and objective's preferred kind first by PREFERRED_BONUS points.
    int32_t Choose_Option(int16_t player, e_crisis crisis);

    // Opens `crisis` for `player` at `colony` (-1: the capital); the option is chosen at once for a
    // computer empire. False when it cannot open (flag off, a human, no colony, the table full).
    bool Open(int16_t player, e_crisis crisis, int16_t colony, int32_t delay = 0);
    // The check of an open crisis with the dice given (for the tests); the game rolls.
    e_result Resolve_With_Dice(int32_t slot, const int32_t* dice_values);

    // Once a turn after the new events (nextturn.cpp): due crises are resolved, follow-ups open,
    // and new crises may start.
    void Turn();

    struct s_active {
        uint8_t crisis = CRISIS_NONE;
        int8_t player = -1;
        int16_t colony = -1;
        int8_t option = -1;      // chosen option, -1 before the choice
        int32_t due = 0;         // stardate of the check (or of the opening, for a follow-up)
        uint8_t result = RESULT_PENDING;
        int8_t roll = 0;         // the dice total, for replays and the journal
    };
    s_active* Slots();          // MAX_ACTIVE slots

    void Clear();
    bool Empty();

    // Save section 'CRIS' (game/rules_sections.h): payload version 1, then MAX_ACTIVE slots of
    // crisis (uint8), player (int8), colony (int16), option (int8), due (int32), result (uint8),
    // roll (int8).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace crises
