#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/advisor.h"
#include "game/embargo.h"
#include "game/opinion.h"
#include "game/resolutions.h"
#include "game/serialization.h"

#include <memory>

namespace {
    struct s_resolutions_scope {
        test::s_saved_value<bool> embargo_enabled{embargo::enabled};
        test::s_saved_value<bool> council{embargo::council_resolutions};
        test::s_saved_value<int32_t> embargo_period{embargo::period};
        test::s_saved_value<int32_t> turns{resolutions::turns};
        test::s_saved_value<int32_t> violation{resolutions::violation_opinion};
        test::s_saved_value<int32_t> fund_percent{resolutions::fund_percent};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<bool> advisor_enabled{advisor::enabled};

        s_resolutions_scope() {
            resolutions::Clear();
            embargo::Clear();
            opinion::Clear();
            embargo::enabled = true;
            embargo::council_resolutions = true;
            embargo::period = 10;
            resolutions::turns = 100;
            resolutions::violation_opinion = -40;
            resolutions::fund_percent = 2;
            opinion::enabled = true;
            advisor::enabled = false;
        }
        ~s_resolutions_scope() {
            resolutions::Clear();
            embargo::Clear();
            opinion::Clear();
        }
    };

    // Five computer empires in contact, 10 council votes, 100 BC income and 1000 BC each.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 5;
        MOX::stardate = 35010;
        for (int i = 0; i < 5; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[i].total_pop = 100;
            state->players[i].surplus_bc = 100;
            state->players[i].bc = 1000;
            for (int j = 0; j < 5; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
    }
} // namespace

DOCTEST_TEST_SUITE("council resolutions") {

DOCTEST_TEST_CASE("the council bans biological weapons after their use; a violation brings anger and a council embargo") {
    s_resolutions_scope scope;
    test::s_game_state state;
    Setup_(&state);
    resolutions::On_Banned_Act(3, resolutions::BAN_BIO_WEAPONS);
    DOCTEST_CHECK(resolutions::State()->last_use[resolutions::BAN_BIO_WEAPONS][3] == 35010);
    DOCTEST_CHECK_FALSE(embargo::Active(3));  // no ban yet: nothing more
    MOX::stardate = 35020;
    resolutions::Council_Meeting();  // 0, 1, 2 and 4 never used them: 40 of 50
    DOCTEST_CHECK(resolutions::Ban_In_Force(resolutions::BAN_BIO_WEAPONS));
    DOCTEST_CHECK_FALSE(resolutions::Ban_In_Force(resolutions::BAN_PLANET_DESTRUCTION));
    DOCTEST_CHECK(resolutions::State()->last_meeting == 35020);

    MOX::stardate = 35021;
    resolutions::On_Banned_Act(3, resolutions::BAN_BIO_WEAPONS);
    resolutions::On_Banned_Act(3, resolutions::BAN_BIO_WEAPONS);  // same turn: punished once
    for (int k : {0, 1, 2, 4}) {
        DOCTEST_CHECK(opinion::Modifier(static_cast<int16_t>(k), 3, opinion::REASON_BROKE_BAN) == -40);
    }
    DOCTEST_CHECK(embargo::Active(3));
    DOCTEST_CHECK(embargo::Record(3)->by_council == 1);
    DOCTEST_CHECK(embargo::Participates(4, 3));

    // The ban ends with its term.
    resolutions::turns = 10;
    MOX::stardate = 35030;
    resolutions::End_Of_Turn();
    DOCTEST_CHECK_FALSE(resolutions::Ban_In_Force(resolutions::BAN_BIO_WEAPONS));
}

DOCTEST_TEST_CASE("the Antaran fund: proposed after an attack, paid by the computer empires, half to the victim") {
    s_resolutions_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[4].surplus_bc = 500;  // the richest votes against
    resolutions::On_Antaran_Attack(2);
    MOX::stardate = 35020;
    resolutions::Council_Meeting();  // 40 of 50
    DOCTEST_REQUIRE(resolutions::State()->fund == 1);
    resolutions::End_Of_Turn();
    DOCTEST_CHECK(state.players[0].bc == 998);
    DOCTEST_CHECK(state.players[4].bc == 990);
    DOCTEST_CHECK(resolutions::State()->fund_pool == 18);
    resolutions::On_Antaran_Attack(1);
    DOCTEST_CHECK(state.players[1].bc == 1007);
    DOCTEST_CHECK(resolutions::State()->fund_pool == 9);
    resolutions::turns = 10;
    MOX::stardate = 35030;
    resolutions::End_Of_Turn();
    DOCTEST_CHECK(resolutions::State()->fund == 0);
}

DOCTEST_TEST_CASE("the proposal with the most support is put to the vote") {
    s_resolutions_scope scope;
    test::s_game_state state;
    Setup_(&state);
    // An embargo proposal (1 on 3, with 2): 20 votes. A ban after 3 destroyed a planet: 40 votes.
    state.players[1].relations[3] = -60;
    state.players[1].broken_treaties_modifier[3] = -5;
    state.players[2].relations[3] = -30;
    resolutions::On_Banned_Act(3, resolutions::BAN_PLANET_DESTRUCTION);
    MOX::stardate = 35020;
    resolutions::Council_Meeting();
    DOCTEST_CHECK(resolutions::Ban_In_Force(resolutions::BAN_PLANET_DESTRUCTION));
    DOCTEST_CHECK_FALSE(embargo::Active(3));
    // Nothing new since the meeting: the next one puts the embargo to the vote, and it fails.
    MOX::stardate = 35030;
    resolutions::Council_Meeting();
    DOCTEST_CHECK_FALSE(embargo::Active(3));
}

DOCTEST_TEST_CASE("off: nothing is remembered or voted") {
    s_resolutions_scope scope;
    test::s_game_state state;
    Setup_(&state);
    embargo::council_resolutions = false;
    resolutions::On_Banned_Act(3, resolutions::BAN_BIO_WEAPONS);
    resolutions::On_Antaran_Attack(2);
    resolutions::Council_Meeting();
    DOCTEST_CHECK(resolutions::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_resolutions_scope scope;
    test::s_game_state state;
    Setup_(&state);
    resolutions::On_Banned_Act(3, resolutions::BAN_BIO_WEAPONS);
    resolutions::On_Antaran_Attack(2);
    MOX::stardate = 35020;
    resolutions::Council_Meeting();
    resolutions::State()->fund_pool = 77;
    auto buffer = std::make_unique<uint8_t[]>(resolutions::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), resolutions::Save_Size(), "test overflow");
    resolutions::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == resolutions::Save_Size());

    const resolutions::s_state saved = *resolutions::State();
    resolutions::Clear();
    SERIAL::Reader_ reader(buffer.get(), resolutions::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(resolutions::Parse_Save(&reader, &error));
    DOCTEST_CHECK(resolutions::Empty());  // nothing changes before the commit
    resolutions::Commit_Save();
    DOCTEST_CHECK(resolutions::State()->bans == saved.bans);
    DOCTEST_CHECK(resolutions::State()->fund_pool == 77);
    DOCTEST_CHECK(resolutions::State()->last_use[resolutions::BAN_BIO_WEAPONS][3] == 35010);
    DOCTEST_CHECK(resolutions::State()->last_meeting == 35020);

    buffer[2] = 0xFF;  // bans byte: unknown bans
    SERIAL::Reader_ bad(buffer.get(), resolutions::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(resolutions::Parse_Save(&bad, &error));
}

}
