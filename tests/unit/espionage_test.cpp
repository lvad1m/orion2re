#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/advisor.h"
#include "game/espionage.h"
#include "game/hostages.h"
#include "game/opinion.h"
#include "game/serialization.h"

#include <memory>

namespace {
    struct s_espionage_scope {
        test::s_saved_value<bool> network{espionage::network};
        test::s_saved_value<int32_t> per_spy{espionage::points_per_spy};
        test::s_saved_value<int32_t> cap{espionage::network_cap};
        test::s_saved_value<int32_t> free_turns{espionage::free_spy_turns};
        test::s_saved_value<bool> ci{espionage::counterintelligence};
        test::s_saved_value<int32_t> accusation{espionage::witch_hunt_accusation};
        test::s_saved_value<int32_t> caught{espionage::caught_opinion};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<bool> advisor_enabled{advisor::enabled};

        s_espionage_scope() {
            espionage::Clear();
            opinion::Clear();
            espionage::network = true;
            espionage::points_per_spy = 1;
            espionage::network_cap = 30;
            espionage::free_spy_turns = 20;
            espionage::counterintelligence = true;
            espionage::witch_hunt_accusation = 20;
            espionage::caught_opinion = -15;
            opinion::enabled = true;
            advisor::enabled = false;
        }
        ~s_espionage_scope() {
            espionage::Clear();
            opinion::Clear();
        }
    };

    // Three computer empires; stardate 35001 (no free spy, no choice of mode).
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 3;
        MOX::stardate = 35001;
        for (int i = 0; i < 3; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            for (int j = 0; j < 3; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
    }
} // namespace

DOCTEST_TEST_SUITE("spy network core") {

DOCTEST_TEST_CASE("spies build network points up to the cap; without spies the points melt") {
    s_espionage_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].spies[2] = 3;  // 3 spies at player 2, mission steal
    espionage::Turn();
    DOCTEST_CHECK(espionage::Points(1, 2) == 3);
    DOCTEST_CHECK(espionage::Points(2, 1) == 0);
    for (int i = 0; i < 12; ++i) espionage::Turn();
    DOCTEST_CHECK(espionage::Points(1, 2) == 30);
    DOCTEST_CHECK(espionage::Spend_Points(1, 2, 25));
    DOCTEST_CHECK_FALSE(espionage::Spend_Points(1, 2, 6));  // only 5 left: nothing spent
    DOCTEST_CHECK(espionage::Points(1, 2) == 5);
    state.players[1].spies[2] = 0;
    espionage::Turn();
    espionage::Turn();
    DOCTEST_CHECK(espionage::Points(1, 2) == 3);
}

DOCTEST_TEST_CASE("a spy master or the neural scanner brings a free spy now and then") {
    s_espionage_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].tech_applications[TECH_APP_NEURAL_SCANNER] = TECH_RESEARCH_STATUS_RESEARCHED;
    s_leader_data& master = state.leaders[7];
    master.player_index = 2;
    master.status = 0;
    master.general_skills = LEADER_GENERAL_SKILL_SPY_MASTER;
    espionage::Turn();  // 35001: not a free-spy turn
    DOCTEST_CHECK((state.players[1].spies[1] & 0x3F) == 0);
    MOX::stardate = 35020;
    espionage::Turn();
    DOCTEST_CHECK((state.players[1].spies[1] & 0x3F) == 1);
    DOCTEST_CHECK((state.players[2].spies[2] & 0x3F) == 1);
    DOCTEST_CHECK((state.players[0].spies[0] & 0x3F) == 0);
}

DOCTEST_TEST_CASE("the modes add defence, cost income and morale; a caught spy is an incident") {
    s_espionage_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK(espionage::Defense_Bonus(1) == 0);
    espionage::Set_Mode(1, espionage::MODE_HEIGHTENED);
    DOCTEST_CHECK(espionage::Defense_Bonus(1) == 10);
    DOCTEST_CHECK(espionage::Morale_Effect(1) == -1);
    state.players[1].surplus_bc = 100;
    state.players[1].bc = 500;
    espionage::Turn();
    DOCTEST_CHECK(state.players[1].bc == 498);
    espionage::Set_Mode(1, espionage::MODE_WITCH_HUNT);
    DOCTEST_CHECK(espionage::Defense_Bonus(1) == 25);
    DOCTEST_CHECK(espionage::Morale_Effect(1) == -3);

    espionage::On_Spy_Caught(1, 2);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_CAUGHT_SPYING) == -15);
    DOCTEST_CHECK(espionage::State()->caught[1] == 1);
}

DOCTEST_TEST_CASE("a witch hunt jails honest leaders on false accusations") {
    s_espionage_scope scope;
    test::s_game_state state;
    Setup_(&state);
    s_leader_data& leader = state.leaders[4];
    leader.player_index = 1;
    leader.status = 0;
    leader.location = -1;
    espionage::Set_Mode(1, espionage::MODE_WITCH_HUNT);
    espionage::witch_hunt_accusation = 0;
    espionage::Turn();
    DOCTEST_CHECK(leader.status == 0);
    espionage::witch_hunt_accusation = 1000;  // certain
    espionage::Turn();
    DOCTEST_CHECK(leader.status == -2);  // jailed (ab-review 3.1), not dismissed to the hire list
    DOCTEST_CHECK(leader.player_index == 1);
    DOCTEST_CHECK(hostages::Find(4) >= 0);
    hostages::Clear();
}

DOCTEST_TEST_CASE("computer empires choose their mode by the spies they caught lately") {
    s_espionage_scope scope;
    test::s_game_state state;
    Setup_(&state);
    espionage::On_Spy_Caught(1, 2);
    MOX::stardate = 35010;
    espionage::Turn();
    DOCTEST_CHECK(espionage::Mode(1) == espionage::MODE_HEIGHTENED);
    DOCTEST_CHECK(espionage::Mode(2) == espionage::MODE_NORMAL);
    espionage::On_Spy_Caught(1, 2);
    espionage::On_Spy_Caught(1, 0);
    MOX::stardate = 35020;
    espionage::Turn();
    DOCTEST_CHECK(espionage::Mode(1) == espionage::MODE_WITCH_HUNT);
    MOX::stardate = 35050;  // the catches are old now
    espionage::Turn();
    DOCTEST_CHECK(espionage::Mode(1) == espionage::MODE_NORMAL);
}

DOCTEST_TEST_CASE("off: no points, no modes, no incidents") {
    s_espionage_scope scope;
    test::s_game_state state;
    Setup_(&state);
    espionage::network = false;
    espionage::counterintelligence = false;
    state.players[1].spies[2] = 3;
    espionage::Turn();
    espionage::Set_Mode(1, espionage::MODE_WITCH_HUNT);
    espionage::On_Spy_Caught(1, 2);
    DOCTEST_CHECK(espionage::Points(1, 2) == 0);
    DOCTEST_CHECK(espionage::Mode(1) == espionage::MODE_NORMAL);
    DOCTEST_CHECK(espionage::Defense_Bonus(1) == 0);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_CAUGHT_SPYING) == 0);
    DOCTEST_CHECK(espionage::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_espionage_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].spies[2] = 4;
    espionage::Turn();
    espionage::Set_Mode(2, espionage::MODE_HEIGHTENED);
    espionage::On_Spy_Caught(0, 1);
    auto buffer = std::make_unique<uint8_t[]>(espionage::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), espionage::Save_Size(), "test overflow");
    espionage::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == espionage::Save_Size());
    espionage::Clear();
    SERIAL::Reader_ reader(buffer.get(), espionage::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(espionage::Parse_Save(&reader, &error));
    DOCTEST_CHECK(espionage::Empty());
    espionage::Commit_Save();
    DOCTEST_CHECK(espionage::Points(1, 2) == 4);
    DOCTEST_CHECK(espionage::Mode(2) == espionage::MODE_HEIGHTENED);
    DOCTEST_CHECK(espionage::State()->caught[0] == 1);
    // The modes follow the version and 64 int16 points.
    buffer[2 + 64 * 2 + 1] = 7;  // player 1's mode
    SERIAL::Reader_ bad(buffer.get(), espionage::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(espionage::Parse_Save(&bad, &error));
}

}
