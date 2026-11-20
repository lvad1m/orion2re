#include "pch.h"

#include <string>

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/dice.h"

namespace {
    struct s_dice_scope {
        test::s_saved_value<uint8_t> kind{dice::roll_kind};
        test::s_saved_value<int32_t> brilliant{dice::brilliant_margin};
        test::s_saved_value<int32_t> exposed{dice::exposed_margin};

        s_dice_scope() {
            dice::roll_kind = dice::ROLL_TWO_D10;
            dice::brilliant_margin = 5;
            dice::exposed_margin = -5;
        }
    };

    dice::s_check Resolve_(int32_t a, int32_t b, int32_t bonus, int32_t difficulty) {
        const int32_t rolled[2] = {a, b};
        const dice::s_modifier modifier{"bonus", bonus};
        return dice::Resolve_Check(dice::ROLL_TWO_D10, rolled, &modifier, 1, difficulty);
    }
} // namespace

DOCTEST_TEST_SUITE("dice") {

DOCTEST_TEST_CASE("the margin grades the outcome; natural extremes override it") {
    s_dice_scope scope;
    DOCTEST_CHECK(Resolve_(5, 5, 0, 10).outcome == dice::OUTCOME_SUCCESS);      // margin 0
    DOCTEST_CHECK(Resolve_(5, 5, 4, 10).outcome == dice::OUTCOME_SUCCESS);      // +4
    DOCTEST_CHECK(Resolve_(5, 5, 5, 10).outcome == dice::OUTCOME_BRILLIANT);    // +5
    DOCTEST_CHECK(Resolve_(5, 4, 0, 10).outcome == dice::OUTCOME_FAILURE);      // -1
    DOCTEST_CHECK(Resolve_(3, 2, 0, 10).outcome == dice::OUTCOME_EXPOSED);      // -5
    DOCTEST_CHECK(Resolve_(1, 1, 30, 10).outcome == dice::OUTCOME_CATASTROPHE); // natural 2
    DOCTEST_CHECK(Resolve_(10, 10, -30, 10).outcome == dice::OUTCOME_TRIUMPH);  // natural 20

    const dice::s_check check = Resolve_(12, 0, 3, 10);                         // dice clamp to 1..10
    DOCTEST_CHECK(check.dice[0] == 10);
    DOCTEST_CHECK(check.dice[1] == 1);
    DOCTEST_CHECK(check.total == 14);
    DOCTEST_CHECK(check.margin == 4);
}

DOCTEST_TEST_CASE("chances match the 2d10 table of the design") {
    // mechanics.md 3.6.3: 7 -> 85 %, 11 -> 55 %, 15 -> 21 %, 17 -> 10 % (natural 2 always fails).
    DOCTEST_CHECK(dice::Success_Chance_Percent(dice::ROLL_TWO_D10, 0, 7) == 85);
    DOCTEST_CHECK(dice::Success_Chance_Percent(dice::ROLL_TWO_D10, 0, 11) == 55);
    DOCTEST_CHECK(dice::Success_Chance_Percent(dice::ROLL_TWO_D10, 0, 15) == 21);
    DOCTEST_CHECK(dice::Success_Chance_Percent(dice::ROLL_TWO_D10, 0, 17) == 10);
    DOCTEST_CHECK(dice::Success_Chance_Percent(dice::ROLL_TWO_D10, 4, 15) == 55);
    DOCTEST_CHECK(dice::Success_Chance_Percent(dice::ROLL_TWO_D10, 0, 40) == 1);   // natural 20 only
    DOCTEST_CHECK(dice::Success_Chance_Percent(dice::ROLL_TWO_D10, 30, 0) == 99);  // natural 2 fails
    DOCTEST_CHECK(dice::Success_Chance_Percent(dice::ROLL_D20, 0, 11) == 50);
    DOCTEST_CHECK(dice::Success_Chance_Percent(dice::ROLL_THREE_D6, 0, 11) == 50);
}

DOCTEST_TEST_CASE("rolls come from the game generator and repeat with its seed") {
    s_dice_scope scope;
    test::s_game_state state;
    const dice::s_modifier modifiers[2] = {{"agent level 2", 4}, {"telepath", 2}};

    game_random::Set_Random_Seed(12345);
    const dice::s_check first = dice::Roll_Check(modifiers, 2, 16);
    game_random::Set_Random_Seed(12345);
    const dice::s_check second = dice::Roll_Check(modifiers, 2, 16);
    DOCTEST_CHECK(first.dice[0] == second.dice[0]);
    DOCTEST_CHECK(first.dice[1] == second.dice[1]);
    DOCTEST_CHECK(first.total == first.natural + 6);
    DOCTEST_CHECK(first.dice_count == 2);

    int32_t histogram[21] = {};
    for (int i = 0; i < 20000; ++i) ++histogram[dice::Roll_Check(nullptr, 0, 11).natural];
    DOCTEST_CHECK(histogram[0] == 0);
    DOCTEST_CHECK(histogram[1] == 0);
    DOCTEST_CHECK(histogram[11] > histogram[2] * 5);   // bell: the middle is ten times the edge
    DOCTEST_CHECK(histogram[11] > histogram[20] * 5);
    DOCTEST_CHECK(histogram[2] > 0);
    DOCTEST_CHECK(histogram[20] > 0);
}

DOCTEST_TEST_CASE("the journal line names the dice, modifiers and outcome") {
    s_dice_scope scope;
    const int32_t rolled[2] = {6, 7};
    const dice::s_modifier modifiers[3] = {{"agent level 2", 4}, {"telepath", 2}, {"guarded", -3}};
    const dice::s_check check = dice::Resolve_Check(dice::ROLL_TWO_D10, rolled, modifiers, 3, 16);
    const std::string line = dice::Describe_Check("forge a letter", check, modifiers, 3);
    DOCTEST_CHECK(line == "forge a letter: 2d10 = 13 (6+7) + 4 (agent level 2) + 2 (telepath) - 3 (guarded) = 16 vs 16 - success (+0)");
}

} // DOCTEST_TEST_SUITE
