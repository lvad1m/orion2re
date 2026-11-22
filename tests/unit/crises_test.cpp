#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/beliefs.h"
#include "game/crises.h"
#include "game/dice.h"
#include "game/serialization.h"

#include <memory>

namespace {
    struct s_crises_scope {
        test::s_saved_value<bool> enabled{crises::enabled};
        test::s_saved_value<int32_t> chance{crises::chance};
        test::s_saved_value<bool> model{beliefs::neighbour_model};
        test::s_saved_value<uint8_t> roll_kind{dice::roll_kind};

        s_crises_scope() {
            crises::Clear();
            beliefs::Clear();
            crises::enabled = true;
            crises::chance = 0;  // nothing starts by chance in these tests
            beliefs::neighbour_model = true;
            dice::roll_kind = dice::ROLL_TWO_D10;
        }
        ~s_crises_scope() {
            crises::Clear();
            beliefs::Clear();
        }
    };

    const int32_t TRIUMPH[2] = {10, 10};
    const int32_t NEAR_MISS[2] = {5, 6};   // 11 against 12: a plain failure, a partial success
    const int32_t CATASTROPHE[2] = {1, 1};

    // Two computer empires; player 0 holds colonies 0 (the capital, a hydroponic farm) and 1.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 2;
        MOX::NUM_COLONIES = 2;
        MOX::stardate = 35100;
        for (int i = 0; i < 2; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[i].personality = PLAYER_PERSONALITY_PACIFIST;
            for (int j = 0; j < 2; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
        state->colonies[0].owner = 0;
        state->colonies[0].n_pops = 6;
        state->colonies[0].planet = 0;
        state->colonies[0].buildings[BUILDING_CAPITOL] = 1;
        state->colonies[0].buildings[BUILDING_HYDROPONIC_FARM] = 1;
        state->colonies[1].owner = 0;
        state->colonies[1].n_pops = 3;
        state->colonies[1].planet = 1;
        state->planets[0].colony_index = 0;
        state->planets[1].colony_index = 1;
    }
} // namespace

DOCTEST_TEST_SUITE("crises") {

DOCTEST_TEST_CASE("check bonuses come from the empire's standing and a leader's skill") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].research_produced = 150;
    DOCTEST_CHECK(crises::Check_Bonus(0, crises::CHECK_SCIENCE) == 3);
    s_leader_data& leader = state.leaders[2];
    leader.player_index = 0;
    leader.status = 1;
    leader.general_skills = LEADER_GENERAL_SKILL_RESEARCHER;
    DOCTEST_CHECK(crises::Check_Bonus(0, crises::CHECK_SCIENCE) == 3 + crises::LEADER_BONUS);
    state.players[0].spies[0] = 6;
    DOCTEST_CHECK(crises::Check_Bonus(0, crises::CHECK_ESPIONAGE) == 3);
}

DOCTEST_TEST_CASE("computers choose by character among the options they can pay for") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    // A pacifist leans to diplomacy: evacuate.
    DOCTEST_CHECK(crises::Choose_Option(0, crises::CRISIS_ASTEROID) == 2);
    state.players[0].personality = PLAYER_PERSONALITY_AGGRESSIVE;
    DOCTEST_CHECK(crises::Choose_Option(0, crises::CRISIS_ASTEROID) == 2);  // cannot pay 50 BC to blow it up
    state.players[0].bc = 60;
    DOCTEST_CHECK(crises::Choose_Option(0, crises::CRISIS_ASTEROID) == 1);
}

DOCTEST_TEST_CASE("an asteroid: the choice costs now, the check decides three turns later") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].personality = PLAYER_PERSONALITY_AGGRESSIVE;
    state.players[0].bc = 60;
    DOCTEST_REQUIRE(crises::Open(0, crises::CRISIS_ASTEROID, -1));
    DOCTEST_CHECK(state.players[0].bc == 10);
    DOCTEST_CHECK(crises::Slots()[0].option == 1);
    DOCTEST_CHECK(crises::Slots()[0].due == 35100 + crises::CHECK_DELAY);
    DOCTEST_CHECK_FALSE(crises::Open(0, crises::CRISIS_REVOLUTIONARIES, 1));  // one crisis at a time
    DOCTEST_CHECK(crises::Resolve_With_Dice(0, NEAR_MISS) == crises::RESULT_PARTIAL);
    DOCTEST_CHECK(state.colonies[0].buildings[BUILDING_HYDROPONIC_FARM] == 0);
    DOCTEST_CHECK(crises::Empty());
    DOCTEST_REQUIRE(crises::Open(0, crises::CRISIS_ASTEROID, -1));
    DOCTEST_CHECK(crises::Resolve_With_Dice(0, TRIUMPH) == crises::RESULT_SUCCESS);
    DOCTEST_CHECK(state.colonies[0].n_pops == 6);
}

DOCTEST_TEST_CASE("a chain: failed revolutionaries rise again eight turns later") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(crises::Open(0, crises::CRISIS_REVOLUTIONARIES, 1));
    DOCTEST_CHECK(crises::Resolve_With_Dice(0, CATASTROPHE) == crises::RESULT_FAILURE);
    const crises::s_active& slot = crises::Slots()[0];
    DOCTEST_CHECK(slot.crisis == crises::CRISIS_REVOLUTION);
    DOCTEST_CHECK(slot.option == -1);
    DOCTEST_CHECK(slot.due == 35108);
    MOX::stardate = 35108;
    crises::Turn();  // the follow-up opens and its option is chosen
    DOCTEST_CHECK(slot.option >= 0);
    DOCTEST_CHECK(slot.due == 35108 + crises::CHECK_DELAY);
    DOCTEST_CHECK(crises::Resolve_With_Dice(0, TRIUMPH) == crises::RESULT_SUCCESS);
    DOCTEST_CHECK(crises::Empty());
}

DOCTEST_TEST_CASE("a cruel choice marks the reputation; humans get no crisis yet") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].personality = PLAYER_PERSONALITY_XENOPHOBIC;  // leans to espionage: a weapon
    DOCTEST_REQUIRE(crises::Open(0, crises::CRISIS_BRAIN_PARASITES, 1));
    DOCTEST_CHECK(crises::Slots()[0].option == 2);
    DOCTEST_CHECK(beliefs::Evidence(1, 0, beliefs::BELIEF_HOSTILE) == 1);
    crises::Clear();
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_CHECK_FALSE(crises::Open(1, crises::CRISIS_ASTEROID, -1));
}

DOCTEST_TEST_CASE("off: nothing opens") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    crises::enabled = false;
    DOCTEST_CHECK_FALSE(crises::Open(0, crises::CRISIS_ASTEROID, -1));
    crises::Turn();
    DOCTEST_CHECK(crises::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(crises::Open(0, crises::CRISIS_REVOLUTIONARIES, 1, 5));  // a follow-up-style delayed opening
    auto buffer = std::make_unique<uint8_t[]>(crises::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), crises::Save_Size(), "test overflow");
    crises::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == crises::Save_Size());
    crises::Clear();
    SERIAL::Reader_ reader(buffer.get(), crises::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(crises::Parse_Save(&reader, &error));
    DOCTEST_CHECK(crises::Empty());
    crises::Commit_Save();
    DOCTEST_CHECK(crises::Slots()[0].crisis == crises::CRISIS_REVOLUTIONARIES);
    DOCTEST_CHECK(crises::Slots()[0].due == 35105);
    buffer[2] = 99;  // an unknown crisis
    SERIAL::Reader_ bad(buffer.get(), crises::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(crises::Parse_Save(&bad, &error));
    DOCTEST_CHECK(crises::Slots()[0].crisis == crises::CRISIS_REVOLUTIONARIES);
}

}
