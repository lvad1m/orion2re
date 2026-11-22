#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/advisor.h"
#include "game/advisor_intel.h"
#include "game/dice.h"
#include "game/espionage.h"
#include "game/opinion.h"
#include "game/serialization.h"
#include "game/spy_ops.h"
#include "game/stability.h"

#include <memory>

namespace {
    struct s_spy_ops_scope {
        test::s_saved_value<int32_t> operations{spy_ops::operations};
        test::s_saved_value<bool> network{espionage::network};
        test::s_saved_value<bool> ci{espionage::counterintelligence};
        test::s_saved_value<int32_t> caught{espionage::caught_opinion};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<bool> stability_enabled{stability::enabled};
        test::s_saved_value<bool> advisor_enabled{advisor::enabled};
        test::s_saved_value<uint8_t> roll_kind{dice::roll_kind};

        s_spy_ops_scope() {
            spy_ops::Clear();
            espionage::Clear();
            opinion::Clear();
            advisor::Clear_Seen_Ships();
            spy_ops::operations = 63;
            espionage::network = true;
            espionage::counterintelligence = true;
            espionage::caught_opinion = -15;
            opinion::enabled = true;
            stability::enabled = false;
            advisor::enabled = false;
            dice::roll_kind = dice::ROLL_TWO_D10;
        }
        ~s_spy_ops_scope() {
            spy_ops::Clear();
            espionage::Clear();
            opinion::Clear();
            advisor::Clear_Seen_Ships();
        }
    };

    const int32_t TRIUMPH[2] = {10, 10};
    const int32_t CATASTROPHE[2] = {1, 1};

    // Three computer empires; player 1 has 4 spies and 30 network points at player 2, whose colony 0
    // is not its capitol. Stardate 35001 (no computer turn).
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 3;
        MOX::NUM_COLONIES = 1;
        MOX::stardate = 35001;
        for (int i = 0; i < 3; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            for (int j = 0; j < 3; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
        state->players[1].spies[2] = 4;
        espionage::State()->points[1][2] = 30;
        s_colony& c = state->colonies[0];
        c.owner = 2;
        c.n_pops = 5;
        for (int i = 0; i < 5; ++i) c.pop[i] = 2;
    }

    s_leader_data& Leader_(test::s_game_state* state, int index, int owner, int8_t status) {
        s_leader_data& l = state->leaders[index];
        l.player_index = static_cast<int8_t>(owner);
        l.status = status;
        l.location = -1;
        return l;
    }
} // namespace

DOCTEST_TEST_SUITE("spy operations") {

DOCTEST_TEST_CASE("an operation needs the flag, spies and points, and spends the points") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(2, spy_ops::OP_UNREST, 1, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);  // no spies
    spy_ops::operations = 63 & ~1;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);
    spy_ops::operations = 63;
    espionage::State()->points[1][2] = 9;  // unrest costs 10
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);
    DOCTEST_CHECK(espionage::Points(1, 2) == 9);
    espionage::State()->points[1][2] = 30;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(espionage::Points(1, 2) == 20);
    espionage::network = false;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);
}

DOCTEST_TEST_CASE("unrest lowers the colony's stability and fades; an exposed operation is an incident") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    stability::enabled = true;
    const int32_t calm = stability::Compute(0);
    DOCTEST_REQUIRE(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(spy_ops::Unrest(0) == spy_ops::UNREST_PER_OPERATION);
    DOCTEST_CHECK(stability::Compute(0) == calm - spy_ops::UNREST_PER_OPERATION);
    spy_ops::Turn();
    DOCTEST_CHECK(spy_ops::Unrest(0) == spy_ops::UNREST_PER_OPERATION - 1);

    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, CATASTROPHE) == spy_ops::RESULT_EXPOSED);
    DOCTEST_CHECK((state.players[1].spies[2] & 0x3F) == 3);  // the spy is lost
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_STIRRED_UNREST) == -25);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_CAUGHT_SPYING) == -15);
    DOCTEST_CHECK(espionage::State()->caught[2] == 1);
}

DOCTEST_TEST_CASE("a scientist is killed, a leader bribed or framed; a framed leader once exposed is immune") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    s_leader_data& scientist = Leader_(&state, 5, 2, 1);
    scientist.special_skills = LEADER_COLONY_SKILL_SCIENCE_LEADER;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_KILL_SCIENTIST, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(scientist.status == -2);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_KILL_SCIENTIST, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);

    espionage::State()->points[1][2] = 30;
    Leader_(&state, 6, 2, 1).type = 1;  // a governor at a post
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_BRIBERY, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(spy_ops::Bribed_By(6) == 1);

    espionage::State()->points[1][2] = 30;
    s_leader_data& honest = Leader_(&state, 7, 2, 0);
    honest.xp = 1000;  // the highest level: the one framed
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_FRAME_LEADER, 2, -1, CATASTROPHE) == spy_ops::RESULT_EXPOSED);
    DOCTEST_CHECK(*spy_ops::Immune_Slot(7) == 1);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_FRAMED_OUR_LEADER) == -25);
    // Only the bribed governor is left to frame now.
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_FRAME_LEADER, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(state.leaders[6].status == -1);
    DOCTEST_CHECK(honest.status == 0);
}

DOCTEST_TEST_CASE("data collection fills the intel memory; a false flag needs someone to blame") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::NUM_SHIPS = 1;
    s_ship_data& ship = state.ships[0];
    ship.owner = 2;
    ship.status = 0;
    ship.d.ship_weapon[0].type = WEAPON_NUCLEAR_MISSILE;
    ship.d.ship_weapon[0].count = 4;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_DATA, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(advisor::Seen_Ships(1, 2).ships == 1);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_FALSE_FLAG, 2, 1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_FALSE_FLAG, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_FALSE_FLAG, 2, 0, TRIUMPH) == spy_ops::RESULT_DONE);
}

DOCTEST_TEST_CASE("the difficulty grows with the target's watch; paranoia makes framing easier") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    const int32_t calm = spy_ops::Difficulty(1, spy_ops::OP_UNREST, 2);
    const int32_t frame_calm = spy_ops::Difficulty(1, spy_ops::OP_FRAME_LEADER, 2);
    espionage::Set_Mode(2, espionage::MODE_WITCH_HUNT);
    DOCTEST_CHECK(spy_ops::Difficulty(1, spy_ops::OP_UNREST, 2) == calm + 4);
    DOCTEST_CHECK(spy_ops::Difficulty(1, spy_ops::OP_FRAME_LEADER, 2) == frame_calm);
    DOCTEST_CHECK(spy_ops::Difficulty(1, spy_ops::OP_KILL_SCIENTIST, 2) == calm + 4 + 2);
    DOCTEST_CHECK(spy_ops::Bonus(1, 2) == 2);  // 4 spies, no spy skill
}

DOCTEST_TEST_CASE("computer empires at war go for the enemy's scientists") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].treaty[2] = state.players[2].treaty[1] = 4;
    Leader_(&state, 5, 2, 1).special_skills = LEADER_COLONY_SKILL_SCIENCE_LEADER;
    MOX::stardate = 35010;
    spy_ops::Turn();
    DOCTEST_CHECK(espionage::Points(1, 2) == 10);  // 20 spent on the operation, whatever the dice said
}

DOCTEST_TEST_CASE("off: no state, no effect") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    spy_ops::operations = 0;
    spy_ops::Turn();
    DOCTEST_CHECK(spy_ops::Unrest(0) == 0);
    DOCTEST_CHECK(spy_ops::Bribed_By(6) == -1);
    DOCTEST_CHECK(spy_ops::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Leader_(&state, 6, 2, 1).type = 1;
    DOCTEST_REQUIRE(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    espionage::State()->points[1][2] = 30;  // a bribe costs 25
    DOCTEST_REQUIRE(spy_ops::Execute_With_Dice(1, spy_ops::OP_BRIBERY, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    auto buffer = std::make_unique<uint8_t[]>(spy_ops::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), spy_ops::Save_Size(), "test overflow");
    spy_ops::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == spy_ops::Save_Size());
    spy_ops::Clear();
    SERIAL::Reader_ reader(buffer.get(), spy_ops::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(spy_ops::Parse_Save(&reader, &error));
    DOCTEST_CHECK(spy_ops::Empty());
    spy_ops::Commit_Save();
    DOCTEST_CHECK(spy_ops::Unrest(0) == spy_ops::UNREST_PER_OPERATION);
    DOCTEST_CHECK(spy_ops::Bribed_By(6) == 1);
    buffer[2] = 200;  // colony 0's unrest over the maximum
    SERIAL::Reader_ bad(buffer.get(), spy_ops::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(spy_ops::Parse_Save(&bad, &error));
}

}
