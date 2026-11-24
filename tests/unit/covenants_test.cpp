#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/covenants.h"
#include "game/opinion.h"
#include "game/serialization.h"

#include <memory>
#include <string>

namespace {
    struct s_covenants_scope {
        test::s_saved_value<bool> clauses{covenants::secret_clauses};
        test::s_saved_value<bool> marriages{covenants::marriages};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};

        s_covenants_scope() {
            covenants::Clear();
            opinion::Clear();
            covenants::secret_clauses = true;
            covenants::marriages = true;
            opinion::enabled = true;
        }
        ~s_covenants_scope() {
            covenants::Clear();
            opinion::Clear();
        }
    };

    constexpr int8_t TREATY_WAR = 4;

    // Three computer empires in contact: players 0 and 1 are friends (and at peace), both dislike player 2.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 3;
        MOX::stardate = 35103;
        for (int i = 0; i < 3; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            for (int j = 0; j < 3; ++j) {
                state->players[i].contact[j] = i == j ? 0 : 1;
                state->players[i].within_range[j] = i == j ? 0 : 1;
            }
        }
        state->players[0].treaty[1] = state->players[1].treaty[0] = 1;
        opinion::Add(0, 1, opinion::REASON_DID_US_A_SERVICE, 35);
        opinion::Add(1, 0, opinion::REASON_DID_US_A_SERVICE, 35);
        opinion::Add(0, 2, opinion::REASON_SNEAK_ATTACK, -25);
        opinion::Add(1, 2, opinion::REASON_SNEAK_ATTACK, -25);
    }
} // namespace

DOCTEST_TEST_SUITE("covenants") {

DOCTEST_TEST_CASE("friends who dislike a third sign a pair of clauses on the review turn, once") {
    s_covenants_scope scope;
    test::s_game_state state;
    Setup_(&state);
    covenants::marriages = false;
    DOCTEST_CHECK(covenants::Can_Sign(0, 1, 2));
    MOX::stardate = 35104;
    covenants::Turn();
    DOCTEST_CHECK(covenants::Empty());  // not the review phase
    MOX::stardate = 35103;
    covenants::Turn();
    DOCTEST_CHECK(covenants::State()->count == 2);
    DOCTEST_CHECK(covenants::Find_Clause(0, 1, 2) >= 0);
    DOCTEST_CHECK(covenants::Find_Clause(1, 0, 2) >= 0);
    DOCTEST_CHECK(covenants::Clause_Against(0, 2));
    DOCTEST_CHECK_FALSE(covenants::Clause_Against(2, 2));
    MOX::stardate = 35113;
    covenants::Turn();
    DOCTEST_CHECK(covenants::State()->count == 2);  // no duplicates
    // Conditions: a lukewarm friend, a war, an alliance with the third.
    covenants::Clear();
    opinion::Set(0, 1, opinion::REASON_DID_US_A_SERVICE, 10);
    DOCTEST_CHECK_FALSE(covenants::Can_Sign(0, 1, 2));
    opinion::Set(0, 1, opinion::REASON_DID_US_A_SERVICE, 35);
    state.players[0].treaty[2] = 2;
    DOCTEST_CHECK_FALSE(covenants::Can_Sign(0, 1, 2));
    state.players[0].treaty[2] = 0;
    state.players[0].treaty[1] = TREATY_WAR;
    DOCTEST_CHECK_FALSE(covenants::Can_Sign(0, 1, 2));
}

DOCTEST_TEST_CASE("a clause called on: the guarantor goes to war, the protected remembers, the clause is revealed")
{
    s_covenants_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(covenants::Sign(0, 1, 2));
    covenants::On_Aggression(2, 1);
    DOCTEST_CHECK(state.players[0].treaty[2] >= TREATY_WAR);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_DEFENDED_US) == covenants::DEFENDED_OPINION);
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_PLOTTED_AGAINST_US) == covenants::REVEALED_OPINION);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_PLOTTED_AGAINST_US) == covenants::REVEALED_OPINION);
    DOCTEST_CHECK(covenants::State()->clauses[0].revealed == 1);
    DOCTEST_CHECK(covenants::State()->clauses[1].revealed == 1);
    DOCTEST_CHECK(covenants::State()->count == 2);  // still in force
}

DOCTEST_TEST_CASE("a guarantor allied with the aggressor fails: betrayal remembered, the clause ends")
{
    s_covenants_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(covenants::Sign(0, 1, 2));
    state.players[0].treaty[2] = state.players[2].treaty[0] = 2;
    covenants::On_Aggression(2, 1);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_BETRAYED_A_SECRET_PACT) == covenants::BETRAYED_OPINION);
    DOCTEST_CHECK(covenants::Find_Clause(0, 1, 2) < 0);
    DOCTEST_CHECK(covenants::Find_Clause(1, 0, 2) >= 0);  // the other guarantor's clause stands
    DOCTEST_CHECK(state.players[0].treaty[2] == 2);
}

DOCTEST_TEST_CASE("the aggressor's spies reveal a clause: by the roll, once")
{
    s_covenants_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(covenants::Sign(0, 1, 2));
    DOCTEST_CHECK_FALSE(covenants::Spy_Reveal_With_Roll(0, 0));  // no spies
    state.players[2].spies[0] = 3;
    DOCTEST_CHECK_FALSE(covenants::Spy_Reveal_With_Roll(0, 60));  // 3 spies = 60 per mille
    DOCTEST_CHECK(covenants::State()->clauses[0].revealed == 0);
    DOCTEST_CHECK(covenants::Spy_Reveal_With_Roll(0, 59));
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_PLOTTED_AGAINST_US) == covenants::REVEALED_OPINION);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_PLOTTED_AGAINST_US) == covenants::REVEALED_OPINION);
    DOCTEST_CHECK_FALSE(covenants::Spy_Reveal_With_Roll(0, 0));  // already known
    DOCTEST_CHECK(covenants::State()->clauses[1].revealed == 1);
    DOCTEST_CHECK(covenants::State()->count == 2);
}

DOCTEST_TEST_CASE("a clause runs out after its term")
{
    s_covenants_scope scope;
    test::s_game_state state;
    Setup_(&state);
    covenants::marriages = false;
    DOCTEST_REQUIRE(covenants::Sign(0, 1, 2));
    MOX::stardate = 35103 + covenants::CLAUSE_TURNS + 10 - covenants::CLAUSE_TURNS % 10;
    covenants::Turn();  // expires, then the friends sign afresh
    DOCTEST_CHECK(covenants::State()->count == 2);
    DOCTEST_CHECK(covenants::State()->clauses[0].since == MOX::stardate);
}

DOCTEST_TEST_CASE("a marriage is made, kept up, and broken by war with the penalties")
{
    s_covenants_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK(covenants::Can_Marry(0, 1));
    DOCTEST_CHECK_FALSE(covenants::Can_Marry(0, 2));  // no treaty, no friendship
    DOCTEST_REQUIRE(covenants::Marry(0, 1));
    DOCTEST_CHECK(covenants::Spouse(0) == 1);
    DOCTEST_CHECK(covenants::Spouse(1) == 0);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_FAMILY_TIES) == covenants::FAMILY_OPINION);
    DOCTEST_CHECK_FALSE(covenants::Marry(1, 2));  // 1 is taken
    DOCTEST_CHECK_FALSE(covenants::Can_Marry(0, 1));
    opinion::Set(0, 1, opinion::REASON_FAMILY_TIES, 5);
    covenants::Turn();  // review phase 3: the ties are set back
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_FAMILY_TIES) == covenants::FAMILY_OPINION);
    DOCTEST_CHECK(covenants::Spouse(0) == 1);
    covenants::On_Aggression(0, 1);  // player 0 attacks its spouse
    DOCTEST_CHECK(covenants::Spouse(0) == -1);
    DOCTEST_CHECK(covenants::Spouse(1) == -1);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_BROKE_ITS_WORD) == covenants::BROKEN_OPINION);
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_BAD_RUMOURS) == covenants::RUMOUR_OPINION);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_FAMILY_TIES) == 0);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_FAMILY_TIES) == 0);
}

DOCTEST_TEST_CASE("a broken treaty ends a marriage; a war that began unseen ends it without penalties")
{
    s_covenants_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(covenants::Marry(0, 1));
    covenants::On_Treaty_Broken(1, 0);
    DOCTEST_CHECK(covenants::Spouse(0) == -1);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_BROKE_ITS_WORD) == covenants::BROKEN_OPINION);
    DOCTEST_REQUIRE(covenants::Marry(0, 1));
    state.players[0].treaty[1] = state.players[1].treaty[0] = TREATY_WAR;
    covenants::Turn();
    DOCTEST_CHECK(covenants::Spouse(0) == -1);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_BROKE_ITS_WORD) == 0);
}

DOCTEST_TEST_CASE("off: nothing is signed, married or answered")
{
    s_covenants_scope scope;
    test::s_game_state state;
    Setup_(&state);
    covenants::secret_clauses = false;
    covenants::marriages = false;
    DOCTEST_CHECK_FALSE(covenants::Can_Sign(0, 1, 2));
    DOCTEST_CHECK_FALSE(covenants::Sign(0, 1, 2));
    DOCTEST_CHECK_FALSE(covenants::Can_Marry(0, 1));
    DOCTEST_CHECK_FALSE(covenants::Marry(0, 1));
    covenants::Turn();
    covenants::On_Aggression(2, 1);
    covenants::On_Treaty_Broken(0, 1);
    DOCTEST_CHECK(covenants::Empty());
    DOCTEST_CHECK(state.players[0].treaty[2] == 0);
    DOCTEST_CHECK(covenants::Spouse(0) == -1);
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused")
{
    s_covenants_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(covenants::Sign(0, 1, 2));
    DOCTEST_REQUIRE(covenants::Marry(0, 1));
    const uint32_t size = covenants::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    covenants::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    covenants::Clear();
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(covenants::Parse_Save(&reader, &error));
    DOCTEST_CHECK(covenants::Empty());
    covenants::Commit_Save();
    DOCTEST_CHECK(covenants::State()->count == 2);
    DOCTEST_CHECK(covenants::Find_Clause(1, 0, 2) >= 0);
    DOCTEST_CHECK(covenants::Spouse(1) == 0);

    auto bad_party = std::make_unique<uint8_t[]>(size);
    std::copy(buffer.get(), buffer.get() + size, bad_party.get());
    bad_party[3] = 9;  // the first clause's guarantor
    SERIAL::Reader_ r1(bad_party.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(covenants::Parse_Save(&r1, &error));

    auto one_sided = std::make_unique<uint8_t[]>(size);
    std::copy(buffer.get(), buffer.get() + size, one_sided.get());
    one_sided[3 + 2 * 8] = 2;  // player 0's spouse becomes 2, who is single
    SERIAL::Reader_ r2(one_sided.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(covenants::Parse_Save(&r2, &error));

    SERIAL::Reader_ r3(buffer.get(), size - 1, "test underflow", true);
    DOCTEST_CHECK_FALSE(covenants::Parse_Save(&r3, &error));
    DOCTEST_CHECK(covenants::State()->count == 2);  // the failed parses left the state alone
}

}
