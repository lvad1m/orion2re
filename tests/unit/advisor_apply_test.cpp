#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/advisor.h"
#include "game/advisor_apply.h"
#include "game/advisor_directive.h"

namespace {
    // Restores every advisor global the tests touch.
    struct s_apply_scope {
        bool old_enabled = advisor::enabled;
        bool old_rules_objectives = advisor::rules_objectives;
        bool old_rules_spies = advisor::rules_spies;
        bool old_rules_expansion = advisor::rules_expansion;
        bool old_rules_doctrine = advisor::rules_doctrine;
        bool old_rules_counter_design = advisor::rules_counter_design;
        bool old_rules_outposts = advisor::rules_outposts;
        bool old_rules_exploration = advisor::rules_exploration;
        bool old_rules_lanchester = advisor::rules_lanchester;
        bool old_rules_balance = advisor::rules_balance;
        int32_t old_active_players = advisor::active_players;
        int32_t old_min_turns = advisor::objective_min_turns;
        int32_t old_weight_percent = advisor::spy_target_weight_percent;
        int32_t old_doctrine_parts = advisor::doctrine_parts;

        s_apply_scope() {
            advisor::Clear_All_Directives();
            advisor::enabled = true;
            advisor::rules_objectives = true;
            advisor::rules_spies = true;
            advisor::rules_expansion = true;
            advisor::rules_doctrine = true;
            advisor::rules_counter_design = true;
            advisor::rules_outposts = true;
            advisor::rules_exploration = true;
            advisor::rules_lanchester = true;
            advisor::rules_balance = true;
            advisor::active_players = 255;
            advisor::objective_min_turns = 10;
            advisor::spy_target_weight_percent = 300;
            advisor::doctrine_parts = advisor::DOCTRINE_PART_ALL;
        }
        ~s_apply_scope() {
            advisor::Clear_All_Directives();
            advisor::enabled = old_enabled;
            advisor::rules_objectives = old_rules_objectives;
            advisor::rules_spies = old_rules_spies;
            advisor::rules_expansion = old_rules_expansion;
            advisor::rules_doctrine = old_rules_doctrine;
            advisor::rules_counter_design = old_rules_counter_design;
            advisor::rules_outposts = old_rules_outposts;
            advisor::rules_exploration = old_rules_exploration;
            advisor::rules_lanchester = old_rules_lanchester;
            advisor::rules_balance = old_rules_balance;
            advisor::active_players = old_active_players;
            advisor::objective_min_turns = old_min_turns;
            advisor::spy_target_weight_percent = old_weight_percent;
            advisor::doctrine_parts = old_doctrine_parts;
        }
    };

    // Player 0 is human, 1..3 are computer empires (3 is eliminated). Stardate 35000.
    void Setup_World_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 4;
        MOX::stardate = 35000;
        state->players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
        for (int i = 1; i < 4; ++i) state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
        state->players[3].eliminated = 1;
    }

    advisor::s_directive Directive_() {
        advisor::s_directive directive;
        directive.source = advisor::SOURCE_RULES;
        directive.issued_stardate = 35000;
        directive.expires_stardate = 35005;
        directive.objective = PLAYER_OBJECTIVE_ECOLOGIST;
        directive.spy_target = 2;
        directive.spy_mission = advisor::SPY_SABOTAGE;
        return directive;
    }

    uint8_t Objective_(const test::s_game_state& state, int player) {
        return state.players[player].objectives;
    }
} // namespace

DOCTEST_TEST_CASE("Apply_Directives writes the objective of an active directive") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    const advisor::s_directive directive = Directive_();
    advisor::Set_Directive(1, &directive);

    advisor::Apply_Directives(&game::current);
    DOCTEST_CHECK(Objective_(state, 1) == PLAYER_OBJECTIVE_ECOLOGIST);
    DOCTEST_CHECK(advisor::Last_Objective_Change(1) == 35000);
    DOCTEST_CHECK(Objective_(state, 2) == PLAYER_OBJECTIVE_MILITARIST);
}

DOCTEST_TEST_CASE("Apply_Directives changes nothing when it must not") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    advisor::s_directive directive = Directive_();
    advisor::Set_Directive(0, &directive);  // human
    advisor::Set_Directive(1, &directive);
    advisor::Set_Directive(3, &directive);  // eliminated

    advisor::enabled = false;
    advisor::Apply_Directives(&game::current);
    advisor::enabled = true;
    advisor::rules_objectives = false;
    advisor::Apply_Directives(&game::current);
    advisor::rules_objectives = true;
    DOCTEST_CHECK(Objective_(state, 1) == PLAYER_OBJECTIVE_MILITARIST);

    MOX::stardate = 35006;  // directive expired
    advisor::Apply_Directives(&game::current);
    DOCTEST_CHECK(Objective_(state, 1) == PLAYER_OBJECTIVE_MILITARIST);

    MOX::stardate = 35000;
    advisor::Apply_Directives(&game::current);
    DOCTEST_CHECK(Objective_(state, 0) == PLAYER_OBJECTIVE_HUMAN);
    DOCTEST_CHECK(Objective_(state, 3) == PLAYER_OBJECTIVE_MILITARIST);
    DOCTEST_CHECK(Objective_(state, 1) == PLAYER_OBJECTIVE_ECOLOGIST);

    directive.objective = advisor::OBJECTIVE_KEEP;
    advisor::Set_Directive(2, &directive);
    advisor::Apply_Directives(&game::current);
    DOCTEST_CHECK(Objective_(state, 2) == PLAYER_OBJECTIVE_MILITARIST);
    DOCTEST_CHECK(advisor::Last_Objective_Change(2) == 0);
}

DOCTEST_TEST_CASE("Apply_Directives rate limits objective changes") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    advisor::s_directive directive = Directive_();
    directive.expires_stardate = 35100;
    advisor::Set_Directive(1, &directive);
    advisor::Apply_Directives(&game::current);
    DOCTEST_REQUIRE(Objective_(state, 1) == PLAYER_OBJECTIVE_ECOLOGIST);

    directive.objective = PLAYER_OBJECTIVE_TECHNOLOGIST;
    advisor::Set_Directive(1, &directive);
    MOX::stardate = 35009;  // 9 turns after the change
    advisor::Apply_Directives(&game::current);
    DOCTEST_CHECK(Objective_(state, 1) == PLAYER_OBJECTIVE_ECOLOGIST);
    DOCTEST_CHECK(advisor::Last_Objective_Change(1) == 35000);

    MOX::stardate = 35010;
    advisor::Apply_Directives(&game::current);
    DOCTEST_CHECK(Objective_(state, 1) == PLAYER_OBJECTIVE_TECHNOLOGIST);
    DOCTEST_CHECK(advisor::Last_Objective_Change(1) == 35010);
}

DOCTEST_TEST_CASE("spy hooks give the directive target a higher weight") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    const advisor::s_directive directive = Directive_();
    advisor::Set_Directive(1, &directive);

    DOCTEST_CHECK(advisor::Spy_Target_Weight_Percent(1, 2) == 300);
    DOCTEST_CHECK(advisor::Spy_Target_Weight_Percent(1, 0) == 100);
    DOCTEST_CHECK(advisor::Spy_Target_Weight_Percent(2, 2) == 100);  // no directive for player 2

    advisor::spy_target_weight_percent = 450;
    DOCTEST_CHECK(advisor::Spy_Target_Weight_Percent(1, 2) == 450);

    advisor::rules_spies = false;
    DOCTEST_CHECK(advisor::Spy_Target_Weight_Percent(1, 2) == 100);
    advisor::rules_spies = true;
    advisor::enabled = false;
    DOCTEST_CHECK(advisor::Spy_Target_Weight_Percent(1, 2) == 100);
    advisor::enabled = true;

    MOX::stardate = 35006;  // expired
    DOCTEST_CHECK(advisor::Spy_Target_Weight_Percent(1, 2) == 100);
}

DOCTEST_TEST_CASE("Preferred_Spy_Mission maps steal technology and sabotage") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    advisor::s_directive directive = Directive_();
    advisor::Set_Directive(1, &directive);

    int8_t mission = -1;
    DOCTEST_CHECK(advisor::Preferred_Spy_Mission(1, 2, &mission));
    DOCTEST_CHECK(mission == 1);

    directive.spy_mission = advisor::SPY_STEAL_TECH;
    advisor::Set_Directive(1, &directive);
    DOCTEST_CHECK(advisor::Preferred_Spy_Mission(1, 2, &mission));
    DOCTEST_CHECK(mission == 0);

    mission = 5;
    DOCTEST_CHECK_FALSE(advisor::Preferred_Spy_Mission(1, 0, &mission));  // other target
    directive.spy_mission = advisor::SPY_GATHER_INTEL;
    advisor::Set_Directive(1, &directive);
    DOCTEST_CHECK_FALSE(advisor::Preferred_Spy_Mission(1, 2, &mission));
    directive.spy_mission = advisor::SPY_KEEP;
    advisor::Set_Directive(1, &directive);
    DOCTEST_CHECK_FALSE(advisor::Preferred_Spy_Mission(1, 2, &mission));
    DOCTEST_CHECK(mission == 5);

    directive.spy_mission = advisor::SPY_SABOTAGE;
    advisor::Set_Directive(1, &directive);
    advisor::rules_spies = false;
    DOCTEST_CHECK_FALSE(advisor::Preferred_Spy_Mission(1, 2, &mission));
    advisor::rules_spies = true;
    advisor::enabled = false;
    DOCTEST_CHECK_FALSE(advisor::Preferred_Spy_Mission(1, 2, &mission));
}

DOCTEST_TEST_CASE("Colonization_Push follows the colonization weight of an active directive") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    advisor::s_directive high = Directive_();
    high.weights[advisor::WEIGHT_COLONIZATION] = advisor::LEVEL_HIGH;
    advisor::s_directive normal = Directive_();
    normal.weights[advisor::WEIGHT_COLONIZATION] = advisor::LEVEL_NORMAL;
    advisor::Set_Directive(0, &high);    // human
    advisor::Set_Directive(1, &high);
    advisor::Set_Directive(2, &normal);
    advisor::Set_Directive(3, &high);    // eliminated

    DOCTEST_CHECK(advisor::Colonization_Push(1));
    DOCTEST_CHECK_FALSE(advisor::Colonization_Push(0));
    DOCTEST_CHECK_FALSE(advisor::Colonization_Push(2));
    DOCTEST_CHECK_FALSE(advisor::Colonization_Push(3));
    DOCTEST_CHECK_FALSE(advisor::Colonization_Push(advisor::NO_PLAYER));

    MOX::stardate = 35006;  // past expires_stardate
    DOCTEST_CHECK_FALSE(advisor::Colonization_Push(1));
    MOX::stardate = 35000;

    advisor::rules_expansion = false;
    DOCTEST_CHECK_FALSE(advisor::Colonization_Push(1));
    advisor::rules_expansion = true;
    advisor::enabled = false;
    DOCTEST_CHECK_FALSE(advisor::Colonization_Push(1));
}

DOCTEST_TEST_CASE("Desired_Spies: an observer per contact plus more at the spy target, within income") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    advisor::s_directive directive = Directive_();  // spy_target 2
    advisor::Set_Directive(0, &directive);  // human
    advisor::Set_Directive(1, &directive);
    state.players[1].contact[0] = 1;
    state.players[1].contact[2] = 1;
    state.players[1].contact[3] = 1;  // eliminated: not counted

    uint8_t count = 0;
    state.players[1].surplus_bc = 50;
    state.players[1].treaty[2] = 0;  // peace: 2 observers + 1 more at the target
    DOCTEST_REQUIRE(advisor::Desired_Spies(1, &count));
    DOCTEST_CHECK(count == 2 + advisor::DESIRED_SPIES_AT_PEACE - 1);
    state.players[1].treaty[2] = 5;  // war: 2 observers + 3 more at the target
    DOCTEST_REQUIRE(advisor::Desired_Spies(1, &count));
    DOCTEST_CHECK(count == 2 + advisor::DESIRED_SPIES_AT_WAR - 1);

    state.players[1].stop_spying_duration[0] = 5;  // agreed not to spy on player 0
    DOCTEST_REQUIRE(advisor::Desired_Spies(1, &count));
    DOCTEST_CHECK(count == 1 + advisor::DESIRED_SPIES_AT_WAR - 1);
    state.players[1].stop_spying_duration[0] = 0;

    state.players[1].surplus_bc = 12;  // owns none, affords two more
    DOCTEST_REQUIRE(advisor::Desired_Spies(1, &count));
    DOCTEST_CHECK(count == 2);
    BILL::Set_Their_Spy_Number(1, 2, 3);  // owned spies count against the budget as already paid
    DOCTEST_REQUIRE(advisor::Desired_Spies(1, &count));
    DOCTEST_CHECK(count == 5);

    DOCTEST_CHECK_FALSE(advisor::Desired_Spies(0, &count));  // human
    DOCTEST_CHECK_FALSE(advisor::Desired_Spies(2, &count));  // no directive
    advisor::rules_spies = false;
    DOCTEST_CHECK_FALSE(advisor::Desired_Spies(1, &count));
}

DOCTEST_TEST_CASE("Cover_Contacts_With_Spies puts an observer at every uncovered contact") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    MOX::NUM_PLAYERS = 5;
    state.players[4].objectives = PLAYER_OBJECTIVE_MILITARIST;
    advisor::s_directive directive = Directive_();  // spy_target 2, sabotage
    advisor::Set_Directive(1, &directive);
    for (int i : {0, 2, 4}) state.players[1].contact[i] = 1;
    state.players[1].relations[0] = 20;
    state.players[1].relations[4] = -30;
    BILL::Set_Their_Spy_Number(1, 2, 3);
    BILL::Set_Their_Spy_Number(1, 1, 0);

    advisor::Cover_Contacts_With_Spies(1);

    // Player 4 (lower relations) is covered first, from the target's three spies; then player 0.
    DOCTEST_CHECK(BILL::Get_Their_Spy_Number(1, 4) == 1);
    DOCTEST_CHECK(BILL::Get_Their_Spy_Number(1, 0) == 1);
    DOCTEST_CHECK(BILL::Get_Their_Spy_Number(1, 2) == 1);
    DOCTEST_CHECK(BILL::Get_Their_Spy_Mission(1, 4) == advisor::SPY_MISSION_HIDE);
    DOCTEST_CHECK(BILL::Get_Their_Spy_Mission(1, 0) == advisor::SPY_MISSION_HIDE);

    // Nothing left to give: every contact keeps what it has.
    state.players[1].contact[3] = 0;
    advisor::Cover_Contacts_With_Spies(1);
    DOCTEST_CHECK(BILL::Get_Their_Spy_Number(1, 2) == 1);

    // Off without the spy mechanic.
    BILL::Set_Their_Spy_Number(1, 0, 0);
    BILL::Set_Their_Spy_Number(1, 2, 4);
    advisor::rules_spies = false;
    advisor::Cover_Contacts_With_Spies(1);
    DOCTEST_CHECK(BILL::Get_Their_Spy_Number(1, 0) == 0);
}

DOCTEST_TEST_CASE("Lanchester_Percent weighs warship numbers for advised computer empires") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    MOX::NUM_SHIPS = 8;
    for (int i = 0; i < 8; ++i) {
        MOX::ship[i].d.ship_type = SHIP_TYPE_COMBAT;
        MOX::ship[i].status = 0;
        MOX::ship[i].owner = static_cast<int8_t>(i < 5 ? 1 : 2);  // five warships for 1, three for 2
    }
    MOX::ship[4].d.ship_type = SHIP_TYPE_COLONY;  // not a warship: four for 1
    MOX::ship[7].status = 5;                      // destroyed: two for 2

    DOCTEST_CHECK(advisor::Lanchester_Percent(1, 2) == 100 * 5 / 3);
    DOCTEST_CHECK(advisor::Lanchester_Percent(2, 1) == 100 * 3 / 5);
    DOCTEST_CHECK(advisor::Lanchester_Percent(1, 3) == advisor::LANCHESTER_MAX_PERCENT);
    DOCTEST_CHECK(advisor::Lanchester_Percent(0, 2) == 100);  // human

    advisor::active_players = 4;  // only player 2 is advised
    DOCTEST_CHECK(advisor::Lanchester_Percent(1, 2) == 100);
    DOCTEST_CHECK(advisor::Lanchester_Percent(2, 1) == 100 * 3 / 5);
    advisor::active_players = 255;
    advisor::rules_lanchester = false;
    DOCTEST_CHECK(advisor::Lanchester_Percent(1, 2) == 100);
}

DOCTEST_TEST_CASE("Attitude_Bias follows the directive's attitudes") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    advisor::s_directive directive = Directive_();
    directive.attitude[2] = advisor::ATTITUDE_HOSTILE;
    directive.attitude[0] = advisor::ATTITUDE_FRIENDLY;
    advisor::Set_Directive(1, &directive);
    advisor::Set_Directive(0, &directive);  // human: ignored

    DOCTEST_CHECK(advisor::Attitude_Bias(1, 2) == -50);
    DOCTEST_CHECK(advisor::Attitude_Bias(1, 0) == 50);
    DOCTEST_CHECK(advisor::Attitude_Bias(1, 3) == 0);  // no attitude: no bias
    DOCTEST_CHECK(advisor::Attitude_Bias(0, 2) == 0);
    advisor::rules_balance = false;
    advisor::rules_doctrine = false;
    DOCTEST_CHECK(advisor::Attitude_Bias(1, 2) == 0);
}

DOCTEST_TEST_CASE("Range_Research_Push follows the directive's propulsion focus") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    advisor::s_directive directive = Directive_();
    directive.research_focus = advisor::RESEARCH_PROPULSION;
    advisor::Set_Directive(0, &directive);  // human
    advisor::Set_Directive(1, &directive);
    advisor::s_directive weapons = Directive_();
    weapons.research_focus = advisor::RESEARCH_WEAPONS;
    advisor::Set_Directive(2, &weapons);

    DOCTEST_CHECK(advisor::Range_Research_Push(1));
    DOCTEST_CHECK_FALSE(advisor::Range_Research_Push(0));
    DOCTEST_CHECK_FALSE(advisor::Range_Research_Push(2));
    advisor::active_players = 4;
    DOCTEST_CHECK_FALSE(advisor::Range_Research_Push(1));
    advisor::active_players = 255;
    advisor::rules_exploration = false;
    DOCTEST_CHECK_FALSE(advisor::Range_Research_Push(1));

    DOCTEST_CHECK(advisor::Is_Range_Application(TECH_APP_DEUTERIUM_FUEL_CELLS));
    DOCTEST_CHECK(advisor::Is_Range_Application(TECH_APP_EXTENDED_FUEL_TANKS));
    DOCTEST_CHECK_FALSE(advisor::Is_Range_Application(TECH_APP_COLONY_SHIP));
}

DOCTEST_TEST_CASE("Colonization_Push_Ships maps the colonization weight to concurrent colony ships") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    advisor::s_directive high = Directive_();
    high.weights[advisor::WEIGHT_COLONIZATION] = advisor::LEVEL_HIGH;
    advisor::s_directive max = Directive_();
    max.weights[advisor::WEIGHT_COLONIZATION] = advisor::LEVEL_MAX;
    advisor::Set_Directive(1, &high);
    advisor::Set_Directive(2, &max);
    DOCTEST_CHECK(advisor::Colonization_Push_Ships(1) == 1);
    DOCTEST_CHECK(advisor::Colonization_Push_Ships(2) == 2);
    DOCTEST_CHECK(advisor::Colonization_Push_Ships(0) == 0);  // human
    advisor::rules_expansion = false;
    DOCTEST_CHECK(advisor::Colonization_Push_Ships(2) == 0);
}

DOCTEST_TEST_CASE("Outpost_Push follows the colonization weight with its own flag") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    advisor::s_directive high = Directive_();
    high.weights[advisor::WEIGHT_COLONIZATION] = advisor::LEVEL_HIGH;
    advisor::Set_Directive(1, &high);
    DOCTEST_CHECK(advisor::Outpost_Push(1));
    advisor::rules_expansion = false;  // colony ships off, outposts still on
    DOCTEST_CHECK(advisor::Outpost_Push(1));
    DOCTEST_CHECK_FALSE(advisor::Colonization_Push(1));
    advisor::rules_outposts = false;
    DOCTEST_CHECK_FALSE(advisor::Outpost_Push(1));
}

DOCTEST_TEST_CASE("Attack_Margin_Percent asks advised empires for decisive force") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    DOCTEST_CHECK(advisor::Attack_Margin_Percent(1) == advisor::DECISIVE_FORCE_PERCENT);
    DOCTEST_CHECK(advisor::Attack_Margin_Percent(0) == 100);  // human
    advisor::active_players = 4;
    DOCTEST_CHECK(advisor::Attack_Margin_Percent(1) == 100);
    advisor::active_players = 255;
    advisor::rules_lanchester = false;
    DOCTEST_CHECK(advisor::Attack_Margin_Percent(1) == 100);
}

DOCTEST_TEST_CASE("Design_Reference_Enemy picks the real enemy for ship design") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);  // 0 human, 1-3 computer, 3 eliminated
    state.players[1].contact[0] = 1;
    state.players[1].contact[2] = 1;
    state.players[1].contact[3] = 1;
    state.players[0].total_ship_strength[1] = 50;
    state.players[2].total_ship_strength[1] = 400;
    state.players[3].total_ship_strength[1] = 9000;  // eliminated: ignored

    DOCTEST_CHECK(advisor::Design_Reference_Enemy(1, 0) == 2);  // strongest living contact
    advisor::s_directive directive = Directive_();
    directive.main_rival = 0;
    advisor::Set_Directive(1, &directive);
    DOCTEST_CHECK(advisor::Design_Reference_Enemy(1, 0) == 0);  // the advisor's main rival

    DOCTEST_CHECK(advisor::Design_Reference_Enemy(2, 1) == 1);  // no contacts: the fallback
    DOCTEST_CHECK(advisor::Design_Reference_Enemy(0, 3) == 3);  // human: the fallback
    advisor::rules_counter_design = false;
    DOCTEST_CHECK(advisor::Design_Reference_Enemy(1, 3) == 3);
}

DOCTEST_TEST_CASE("Peace_Eagerness_Turns: buy peace from the rival when losing") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    advisor::s_directive directive = Directive_();
    directive.posture = advisor::POSTURE_MAKE_PEACE;
    directive.main_rival = 2;
    advisor::Set_Directive(1, &directive);
    DOCTEST_CHECK(advisor::Peace_Eagerness_Turns(1, 2) == advisor::DOCTRINE_PEACE_TURNS);
    DOCTEST_CHECK(advisor::Peace_Eagerness_Turns(1, 3) == 0);
    advisor::rules_doctrine = false;
    DOCTEST_CHECK(advisor::Peace_Eagerness_Turns(1, 2) == 0);
    DOCTEST_CHECK(advisor::Attitude_Bias(1, 3) == 0);
}

DOCTEST_TEST_CASE("Raid_Worthiness_Percent favours undefended targets under the doctrine") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    DOCTEST_CHECK(advisor::Raid_Worthiness_Percent(1, true) == advisor::DOCTRINE_RAID_PERCENT);
    DOCTEST_CHECK(advisor::Raid_Worthiness_Percent(1, false) == 100);
    DOCTEST_CHECK(advisor::Raid_Worthiness_Percent(0, true) == 100);  // human
    advisor::rules_doctrine = false;
    DOCTEST_CHECK(advisor::Raid_Worthiness_Percent(1, true) == 100);
}

DOCTEST_TEST_CASE("Attitude_Bias: NEUTRAL carries the doctrine's peace bias") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    advisor::s_directive directive = Directive_();
    directive.attitude[2] = advisor::ATTITUDE_NEUTRAL;
    advisor::Set_Directive(1, &directive);
    DOCTEST_CHECK(advisor::Attitude_Bias(1, 2) == advisor::DOCTRINE_PEACE_BIAS);
    advisor::rules_balance = false;  // the doctrine alone is enough
    DOCTEST_CHECK(advisor::Attitude_Bias(1, 2) == advisor::DOCTRINE_PEACE_BIAS);
    advisor::doctrine_parts = advisor::DOCTRINE_PART_ALL & ~advisor::DOCTRINE_PART_PEACE;
    DOCTEST_CHECK(advisor::Attitude_Bias(1, 2) == 0);
}

DOCTEST_TEST_CASE("Readiness and the other doctrine parts follow doctrine_parts") {
    s_apply_scope scope;
    test::s_game_state state;
    Setup_World_(&state);
    DOCTEST_CHECK(advisor::Readiness(1));
    DOCTEST_CHECK_FALSE(advisor::Readiness(0));  // human
    advisor::active_players = 0xFF & ~(1 << 1);
    DOCTEST_CHECK_FALSE(advisor::Readiness(1));
    advisor::active_players = 255;
    advisor::doctrine_parts = advisor::DOCTRINE_PART_DEFAULT;  // 61: everything but readiness
    DOCTEST_CHECK(advisor::DOCTRINE_PART_DEFAULT == 61);
    DOCTEST_CHECK_FALSE(advisor::Readiness(1));
    DOCTEST_CHECK(advisor::Raid_Worthiness_Percent(1, true) == advisor::DOCTRINE_RAID_PERCENT);
    advisor::doctrine_parts = advisor::DOCTRINE_PART_ALL & ~advisor::DOCTRINE_PART_RAIDS;
    DOCTEST_CHECK(advisor::Raid_Worthiness_Percent(1, true) == 100);
    advisor::doctrine_parts = advisor::DOCTRINE_PART_ALL;
    advisor::rules_doctrine = false;
    DOCTEST_CHECK_FALSE(advisor::Readiness(1));
}
