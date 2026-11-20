#pragma once

#include <cstdint>
#include <string>

// The single check of the new mechanics (docs/LocalLLM/mechanics.md, 3.6.3): roll + modifiers
// against a difficulty, graded by the margin, with a line for the journal. Only the engine rolls,
// through the game's random generator, so replays, saves and network games stay in step. Nothing
// in the classic game calls it; spy operations, loyalty and crises will.
namespace dice {

    enum e_roll_kind : uint8_t {
        ROLL_TWO_D10 = 0,   // 2..20, bell-shaped: extremes are rare (default)
        ROLL_D20 = 1,       // 1..20, flat: every result 5 %
        ROLL_THREE_D6 = 2,  // 3..18, steeper bell
    };

    extern uint8_t roll_kind;           // dice roll
    extern int32_t brilliant_margin;    // dice brilliant_margin: margin for a brilliant success
    extern int32_t exposed_margin;      // dice exposed_margin: margin at or below which a failure exposes

    enum e_outcome : uint8_t {
        OUTCOME_CATASTROPHE = 0,  // the lowest natural roll, whatever the modifiers
        OUTCOME_EXPOSED = 1,      // margin <= exposed_margin: failed and caught
        OUTCOME_FAILURE = 2,      // margin < 0
        OUTCOME_SUCCESS = 3,      // margin >= 0
        OUTCOME_BRILLIANT = 4,    // margin >= brilliant_margin: success with an extra effect
        OUTCOME_TRIUMPH = 5,      // the highest natural roll, whatever the modifiers
    };

    struct s_modifier {
        const char* label = "";  // shown in the journal line: "agent level 2"
        int32_t value = 0;
    };

    struct s_check {
        e_roll_kind kind = ROLL_TWO_D10;
        int32_t dice[3] = {};
        int32_t dice_count = 0;
        int32_t natural = 0;     // sum of the dice
        int32_t total = 0;       // natural + modifiers
        int32_t difficulty = 0;
        int32_t margin = 0;      // total - difficulty
        e_outcome outcome = OUTCOME_FAILURE;
    };

    // Rolls `roll_kind` with game_random::Random and resolves the check.
    s_check Roll_Check(const s_modifier* modifiers, int32_t modifier_count, int32_t difficulty);

    // Resolves a check from dice already rolled (the dice are clamped to their faces).
    s_check Resolve_Check(e_roll_kind kind, const int32_t* dice, const s_modifier* modifiers, int32_t modifier_count,
        int32_t difficulty);

    // Number of dice and faces of a roll kind.
    int32_t Dice_Count(e_roll_kind kind);
    int32_t Dice_Faces(e_roll_kind kind);

    // Chance in percent (rounded down) that a roll of `kind` plus `bonus` reaches `difficulty`,
    // natural extremes included: for the interface and for AI decisions.
    int32_t Success_Chance_Percent(e_roll_kind kind, int32_t bonus, int32_t difficulty);

    // "forge a letter: 2d10 = 13 (6+7) + 4 (agent level 2) + 2 (telepath) = 19 vs 16 - success (+3)"
    std::string Describe_Check(const char* action, const s_check& check, const s_modifier* modifiers,
        int32_t modifier_count);

    const char* Outcome_Name(e_outcome outcome);

} // namespace dice
