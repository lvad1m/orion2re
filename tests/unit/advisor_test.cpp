#include "pch.h"

#include "config/cfg_parser.h"
#include "config/config_decode.h"
#include "config/config_runtime.h"
#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/advisor.h"
#include "game/advisor_directive.h"

namespace {
    bool Append_Decoded_(const char* source, config::s_config_write_set* write_set, config::s_cfg_diagnostic* diagnostic) {
        config::s_cfg_document document;
        if (!config::Parse_Cfg("advisor.cfg", source, std::strlen(source), &document, diagnostic)) {
            return false;
        }
        for (const config::s_cfg_statement& statement : document.statements) {
            config::s_config_write_set statement_writes;
            if (!config::Decode_Assignment(&statement, &statement_writes, diagnostic)) {
                return false;
            }
            for (config::s_config_write& write : statement_writes.writes) {
                write_set->writes.push_back(std::move(write));
            }
        }
        return true;
    }

    struct s_advisor_scope {
        bool old_enabled = advisor::enabled;
        uint8_t old_journal = advisor::journal;

        s_advisor_scope() {
            advisor::Reset();
        }
        ~s_advisor_scope() {
            advisor::Reset();
            advisor::enabled = old_enabled;
            advisor::journal = old_journal;
        }
    };

    void Setup_Two_Computer_Players_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 2;
        for (int i = 0; i < 2; i++) {
            state->players[i].objectives = (uint8_t)(i + 1);
            state->players[i].eliminated = 0;
        }
    }
} // namespace

DOCTEST_TEST_CASE("advisor configuration defaults and binding") {
    s_advisor_scope scope;
    config::s_config_state state;
    DOCTEST_REQUIRE(config::Initialize_Defaults(&state));
    DOCTEST_REQUIRE(config::Commit_State(&state));
    DOCTEST_CHECK_FALSE(advisor::enabled);
    DOCTEST_CHECK(advisor::journal == advisor::JOURNAL_OFF);

    config::s_config_write_set writes;
    config::s_cfg_diagnostic diagnostic;
    DOCTEST_REQUIRE(Append_Decoded_(
        "advisor enabled = true;"
        "advisor journal = full;",
        &writes,
        &diagnostic));
    DOCTEST_REQUIRE(config::Apply_Write_Set(&writes, &state, &diagnostic));
    DOCTEST_REQUIRE(config::Commit_State(&state));
    DOCTEST_CHECK(advisor::enabled);
    DOCTEST_CHECK(advisor::journal == advisor::JOURNAL_FULL);
}

DOCTEST_TEST_CASE("advisor disabled stores nothing and leaves players unchanged") {
    s_advisor_scope scope;
    test::s_game_state state;
    Setup_Two_Computer_Players_(&state);
    advisor::enabled = false;
    advisor::journal = advisor::JOURNAL_OFF;

    advisor::Run_Turn(&game::current);

    for (int i = 0; i < 2; i++) {
        DOCTEST_CHECK(state.players[i].objectives == i + 1);
        const advisor::s_directive* stored = advisor::Get_Directive((uint8_t)i);
        DOCTEST_CHECK((stored == nullptr || stored->source == advisor::SOURCE_NONE));
    }
}

DOCTEST_TEST_CASE("advisor enabled with its mechanics off leaves objectives unchanged") {
    s_advisor_scope scope;
    test::s_game_state state;
    Setup_Two_Computer_Players_(&state);
    advisor::enabled = true;
    advisor::journal = advisor::JOURNAL_OFF;
    const bool rules_objectives = advisor::rules_objectives;
    advisor::rules_objectives = false;

    advisor::Run_Turn(&game::current);

    for (int i = 0; i < 2; i++) {
        DOCTEST_CHECK(state.players[i].objectives == i + 1);
    }
    advisor::rules_objectives = rules_objectives;
}

DOCTEST_TEST_CASE("advisor enabled: a militarist at peace turns technologist (rule 105)") {
    s_advisor_scope scope;
    test::s_game_state state;
    Setup_Two_Computer_Players_(&state);  // player 0 MILITARIST, player 1 EXPANSIONIST
    // Rule 105 needs at least one (peaceful) contact and turn 50 or later.
    state.players[0].contact[1] = 1;
    state.players[0].relations[1] = 10;
    MOX::stardate = 35060;  // the fixture restores the stardate on exit
    advisor::enabled = true;
    advisor::journal = advisor::JOURNAL_OFF;
    const bool rules_objectives = advisor::rules_objectives;
    advisor::rules_objectives = true;

    advisor::Run_Turn(&game::current);

    const advisor::s_directive* stored = advisor::Get_Directive(0);
    DOCTEST_REQUIRE(stored != nullptr);
    DOCTEST_CHECK(stored->source == advisor::SOURCE_RULES);
    DOCTEST_CHECK(stored->rule_id == 105);
    DOCTEST_CHECK(state.players[0].objectives != PLAYER_OBJECTIVE_MILITARIST);
    DOCTEST_CHECK(advisor::Last_Objective_Change(0) == game::current.Stardate());
    // Player 1 is not a militarist and no other rule fires in this galaxy.
    DOCTEST_CHECK(state.players[1].objectives == PLAYER_OBJECTIVE_EXPANSIONIST);
    advisor::rules_objectives = rules_objectives;
}
