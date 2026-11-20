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
        int32_t old_min_turns = advisor::objective_min_turns;
        int32_t old_weight_percent = advisor::spy_target_weight_percent;

        s_apply_scope() {
            advisor::Clear_All_Directives();
            advisor::enabled = true;
            advisor::rules_objectives = true;
            advisor::rules_spies = true;
            advisor::rules_expansion = true;
            advisor::objective_min_turns = 10;
            advisor::spy_target_weight_percent = 300;
        }
        ~s_apply_scope() {
            advisor::Clear_All_Directives();
            advisor::enabled = old_enabled;
            advisor::rules_objectives = old_rules_objectives;
            advisor::rules_spies = old_rules_spies;
            advisor::rules_expansion = old_rules_expansion;
            advisor::objective_min_turns = old_min_turns;
            advisor::spy_target_weight_percent = old_weight_percent;
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
