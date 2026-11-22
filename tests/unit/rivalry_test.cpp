#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/advisor.h"
#include "game/advisor_directive.h"
#include "game/diplomac.h"
#include "game/opinion.h"
#include "game/rivalry.h"
#include "game/serialization.h"

#include <memory>

namespace {
    struct s_rivalry_scope {
        test::s_saved_value<bool> enabled{rivalry::enabled};
        test::s_saved_value<int32_t> period{rivalry::period};
        test::s_saved_value<int32_t> opinion_value{rivalry::opinion_value};
        test::s_saved_value<int32_t> strength_percent{rivalry::strength_percent};
        test::s_saved_value<int32_t> hatred{rivalry::hatred};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<int32_t> decay_period{opinion::decay_period};
        test::s_saved_value<bool> advisor_enabled{advisor::enabled};
        test::s_saved_value<int32_t> active_players{advisor::active_players};

        s_rivalry_scope() {
            rivalry::Clear();
            opinion::Clear();
            advisor::Clear_All_Directives();
            rivalry::enabled = true;
            rivalry::period = 10;
            rivalry::opinion_value = -20;
            rivalry::strength_percent = 110;
            rivalry::hatred = -50;
            opinion::enabled = true;
            opinion::decay_period = 1;
            advisor::enabled = false;
            advisor::active_players = 255;
        }
        ~s_rivalry_scope() {
            rivalry::Clear();
            opinion::Clear();
            advisor::Clear_All_Directives();
        }
    };

    constexpr int8_t TREATY_PEACE = 3;

    // Four computer empires in contact and in range of each other, at a review turn.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 4;
        MOX::stardate = 35010;
        for (int i = 0; i < 4; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            for (int j = 0; j < 4; ++j) {
                state->players[i].contact[j] = i == j ? 0 : 1;
                state->players[i].within_range[j] = i == j ? 0 : 1;
            }
        }
    }
} // namespace

DOCTEST_TEST_SUITE("rivalry") {

DOCTEST_TEST_CASE("an empire without an advisor takes the contact it hates most, and drops it when the hatred passes") {
    s_rivalry_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].relations[2] = -60;
    state.players[1].relations[3] = -40;
    rivalry::End_Of_Turn();
    DOCTEST_CHECK(rivalry::Rival_Of(1) == 2);
    DOCTEST_CHECK(rivalry::Rival_Since(1) == 35010);
    DOCTEST_CHECK(rivalry::Rival_Of(2) == rivalry::NO_RIVAL);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_RIVAL) == -20);  // the rival remembers

    // Between reviews the memory holds at full value; after a drop it fades.
    MOX::stardate = 35011;
    opinion::End_Of_Turn();
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_RIVAL) == -19);
    rivalry::End_Of_Turn();
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_RIVAL) == -20);

    state.players[1].relations[2] = 10;
    MOX::stardate = 35020;
    rivalry::End_Of_Turn();
    DOCTEST_CHECK(rivalry::Rival_Of(1) == rivalry::NO_RIVAL);  // opinion above zero; 3 at -40 is not enough
    opinion::End_Of_Turn();
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_RIVAL) == -19);

    // Peace ends a rivalry and keeps one from being declared.
    state.players[1].relations[3] = -80;
    state.players[1].treaty[3] = TREATY_PEACE;
    MOX::stardate = 35030;
    rivalry::End_Of_Turn();
    DOCTEST_CHECK(rivalry::Rival_Of(1) == rivalry::NO_RIVAL);
    state.players[1].treaty[3] = 0;
    MOX::stardate = 35040;
    rivalry::End_Of_Turn();
    DOCTEST_CHECK(rivalry::Rival_Of(1) == 3);
    state.players[3].eliminated = 1;
    MOX::stardate = 35050;
    rivalry::End_Of_Turn();
    DOCTEST_CHECK(rivalry::Rival_Of(1) == rivalry::NO_RIVAL);
}

DOCTEST_TEST_CASE("an advised empire takes its main rival when hostile to it, and follows the directive") {
    s_rivalry_scope scope;
    test::s_game_state state;
    Setup_(&state);
    advisor::enabled = true;
    advisor::s_directive directive;
    directive.source = advisor::SOURCE_RULES;
    directive.issued_stardate = 35000;
    directive.expires_stardate = 35100;
    directive.main_rival = 3;
    directive.attitude[3] = advisor::ATTITUDE_WARY;
    advisor::Set_Directive(1, &directive);
    state.players[1].relations[2] = -90;  // the directive decides, not hatred
    rivalry::End_Of_Turn();
    DOCTEST_CHECK(rivalry::Rival_Of(1) == rivalry::NO_RIVAL);  // wary is not enough
    directive.attitude[3] = advisor::ATTITUDE_HOSTILE;
    advisor::Set_Directive(1, &directive);
    MOX::stardate = 35020;
    rivalry::End_Of_Turn();
    DOCTEST_CHECK(rivalry::Rival_Of(1) == 3);
    directive.main_rival = 2;
    directive.attitude[2] = advisor::ATTITUDE_WAR;
    advisor::Set_Directive(1, &directive);
    MOX::stardate = 35030;
    rivalry::End_Of_Turn();  // dropped and replaced in the same review
    DOCTEST_CHECK(rivalry::Rival_Of(1) == 2);

    // The human is never reviewed, but may be someone's rival.
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.players[0].relations[1] = -100;
    state.players[3].relations[0] = -100;
    MOX::stardate = 35040;
    rivalry::End_Of_Turn();
    DOCTEST_CHECK(rivalry::Rival_Of(0) == rivalry::NO_RIVAL);
    DOCTEST_CHECK(rivalry::Rival_Of(3) == 0);
}

DOCTEST_TEST_CASE("an empire weighs war on its rival more boldly") {
    s_rivalry_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].total_ship_strength[2] = 24;
    state.players[2].total_ship_strength[1] = 99;
    const int classic = diplomac::Fleet_Strength_Comparison(1, 2);  // ((24 * 4 + 4) * 25) / 100
    DOCTEST_CHECK(classic == 25);
    const int theirs = diplomac::Fleet_Strength_Comparison(2, 1);
    rivalry::Declare(1, 2);
    DOCTEST_CHECK(rivalry::Strength_Percent(1, 2) == 110);
    DOCTEST_CHECK(rivalry::Strength_Percent(2, 1) == 100);
    DOCTEST_CHECK(diplomac::Fleet_Strength_Comparison(1, 2) == 27);
    DOCTEST_CHECK(diplomac::Fleet_Strength_Comparison(2, 1) == theirs);  // only the declarer is bolder
    rivalry::enabled = false;
    DOCTEST_CHECK(rivalry::Rival_Of(1) == rivalry::NO_RIVAL);
    DOCTEST_CHECK(diplomac::Fleet_Strength_Comparison(1, 2) == classic);
}

DOCTEST_TEST_CASE("off: no rivals are declared") {
    s_rivalry_scope scope;
    test::s_game_state state;
    Setup_(&state);
    rivalry::enabled = false;
    state.players[1].relations[2] = -100;
    rivalry::End_Of_Turn();
    rivalry::Declare(1, 2);
    DOCTEST_CHECK(rivalry::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; a bad payload leaves the current rivalries") {
    s_rivalry_scope scope;
    test::s_game_state state;
    Setup_(&state);
    rivalry::Declare(1, 2);
    rivalry::Declare(3, 0);
    auto buffer = std::make_unique<uint8_t[]>(rivalry::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), rivalry::Save_Size(), "test overflow");
    rivalry::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == rivalry::Save_Size());

    rivalry::Clear();
    rivalry::Declare(2, 3);
    SERIAL::Reader_ reader(buffer.get(), rivalry::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(rivalry::Parse_Save(&reader, &error));
    DOCTEST_CHECK(rivalry::Rival_Of(2) == 3);  // nothing changes before the commit
    rivalry::Commit_Save();
    DOCTEST_CHECK(rivalry::Rival_Of(1) == 2);
    DOCTEST_CHECK(rivalry::Rival_Of(3) == 0);
    DOCTEST_CHECK(rivalry::Rival_Of(2) == rivalry::NO_RIVAL);
    DOCTEST_CHECK(rivalry::Rival_Since(1) == 35010);

    buffer[2] = 0;  // player 0 its own rival
    SERIAL::Reader_ bad(buffer.get(), rivalry::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(rivalry::Parse_Save(&bad, &error));
    rivalry::Commit_Save();
    DOCTEST_CHECK(rivalry::Rival_Of(1) == 2);
}

}
