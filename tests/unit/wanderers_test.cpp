#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "../fixtures/texts_fixture.h"
#include "game/choices.h"
#include "game/projects.h"
#include "game/random.h"
#include "game/serialization.h"
#include "game/wanderers.h"

#include <memory>
#include <string>

namespace {
    struct s_wanderers_scope {
        test::s_saved_value<bool> enabled{wanderers::enabled};
        test::s_saved_value<int16_t> stars{MOX::NUM_STARS};

        s_wanderers_scope() {
            wanderers::Clear();
            wanderers::enabled = true;
        }
        ~s_wanderers_scope() {
            wanderers::Clear();
        }
    };

    // Six stars on a line, ten apart. Player 0 (computer) has colony 0 at star 5, player 1 (`human` or a
    // computer) colony 1 at star 3 with 4 people.
    void Setup_(test::s_game_state* state, bool human) {
        MOX::NUM_PLAYERS = 2;
        MOX::settings.strategic_combat_flag = 1;  // a computer's new technology then redesigns its ships the light way
        MOX::NUM_STARS = 6;
        MOX::NUM_COLONIES = 2;
        MOX::stardate = 35001;
        for (int s = 0; s < 6; ++s) {
            state->stars[s].x = static_cast<int16_t>(10 * s);
            state->stars[s].y = 0;
        }
        state->players[0].objectives = PLAYER_OBJECTIVE_MILITARIST;
        state->players[0].bc = 1000;
        state->players[1].objectives = human ? PLAYER_OBJECTIVE_HUMAN : PLAYER_OBJECTIVE_MILITARIST;
        state->players[1].bc = 1000;
        state->planets[0].star_index = 5;
        state->planets[0].colony_index = 0;
        state->colonies[0].owner = 0;
        state->colonies[0].planet = 0;
        state->colonies[0].n_pops = 6;
        state->planets[1].star_index = 3;
        state->planets[1].colony_index = 1;
        state->colonies[1].owner = 1;
        state->colonies[1].planet = 1;
        state->colonies[1].n_pops = 4;
    }

    // A seed whose next `draws` rolls of 1..1000 all miss the chance of a new wanderer.
    void Calm_(int draws) {
        for (uint32_t seed = 1; seed < 100000; ++seed) {
            game_random::Set_Random_Seed(seed);
            bool calm = true;
            for (int i = 0; i < draws; ++i) calm = calm && game_random::Random(1000) > static_cast<uint32_t>(wanderers::STORM_CHANCE + wanderers::TRADER_CHANCE);
            if (calm) {
                game_random::Set_Random_Seed(seed);
                return;
            }
        }
    }

    // A seed whose next roll of 1..100 hits (or misses) the storm.
    void Hit_Seed_(bool hit) {
        for (uint32_t seed = 1; seed < 100000; ++seed) {
            game_random::Set_Random_Seed(seed);
            const bool h = game_random::Random(100) <= static_cast<uint32_t>(wanderers::STORM_HIT_PERCENT);
            if (h == hit) {
                game_random::Set_Random_Seed(seed);
                return;
            }
        }
    }
} // namespace

DOCTEST_TEST_SUITE("wanderers") {

DOCTEST_TEST_CASE("flag off: nothing starts, nothing grounds, no draw from game_random")
{
    s_wanderers_scope scope;
    test::s_game_state state;
    Setup_(&state, false);
    const int16_t route[2] = {0, 1};
    wanderers::enabled = true;
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_STORM, route, 2, 2) >= 0);
    DOCTEST_CHECK(wanderers::Star_Grounded(0));
    wanderers::enabled = false;
    DOCTEST_CHECK(wanderers::Add(wanderers::KIND_STORM, route, 2, 2) == -1);
    DOCTEST_CHECK_FALSE(wanderers::Star_Grounded(0));
    DOCTEST_CHECK_FALSE(wanderers::Start_Storm());
    DOCTEST_CHECK_FALSE(wanderers::Start_Trader());
    wanderers::Clear();
    const uint32_t seed = game_random::Get_Random_Seed();
    for (int i = 0; i < 20; ++i) wanderers::Turn();
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
    DOCTEST_CHECK(wanderers::Empty());
}

DOCTEST_TEST_CASE("a route of nearest-neighbour hops toward the goal")
{
    s_wanderers_scope scope;
    test::s_game_state state;
    Setup_(&state, false);
    int16_t route[wanderers::MAX_ROUTE];
    const int32_t length = wanderers::Greedy_Route(0, 5, route);
    DOCTEST_REQUIRE(length == 3);  // 0 -> 4 (the nearest-four star closest to the goal) -> 5
    DOCTEST_CHECK(route[0] == 0);
    DOCTEST_CHECK(route[1] == 4);
    DOCTEST_CHECK(route[2] == 5);
    DOCTEST_CHECK(wanderers::Greedy_Route(0, 0, route) == 1);
    DOCTEST_CHECK(wanderers::Greedy_Route(0, 9, route) == 0);  // no such star
}

DOCTEST_TEST_CASE("a storm moves along its route, grounds its star and leaves a trail, then dissolves")
{
    s_wanderers_scope scope;
    test::s_game_state state;
    Setup_(&state, false);
    Calm_(10);
    const int16_t route[3] = {0, 1, 2};  // no colonies on it: no hits
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_STORM, route, 3, wanderers::STORM_MOVE_TURNS) == 0);
    DOCTEST_CHECK(wanderers::At(wanderers::KIND_STORM, 0) != nullptr);
    DOCTEST_CHECK(wanderers::Star_Grounded(0));
    DOCTEST_CHECK_FALSE(wanderers::Star_Grounded(1));
    wanderers::Turn();  // the same turn: it has not moved yet
    DOCTEST_CHECK(wanderers::At(wanderers::KIND_STORM, 0) != nullptr);
    MOX::stardate += wanderers::STORM_MOVE_TURNS;
    wanderers::Turn();
    DOCTEST_CHECK(wanderers::At(wanderers::KIND_STORM, 1) != nullptr);
    DOCTEST_CHECK(wanderers::Star_Grounded(1));
    DOCTEST_CHECK(wanderers::Star_Grounded(0));  // the trail
    MOX::stardate += wanderers::STORM_TRAIL_TURNS;
    DOCTEST_CHECK_FALSE(wanderers::Star_Grounded(0));
    DOCTEST_CHECK(wanderers::Star_Grounded(1));
    wanderers::Turn();
    DOCTEST_CHECK(wanderers::At(wanderers::KIND_STORM, 2) != nullptr);
    MOX::stardate += wanderers::STORM_MOVE_TURNS;
    wanderers::Turn();  // the end of the route
    DOCTEST_CHECK(wanderers::Empty());
    DOCTEST_CHECK_FALSE(wanderers::Star_Grounded(2));
}

DOCTEST_TEST_CASE("a storm that arrives at a colony may hit it")
{
    s_wanderers_scope scope;
    test::s_game_state state;
    Setup_(&state, false);
    const int16_t route[2] = {3, 4};
    Hit_Seed_(false);
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_STORM, route, 2, 2) >= 0);
    DOCTEST_CHECK(state.colonies[1].n_pops == 4);
    wanderers::Clear();
    wanderers::enabled = true;
    Hit_Seed_(true);
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_STORM, route, 2, 2) >= 0);
    DOCTEST_CHECK(state.colonies[1].n_pops < 4);
}

DOCTEST_TEST_CASE("a trader sells the cheapest unknown application to a computer that can pay and keep its reserve")
{
    s_wanderers_scope scope;
    test::s_game_state state;
    Setup_(&state, true);  // player 1 is a human
    Calm_(10);
    const int16_t app = projects::Cheapest_Unknown_App(0);
    DOCTEST_REQUIRE(app >= 0);
    const int32_t price = std::clamp(projects::App_Cost(app) / 2, wanderers::TRADER_MIN_PRICE, wanderers::TRADER_MAX_PRICE);
    const int16_t route[3] = {3, 5, 4};
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_TRADER, route, 3, wanderers::TRADER_MOVE_TURNS) >= 0);
    DOCTEST_CHECK(state.players[1].bc == 1000);  // a human buys nothing
    DOCTEST_CHECK(wanderers::Star_Grounded(3) == false);  // a trader grounds nobody
    MOX::stardate += wanderers::TRADER_MOVE_TURNS;
    wanderers::Turn();  // at star 5: player 0 buys
    DOCTEST_CHECK(state.players[0].bc == 1000 - price);
    DOCTEST_CHECK(state.players[0].tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED);
    DOCTEST_CHECK(wanderers::State()->wanderer[0].value == price);
    MOX::stardate += wanderers::TRADER_MOVE_TURNS;
    wanderers::Turn();  // at star 4, the last stop
    MOX::stardate += wanderers::TRADER_MOVE_TURNS;
    wanderers::Turn();
    DOCTEST_CHECK(wanderers::Empty());
}

DOCTEST_TEST_CASE("a trader's customer that cannot keep the reserve buys nothing")
{
    s_wanderers_scope scope;
    test::s_game_state state;
    Setup_(&state, false);
    const int16_t app = projects::Cheapest_Unknown_App(0);
    DOCTEST_REQUIRE(app >= 0);
    const int32_t price = std::clamp(projects::App_Cost(app) / 2, wanderers::TRADER_MIN_PRICE, wanderers::TRADER_MAX_PRICE);
    state.players[0].bc = price + wanderers::TRADER_RESERVE - 1;
    const int16_t route[2] = {5, 4};
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_TRADER, route, 2, 4) >= 0);
    DOCTEST_CHECK(state.players[0].bc == price + wanderers::TRADER_RESERVE - 1);
    DOCTEST_CHECK(state.players[0].tech_applications[app] != TECH_RESEARCH_STATUS_RESEARCHED);
}

DOCTEST_TEST_CASE("a storm and a trader start by chance with a route; limits hold")
{
    s_wanderers_scope scope;
    test::s_game_state state;
    Setup_(&state, false);
    DOCTEST_REQUIRE(wanderers::Start_Trader());
    DOCTEST_CHECK_FALSE(wanderers::Start_Trader());  // one at a time
    const wanderers::s_wanderer& trader = wanderers::State()->wanderer[0];
    DOCTEST_CHECK(trader.length == 2);  // two colony stars
    DOCTEST_REQUIRE(wanderers::Start_Storm());
    DOCTEST_REQUIRE(wanderers::Start_Storm());
    DOCTEST_CHECK_FALSE(wanderers::Start_Storm());
    DOCTEST_CHECK(wanderers::State()->count == 3);
    for (int32_t i = 1; i < 3; ++i) {
        const wanderers::s_wanderer& storm = wanderers::State()->wanderer[i];
        DOCTEST_CHECK(storm.kind == wanderers::KIND_STORM);
        DOCTEST_CHECK(storm.length >= 2);
        DOCTEST_CHECK(storm.star == storm.route[0]);
    }
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused")
{
    s_wanderers_scope scope;
    test::s_game_state state;
    Setup_(&state, false);
    state.colonies[0].n_pops = 0;  // the storm finds nobody
    state.colonies[1].n_pops = 0;
    const int16_t storm[3] = {0, 1, 2};
    const int16_t trader[2] = {4, 5};
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_STORM, storm, 3, 2) >= 0);
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_TRADER, trader, 2, 4) >= 0);
    wanderers::State()->wanderer[0].trail_star = 3;
    wanderers::State()->wanderer[0].trail_until = 35010;
    const uint32_t size = wanderers::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    wanderers::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    wanderers::Clear();
    wanderers::enabled = true;
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(wanderers::Parse_Save(&reader, &error));
    DOCTEST_CHECK(wanderers::Empty());
    wanderers::Commit_Save();
    DOCTEST_REQUIRE(wanderers::State()->count == 2);
    DOCTEST_CHECK(wanderers::State()->wanderer[0].kind == wanderers::KIND_STORM);
    DOCTEST_CHECK(wanderers::State()->wanderer[0].route[2] == 2);
    DOCTEST_CHECK(wanderers::State()->wanderer[0].trail_star == 3);
    DOCTEST_CHECK(wanderers::State()->wanderer[1].route[1] == 5);

    const auto refuse = [&](size_t offset, uint8_t value) {
        auto bad = std::make_unique<uint8_t[]>(size);
        std::copy(buffer.get(), buffer.get() + size, bad.get());
        bad[offset] = value;
        SERIAL::Reader_ r(bad.get(), size, "test underflow", true);
        return !wanderers::Parse_Save(&r, &error);
    };
    DOCTEST_CHECK(refuse(3, 9));      // the first record's kind
    DOCTEST_CHECK(refuse(4, 3));      // its star no longer matches its route
    DOCTEST_CHECK(refuse(7, 0));      // its length: fewer than two stars
    DOCTEST_CHECK(refuse(6, 5));      // its step is past the route
    DOCTEST_CHECK(refuse(9, 0xFF));   // a route star out of range (high byte)
    DOCTEST_CHECK(refuse(2, 20));     // too many records
    DOCTEST_CHECK(refuse(0, 7));      // the version
    SERIAL::Reader_ r4(buffer.get(), size - 1, "test underflow", true);
    DOCTEST_CHECK_FALSE(wanderers::Parse_Save(&r4, &error));
    DOCTEST_CHECK(wanderers::State()->count == 2);  // the failed parses left the state alone
}


DOCTEST_TEST_CASE("a lightning rod (a planetary shield) lowers the storm's hit chance")
{
    s_wanderers_scope scope;
    test::s_game_state state;
    Setup_(&state, false);
    const int16_t route[2] = {3, 4};
    // A seed whose first roll lies between the rod's chance and the storm's, or within the rod's.
    const auto seed_between = [](int32_t above, int32_t up_to) {
        for (uint32_t seed = 1; seed < 100000; ++seed) {
            game_random::Set_Random_Seed(seed);
            const int32_t roll = static_cast<int32_t>(game_random::Random(100));
            if (roll > above && roll <= up_to) {
                game_random::Set_Random_Seed(seed);
                return;
            }
        }
    };
    DOCTEST_CHECK_FALSE(wanderers::Has_Lightning_Rod(1));
    state.colonies[1].buildings[BUILDING_BARRIER_SHIELD] = 1;
    DOCTEST_CHECK(wanderers::Has_Lightning_Rod(1));
    // A roll the rod turns aside: nothing is lost.
    seed_between(wanderers::STORM_ROD_HIT_PERCENT, wanderers::STORM_HIT_PERCENT);
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_STORM, route, 2, 2) >= 0);
    DOCTEST_CHECK(state.colonies[1].n_pops == 4);
    DOCTEST_CHECK(state.colonies[1].buildings[BUILDING_BARRIER_SHIELD] == 1);
    // The same roll without the rod is a hit.
    wanderers::Clear();
    wanderers::enabled = true;
    state.colonies[1].buildings[BUILDING_BARRIER_SHIELD] = 0;
    seed_between(wanderers::STORM_ROD_HIT_PERCENT, wanderers::STORM_HIT_PERCENT);
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_STORM, route, 2, 2) >= 0);
    DOCTEST_CHECK(state.colonies[1].n_pops < 4);
    // A roll within the rod's chance still hits (the shield or a person is lost).
    wanderers::Clear();
    wanderers::enabled = true;
    state.colonies[1].n_pops = 4;
    state.colonies[1].buildings[BUILDING_FLUX_SHIELD] = 1;
    seed_between(0, wanderers::STORM_ROD_HIT_PERCENT);
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_STORM, route, 2, 2) >= 0);
    DOCTEST_CHECK((state.colonies[1].n_pops < 4 || state.colonies[1].buildings[BUILDING_FLUX_SHIELD] == 0));
}
}

// ---- The choice window (human-ui.md section 5, slice R1, part D): a human is offered what the trader sells.

namespace {
    struct s_trader_scope {
        test::s_saved_value<bool> on{choices::enabled};
        s_trader_scope() {
            choices::Clear();
            choices::enabled = true;
            test::Load_Base_Texts();
        }
        ~s_trader_scope() {
            choices::Clear();
        }
    };
}

DOCTEST_TEST_CASE("choices: a human at the trader's star is offered the application at the computer's price")
{
    s_wanderers_scope scope;
    s_trader_scope window;
    test::s_game_state state;
    Setup_(&state, true);  // player 1 is a human
    Calm_(10);
    const int16_t app = projects::Cheapest_Unknown_App(1);
    DOCTEST_REQUIRE(app >= 0);
    const int32_t price = std::clamp(projects::App_Cost(app) / 2, wanderers::TRADER_MIN_PRICE, wanderers::TRADER_MAX_PRICE);
    const int16_t route[3] = {3, 5, 4};
    const uint32_t seed = game_random::Get_Random_Seed();
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_TRADER, route, 3, wanderers::TRADER_MOVE_TURNS) >= 0);
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);  // no draw
    DOCTEST_CHECK(state.players[1].bc == 1000);             // nothing bought yet
    DOCTEST_REQUIRE(choices::Count(1) == 1);
    const choices::s_choice c = choices::State()->choice[0];
    DOCTEST_CHECK(c.module == choices::MODULE_WANDERERS);
    DOCTEST_CHECK(c.value == price);
    choices::s_text text;
    DOCTEST_REQUIRE(wanderers::Choice_Text(c, &text));
    DOCTEST_CHECK(text.count == 2);
    DOCTEST_CHECK(std::string(text.body).size() > 40);
    DOCTEST_CHECK(std::string(text.option[0]).find(std::to_string(price)) != std::string::npos);
    DOCTEST_REQUIRE(choices::Apply_Pending(1, 0));  // buy
    DOCTEST_CHECK(state.players[1].bc == 1000 - price);
    DOCTEST_CHECK(state.players[1].tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED);
    DOCTEST_CHECK(wanderers::State()->wanderer[0].value == price);
}

DOCTEST_TEST_CASE("choices: declining buys nothing; an unaffordable offer says so and buys nothing")
{
    s_wanderers_scope scope;
    s_trader_scope window;
    test::s_game_state state;
    Setup_(&state, true);
    Calm_(10);
    const int16_t app = projects::Cheapest_Unknown_App(1);
    const int16_t route[2] = {3, 4};
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_TRADER, route, 2, 4) >= 0);
    DOCTEST_REQUIRE(choices::Count(1) == 1);
    DOCTEST_REQUIRE(choices::Apply_Pending(1, 1));  // decline
    DOCTEST_CHECK(state.players[1].bc == 1000);
    DOCTEST_CHECK(state.players[1].tech_applications[app] != TECH_RESEARCH_STATUS_RESEARCHED);
    wanderers::Clear();
    wanderers::enabled = true;
    state.players[1].bc = 5;
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_TRADER, route, 2, 4) >= 0);
    DOCTEST_REQUIRE(choices::Count(1) == 1);
    choices::s_text text;
    DOCTEST_REQUIRE(wanderers::Choice_Text(choices::State()->choice[0], &text));
    DOCTEST_CHECK(std::string(text.option[0]).find("not enough") != std::string::npos);
    DOCTEST_REQUIRE(choices::Apply_Pending(1, 0));
    DOCTEST_CHECK(state.players[1].bc == 5);
    DOCTEST_CHECK(state.players[1].tech_applications[app] != TECH_RESEARCH_STATUS_RESEARCHED);
}

DOCTEST_TEST_CASE("choices: a trader that has left makes the offer's text false; the setting off keeps the old rule")
{
    s_wanderers_scope scope;
    s_trader_scope window;
    test::s_game_state state;
    Setup_(&state, true);
    Calm_(10);
    const int16_t route[3] = {3, 5, 4};
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_TRADER, route, 3, wanderers::TRADER_MOVE_TURNS) >= 0);
    const choices::s_choice c = choices::State()->choice[0];
    choices::s_text text;
    DOCTEST_CHECK(wanderers::Choice_Text(c, &text));
    MOX::stardate += wanderers::TRADER_MOVE_TURNS;
    wanderers::Turn();  // the trader is at star 5 now
    DOCTEST_CHECK_FALSE(wanderers::Choice_Text(c, &text));
    DOCTEST_CHECK(choices::Has_Pending(1, false) == 0);  // dropped
    // Setting off: a human buys nothing and nothing is queued.
    choices::Clear();
    choices::enabled = false;
    wanderers::Clear();
    wanderers::enabled = true;
    MOX::stardate = 35001;
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_TRADER, route, 3, wanderers::TRADER_MOVE_TURNS) >= 0);
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(state.players[1].bc == 1000);
}

DOCTEST_TEST_CASE("choices: a computer at the trader's star is not asked and buys as before")
{
    s_wanderers_scope scope;
    s_trader_scope window;
    test::s_game_state state;
    Setup_(&state, false);
    Calm_(10);
    const int16_t app = projects::Cheapest_Unknown_App(1);
    DOCTEST_REQUIRE(app >= 0);
    const int32_t price = std::clamp(projects::App_Cost(app) / 2, wanderers::TRADER_MIN_PRICE, wanderers::TRADER_MAX_PRICE);
    const int16_t route[2] = {3, 4};
    DOCTEST_REQUIRE(wanderers::Add(wanderers::KIND_TRADER, route, 2, 4) >= 0);
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(state.players[1].bc == 1000 - price);
}
