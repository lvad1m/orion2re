#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/diplomac.h"
#include "game/npcdiplo.h"
#include "game/opinion.h"
#include "game/serialization.h"

#include <memory>

namespace {
    struct s_opinion_scope {
        test::s_saved_value<bool> enabled{opinion::enabled};
        test::s_saved_value<int32_t> decay_period{opinion::decay_period};
        test::s_saved_value<int32_t> sneak_attack{opinion::sneak_attack};
        test::s_saved_value<int32_t> helped_in_war{opinion::helped_in_war};

        s_opinion_scope() {
            opinion::Clear();
            opinion::enabled = true;
            opinion::decay_period = 2;
            opinion::sneak_attack = -40;
            opinion::helped_in_war = 25;
        }
        ~s_opinion_scope() {
            opinion::Clear();
        }
    };

    constexpr int8_t TREATY_WAR = 4;

    // Four computer empires in contact with each other, no treaties, at an even stardate.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 4;
        MOX::stardate = 35010;
        for (int i = 0; i < 4; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            for (int j = 0; j < 4; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
    }
} // namespace

DOCTEST_TEST_SUITE("opinion modifiers") {

DOCTEST_TEST_CASE("a memory deepens, fades with the period and adds to the relations decisions read") {
    s_opinion_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].relations[2] = 30;

    opinion::Add(1, 2, opinion::REASON_SNEAK_ATTACK, -40);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_SNEAK_ATTACK) == -40);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_SNEAK_ATTACK) == 0);  // one-sided
    opinion::Add(1, 2, opinion::REASON_HELPED_IN_WAR, 25);
    DOCTEST_CHECK(opinion::Sum(1, 2) == -15);
    DOCTEST_CHECK(opinion::Relations(1, 2) == 15);
    DOCTEST_CHECK(state.players[1].relations[2] == 30);  // the classic value is left alone

    opinion::Add(1, 2, opinion::REASON_SNEAK_ATTACK, -80);  // deepens up to the limit
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_SNEAK_ATTACK) == -opinion::MODIFIER_LIMIT);
    DOCTEST_CHECK(opinion::Relations(1, 2) == -45);
    state.players[1].relations[2] = -90;
    DOCTEST_CHECK(opinion::Relations(1, 2) == -100);  // kept within -100..100

    opinion::End_Of_Turn();  // 35010: a step
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_SNEAK_ATTACK) == -99);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_HELPED_IN_WAR) == 24);
    MOX::stardate = 35011;
    opinion::End_Of_Turn();  // not a step
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_HELPED_IN_WAR) == 24);
    MOX::stardate = 35012;
    for (int i = 0; i < 30; ++i) opinion::End_Of_Turn();  // the period is checked, not counted
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_HELPED_IN_WAR) == 0);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_SNEAK_ATTACK) == -69);
}

DOCTEST_TEST_CASE("off: no memories are kept and decisions read the classic relations") {
    s_opinion_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].relations[2] = 30;
    opinion::enabled = false;
    opinion::Add(1, 2, opinion::REASON_SNEAK_ATTACK, -40);
    opinion::On_Sneak_Attack(2, 1);
    DOCTEST_CHECK(opinion::Empty());
    DOCTEST_CHECK(opinion::Relations(1, 2) == 30);
    opinion::enabled = true;
    opinion::Add(1, 2, opinion::REASON_SNEAK_ATTACK, -40);
    opinion::enabled = false;  // a memory from before is not read
    DOCTEST_CHECK(opinion::Sum(1, 2) == 0);
    DOCTEST_CHECK(opinion::Relations(1, 2) == 30);
}

DOCTEST_TEST_CASE("the human holds no opinions, but computer empires hold them about the human") {
    s_opinion_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    opinion::Add(0, 1, opinion::REASON_SNEAK_ATTACK, -40);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_SNEAK_ATTACK) == 0);
    opinion::On_Sneak_Attack(0, 1);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_SNEAK_ATTACK) == -40);
    state.players[3].eliminated = 1;
    opinion::Add(1, 3, opinion::REASON_RIVAL, -20);
    DOCTEST_CHECK(opinion::Modifier(1, 3, opinion::REASON_RIVAL) == 0);
    opinion::Add(1, 1, opinion::REASON_RIVAL, -20);
    DOCTEST_CHECK(opinion::Modifier(1, 1, opinion::REASON_RIVAL) == 0);
}

DOCTEST_TEST_CASE("a war between two empires is help for the enemies of each") {
    s_opinion_scope scope;
    test::s_game_state state;
    Setup_(&state);
    // 0 already fights 2; 3 already fights 1. Now 1 and 2 go to war.
    state.players[0].treaty[2] = state.players[2].treaty[0] = TREATY_WAR;
    state.players[3].treaty[1] = state.players[1].treaty[3] = TREATY_WAR;
    opinion::On_War_Started(1, 2);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_HELPED_IN_WAR) == 25);  // 1 joined against 0's enemy
    DOCTEST_CHECK(opinion::Modifier(3, 2, opinion::REASON_HELPED_IN_WAR) == 25);  // 2 joined against 3's enemy
    DOCTEST_CHECK(opinion::Modifier(0, 2, opinion::REASON_HELPED_IN_WAR) == 0);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_HELPED_IN_WAR) == 0);
    state.players[0].contact[1] = 0;  // no help remembered from an unknown empire
    opinion::Clear();
    opinion::On_War_Started(1, 2);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_HELPED_IN_WAR) == 0);
}

DOCTEST_TEST_CASE("the engine's war declaration and a decision read the memories") {
    s_opinion_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].treaty[2] = state.players[2].treaty[0] = TREATY_WAR;
    diplomac::Declare_War(1, 2);
    DOCTEST_CHECK(state.players[1].treaty[2] >= TREATY_WAR);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_HELPED_IN_WAR) == 25);

    // Get_Tech_Exchange_Reaction adds the relations it reads (a non-negative reaction returns
    // reaction + 100); with the bonus flag its random draw does not enter the result.
    opinion::enabled = false;
    const int16_t classic = npcdiplo::Get_Tech_Exchange_Reaction(0, 1, 1);
    opinion::enabled = true;
    DOCTEST_CHECK(npcdiplo::Get_Tech_Exchange_Reaction(0, 1, 1) == classic + 25);  // the help remembered
    opinion::Add(0, 1, opinion::REASON_DEFENDED_US, 50);
    DOCTEST_CHECK(npcdiplo::Get_Tech_Exchange_Reaction(0, 1, 1) == classic + 75);
}

DOCTEST_TEST_CASE("the save payload round-trips; a bad payload leaves the current memories") {
    s_opinion_scope scope;
    test::s_game_state state;
    Setup_(&state);
    opinion::Add(1, 2, opinion::REASON_SNEAK_ATTACK, -40);
    opinion::Add(3, 0, opinion::REASON_HELPED_IN_WAR, 25);
    auto buffer = std::make_unique<uint8_t[]>(opinion::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), opinion::Save_Size(), "test overflow");
    opinion::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == opinion::Save_Size());

    opinion::Clear();
    opinion::Add(2, 3, opinion::REASON_RIVAL, -10);
    SERIAL::Reader_ reader(buffer.get(), opinion::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(opinion::Parse_Save(&reader, &error));
    DOCTEST_CHECK(opinion::Modifier(2, 3, opinion::REASON_RIVAL) == -10);  // nothing changes before the commit
    opinion::Commit_Save();
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_SNEAK_ATTACK) == -40);
    DOCTEST_CHECK(opinion::Modifier(3, 0, opinion::REASON_HELPED_IN_WAR) == 25);
    DOCTEST_CHECK(opinion::Modifier(2, 3, opinion::REASON_RIVAL) == 0);

    buffer[2] = 0x7F;  // first value out of range (127 > MODIFIER_LIMIT)
    SERIAL::Reader_ bad(buffer.get(), opinion::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(opinion::Parse_Save(&bad, &error));
    opinion::Commit_Save();  // nothing pending
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_SNEAK_ATTACK) == -40);

    buffer[0] = 9;  // payload version
    buffer[2] = 0;
    SERIAL::Reader_ wrong(buffer.get(), opinion::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(opinion::Parse_Save(&wrong, &error));
    SERIAL::Reader_ short_read(buffer.get(), 1, "test underflow", true);
    DOCTEST_CHECK_FALSE(opinion::Parse_Save(&short_read, &error));
}

}
