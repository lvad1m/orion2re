#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/diplomac.h"
#include "game/diplomacy_memory.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/reputation.h"
#include "game/serialization.h"
#include "game/ultimatums.h"

#include <algorithm>
#include <memory>
#include <string>

namespace {
    constexpr int8_t TREATY_PEACE = 3;
    constexpr int8_t TREATY_ALLIANCE = 2;
    constexpr int8_t TREATY_WAR = 4;

    struct s_ultimatums_scope {
        test::s_saved_value<bool> enabled{ultimatums::enabled};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<bool> titles{reputation::enabled};
        test::s_saved_value<bool> memory_enabled{diplomemory::enabled};
        test::s_saved_value<int32_t> weary_limit{diplomemory::weary_limit};

        s_ultimatums_scope() {
            ultimatums::Clear();
            opinion::Clear();
            reputation::Clear();
            ultimatums::enabled = true;
            opinion::enabled = true;
            reputation::enabled = false;
            diplomemory::enabled = false;
            diplomemory::weary_limit = 0;
        }
        ~s_ultimatums_scope() {
            ultimatums::Clear();
            opinion::Clear();
            reputation::Clear();
        }
    };

    // Four computer empires that know each other, 1000 BC each, at stardate 35005 (not a demand turn). Player 0
    // (aggressive) dislikes 1 (erratic), 2 (ruthless) and has a fleet ten times theirs; player 3 is calm.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 4;
        MOX::stardate = 35005;
        for (int p = 0; p < 4; ++p) {
            state->players[p].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[p].personality = PLAYER_PERSONALITY_ERRATIC;
            state->players[p].bc = 1000;
            for (int q = 0; q < 4; ++q) state->players[p].contact[q] = p == q ? 0 : 1;
        }
        state->players[0].personality = PLAYER_PERSONALITY_AGGRESSIVE;
        state->players[2].personality = PLAYER_PERSONALITY_RUTHLESS;
        state->players[0].relations[1] = -20;
        state->players[0].relations[2] = -20;
        state->players[0].total_ship_strength[1] = 100;
        state->players[1].total_ship_strength[0] = 10;
        state->players[0].total_ship_strength[2] = 100;
        state->players[2].total_ship_strength[0] = 50;
    }

    bool At_War_(const test::s_game_state& state, int a, int b) {
        return state.players[a].treaty[b] >= TREATY_WAR;
    }
} // namespace

DOCTEST_TEST_SUITE("ultimatums") {

DOCTEST_TEST_CASE("flag off: nothing is opened, nothing is kept, no draw from game_random, no lawful cause")
{
    s_ultimatums_scope scope;
    test::s_game_state state;
    Setup_(&state);
    ultimatums::enabled = false;
    DOCTEST_CHECK_FALSE(ultimatums::Issue(0, 1, 100));
    ultimatums::On_Demand(0, 3);
    ultimatums::On_Demand_Met(0, 3);
    MOX::stardate = 35004;  // a demand turn
    const uint32_t seed = game_random::Get_Random_Seed();
    for (int i = 0; i < 30; ++i) ultimatums::Turn();
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
    DOCTEST_CHECK(ultimatums::Empty());
    DOCTEST_CHECK_FALSE(ultimatums::Has_Cause(0, 1));
    // A record that exists (a save loaded with the flag off) gives no cause either.
    ultimatums::enabled = true;
    DOCTEST_REQUIRE(ultimatums::Issue(0, 1, 100));
    ultimatums::State()->record[0].state = ultimatums::STATE_CAUSE;
    DOCTEST_CHECK(ultimatums::Has_Cause(0, 1));
    ultimatums::enabled = false;
    DOCTEST_CHECK_FALSE(ultimatums::Has_Cause(0, 1));
}

DOCTEST_TEST_CASE("a strong computer demands tribute of the richest weaker neighbour it dislikes; a calm victim pays in time")
{
    s_ultimatums_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[2].bc = 500;
    MOX::stardate = 35004;
    ultimatums::Turn();
    DOCTEST_REQUIRE(ultimatums::State()->count == 1);
    const ultimatums::s_record rec = ultimatums::State()->record[0];
    DOCTEST_CHECK(rec.demander == 0);
    DOCTEST_CHECK(rec.victim == 1);  // the richest of the two it dislikes
    DOCTEST_CHECK(rec.amount == 1000 / 100 * ultimatums::DEMAND_PERCENT);
    DOCTEST_CHECK(rec.state == ultimatums::STATE_OPEN);
    DOCTEST_CHECK(rec.deadline == 35004 + ultimatums::DEADLINE_TURNS);
    DOCTEST_CHECK_FALSE(ultimatums::Has_Cause(0, 1));
    // An erratic victim pays the next turn.
    MOX::stardate = 35005;
    ultimatums::Turn();
    DOCTEST_CHECK(ultimatums::Empty());
    DOCTEST_CHECK(state.players[1].bc == 1000 - rec.amount);
    DOCTEST_CHECK(state.players[0].bc == 1000 + rec.amount);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_BLACKMAILED_US) == ultimatums::PAID_OPINION);
    DOCTEST_CHECK_FALSE(At_War_(state, 0, 1));
}

DOCTEST_TEST_CASE("who demands and of whom: honorable and pacifist do not, allies and the stronger are spared")
{
    s_ultimatums_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::stardate = 35004;
    state.players[0].personality = PLAYER_PERSONALITY_HONORABLE;
    ultimatums::Turn();
    DOCTEST_CHECK(ultimatums::Empty());
    state.players[0].personality = PLAYER_PERSONALITY_PACIFIST;
    ultimatums::Turn();
    DOCTEST_CHECK(ultimatums::Empty());
    state.players[0].personality = PLAYER_PERSONALITY_AGGRESSIVE;
    // Allied with both, or no stronger: nobody to demand of.
    state.players[0].treaty[1] = state.players[1].treaty[0] = TREATY_ALLIANCE;
    state.players[0].treaty[2] = state.players[2].treaty[0] = TREATY_ALLIANCE;
    ultimatums::Turn();
    DOCTEST_CHECK(ultimatums::Empty());
    state.players[0].treaty[1] = state.players[1].treaty[0] = 0;
    state.players[0].treaty[2] = state.players[2].treaty[0] = 0;
    state.players[1].total_ship_strength[0] = 60;  // not twice as weak
    state.players[2].total_ship_strength[0] = 60;
    ultimatums::Turn();
    DOCTEST_CHECK(ultimatums::Empty());
    state.players[1].total_ship_strength[0] = 10;
    state.players[0].relations[1] = 10;  // it does not dislike player 1
    ultimatums::Turn();
    DOCTEST_CHECK(ultimatums::Empty());
    // A human is never demanded of by this step, and never demands.
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.players[0].relations[1] = -20;
    state.players[2].total_ship_strength[0] = 60;
    ultimatums::Turn();
    DOCTEST_CHECK(ultimatums::Empty());
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_CHECK_FALSE(ultimatums::Issue(0, 3, 100));
}

DOCTEST_TEST_CASE("a proud victim refuses; the deadline passes into a lawful cause, used or lapsing")
{
    s_ultimatums_scope scope;
    test::s_game_state state;
    Setup_(&state);
    // Ruthless player 2 has a fleet half of player 0's: it refuses.
    DOCTEST_REQUIRE(ultimatums::Issue(0, 2, 150));
    DOCTEST_CHECK_FALSE(ultimatums::Issue(0, 2, 150));  // one record a pair
    const int32_t deadline = ultimatums::State()->record[0].deadline;
    for (MOX::stardate = 35006; MOX::stardate < deadline; ++MOX::stardate) {
        ultimatums::Turn();
        DOCTEST_CHECK(ultimatums::State()->count == 1);
        DOCTEST_CHECK_FALSE(ultimatums::Has_Cause(0, 2));
    }
    DOCTEST_CHECK(state.players[2].bc == 1000);
    // Allied: the demander keeps its cause but does not strike.
    state.players[0].treaty[2] = state.players[2].treaty[0] = TREATY_ALLIANCE;
    MOX::stardate = deadline;
    ultimatums::Turn();
    DOCTEST_REQUIRE(ultimatums::State()->count == 1);
    DOCTEST_CHECK(ultimatums::State()->record[0].state == ultimatums::STATE_CAUSE);
    DOCTEST_CHECK(ultimatums::Has_Cause(0, 2));
    DOCTEST_CHECK_FALSE(ultimatums::Has_Cause(2, 0));
    DOCTEST_CHECK_FALSE(At_War_(state, 0, 2));
    // Too late to pay now: a meeting of the demand closes nothing.
    ultimatums::On_Demand_Met(0, 2);
    DOCTEST_CHECK(ultimatums::Has_Cause(0, 2));
    // The cause lapses after CAUSE_TURNS.
    MOX::stardate = deadline + ultimatums::CAUSE_TURNS;
    DOCTEST_CHECK_FALSE(ultimatums::Has_Cause(0, 2));
    ultimatums::Turn();
    DOCTEST_CHECK(ultimatums::Empty());
}

DOCTEST_TEST_CASE("a proud victim bows to a fleet three times its own")
{
    s_ultimatums_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].total_ship_strength[2] = 200;
    DOCTEST_REQUIRE(ultimatums::Issue(0, 2, 150));
    ultimatums::Turn();
    DOCTEST_CHECK(ultimatums::Empty());
    DOCTEST_CHECK(state.players[2].bc == 850);
    DOCTEST_CHECK(state.players[0].bc == 1150);
}

DOCTEST_TEST_CASE("an unmet ultimatum: the computer demander declares its lawful war at the deadline")
{
    s_ultimatums_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].treaty[2] = state.players[2].treaty[0] = TREATY_PEACE;
    DOCTEST_REQUIRE(ultimatums::Issue(0, 2, 150));
    MOX::stardate = ultimatums::State()->record[0].deadline;
    ultimatums::Turn();
    DOCTEST_CHECK(At_War_(state, 0, 2));
    DOCTEST_CHECK(ultimatums::Empty());  // the cause is spent
}

DOCTEST_TEST_CASE("a demand of the original game to a human: met in time it closes, unmet it ends in a lawful war")
{
    s_ultimatums_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[3].objectives = PLAYER_OBJECTIVE_HUMAN;
    ultimatums::On_Demand(0, 3);
    DOCTEST_REQUIRE(ultimatums::State()->count == 1);
    DOCTEST_CHECK(ultimatums::State()->record[0].amount == 0);
    ultimatums::On_Demand_Met(0, 3);
    DOCTEST_CHECK(ultimatums::Empty());
    // A human demander is not driven.
    ultimatums::On_Demand(3, 0);
    DOCTEST_CHECK(ultimatums::Empty());
    ultimatums::On_Demand(0, 3);
    MOX::stardate = ultimatums::State()->record[0].deadline;
    ultimatums::Turn();
    DOCTEST_CHECK(At_War_(state, 0, 3));
    DOCTEST_CHECK(ultimatums::Empty());
}

DOCTEST_TEST_CASE("a lawful war costs the declarer less with the others than one without a cause")
{
    for (int lawful = 0; lawful < 2; ++lawful) {
        s_ultimatums_scope scope;
        test::s_game_state state;
        Setup_(&state);
        test::s_saved_value<int32_t> sneak{opinion::sneak_attack};
        opinion::sneak_attack = -40;
        reputation::enabled = true;
        state.players[0].treaty[2] = state.players[2].treaty[0] = TREATY_PEACE;
        state.players[3].trust_breaker_player[0] = -1;
        if (lawful != 0) {
            DOCTEST_REQUIRE(ultimatums::Issue(0, 2, 150));
            ultimatums::State()->record[0].state = ultimatums::STATE_CAUSE;
            ultimatums::State()->record[0].deadline = MOX::stardate;
        }
        opinion::On_Sneak_Attack(0, 2);
        diplomac::Declare_War(0, 2);
        if (lawful != 0) {
            DOCTEST_CHECK(reputation::State()->breaches[0][0] == 0);        // no stain, no broken treaty on record
            DOCTEST_CHECK(state.players[3].trust_breaker_player[0] == -1);  // the others do not distrust it
            DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_SNEAK_ATTACK) == 0);
            DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_BROKE_ITS_WORD) == 0);
        } else {
            DOCTEST_CHECK(reputation::State()->breaches[0][0] == MOX::stardate);
            DOCTEST_CHECK(state.players[3].trust_breaker_player[0] == 2);
            DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_SNEAK_ATTACK) == -40);
            DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_BROKE_ITS_WORD) != 0);
        }
        DOCTEST_CHECK(At_War_(state, 0, 2));
    }
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused")
{
    s_ultimatums_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(ultimatums::Issue(0, 1, 150));
    DOCTEST_REQUIRE(ultimatums::Issue(0, 3, 0));
    ultimatums::State()->record[1].state = ultimatums::STATE_CAUSE;
    const uint32_t size = ultimatums::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    ultimatums::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    ultimatums::Clear();
    ultimatums::enabled = true;
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(ultimatums::Parse_Save(&reader, &error));
    DOCTEST_CHECK(ultimatums::Empty());
    ultimatums::Commit_Save();
    DOCTEST_REQUIRE(ultimatums::State()->count == 2);
    DOCTEST_CHECK(ultimatums::State()->record[0].victim == 1);
    DOCTEST_CHECK(ultimatums::State()->record[0].amount == 150);
    DOCTEST_CHECK(ultimatums::State()->record[1].state == ultimatums::STATE_CAUSE);

    // Layout: version (2), count (1), then 15 bytes a record: demander 0, victim 1, state 2, issued 3-6,
    // deadline 7-10, amount 11-14.
    const auto refuse = [&](size_t offset, uint8_t value) {
        auto bad = std::make_unique<uint8_t[]>(size);
        std::copy(buffer.get(), buffer.get() + size, bad.get());
        bad[offset] = value;
        SERIAL::Reader_ r(bad.get(), size, "test underflow", true);
        return !ultimatums::Parse_Save(&r, &error);
    };
    DOCTEST_CHECK(refuse(3, 9));          // demander
    DOCTEST_CHECK(refuse(4, 9));          // victim
    DOCTEST_CHECK(refuse(4, 0));          // the same empire twice
    DOCTEST_CHECK(refuse(5, 0));          // a state that is none
    DOCTEST_CHECK(refuse(5, 7));
    DOCTEST_CHECK(refuse(9, 0x80));       // issued negative
    DOCTEST_CHECK(refuse(17, 0x80));      // amount negative
    DOCTEST_CHECK(refuse(19, 1));         // the second record made the pair (0, 1) again
    DOCTEST_CHECK(refuse(0, 9));          // payload version
    DOCTEST_CHECK(ultimatums::State()->count == 2);  // the committed state is untouched by refusals
    // A truncated payload.
    SERIAL::Reader_ shortened(buffer.get(), size - 1, "test underflow", true);
    DOCTEST_CHECK_FALSE(ultimatums::Parse_Save(&shortened, &error));
    // Too many records.
    auto many = std::make_unique<uint8_t[]>(size);
    std::copy(buffer.get(), buffer.get() + size, many.get());
    many[2] = 200;
    SERIAL::Reader_ lots(many.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(ultimatums::Parse_Save(&lots, &error));
}

}
