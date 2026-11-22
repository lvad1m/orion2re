#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/advisor.h"
#include "game/colcalc.h"
#include "game/rivalry.h"
#include "game/serialization.h"
#include "game/spy_ranks.h"

#include <cstring>
#include <memory>

namespace {
    struct s_spy_ranks_scope {
        test::s_saved_value<bool> counter_agents{spy_ranks::counter_agents};
        test::s_saved_value<bool> ranks{spy_ranks::ranks};
        test::s_saved_value<int32_t> agent_cost{spy_ranks::counter_agent_cost};
        test::s_saved_value<int32_t> saboteur_cost{spy_ranks::saboteur_cost};
        test::s_saved_value<int32_t> resident_cost{spy_ranks::resident_cost};
        test::s_saved_value<bool> rivalry_enabled{rivalry::enabled};
        test::s_saved_value<bool> advisor_enabled{advisor::enabled};

        s_spy_ranks_scope() {
            spy_ranks::Clear();
            rivalry::Clear();
            spy_ranks::counter_agents = true;
            spy_ranks::ranks = true;
            spy_ranks::counter_agent_cost = 35;
            spy_ranks::saboteur_cost = 25;
            spy_ranks::resident_cost = 250;
            rivalry::enabled = false;
            advisor::enabled = false;
        }
        ~s_spy_ranks_scope() {
            spy_ranks::Clear();
            rivalry::Clear();
        }
    };

    // Three computer empires with the spy network; colonies 0 (player 1's capitol, 5 people) and
    // 1 (player 1, 8 people). Stardate 35001 (no computer turn).
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 3;
        MOX::NUM_COLONIES = 2;
        MOX::stardate = 35001;
        for (int i = 0; i < 3; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[i].tech_applications[TECH_APP_SPY_NETWORK] = TECH_RESEARCH_STATUS_RESEARCHED;
            for (int j = 0; j < 3; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
        state->colonies[0].owner = 1;
        state->colonies[0].n_pops = 5;
        state->colonies[0].buildings[BUILDING_CAPITOL] = 1;
        state->colonies[1].owner = 1;
        state->colonies[1].n_pops = 8;
    }
} // namespace

DOCTEST_TEST_SUITE("counter-agents and spy ranks") {

DOCTEST_TEST_CASE("the products: flags, the spy network technology, costs, names, at most 3 counter-agents") {
    s_spy_ranks_scope scope;
    test::s_game_state state;
    Setup_(&state);
    s_colony* colony = &state.colonies[1];
    DOCTEST_CHECK(spy_ranks::Is_Product(spy_ranks::PRODUCT_COUNTER_AGENT));
    DOCTEST_CHECK_FALSE(spy_ranks::Is_Product(COLONY_PRODUCTION_SPY));
    DOCTEST_CHECK(spy_ranks::Can_Build(colony, spy_ranks::PRODUCT_COUNTER_AGENT));
    DOCTEST_CHECK(COLCALC::Colony_Can_Build_Product(colony, spy_ranks::PRODUCT_RESIDENT) == 1);
    DOCTEST_CHECK(spy_ranks::Cost(spy_ranks::PRODUCT_RESIDENT) == 250);
    DOCTEST_CHECK(std::strlen(spy_ranks::Name(spy_ranks::PRODUCT_SABOTEUR)) > 0);
    DOCTEST_CHECK(std::strlen(spy_ranks::Description(spy_ranks::PRODUCT_SABOTEUR)) > 0);
    for (int i = 0; i < 3; ++i) DOCTEST_CHECK(spy_ranks::On_Built(1, spy_ranks::PRODUCT_COUNTER_AGENT));
    DOCTEST_CHECK(spy_ranks::Counter_Agents(1) == 3);
    DOCTEST_CHECK_FALSE(spy_ranks::Can_Build(colony, spy_ranks::PRODUCT_COUNTER_AGENT));
    DOCTEST_CHECK_FALSE(spy_ranks::On_Built(1, spy_ranks::PRODUCT_COUNTER_AGENT));  // half back, as with a spy
    state.players[1].tech_applications[TECH_APP_SPY_NETWORK] = 0;
    DOCTEST_CHECK_FALSE(spy_ranks::Can_Build(colony, spy_ranks::PRODUCT_SABOTEUR));
    state.players[1].tech_applications[TECH_APP_SPY_NETWORK] = TECH_RESEARCH_STATUS_RESEARCHED;
    spy_ranks::ranks = false;
    DOCTEST_CHECK_FALSE(spy_ranks::Can_Build(colony, spy_ranks::PRODUCT_SABOTEUR));
}

DOCTEST_TEST_CASE("residents go to the rival and lift operations there; upkeep and defenders") {
    s_spy_ranks_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK_FALSE(spy_ranks::On_Built(1, spy_ranks::PRODUCT_RESIDENT));  // no rival, no spies anywhere
    state.players[1].spies[2] = 3;  // most of our spies work at player 2
    DOCTEST_CHECK(spy_ranks::On_Built(1, spy_ranks::PRODUCT_RESIDENT));
    DOCTEST_CHECK(spy_ranks::Residents(1, 2) == 1);
    DOCTEST_CHECK(spy_ranks::Resident_Bonus(1, 2) == spy_ranks::RESIDENT_BONUS);
    DOCTEST_CHECK(spy_ranks::Resident_Bonus(1, 0) == 0);
    rivalry::enabled = true;
    rivalry::Declare(1, 0);
    DOCTEST_CHECK(spy_ranks::On_Built(1, spy_ranks::PRODUCT_RESIDENT));
    DOCTEST_CHECK(spy_ranks::Residents(1, 0) == 1);

    DOCTEST_REQUIRE(spy_ranks::On_Built(0, spy_ranks::PRODUCT_COUNTER_AGENT));
    DOCTEST_REQUIRE(spy_ranks::On_Built(0, spy_ranks::PRODUCT_COUNTER_AGENT));
    DOCTEST_REQUIRE(spy_ranks::On_Built(1, spy_ranks::PRODUCT_COUNTER_AGENT));
    DOCTEST_CHECK(spy_ranks::Capitol_Defenders(1) == 1);  // two in the capitol: one defender
    DOCTEST_CHECK(spy_ranks::Upkeep(1) == 3 / 2 + 2 * 2);
    DOCTEST_CHECK(spy_ranks::Uncover_Bonus(0) == 2 * spy_ranks::UNCOVER_BRIBE_PER_MILLE);
    DOCTEST_CHECK_FALSE(spy_ranks::Foil_Sabotage(2, 1 + 100));  // no counter-agents there
}

DOCTEST_TEST_CASE("saboteurs wait for a war, then strike at most 3 a turn and are gone") {
    s_spy_ranks_scope scope;
    test::s_game_state state;
    Setup_(&state);
    for (int i = 0; i < 5; ++i) DOCTEST_REQUIRE(spy_ranks::On_Built(1, spy_ranks::PRODUCT_SABOTEUR));
    DOCTEST_CHECK(spy_ranks::State()->saboteurs[1] == 5);
    spy_ranks::Turn();
    DOCTEST_CHECK(spy_ranks::State()->saboteurs[1] == 5);  // no war: they wait
    state.players[1].treaty[2] = state.players[2].treaty[1] = 4;
    spy_ranks::Turn();
    DOCTEST_CHECK(spy_ranks::State()->saboteurs[1] == 2);
    spy_ranks::Turn();
    DOCTEST_CHECK(spy_ranks::State()->saboteurs[1] == 0);
}

DOCTEST_TEST_CASE("computer empires buy counter-agents, saboteurs at war and residents at peace") {
    s_spy_ranks_scope scope;
    test::s_game_state state;
    Setup_(&state);
    s_player& p = state.players[1];
    p.surplus_bc = 100;  // 5 % = 5 BC of upkeep allowed
    p.bc = 3000;
    state.players[1].spies[0] = 2;  // a resident would go to player 0
    state.players[1].treaty[2] = state.players[2].treaty[1] = 4;
    MOX::stardate = 35010;
    spy_ranks::Turn();
    DOCTEST_CHECK(spy_ranks::Counter_Agents(1) == 1);  // the most populous colony
    DOCTEST_CHECK(spy_ranks::Residents(1, 0) == 1);
    DOCTEST_CHECK(p.bc == 3000 - 70 - 50 - 500);
    // The saboteur bought this turn strikes at the next one.
    DOCTEST_CHECK(spy_ranks::State()->saboteurs[1] == 1);
}

DOCTEST_TEST_CASE("off: nothing is built or bought") {
    s_spy_ranks_scope scope;
    test::s_game_state state;
    Setup_(&state);
    spy_ranks::counter_agents = false;
    spy_ranks::ranks = false;
    DOCTEST_CHECK_FALSE(spy_ranks::Can_Build(&state.colonies[1], spy_ranks::PRODUCT_COUNTER_AGENT));
    DOCTEST_CHECK_FALSE(spy_ranks::On_Built(1, spy_ranks::PRODUCT_SABOTEUR));
    state.players[1].bc = 3000;
    MOX::stardate = 35010;
    spy_ranks::Turn();
    DOCTEST_CHECK(spy_ranks::Empty());
    DOCTEST_CHECK(spy_ranks::Upkeep(1) == 0);
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_spy_ranks_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(spy_ranks::On_Built(1, spy_ranks::PRODUCT_COUNTER_AGENT));
    DOCTEST_REQUIRE(spy_ranks::On_Built(1, spy_ranks::PRODUCT_SABOTEUR));
    spy_ranks::Note_Sabotage(0);
    auto buffer = std::make_unique<uint8_t[]>(spy_ranks::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), spy_ranks::Save_Size(), "test overflow");
    spy_ranks::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == spy_ranks::Save_Size());
    spy_ranks::Clear();
    SERIAL::Reader_ reader(buffer.get(), spy_ranks::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(spy_ranks::Parse_Save(&reader, &error));
    DOCTEST_CHECK(spy_ranks::Empty());
    spy_ranks::Commit_Save();
    DOCTEST_CHECK(spy_ranks::Counter_Agents(1) == 1);
    DOCTEST_CHECK(spy_ranks::State()->saboteurs[1] == 1);
    DOCTEST_CHECK(spy_ranks::State()->last_sabotaged[1] == 0);
    buffer[2 + 1] = 9;  // colony 1 with 9 counter-agents
    SERIAL::Reader_ bad(buffer.get(), spy_ranks::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(spy_ranks::Parse_Save(&bad, &error));
}

}
