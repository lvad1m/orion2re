#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/advisor.h"
#include "game/embargo.h"
#include "game/opinion.h"
#include "game/serialization.h"
#include "game/trade.h"

#include <memory>

namespace {
    struct s_embargo_scope {
        test::s_saved_value<bool> enabled{embargo::enabled};
        test::s_saved_value<bool> resolutions{embargo::council_resolutions};
        test::s_saved_value<int32_t> period{embargo::period};
        test::s_saved_value<int32_t> opinion_value{embargo::opinion_value};
        test::s_saved_value<int32_t> penalty{embargo::penalty_percent};
        test::s_saved_value<int32_t> min_turns{embargo::min_turns};
        test::s_saved_value<int32_t> council_turns{embargo::council_turns};
        test::s_saved_value<int32_t> opinion_min{embargo::opinion_min};
        test::s_saved_value<int32_t> join_opinion{embargo::join_opinion};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<bool> advisor_enabled{advisor::enabled};

        s_embargo_scope() {
            embargo::Clear();
            opinion::Clear();
            embargo::enabled = true;
            embargo::council_resolutions = true;
            embargo::period = 10;
            embargo::opinion_value = -30;
            embargo::penalty_percent = 15;
            embargo::min_turns = 20;
            embargo::council_turns = 50;
            embargo::opinion_min = -40;
            embargo::join_opinion = -20;
            opinion::enabled = true;
            advisor::enabled = false;
        }
        ~s_embargo_scope() {
            embargo::Clear();
            opinion::Clear();
        }
    };

    constexpr int8_t TREATY_ALLIANCE = 2;
    constexpr int8_t TREATY_WAR = 4;

    // Five computer empires in contact with each other, 10 council votes each, at a review turn.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 5;
        MOX::stardate = 35010;
        for (int i = 0; i < 5; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[i].total_pop = 100;
            for (int j = 0; j < 5; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
    }

    void Trade_(test::s_game_state* state, int a, int b) {
        state->players[a].trade_treaty[b] = state->players[b].trade_treaty[a] = 1;
    }
} // namespace

DOCTEST_TEST_SUITE("embargo") {

DOCTEST_TEST_CASE("an embargo ends the participants' treaties, blocks new ones, and the target remembers") {
    s_embargo_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Trade_(&state, 1, 3);
    state.players[1].research_treaty[3] = state.players[3].research_treaty[1] = 1;
    Trade_(&state, 2, 3);
    embargo::Declare(1, 3, false);
    DOCTEST_CHECK(embargo::Participates(1, 3));
    DOCTEST_CHECK(state.players[1].trade_treaty[3] == 0);
    DOCTEST_CHECK(state.players[3].trade_treaty[1] == 0);
    DOCTEST_CHECK(state.players[3].research_treaty[1] == 0);
    DOCTEST_CHECK(state.players[1].broken_treaties_modifier[3] == 0);  // no classic penalty for breaking
    DOCTEST_CHECK(state.players[2].trade_treaty[3] == 1);              // not a participant
    DOCTEST_CHECK(opinion::Modifier(3, 1, opinion::REASON_EMBARGO) == -30);
    DOCTEST_CHECK(embargo::Blocks_Treaty(1, 3));
    DOCTEST_CHECK(embargo::Blocks_Treaty(3, 1));
    DOCTEST_CHECK_FALSE(embargo::Blocks_Treaty(2, 3));
    TRADE::Start_Trade_Agreement(&state.players[3], 1);
    DOCTEST_CHECK(state.players[3].trade_treaty[1] == 0);
    embargo::Declare(2, 3, false);  // one embargo per target: 2 joins rather than declares
    DOCTEST_CHECK(embargo::Record(3)->initiator == 1);
    embargo::Join(2, 3);
    DOCTEST_CHECK(state.players[2].trade_treaty[3] == 0);
}

DOCTEST_TEST_CASE("the target pays while more than half of its former trade partners hold the embargo") {
    s_embargo_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Trade_(&state, 3, 1);
    Trade_(&state, 3, 2);
    Trade_(&state, 3, 4);
    state.players[3].surplus_bc = 100;
    state.players[3].bc = 1000;
    embargo::Declare(1, 3, false);
    DOCTEST_CHECK(embargo::Record(3)->former_partners == ((1u << 1) | (1u << 2) | (1u << 4)));
    MOX::stardate = 35011;
    embargo::End_Of_Turn();
    DOCTEST_CHECK(state.players[3].bc == 1000);  // 1 of 3
    embargo::Join(2, 3);
    embargo::End_Of_Turn();
    DOCTEST_CHECK(state.players[3].bc == 985);   // 2 of 3: 15 % of 100
}

DOCTEST_TEST_CASE("computer empires declare on an empire with a bad name and join when it is cheap") {
    s_embargo_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].relations[3] = -60;
    state.players[1].broken_treaties_modifier[3] = -5;  // 3 broke treaties with 1
    state.players[2].relations[3] = -30;                 // joins: no trade treaty with 3
    state.players[4].relations[3] = -30;                 // does not: trades with 3 and is not spiteful
    Trade_(&state, 4, 3);
    state.players[0].relations[3] = -60;                 // 3 has no bad name for 0, but 0 joins
    embargo::End_Of_Turn();
    DOCTEST_CHECK(embargo::Record(3)->initiator == 1);
    DOCTEST_CHECK(embargo::Participates(2, 3));
    DOCTEST_CHECK(embargo::Participates(0, 3));
    DOCTEST_CHECK_FALSE(embargo::Participates(4, 3));
    DOCTEST_CHECK_FALSE(embargo::Active(1));

    // An ally of the target leaves; the initiator lifts the embargo once it thinks well of 3 again.
    state.players[0].treaty[3] = TREATY_ALLIANCE;
    MOX::stardate = 35011;
    embargo::End_Of_Turn();
    DOCTEST_CHECK_FALSE(embargo::Participates(0, 3));
    state.players[1].relations[3] = 10;
    state.players[1].broken_treaties_modifier[3] = 0;
    MOX::stardate = 35020;  // only 10 turns: too soon
    embargo::End_Of_Turn();
    DOCTEST_CHECK(embargo::Active(3));
    MOX::stardate = 35030;
    embargo::End_Of_Turn();
    DOCTEST_CHECK_FALSE(embargo::Active(3));
}

DOCTEST_TEST_CASE("three wars are a bad name; an empire at war with three is a target") {
    s_embargo_scope scope;
    test::s_game_state state;
    Setup_(&state);
    for (int k : {0, 2, 4}) state.players[3].treaty[k] = state.players[k].treaty[3] = TREATY_WAR;
    state.players[1].relations[3] = -45;
    DOCTEST_CHECK(embargo::Would_Declare(1) == 3);
    state.players[3].treaty[4] = state.players[4].treaty[3] = 0;
    DOCTEST_CHECK(embargo::Would_Declare(1) == -1);
}

DOCTEST_TEST_CASE("the council votes an embargo by council votes; a passed one binds everyone but the target") {
    s_embargo_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].relations[3] = -60;
    state.players[1].broken_treaties_modifier[3] = -5;
    state.players[2].relations[3] = -30;
    embargo::Council_Resolution();  // 1 and 2: 20 of 50 votes
    DOCTEST_CHECK_FALSE(embargo::Active(3));
    state.players[4].relations[3] = -30;
    embargo::Council_Resolution();  // 30 of 50
    DOCTEST_CHECK(embargo::Active(3));
    DOCTEST_CHECK(embargo::Record(3)->by_council == 1);
    for (int k : {0, 1, 2, 4}) DOCTEST_CHECK(embargo::Participates(static_cast<int16_t>(k), 3));
    MOX::stardate = 35059;
    embargo::End_Of_Turn();
    DOCTEST_CHECK(embargo::Active(3));
    MOX::stardate = 35061;  // past 50 turns, and no review this turn (one would declare anew)
    embargo::End_Of_Turn();
    DOCTEST_CHECK_FALSE(embargo::Active(3));
    embargo::council_resolutions = false;
    embargo::Council_Resolution();
    DOCTEST_CHECK_FALSE(embargo::Active(3));
}

DOCTEST_TEST_CASE("off: no embargoes, nothing blocked") {
    s_embargo_scope scope;
    test::s_game_state state;
    Setup_(&state);
    embargo::Declare(1, 3, false);
    embargo::enabled = false;
    DOCTEST_CHECK_FALSE(embargo::Active(3));
    DOCTEST_CHECK_FALSE(embargo::Blocks_Treaty(1, 3));
    embargo::Clear();
    embargo::Declare(1, 3, false);
    state.players[1].relations[3] = -100;
    state.players[1].broken_treaties_modifier[3] = -5;
    embargo::End_Of_Turn();
    DOCTEST_CHECK(embargo::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_embargo_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Trade_(&state, 3, 2);
    embargo::Declare(1, 3, false);
    embargo::Join(4, 3);
    auto buffer = std::make_unique<uint8_t[]>(embargo::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), embargo::Save_Size(), "test overflow");
    embargo::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == embargo::Save_Size());

    embargo::Clear();
    SERIAL::Reader_ reader(buffer.get(), embargo::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(embargo::Parse_Save(&reader, &error));
    DOCTEST_CHECK(embargo::Empty());  // nothing changes before the commit
    embargo::Commit_Save();
    DOCTEST_CHECK(embargo::Participates(1, 3));
    DOCTEST_CHECK(embargo::Participates(4, 3));
    DOCTEST_CHECK(embargo::Record(3)->former_partners == (1u << 2));
    DOCTEST_CHECK(embargo::Record(3)->start == 35010);

    // Record 3 starts at 2 + 3 * 8; its participants byte names player 3 itself.
    buffer[2 + 3 * 8 + 1] = static_cast<uint8_t>(buffer[2 + 3 * 8 + 1] | (1u << 3));
    SERIAL::Reader_ bad(buffer.get(), embargo::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(embargo::Parse_Save(&bad, &error));
}

}
