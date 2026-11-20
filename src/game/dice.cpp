#include "pch.h"

#include "game/dice.h"

#include <algorithm>
#include <cstdio>

namespace dice {

    namespace {
        e_roll_kind Kind_(uint8_t kind) {
            return kind <= ROLL_THREE_D6 ? static_cast<e_roll_kind>(kind) : ROLL_TWO_D10;
        }

        const char* Kind_Name_(e_roll_kind kind) {
            switch (kind) {
                case ROLL_D20: return "d20";
                case ROLL_THREE_D6: return "3d6";
                default: return "2d10";
            }
        }

        e_outcome Grade_(e_roll_kind kind, int32_t natural, int32_t margin) {
            const int32_t count = Dice_Count(kind);
            if (natural <= count) return OUTCOME_CATASTROPHE;
            if (natural >= count * Dice_Faces(kind)) return OUTCOME_TRIUMPH;
            if (margin >= brilliant_margin) return OUTCOME_BRILLIANT;
            if (margin >= 0) return OUTCOME_SUCCESS;
            if (margin <= exposed_margin) return OUTCOME_EXPOSED;
            return OUTCOME_FAILURE;
        }
    } // namespace

    int32_t Dice_Count(e_roll_kind kind) {
        switch (kind) {
            case ROLL_D20: return 1;
            case ROLL_THREE_D6: return 3;
            default: return 2;
        }
    }

    int32_t Dice_Faces(e_roll_kind kind) {
        switch (kind) {
            case ROLL_D20: return 20;
            case ROLL_THREE_D6: return 6;
            default: return 10;
        }
    }

    s_check Resolve_Check(e_roll_kind kind, const int32_t* dice, const s_modifier* modifiers, int32_t modifier_count,
        int32_t difficulty) {
        s_check check;
        check.kind = Kind_(kind);
        check.dice_count = Dice_Count(check.kind);
        const int32_t faces = Dice_Faces(check.kind);
        for (int32_t i = 0; i < check.dice_count; ++i) {
            check.dice[i] = std::clamp(dice != nullptr ? dice[i] : 1, 1, faces);
            check.natural += check.dice[i];
        }
        check.total = check.natural;
        for (int32_t i = 0; modifiers != nullptr && i < modifier_count; ++i) check.total += modifiers[i].value;
        check.difficulty = difficulty;
        check.margin = check.total - difficulty;
        check.outcome = Grade_(check.kind, check.natural, check.margin);
        return check;
    }

    s_check Roll_Check(const s_modifier* modifiers, int32_t modifier_count, int32_t difficulty) {
        const e_roll_kind kind = Kind_(roll_kind);
        int32_t rolled[3] = {};
        for (int32_t i = 0; i < Dice_Count(kind); ++i) {
            rolled[i] = static_cast<int32_t>(game_random::Random(static_cast<uint32_t>(Dice_Faces(kind))));
        }
        return Resolve_Check(kind, rolled, modifiers, modifier_count, difficulty);
    }

    int32_t Success_Chance_Percent(e_roll_kind kind, int32_t bonus, int32_t difficulty) {
        kind = Kind_(kind);
        const int32_t count = Dice_Count(kind);
        const int32_t faces = Dice_Faces(kind);
        int32_t total = 1;
        for (int32_t i = 0; i < count; ++i) total *= faces;
        int32_t successes = 0;
        for (int32_t index = 0; index < total; ++index) {
            int32_t dice[3] = {};
            int32_t rest = index;
            for (int32_t i = 0; i < count; ++i) {
                dice[i] = rest % faces + 1;
                rest /= faces;
            }
            const s_modifier modifier{"", bonus};
            const e_outcome outcome = Resolve_Check(kind, dice, &modifier, 1, difficulty).outcome;
            if (outcome >= OUTCOME_SUCCESS) ++successes;
        }
        return successes * 100 / total;
    }

    const char* Outcome_Name(e_outcome outcome) {
        switch (outcome) {
            case OUTCOME_CATASTROPHE: return "catastrophe";
            case OUTCOME_EXPOSED: return "failure, exposed";
            case OUTCOME_FAILURE: return "failure";
            case OUTCOME_SUCCESS: return "success";
            case OUTCOME_BRILLIANT: return "brilliant success";
            case OUTCOME_TRIUMPH: return "triumph";
        }
        return "?";
    }

    std::string Describe_Check(const char* action, const s_check& check, const s_modifier* modifiers,
        int32_t modifier_count) {
        std::string line = action != nullptr && action[0] != '\0' ? std::string(action) + ": " : std::string();
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer), "%s = %d", Kind_Name_(check.kind), static_cast<int>(check.natural));
        line += buffer;
        if (check.dice_count > 1) {
            line += " (";
            for (int32_t i = 0; i < check.dice_count; ++i) {
                if (i > 0) line += '+';
                line += std::to_string(check.dice[i]);
            }
            line += ')';
        }
        for (int32_t i = 0; modifiers != nullptr && i < modifier_count; ++i) {
            if (modifiers[i].value == 0) continue;
            std::snprintf(buffer, sizeof(buffer), " %c %d (%s)", modifiers[i].value < 0 ? '-' : '+',
                static_cast<int>(modifiers[i].value < 0 ? -modifiers[i].value : modifiers[i].value),
                modifiers[i].label != nullptr ? modifiers[i].label : "");
            line += buffer;
        }
        std::snprintf(buffer, sizeof(buffer), " = %d vs %d - %s (%+d)", static_cast<int>(check.total),
            static_cast<int>(check.difficulty), Outcome_Name(check.outcome), static_cast<int>(check.margin));
        line += buffer;
        return line;
    }

} // namespace dice
