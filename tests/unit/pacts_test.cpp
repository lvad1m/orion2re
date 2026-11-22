#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/advisor.h"
#include "game/advisor_directive.h"
#include "game/diplomacy_memory.h"
#include "game/opinion.h"
#include "game/pacts.h"
#include "game/serialization.h"

#include <memory>

namespace {
    struct s_pacts_scope {
        test::s_saved_value<bool> enabled{pacts::enabled};
        test::s_saved_value<int32_t> period{pacts::period};
        test::s_saved_value<int32_t> opinion_min{pacts::opinion_min};
        test::s_saved_value<int32_t> defended{pacts::defended_opinion};
        test::s_saved_value<int32_t> abandoned{pacts::abandoned_opinion};
        test::s_saved_value<int32_t> guarantees_max{pacts::guarantees_max};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<bool> memory_enabled{diplomemory::enabled};
        test::s_saved_value<int32_t> weary_limit{diplomemory::weary_limit};
        test::s_saved_value<bool> advisor_enabled{advisor::enabled};
        test::s_saved_value<int32_t> active_players{advisor::active_players};

        s_pacts_scope() {
            pacts::Clear();
            opinion::Clear();
            advisor::Clear_All_Directives();
            pacts::enabled = true;
            pacts::period = 10;
            pacts::opinion_min = 20;
            pacts::defended_opinion = 25;
            pacts::abandoned_opinion = -40;
            pacts::guarantees_max = 2;
            opinion::enabled = true;
            diplomemory::enabled = false;
            diplomemory::weary_limit = 0;
            advisor::enabled = false;
            advisor::active_players = 255;
        }
        ~s_pacts_scope() {
            pacts::Clear();
            opinion::Clear();
            advisor::Clear_All_Directives();
        }
    };

    constexpr int8_t TREATY_WAR = 4;

    // Five computer empires in contact and in range of each other, at a review turn.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 5;
        MOX::stardate = 35010;
        for (int i = 0; i < 5; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            for (int j = 0; j < 5; ++j) {
                state->players[i].contact[j] = i == j ? 0 : 1;
                state->players[i].within_range[j] = i == j ? 0 : 1;
            }
        }
    }

    void Like_(test::s_game_state* state, int a, int b, int value) {
        state->players[a].relations[b] = static_cast<int8_t>(value);
        state->players[b].relations[a] = static_cast<int8_t>(value);
    }

    // 1 and 2 like each other and both dislike 3, whose fleet matches each of theirs.
    void Common_Threat_(test::s_game_state* state) {
        Like_(state, 1, 2, 30);
        state->players[1].relations[3] = -10;
        state->players[2].relations[3] = -10;
        state->players[3].total_ship_strength[1] = 100;
        state->players[1].total_ship_strength[3] = 50;
        state->players[3].total_ship_strength[2] = 100;
        state->players[2].total_ship_strength[3] = 50;
    }
} // namespace

DOCTEST_TEST_SUITE("defence pacts") {

DOCTEST_TEST_CASE("empires facing a common threat sign a pact; a strong empire guarantees a weak neighbour it values") {
    s_pacts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Common_Threat_(&state);
    // 0 values 2 and is twice as strong against it; 0 dislikes 3, which outmatches 2.
    state.players[0].relations[2] = 40;
    state.players[0].relations[3] = -5;
    state.players[0].total_ship_strength[2] = 300;
    state.players[2].total_ship_strength[0] = 100;
    pacts::End_Of_Turn();
    DOCTEST_CHECK(pacts::Has_Pact(1, 2));
    DOCTEST_CHECK(pacts::Has_Pact(2, 1));
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 3));
    DOCTEST_CHECK(pacts::Guarantees(0, 2));
    DOCTEST_CHECK_FALSE(pacts::Guarantees(2, 0));
    MOX::stardate = 35011;  // not a review turn
    pacts::Clear();
    pacts::End_Of_Turn();
    DOCTEST_CHECK(pacts::Empty());
}

DOCTEST_TEST_CASE("no pact without the opinion, the threat, or the advisor's leave") {
    s_pacts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Common_Threat_(&state);
    state.players[2].relations[1] = 10;  // below pacts_opinion
    pacts::End_Of_Turn();
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
    state.players[2].relations[1] = 30;
    state.players[3].total_ship_strength[2] = 10;  // 3 no match for 2: no common threat
    MOX::stardate = 35020;
    pacts::End_Of_Turn();
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
    state.players[3].total_ship_strength[2] = 100;
    advisor::enabled = true;
    advisor::s_directive directive;
    directive.source = advisor::SOURCE_RULES;
    directive.issued_stardate = 35000;
    directive.expires_stardate = 35100;
    directive.attitude[2] = advisor::ATTITUDE_WARY;
    advisor::Set_Directive(1, &directive);
    MOX::stardate = 35030;
    pacts::End_Of_Turn();
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
    directive.attitude[2] = advisor::ATTITUDE_FRIENDLY;
    advisor::Set_Directive(1, &directive);
    MOX::stardate = 35040;
    pacts::End_Of_Turn();
    DOCTEST_CHECK(pacts::Has_Pact(1, 2));
}

DOCTEST_TEST_CASE("an attack on a partner brings its partners and guarantors into the war, without a chain reaction") {
    s_pacts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    pacts::Sign_Pact(1, 2, 3);
    pacts::Give_Guarantee(0, 1, 3);
    pacts::Sign_Pact(3, 4, -1);  // the aggressor's own partner
    state.players[3].treaty[1] = state.players[1].treaty[3] = TREATY_WAR;  // 3 attacks 1
    pacts::On_Aggression(3, 1);
    DOCTEST_CHECK(state.players[2].treaty[3] >= TREATY_WAR);
    DOCTEST_CHECK(state.players[0].treaty[3] >= TREATY_WAR);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_DEFENDED_US) == 25);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_DEFENDED_US) == 25);
    DOCTEST_CHECK(state.players[4].treaty[2] < TREATY_WAR);  // defensive wars call on no pacts
    DOCTEST_CHECK(state.players[4].treaty[0] < TREATY_WAR);
    pacts::On_Aggression(3, 1);  // partners already at war: nothing more
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_DEFENDED_US) == 25);
}

DOCTEST_TEST_CASE("a partner too weary for a new war stays out: remembered, and the pact ends") {
    s_pacts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    diplomemory::enabled = true;
    diplomemory::weary_limit = 10;
    // 2 is weary of its war with 4: it lost half of its fleet units (8 of 16 left).
    state.players[2].treaty[4] = state.players[4].treaty[2] = TREATY_WAR;
    MOX::NUM_SHIPS = 1;
    state.ships[0].owner = 2;
    state.ships[0].status = 0;
    state.ships[0].d.size = 3;
    state.players[2].ship_war_losses[4] = 16;
    DOCTEST_REQUIRE(diplomemory::Too_Weary_For_New_War(2));
    pacts::Sign_Pact(1, 2, 3);
    pacts::On_Aggression(3, 1);
    DOCTEST_CHECK(state.players[2].treaty[3] < TREATY_WAR);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_ABANDONED_US) == -40);
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
}

DOCTEST_TEST_CASE("pacts and guarantees end when their terms are gone") {
    s_pacts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Like_(&state, 1, 2, 30);
    Like_(&state, 0, 4, 30);
    pacts::Sign_Pact(1, 2, -1);
    pacts::Sign_Pact(0, 4, -1);
    pacts::Give_Guarantee(0, 3, -1);
    state.players[0].relations[3] = 30;
    state.players[0].total_ship_strength[3] = 100;
    state.players[3].total_ship_strength[0] = 10;
    state.players[2].relations[1] = -1;  // 2 fell out with 1
    state.players[4].eliminated = 1;
    pacts::End_Of_Turn();
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(0, 4));
    DOCTEST_CHECK(pacts::Guarantees(0, 3));
    state.players[3].total_ship_strength[0] = 100;  // no longer the weaker
    MOX::stardate = 35020;
    pacts::End_Of_Turn();
    DOCTEST_CHECK_FALSE(pacts::Guarantees(0, 3));
}

DOCTEST_TEST_CASE("off: nothing is signed and no one answers an attack") {
    s_pacts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    pacts::Sign_Pact(1, 2, 3);
    pacts::enabled = false;
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
    pacts::On_Aggression(3, 1);
    DOCTEST_CHECK(state.players[2].treaty[3] < TREATY_WAR);
    pacts::Clear();
    pacts::Sign_Pact(1, 2, 3);
    pacts::Give_Guarantee(0, 1, 3);
    DOCTEST_CHECK(pacts::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; a one-sided pact is refused") {
    s_pacts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    pacts::Sign_Pact(1, 2, -1);
    pacts::Give_Guarantee(0, 3, -1);
    auto buffer = std::make_unique<uint8_t[]>(pacts::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), pacts::Save_Size(), "test overflow");
    pacts::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == pacts::Save_Size());

    pacts::Clear();
    SERIAL::Reader_ reader(buffer.get(), pacts::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(pacts::Parse_Save(&reader, &error));
    DOCTEST_CHECK(pacts::Empty());  // nothing changes before the commit
    pacts::Commit_Save();
    DOCTEST_CHECK(pacts::Has_Pact(2, 1));
    DOCTEST_CHECK(pacts::Guarantees(0, 3));

    buffer[2 + 1] = 0;  // player 1's pact mask loses 2; player 2's still names 1
    SERIAL::Reader_ bad(buffer.get(), pacts::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(pacts::Parse_Save(&bad, &error));
    buffer[2 + 1] = 0x01;  // player 1 bound to... player 0, who does not name it back
    SERIAL::Reader_ bad2(buffer.get(), pacts::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(pacts::Parse_Save(&bad2, &error));
}

}
