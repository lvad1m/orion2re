#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/dice.h"
#include "game/officer.h"
#include "game/projects.h"
#include "game/random.h"
#include "game/serialization.h"

#include <memory>
#include <string>

namespace {
    struct s_projects_scope {
        test::s_saved_value<bool> enabled{projects::enabled};
        test::s_saved_value<uint8_t> roll_kind{dice::roll_kind};
        test::s_saved_value<int16_t> stars{MOX::NUM_STARS};
        test::s_saved_value<int16_t> ships{MOX::NUM_SHIPS};

        s_projects_scope() {
            projects::Clear();
            projects::enabled = true;
            dice::roll_kind = dice::ROLL_TWO_D10;
        }
        ~s_projects_scope() {
            projects::Clear();
        }
    };

    // Two empires; three stars. Player 0 has explored star 0 and has a colony (planet 0, colony 0)
    // at star 1; star 2 is unexplored. `artifacts` makes star 1 an ancient site.
    void Setup_(test::s_game_state* state, bool artifacts) {
        MOX::NUM_PLAYERS = 2;
        MOX::NUM_STARS = 3;
        MOX::NUM_SHIPS = 0;
        MOX::NUM_COLONIES = 1;
        MOX::stardate = 35001;
        // Humans: a granted technology then only refreshes the designs' equipment (as in the ancient wreck test).
        for (int i = 0; i < 2; ++i) state->players[i].objectives = PLAYER_OBJECTIVE_HUMAN;
        state->stars[0].visited = 1;
        state->stars[1].visited = 1;
        state->stars[1].system_special = artifacts ? SYSTEM_SPECIAL_ANCIENT_ARTIFACTS : SYSTEM_SPECIAL_NO_SPECIAL;
        state->planets[0].star_index = 1;
        state->planets[0].colony_index = 0;
        state->colonies[0].owner = 0;
        state->colonies[0].planet = 0;
        state->colonies[0].n_pops = 3;
    }

    constexpr int32_t TRIUMPH[2] = {10, 10};
    constexpr int32_t SUCCESS[2] = {6, 6};        // 12: margin +1 against a dig, 0 against an anomaly
    constexpr int32_t FAILURE[2] = {5, 4};        // 9
    constexpr int32_t CATASTROPHE[2] = {1, 1};

    bool Known_(const test::s_game_state& state, int16_t app) {
        return state.players[0].tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED;
    }
} // namespace

DOCTEST_TEST_SUITE("projects") {

DOCTEST_TEST_CASE("flag off: nothing starts, nothing is kept, no draw from game_random")
{
    s_projects_scope scope;
    test::s_game_state state;
    Setup_(&state, true);
    projects::enabled = false;
    DOCTEST_CHECK_FALSE(projects::Start_Anomaly(0, 0));
    DOCTEST_CHECK_FALSE(projects::Start_Excavation(0, 1, 2));
    const uint32_t seed = game_random::Get_Random_Seed();
    for (int i = 0; i < 20; ++i) projects::Turn();
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
    DOCTEST_CHECK(projects::Empty());
}

DOCTEST_TEST_CASE("an anomaly needs an explored star and is studied once; the studied map is kept")
{
    s_projects_scope scope;
    test::s_game_state state;
    Setup_(&state, false);
    DOCTEST_CHECK_FALSE(projects::Start_Anomaly(0, 2));  // not explored
    DOCTEST_REQUIRE(projects::Start_Anomaly(0, 0));
    DOCTEST_CHECK(projects::Active(0) == 1);
    DOCTEST_CHECK(projects::Active(1) == 0);
    DOCTEST_CHECK(projects::Find(projects::KIND_ANOMALY, 0, 0) != nullptr);
    DOCTEST_CHECK(projects::Studied(0));
    DOCTEST_CHECK_FALSE(projects::Start_Anomaly(0, 0));
    DOCTEST_CHECK_FALSE(projects::Studied(1));
}

DOCTEST_TEST_CASE("the anomaly's check, with fixed dice: triumph, success, failure, catastrophe")
{
    s_projects_scope scope;
    test::s_game_state state;
    Setup_(&state, false);
    state.players[0].research_accumulated = 1000;

    // Success (12 against 12).
    DOCTEST_REQUIRE(projects::Start_Anomaly(0, 0));
    projects::Check_With_Dice(0, SUCCESS);
    DOCTEST_CHECK(state.players[0].research_accumulated == 1000 + projects::ANOMALY_RESEARCH);
    DOCTEST_CHECK(projects::State()->count == 0);

    // Triumph: the cheapest unknown application, or research points when none is left.
    state.stars[1].visited = 1;
    const int16_t app = projects::Cheapest_Unknown_App(0);
    DOCTEST_REQUIRE(projects::Start_Anomaly(0, 1));
    projects::Check_With_Dice(0, TRIUMPH);
    if (app >= 0) {
        DOCTEST_CHECK(Known_(state, app));
    } else {
        DOCTEST_CHECK(state.players[0].research_accumulated == 1100 + projects::ANOMALY_BRILLIANT_RESEARCH);
    }
    DOCTEST_CHECK(projects::State()->count == 0);

    // Failure: nothing. Catastrophe: 10 per cent of the research is lost.
    state.stars[2].visited = 1;
    const int32_t before = state.players[0].research_accumulated;
    DOCTEST_REQUIRE(projects::Start_Anomaly(0, 2));
    projects::Check_With_Dice(0, FAILURE);
    DOCTEST_CHECK(state.players[0].research_accumulated == before);
    state.stars[2].visited = 1;
    projects::Clear();
    projects::enabled = true;
    DOCTEST_REQUIRE(projects::Start_Anomaly(0, 2));
    projects::Check_With_Dice(0, CATASTROPHE);
    DOCTEST_CHECK(state.players[0].research_accumulated == before - before * projects::ANOMALY_LOSS_PERCENT / 100);
}

DOCTEST_TEST_CASE("the skill is the best scientist's level plus a little for research output")
{
    s_projects_scope scope;
    test::s_game_state state;
    Setup_(&state, false);
    DOCTEST_CHECK(projects::Skill(0) == 0);
    state.leaders[0].player_index = 0;
    state.leaders[0].status = 0;
    state.leaders[0].general_skills = LEADER_GENERAL_SKILL_RESEARCHER;
    const int32_t level = OFFICER::Owned_Officer_Level(0);
    DOCTEST_CHECK(projects::Skill(0) == level);
    state.leaders[1].player_index = 1;  // a scientist of the other empire does not count
    state.leaders[1].status = 0;
    state.leaders[1].general_skills = LEADER_GENERAL_SKILL_RESEARCHER;
    DOCTEST_CHECK(projects::Skill(0) == level);
    state.players[0].research_produced = 10000;
    DOCTEST_CHECK(projects::Skill(0) == level + projects::SKILL_RESEARCH_MAX);
}

DOCTEST_TEST_CASE("an excavation needs an ancient site and a colony; chapters, relic, failures")
{
    s_projects_scope scope;
    test::s_game_state state;
    Setup_(&state, false);
    DOCTEST_CHECK_FALSE(projects::Start_Excavation(0, 1, 2));  // no artifacts here
    state.stars[1].system_special = SYSTEM_SPECIAL_ANCIENT_ARTIFACTS;
    DOCTEST_CHECK_FALSE(projects::Start_Excavation(1, 1, 2));  // player 1 has no colony there
    DOCTEST_CHECK_FALSE(projects::Start_Excavation(0, 1, 0));
    DOCTEST_CHECK_FALSE(projects::Start_Excavation(0, 1, projects::MAX_CHAPTERS + 1));
    DOCTEST_REQUIRE(projects::Start_Excavation(0, 1, 2));
    DOCTEST_CHECK_FALSE(projects::Start_Excavation(0, 1, 2));  // one dig a star

    const int16_t app = projects::Cheapest_Unknown_App(0);
    projects::Check_With_Dice(0, SUCCESS);  // 12 against 11
    DOCTEST_REQUIRE(projects::State()->count == 1);
    DOCTEST_CHECK(projects::State()->project[0].stage == 1);
    projects::Check_With_Dice(0, FAILURE);
    DOCTEST_REQUIRE(projects::State()->count == 1);
    DOCTEST_CHECK(projects::State()->project[0].stage == 1);
    DOCTEST_CHECK(projects::State()->project[0].failures == 1);
    projects::Check_With_Dice(0, SUCCESS);  // the last chapter: a relic
    DOCTEST_CHECK(projects::State()->count == 0);
    if (app >= 0) DOCTEST_CHECK(Known_(state, app));
}

DOCTEST_TEST_CASE("a brilliant chapter counts twice; three failed chapters abandon the dig")
{
    s_projects_scope scope;
    test::s_game_state state;
    Setup_(&state, true);
    DOCTEST_REQUIRE(projects::Start_Excavation(0, 1, 3));
    projects::Check_With_Dice(0, TRIUMPH);
    DOCTEST_REQUIRE(projects::State()->count == 1);
    DOCTEST_CHECK(projects::State()->project[0].stage == 2);
    projects::Check_With_Dice(0, FAILURE);
    projects::Check_With_Dice(0, FAILURE);
    DOCTEST_CHECK(projects::State()->project[0].failures == 2);
    projects::Check_With_Dice(0, FAILURE);
    DOCTEST_CHECK(projects::State()->count == 0);  // abandoned
}

DOCTEST_TEST_CASE("a guardian stops the dig until it is gone; a second one does not appear")
{
    s_projects_scope scope;
    test::s_game_state state;
    Setup_(&state, true);
    state.stars[0].visited = 0;  // nothing else to find: Turn starts no new project
    DOCTEST_REQUIRE(projects::Start_Excavation(0, 1, 2));
    state.ships[0].owner = MAX_PLAYERS;  // a monster at the star
    state.ships[0].status = 0;
    state.ships[0].location = 1;
    MOX::NUM_SHIPS = 1;
    for (int i = 0; i < projects::CHAPTER_TURNS + 3; ++i) projects::Turn();
    DOCTEST_REQUIRE(projects::State()->count == 1);
    DOCTEST_CHECK(projects::State()->project[0].elapsed == 0);
    projects::Check_With_Dice(0, CATASTROPHE);  // a monster is there: no second guardian
    DOCTEST_CHECK(MOX::NUM_SHIPS == 1);
    DOCTEST_CHECK(projects::State()->project[0].failures == 1);
    state.ships[0].status = 5;  // destroyed
    projects::Turn();
    DOCTEST_CHECK(projects::State()->project[0].elapsed == 1);
}

DOCTEST_TEST_CASE("a project is checked when its time is up (random dice) and then gone")
{
    s_projects_scope scope;
    test::s_game_state state;
    Setup_(&state, false);
    state.stars[1].visited = 0;  // nothing else to find: Turn starts no new project
    DOCTEST_REQUIRE(projects::Start_Anomaly(0, 0));
    for (int i = 0; i < projects::ANOMALY_TURNS - 1; ++i) projects::Turn();
    DOCTEST_CHECK(projects::State()->count == 1);
    projects::Turn();
    DOCTEST_CHECK(projects::State()->count == 0);
}

DOCTEST_TEST_CASE("an empire that is gone loses its projects; an old dig is abandoned")
{
    s_projects_scope scope;
    test::s_game_state state;
    Setup_(&state, true);
    state.stars[0].visited = 0;
    state.stars[1].visited = 0;
    DOCTEST_REQUIRE(projects::Start_Excavation(0, 1, 2));
    MOX::stardate += projects::DIG_MAX_AGE;
    projects::Turn();
    DOCTEST_CHECK(projects::State()->count == 0);
    state.stars[0].visited = 1;
    DOCTEST_REQUIRE(projects::Start_Anomaly(0, 0));
    state.players[0].eliminated = 1;
    projects::Turn();
    DOCTEST_CHECK(projects::State()->count == 0);
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused")
{
    s_projects_scope scope;
    test::s_game_state state;
    Setup_(&state, true);
    DOCTEST_REQUIRE(projects::Start_Anomaly(0, 0));
    DOCTEST_REQUIRE(projects::Start_Excavation(0, 1, 4));
    const uint32_t size = projects::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    projects::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    projects::Clear();
    projects::enabled = true;
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(projects::Parse_Save(&reader, &error));
    DOCTEST_CHECK(projects::Empty());
    projects::Commit_Save();
    DOCTEST_REQUIRE(projects::State()->count == 2);
    DOCTEST_CHECK(projects::State()->project[1].kind == projects::KIND_EXCAVATION);
    DOCTEST_CHECK(projects::State()->project[1].chapters == 4);
    DOCTEST_CHECK(projects::Studied(0));
    DOCTEST_CHECK(projects::Studied(1));
    DOCTEST_CHECK_FALSE(projects::Studied(2));

    const auto refuse = [&](size_t offset, uint8_t value) {
        auto bad = std::make_unique<uint8_t[]>(size);
        std::copy(buffer.get(), buffer.get() + size, bad.get());
        bad[offset] = value;
        SERIAL::Reader_ r(bad.get(), size, "test underflow", true);
        return !projects::Parse_Save(&r, &error);
    };
    DOCTEST_CHECK(refuse(3, 9));      // the first record's kind
    DOCTEST_CHECK(refuse(4, 99));     // its empire
    DOCTEST_CHECK(refuse(6, 0xFF));   // its star (high byte of a negative or huge value)
    DOCTEST_CHECK(refuse(2, 40));     // too many records
    DOCTEST_CHECK(refuse(0, 7));      // the version
    SERIAL::Reader_ r4(buffer.get(), size - 1, "test underflow", true);
    DOCTEST_CHECK_FALSE(projects::Parse_Save(&r4, &error));
    DOCTEST_CHECK(projects::State()->count == 2);  // the failed parses left the state alone
}

}
