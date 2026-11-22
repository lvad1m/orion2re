#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/colcalc.h"
#include "game/serialization.h"
#include "game/stability.h"

#include <memory>

namespace {
    struct s_stability_scope {
        test::s_saved_value<bool> enabled{stability::enabled};
        test::s_saved_value<int32_t> threshold{stability::rebellion_threshold};
        test::s_saved_value<int32_t> chance{stability::rebellion_chance};

        s_stability_scope() {
            stability::Clear();
            stability::enabled = true;
            stability::rebellion_threshold = -12;
            stability::rebellion_chance = 20;
        }
        ~s_stability_scope() {
            stability::Clear();
        }
    };

    constexpr int8_t TREATY_WAR = 4;

    // Three computer empires in contact; colony 0, settled, belongs to player 1 (feudal, at peace).
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 3;
        MOX::NUM_COLONIES = 1;
        MOX::stardate = 35010;
        for (int i = 0; i < 3; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[i].home_planet_id = -1;
            for (int j = 0; j < 3; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
        state->players[1].traits[TRAIT_CURRENT_GOVERNMENT] = GOVERNMENT_FEUDAL;
        s_colony& c = state->colonies[0];
        c.owner = 1;
        c.outpost_flag = 0;
        c.n_pops = 3;
        for (int i = 0; i < 3; ++i) c.pop[i] = 1;  // player 1's race
    }
} // namespace

DOCTEST_TEST_SUITE("colony stability") {

DOCTEST_TEST_CASE("stability adds up the factors and the government scales the bad ones") {
    s_stability_scope scope;
    test::s_game_state state;
    Setup_(&state);
    s_player& owner = state.players[1];
    s_colony& c = state.colonies[0];
    DOCTEST_CHECK(stability::Compute(0) == 3);  // peace
    owner.food_shortage_duration = 4;
    DOCTEST_CHECK(stability::Compute(0) == -1);
    owner.tax_rate = 30;
    owner.surplus_bc = -1;
    DOCTEST_CHECK(stability::Compute(0) == 3 - (4 + 3 + 5));
    owner.traits[TRAIT_CURRENT_GOVERNMENT] = GOVERNMENT_DICTATORSHIP;  // bad halved: 6
    DOCTEST_CHECK(stability::Compute(0) == -3);
    owner.traits[TRAIT_CURRENT_GOVERNMENT] = GOVERNMENT_DEMOCRACY;     // bad times 1.5: 18
    DOCTEST_CHECK(stability::Compute(0) == -15);
    owner.traits[TRAIT_CURRENT_GOVERNMENT] = GOVERNMENT_UNIFICATION;   // never below zero
    DOCTEST_CHECK(stability::Compute(0) == 0);
    owner.traits[TRAIT_CURRENT_GOVERNMENT] = GOVERNMENT_FEUDAL;
    owner.food_shortage_duration = 0;
    owner.tax_rate = 0;
    owner.surplus_bc = 10;

    c.buildings[BUILDING_MARINE_BARRACKS] = 1;
    c.buildings[BUILDING_HOLO_SIMULATOR] = 1;
    c.buildings[BUILDING_CAPITOL] = 1;
    c.pop_growth[0] = 1;
    DOCTEST_CHECK(stability::Compute(0) == 3 + 3 + 2 + 5 + 1);
    stability::Note_Victory(1);
    DOCTEST_CHECK(stability::Compute(0) == 16);
    MOX::stardate = 35021;  // the victory is 11 turns old
    DOCTEST_CHECK(stability::Compute(0) == 14);
    owner.treaty[2] = state.players[2].treaty[1] = TREATY_WAR;  // no peace bonus
    DOCTEST_CHECK(stability::Compute(0) == 11);
    c.outpost_flag = 1;  // outposts have no people
    DOCTEST_CHECK(stability::Compute(0) == 0);
}

DOCTEST_TEST_CASE("unrest lowers the colony's morale by half its stability, at most 50 percent") {
    s_stability_scope scope;
    test::s_game_state state;
    Setup_(&state);
    s_player& owner = state.players[1];
    owner.food_shortage_duration = 9;  // 3 - 9 = -6
    stability::Turn();
    DOCTEST_CHECK(stability::Of(0) == -6);
    DOCTEST_CHECK(stability::Morale_Effect(&state.colonies[0]) == -3);
    // Colony_Morale adds it to the classic value.
    stability::enabled = false;
    COLCALC::Colony_Morale(&state.colonies[0], nullptr);
    const int classic = state.colonies[0].morale;
    stability::enabled = true;
    COLCALC::Colony_Morale(&state.colonies[0], nullptr);
    DOCTEST_CHECK(state.colonies[0].morale == classic - 3);

    owner.tax_rate = 50;
    owner.surplus_bc = -1;
    owner.food_shortage_duration = 10;
    owner.traits[TRAIT_CURRENT_GOVERNMENT] = GOVERNMENT_DEMOCRACY;  // (10 + 5 + 5) * 1.5 = 30 - 3
    stability::Turn();
    DOCTEST_CHECK(stability::Of(0) == -27);
    DOCTEST_CHECK(stability::Morale_Effect(&state.colonies[0]) == stability::MORALE_EFFECT_MIN);
}

DOCTEST_TEST_CASE("a people leans toward its race, a democracy and peace; unrest counts the turns") {
    s_stability_scope scope;
    test::s_game_state state;
    Setup_(&state);
    s_colony& c = state.colonies[0];
    DOCTEST_CHECK(stability::Leaning_Neighbour(0) == -1);  // its own race lives there; no one draws it
    c.pop[0] = c.pop[1] = 2;  // two of player 2's race
    DOCTEST_CHECK(stability::Leaning_Neighbour(0) == -1);  // 2 cannot reach the colony
    state.players[2].within_range[1] = 1;
    DOCTEST_CHECK(stability::Leaning_Neighbour(0) == 2);
    state.players[0].within_range[1] = 1;
    state.players[0].traits[TRAIT_CURRENT_GOVERNMENT] = GOVERNMENT_DEMOCRACY;  // 2 + peace 1 < 6 + 1
    DOCTEST_CHECK(stability::Leaning_Neighbour(0) == 2);
    c.pop[0] = c.pop[1] = 1;
    DOCTEST_CHECK(stability::Leaning_Neighbour(0) == 0);

    // Deep unrest with no one to secede to: the unrest turns count on, the colony stays.
    state.players[0].within_range[1] = 0;
    state.players[1].food_shortage_duration = 10;
    state.players[1].tax_rate = 50;
    state.players[1].surplus_bc = -1;  // 3 - 20 = -17
    stability::rebellion_chance = 1000;
    for (int turn = 0; turn < 5; ++turn) stability::Turn();
    DOCTEST_CHECK(*stability::Unrest_Slot(0) == 5);
    DOCTEST_CHECK(c.owner == 1);
    state.players[1].food_shortage_duration = 0;
    stability::Turn();
    DOCTEST_CHECK(*stability::Unrest_Slot(0) == 0);
}

DOCTEST_TEST_CASE("off: no stability, no effect") {
    s_stability_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].food_shortage_duration = 10;
    stability::enabled = false;
    stability::Turn();
    stability::Note_Victory(1);
    DOCTEST_CHECK(stability::Of(0) == 0);
    DOCTEST_CHECK(stability::Morale_Effect(&state.colonies[0]) == 0);
    DOCTEST_CHECK(stability::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_stability_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].food_shortage_duration = 9;
    stability::Note_Victory(2);
    stability::Turn();
    auto buffer = std::make_unique<uint8_t[]>(stability::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), stability::Save_Size(), "test overflow");
    stability::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == stability::Save_Size());
    stability::Clear();
    SERIAL::Reader_ reader(buffer.get(), stability::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(stability::Parse_Save(&reader, &error));
    DOCTEST_CHECK(stability::Empty());
    stability::Commit_Save();
    DOCTEST_CHECK(stability::Of(0) == -6);
    DOCTEST_CHECK(*stability::Last_Victory_Slot(2) == 35010);
    // Colony 0's stability byte follows the version and 8 victory turns.
    buffer[2 + 8 * 4] = 100;
    SERIAL::Reader_ bad(buffer.get(), stability::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(stability::Parse_Save(&bad, &error));
}

}
