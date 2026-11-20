#include "pch.h"

#include "doctest/doctest.h"
#include "game/advisor_view.h"
#include "game/legacy_world.h"
#include "game/shipstak.h"
#include "../fixtures/game_state.h"

namespace {
    struct s_advisor_state : test::s_game_state {
        decltype(SHIPSTAK::scan_bonus) scan = SHIPSTAK::scan_bonus;
        decltype(SHIPSTAK::stealth_bonus) stealth = SHIPSTAK::stealth_bonus;

        s_advisor_state() {
            MOX::NUM_PLAYERS = 3;
            for (int32_t i = 0; i < 3; ++i) {
                std::snprintf(MOX::player[i].race_name, sizeof(MOX::player[i].race_name), "Race%d", i);
                std::snprintf(MOX::player[i].name, sizeof(MOX::player[i].name), "Leader%d", i);
            }
            SHIPSTAK::stealth_bonus.stealthy_ships = 0;
            SHIPSTAK::stealth_bonus.stealth_field = 0;
            SHIPSTAK::stealth_bonus.cloaking_device = 0;
            SHIPSTAK::stealth_bonus.phasing_cloak = 0;
        }

        ~s_advisor_state() {
            SHIPSTAK::scan_bonus = scan;
            SHIPSTAK::stealth_bonus = stealth;
        }

        // Non-combat ship types keep the strength measure (AI tables) out of these tests.
        static void Place_Ship(int32_t index, int8_t owner, int16_t x) {
            MOX::ship[index].owner = owner;
            MOX::ship[index].status = 0;
            MOX::ship[index].x = x;
            MOX::ship[index].y = 0;
            MOX::ship[index].d.ship_type = SHIP_TYPE_OUTPOST;
        }
    };
}

DOCTEST_TEST_CASE_FIXTURE(s_advisor_state, "advisor view without contacts has no contact entries") {
    const game::s_view world = legacy_world::World();
    advisor::s_empire_view view;

    DOCTEST_CHECK(advisor::Build_Empire_View(&world, 0, &view));
    DOCTEST_CHECK(view.contact_count == 0);
    DOCTEST_CHECK(std::strcmp(view.self.race_name, "Race0") == 0);
    DOCTEST_CHECK(view.self.stardate == 35000);
}

DOCTEST_TEST_CASE_FIXTURE(s_advisor_state, "advisor view lists only players in contact") {
    MOX::player[0].contact[1] = 1;
    const game::s_view world = legacy_world::World();
    advisor::s_empire_view view;

    DOCTEST_REQUIRE(advisor::Build_Empire_View(&world, 0, &view));
    DOCTEST_REQUIRE(view.contact_count == 1);
    DOCTEST_CHECK(view.contacts[0].player == 1);
    DOCTEST_CHECK(std::strcmp(view.contacts[0].race_name, "Race1") == 0);
}

DOCTEST_TEST_CASE_FIXTURE(s_advisor_state, "advisor diplomacy facts carry the diplomacy source with high confidence") {
    MOX::player[0].contact[1] = 1;
    MOX::player[0].relations[1] = -78;
    MOX::player[0].treaty[1] = 6;
    MOX::player[0].trade_treaty[1] = 1;
    MOX::player[0].research_treaty[1] = 0;
    const game::s_view world = legacy_world::World();
    advisor::s_empire_view view;

    DOCTEST_REQUIRE(advisor::Build_Empire_View(&world, 0, &view));
    const advisor::s_view_contact& contact = view.contacts[0];
    DOCTEST_CHECK(contact.relations.value == -78);
    DOCTEST_CHECK(contact.treaty.value == 6);
    DOCTEST_CHECK(contact.trade_treaty.value == 1);

    const advisor::s_fact* facts[] = {
        &contact.relations, &contact.treaty, &contact.trade_treaty, &contact.research_treaty};
    for (const advisor::s_fact* fact : facts) {
        DOCTEST_CHECK(fact->source == advisor::FACT_DIPLOMACY);
        DOCTEST_CHECK(fact->confidence == advisor::CONFIDENCE_HIGH);
        DOCTEST_CHECK(fact->stardate == 35000);
    }
}

DOCTEST_TEST_CASE_FIXTURE(s_advisor_state, "advisor counts only enemy ships our scanners detect") {
    MOX::NUM_SHIPS = 3;
    Place_Ship(0, 0, 0);    // ours, extends scan range around itself
    Place_Ship(1, 1, 30);   // one scan step away
    Place_Ship(2, 1, 600);  // twenty scan steps away
    MOX::player[0].contact[1] = 1;
    SHIPSTAK::scan_bonus.no_scanner = 2;
    const game::s_view world = legacy_world::World();
    advisor::s_empire_view view;

    DOCTEST_REQUIRE(advisor::Build_Empire_View(&world, 0, &view));
    DOCTEST_CHECK(view.self.ships == 1);
    DOCTEST_CHECK(view.contacts[0].visible_ships.value == 1);
    DOCTEST_CHECK(view.contacts[0].visible_ships.source == advisor::FACT_SENSORS);
    DOCTEST_CHECK(view.contacts[0].visible_ships.confidence == advisor::CONFIDENCE_HIGH);
}

DOCTEST_TEST_CASE_FIXTURE(s_advisor_state, "advisor knows colonies only at visited stars") {
    MOX::NUM_STARS = 2;
    MOX::planet_count = 2;
    MOX::NUM_COLONIES = 2;
    MOX::planet[0].star_index = 0;
    MOX::planet[1].star_index = 1;
    MOX::colony[0].owner = 1;
    MOX::colony[0].planet = 0;
    MOX::colony[1].owner = 1;
    MOX::colony[1].planet = 1;
    MOX::star[0].visited = 1;  // bit of player 0
    MOX::player[0].contact[1] = 1;
    const game::s_view world = legacy_world::World();
    advisor::s_empire_view view;

    DOCTEST_REQUIRE(advisor::Build_Empire_View(&world, 0, &view));
    DOCTEST_CHECK(view.contacts[0].known_colonies.value == 1);
    DOCTEST_CHECK(view.contacts[0].known_colonies.source == advisor::FACT_OWN_RECORDS);
    DOCTEST_CHECK(view.contacts[0].known_colonies.confidence == advisor::CONFIDENCE_MEDIUM);
    DOCTEST_CHECK(view.self.stars_explored == 1);
    DOCTEST_CHECK(view.self.stars_total == 2);
}

DOCTEST_TEST_CASE_FIXTURE(s_advisor_state, "advisor view rejects eliminated and out-of-range players") {
    MOX::player[0].contact[1] = 1;
    MOX::player[2].eliminated = 1;
    const game::s_view world = legacy_world::World();
    advisor::s_empire_view view;
    view.contact_count = 5;

    DOCTEST_CHECK_FALSE(advisor::Build_Empire_View(&world, 2, &view));
    DOCTEST_CHECK(view.contact_count == 0);

    view.contact_count = 5;
    DOCTEST_CHECK_FALSE(advisor::Build_Empire_View(&world, 3, &view));
    DOCTEST_CHECK(view.contact_count == 0);
    DOCTEST_CHECK_FALSE(advisor::Build_Empire_View(nullptr, 0, &view));
}

DOCTEST_TEST_CASE_FIXTURE(s_advisor_state, "advisor summary names the race and tags facts, deterministically") {
    MOX::player[0].contact[1] = 1;
    MOX::player[0].relations[1] = -78;
    const game::s_view world = legacy_world::World();
    advisor::s_empire_view view;
    DOCTEST_REQUIRE(advisor::Build_Empire_View(&world, 0, &view));

    std::string first;
    std::string second;
    advisor::Format_Empire_Summary(&view, &first);
    advisor::Format_Empire_Summary(&view, &second);

    DOCTEST_CHECK(first == second);
    DOCTEST_CHECK(first.find("Race0") != std::string::npos);
    DOCTEST_CHECK(first.find("Race1") != std::string::npos);
    DOCTEST_CHECK(first.find("relations -78 [diplomacy, 35000, high]") != std::string::npos);
    DOCTEST_CHECK(first.find("[sensors, 35000, high]") != std::string::npos);
    DOCTEST_CHECK(first.find("[own records, 35000, medium]") != std::string::npos);
}

DOCTEST_TEST_CASE("advisor name helpers return stable text for unknown values") {
    DOCTEST_CHECK(std::strcmp(advisor::Objective_Name(1), "militarist") == 0);
    DOCTEST_CHECK(std::strcmp(advisor::Personality_Name(2), "aggressive") == 0);
    DOCTEST_CHECK(std::strcmp(advisor::Fact_Source_Name(advisor::FACT_SENSORS), "sensors") == 0);
    DOCTEST_CHECK(std::strcmp(advisor::Confidence_Name(advisor::CONFIDENCE_HIGH), "high") == 0);
    DOCTEST_CHECK(std::strcmp(advisor::Treaty_Name(99), "unknown") == 0);
    DOCTEST_CHECK(std::strcmp(advisor::Objective_Name(77), "unknown") == 0);
}

DOCTEST_TEST_CASE_FIXTURE(s_advisor_state, "advisor counts free regular planets at visited stars in reach") {
    // Home at star 0; ship range 3 parsecs reaches 135 map units.
    MOX::player[0].ship_range = 3;
    const int16_t star_x[] = {0, 100, 400, 50, 60, 120};
    MOX::NUM_STARS = 6;
    for (int16_t i = 0; i < MOX::NUM_STARS; ++i) {
        MOX::star[i].x = star_x[i];
        MOX::star[i].y = 0;
        MOX::star[i].visited = 1;  // bit of player 0
    }
    MOX::star[3].visited = 0;                       // never visited
    MOX::star[0].black_hole_blocks[0] = 1 << 4;     // star 4 is behind a black hole

    struct s_planet_case {
        int16_t star;
        int8_t type;
        int16_t colony;
    };
    const s_planet_case planets[] = {
        {0, PLANET_TYPE_PLANET, 0},       // our home colony
        {1, PLANET_TYPE_PLANET, -1},      // counted
        {1, PLANET_TYPE_PLANET, -1},      // counted
        {1, PLANET_TYPE_GAS_GIANT, -1},   // not a colony-ship target
        {2, PLANET_TYPE_PLANET, -1},      // out of reach
        {3, PLANET_TYPE_PLANET, -1},      // not visited
        {4, PLANET_TYPE_PLANET, -1},      // blocked by the black hole
        {5, PLANET_TYPE_PLANET, 1},       // already colonized by someone
    };
    MOX::planet_count = 8;
    for (int16_t i = 0; i < MOX::planet_count; ++i) {
        MOX::planet[i].star_index = planets[i].star;
        MOX::planet[i].planet_type = planets[i].type;
        MOX::planet[i].colony_index = planets[i].colony;
    }
    MOX::NUM_COLONIES = 2;
    MOX::colony[0].owner = 0;
    MOX::colony[0].planet = 0;
    MOX::colony[1].owner = 1;
    MOX::colony[1].planet = 7;
    const game::s_view world = legacy_world::World();
    advisor::s_empire_view view;

    DOCTEST_REQUIRE(advisor::Build_Empire_View(&world, 0, &view));
    DOCTEST_CHECK(view.self.free_planets_in_reach == 2);
    DOCTEST_CHECK(view.self.free_outpost_sites_in_reach == 1);  // the gas giant at star 1

    std::string summary;
    advisor::Format_Empire_Summary(&view, &summary);
    DOCTEST_CHECK(summary.find("2 free planets and 1 outpost sites in reach") != std::string::npos);

    // Longer range brings the far star into reach; the black hole still blocks star 4.
    MOX::player[0].ship_range = 10;
    DOCTEST_REQUIRE(advisor::Build_Empire_View(&world, 0, &view));
    DOCTEST_CHECK(view.self.free_planets_in_reach == 3);
}

DOCTEST_TEST_CASE_FIXTURE(s_advisor_state, "advisor counts researched technology applications") {
    MOX::player[0].tech_applications[TECH_APP_COLONY_SHIP] = TECH_RESEARCH_STATUS_RESEARCHED;
    MOX::player[0].tech_applications[TECH_APP_COLONY_BASE] = TECH_RESEARCH_STATUS_RESEARCHED;
    MOX::player[1].tech_applications[TECH_APP_SPACEPORT] = TECH_RESEARCH_STATUS_RESEARCHED;
    const game::s_view world = legacy_world::World();
    advisor::s_empire_view view;

    DOCTEST_REQUIRE(advisor::Build_Empire_View(&world, 0, &view));
    DOCTEST_CHECK(view.self.techs_known == 2);
    std::string summary;
    advisor::Format_Empire_Summary(&view, &summary);
    DOCTEST_CHECK(summary.find("Technologies known: 2.") != std::string::npos);
}

DOCTEST_TEST_CASE_FIXTURE(s_advisor_state, "advisor learns a whole empire from its spies there") {
    MOX::player[0].contact[1] = 1;
    MOX::NUM_STARS = 2;
    MOX::planet_count = 2;
    MOX::NUM_COLONIES = 2;
    MOX::planet[0].star_index = 0;
    MOX::planet[1].star_index = 1;
    MOX::colony[0].owner = 1;
    MOX::colony[0].planet = 0;
    MOX::colony[1].owner = 1;
    MOX::colony[1].planet = 1;
    MOX::star[0].visited = 1;  // we have seen only one of their colonies
    MOX::player[1].tech_applications[TECH_APP_SPACEPORT] = TECH_RESEARCH_STATUS_RESEARCHED;
    const game::s_view world = legacy_world::World();
    advisor::s_empire_view view;

    DOCTEST_REQUIRE(advisor::Build_Empire_View(&world, 0, &view));
    DOCTEST_CHECK(view.contacts[0].known_colonies.value == 1);
    DOCTEST_CHECK(view.contacts[0].known_colonies.source == advisor::FACT_OWN_RECORDS);
    DOCTEST_CHECK(view.contacts[0].techs_known.source == advisor::FACT_UNKNOWN);

    MOX::player[0].spies[1] = 0x80;  // mission bits only (hide), no spy
    DOCTEST_REQUIRE(advisor::Build_Empire_View(&world, 0, &view));
    DOCTEST_CHECK(view.contacts[0].our_spies == 0);
    DOCTEST_CHECK(view.contacts[0].known_colonies.source == advisor::FACT_OWN_RECORDS);

    MOX::player[0].spies[1] = 0x81;  // one spy hiding
    DOCTEST_REQUIRE(advisor::Build_Empire_View(&world, 0, &view));
    DOCTEST_CHECK(view.contacts[0].our_spies == 1);
    DOCTEST_CHECK(view.contacts[0].known_colonies.value == 2);
    DOCTEST_CHECK(view.contacts[0].known_colonies.source == advisor::FACT_SPY);
    DOCTEST_CHECK(view.contacts[0].techs_known.value == 1);
    DOCTEST_CHECK(view.contacts[0].techs_known.source == advisor::FACT_SPY);
    std::string summary;
    advisor::Format_Empire_Summary(&view, &summary);
    DOCTEST_CHECK(summary.find("technologies known 1 [spy") != std::string::npos);
}
