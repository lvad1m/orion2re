#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/intrigue.h"
#include "game/serialization.h"
#include "game/world_events.h"

#include <memory>

namespace {
    struct s_events_scope {
        test::s_saved_value<bool> enabled{world_events::enabled};
        test::s_saved_value<int32_t> galaxy{world_events::galaxy_chance};
        test::s_saved_value<int32_t> empire{world_events::empire_chance};
        test::s_saved_value<bool> fate{intrigue::spy_fate};

        s_events_scope() {
            world_events::Clear();
            intrigue::Clear();
            world_events::enabled = true;
            world_events::galaxy_chance = 0;  // Turn starts nothing by chance in these tests
            world_events::empire_chance = 0;
            intrigue::spy_fate = true;
        }
        ~s_events_scope() {
            world_events::Clear();
            intrigue::Clear();
        }
    };

    // Two empires; player 0 holds colonies 0 (the capital, 8 people, ocean) and 1 (4 people).
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 2;
        MOX::NUM_COLONIES = 2;
        MOX::stardate = 35001;
        for (int i = 0; i < 2; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            for (int j = 0; j < 2; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
        state->colonies[0].owner = 0;
        state->colonies[0].n_pops = 8;
        state->colonies[0].planet = 0;
        state->colonies[0].buildings[BUILDING_CAPITOL] = 1;
        state->colonies[1].owner = 0;
        state->colonies[1].n_pops = 4;
        state->colonies[1].planet = 1;
        state->planets[0].climate = PLANET_CLIMATE_OCEAN;
        state->planets[0].colony_index = 0;
        state->planets[0].mineral_class = MINERAL_RESOURCE_ABUNDANT;
        state->planets[1].climate = PLANET_CLIMATE_DESERT;
        state->planets[1].colony_index = 1;
        state->planets[1].mineral_class = MINERAL_RESOURCE_POOR;
    }
} // namespace

DOCTEST_TEST_SUITE("new events") {

DOCTEST_TEST_CASE("a golden age pays a fifth of the surplus for 15 turns; one galaxy modifier at a time") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].surplus_bc = 50;
    DOCTEST_REQUIRE(world_events::Start_Galaxy_Event(world_events::KIND_GOLDEN_AGE));
    DOCTEST_CHECK_FALSE(world_events::Start_Galaxy_Event(world_events::KIND_TOURIST_BOOM));
    world_events::Turn();
    DOCTEST_CHECK(state.players[0].bc == 10);
    MOX::stardate = 35001 + world_events::GALAXY_MODIFIER_TURNS;
    world_events::Turn();
    DOCTEST_CHECK(state.players[0].bc == 10);
    DOCTEST_CHECK(world_events::Find(world_events::KIND_GOLDEN_AGE, -1) == nullptr);
}

DOCTEST_TEST_CASE("race modifiers: fair wind, duel of honour, warm current, clutch; they run out") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_FAIR_WIND));
    DOCTEST_CHECK_FALSE(world_events::Start_Race_Event(0, world_events::RACE_FAIR_WIND));  // already blowing
    DOCTEST_CHECK(world_events::Speed_Bonus(0) == 1);
    DOCTEST_CHECK(world_events::Speed_Bonus(1) == 0);
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_DUEL_OF_HONOUR));
    DOCTEST_CHECK(world_events::Morale_Bonus(&state.colonies[1]) == 1);
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_WARM_CURRENT));
    DOCTEST_CHECK(world_events::Food2_Bonus(&state.colonies[0]) == 2);
    DOCTEST_CHECK(world_events::Food2_Bonus(&state.colonies[1]) == 0);  // not an ocean world
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_CLUTCH));
    DOCTEST_CHECK(world_events::Population_Boom(0));
    DOCTEST_CHECK(world_events::Population_Boom(1));
    MOX::stardate = 35001 + world_events::MODIFIER_TURNS;
    DOCTEST_CHECK(world_events::Speed_Bonus(0) == 0);
    DOCTEST_CHECK_FALSE(world_events::Population_Boom(0));
    world_events::enabled = false;
    MOX::stardate = 35002;
    DOCTEST_CHECK(world_events::Speed_Bonus(0) == 0);
}

DOCTEST_TEST_CASE("one-off race events: bargain, network failure, plate shift, prophecy, double agent") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_GOOD_BARGAIN));
    DOCTEST_CHECK(state.players[0].bc == 50);
    state.players[0].research_accumulated = 100;
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_NETWORK_FAILURE));
    DOCTEST_CHECK((state.players[0].research_accumulated == 50 || state.players[0].research_accumulated == 200));
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_PLATE_SHIFT));
    DOCTEST_CHECK(state.planets[0].mineral_class == MINERAL_RESOURCE_RICH);  // the most populous colony
    MOX::NUM_STARS = 3;
    state.planets[0].star_index = 0;
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_PROPHECY));
    DOCTEST_CHECK((state.stars[1].visited & 1) != 0);
    DOCTEST_CHECK((state.stars[2].visited & 1) != 0);
    DOCTEST_CHECK_FALSE(world_events::Start_Race_Event(0, world_events::RACE_DOUBLE_AGENT));  // no spies here
    state.players[1].spies[0] = 2;
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_DOUBLE_AGENT));
    DOCTEST_CHECK(intrigue::Has_Double_Agent(0, 1));
}

DOCTEST_TEST_CASE("bankruptcy: the scale grows in debt, sells a building at 75, recovers out of debt") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.colonies[0].buildings[BUILDING_HYDROPONIC_FARM] = 1;
    state.players[0].bc = -10;
    for (int i = 0; i < 7; ++i) {
        MOX::stardate = 35001 + i;
        world_events::Turn();
    }
    const world_events::s_entry* scale = world_events::Find(world_events::KIND_BANKRUPTCY, 0);
    DOCTEST_REQUIRE(scale != nullptr);
    DOCTEST_CHECK(scale->value == 70);
    MOX::stardate = 35008;
    world_events::Turn();
    DOCTEST_CHECK(state.colonies[0].buildings[BUILDING_HYDROPONIC_FARM] == 0);
    DOCTEST_CHECK(state.players[0].bc > -10);
    state.players[0].bc = 100;
    MOX::stardate = 35009;
    world_events::Turn();
    DOCTEST_CHECK(world_events::Find(world_events::KIND_BANKRUPTCY, 0)->value == 75);
}

DOCTEST_TEST_CASE("off: nothing starts, nothing is kept") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    world_events::enabled = false;
    state.players[0].bc = -10;
    DOCTEST_CHECK_FALSE(world_events::Start_Galaxy_Event(world_events::KIND_GOLDEN_AGE));
    DOCTEST_CHECK_FALSE(world_events::Start_Race_Event(0, world_events::RACE_FAIR_WIND));
    world_events::Turn();
    DOCTEST_CHECK(world_events::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_FAIR_WIND));
    auto buffer = std::make_unique<uint8_t[]>(world_events::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), world_events::Save_Size(), "test overflow");
    world_events::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == world_events::Save_Size());
    world_events::Clear();
    SERIAL::Reader_ reader(buffer.get(), world_events::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(world_events::Parse_Save(&reader, &error));
    DOCTEST_CHECK(world_events::Empty());
    world_events::Commit_Save();
    DOCTEST_CHECK(world_events::Speed_Bonus(0) == 1);
    buffer[2] = 99;  // an unknown kind in the first entry
    SERIAL::Reader_ bad(buffer.get(), world_events::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(world_events::Parse_Save(&bad, &error));
    DOCTEST_CHECK(world_events::Speed_Bonus(0) == 1);
}

}
