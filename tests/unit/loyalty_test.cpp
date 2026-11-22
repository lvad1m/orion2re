#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/loyalty.h"
#include "game/serialization.h"

#include <memory>

namespace {
    struct s_loyalty_scope {
        test::s_saved_value<bool> enabled{loyalty::enabled};
        test::s_saved_value<int32_t> period{loyalty::period};

        s_loyalty_scope() {
            loyalty::Clear();
            loyalty::enabled = true;
            loyalty::period = 5;
        }
        ~s_loyalty_scope() {
            loyalty::Clear();
        }
    };

    constexpr int8_t SHIP_LEADER = 0;
    constexpr int8_t COLONY_LEADER = 1;

    // Three computer empires; leader 3 is in player 1's reserve. Stardate 35001 (not a step).
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 3;
        MOX::stardate = 35001;
        for (int i = 0; i < 3; ++i) state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
        s_leader_data& leader = state->leaders[3];
        leader.player_index = 1;
        leader.status = 0;
        leader.type = SHIP_LEADER;
        leader.xp = 0;
    }
} // namespace

DOCTEST_TEST_SUITE("leader loyalty") {

DOCTEST_TEST_CASE("a leader entering service starts at 50, and one leaving it is forgotten") {
    s_loyalty_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK(loyalty::Of(3) == loyalty::UNTRACKED);
    loyalty::Turn();
    DOCTEST_CHECK(loyalty::Of(3) == 50);
    DOCTEST_CHECK(loyalty::Of(4) == loyalty::UNTRACKED);  // no one's leader
    state.leaders[3].player_index = 2;  // now another empire's: starts afresh
    loyalty::Record(3)->loyalty = 70;
    loyalty::Turn();
    DOCTEST_CHECK(loyalty::Of(3) == 50);
    state.leaders[3].status = -1;  // back in the galaxy
    loyalty::Turn();
    DOCTEST_CHECK(loyalty::Of(3) == loyalty::UNTRACKED);
}

DOCTEST_TEST_CASE("service and promotion raise loyalty; long idleness and a war-weary or sullen empire lower it") {
    s_loyalty_scope scope;
    test::s_game_state state;
    Setup_(&state);
    s_leader_data& leader = state.leaders[3];
    loyalty::Turn();  // 50
    leader.xp = 150;  // two levels up
    loyalty::Turn();
    DOCTEST_CHECK(loyalty::Of(3) == 70);
    leader.status = 1;  // at a post
    MOX::stardate = 35005;
    loyalty::Turn();
    DOCTEST_CHECK(loyalty::Of(3) == 71);

    // Back in reserve: after 20 idle turns each step costs one.
    leader.status = 0;
    for (MOX::stardate = 35006; MOX::stardate <= 35025; ++MOX::stardate) loyalty::Turn();
    DOCTEST_CHECK(loyalty::Of(3) == 71);  // idle 20, not yet more than 20
    for (; MOX::stardate <= 35030; ++MOX::stardate) loyalty::Turn();
    DOCTEST_CHECK(loyalty::Of(3) == 70);

    // A contented empire (average colony morale 4 or more) raises it at the step; a sullen one lowers it.
    leader.status = 1;
    MOX::NUM_COLONIES = 1;
    state.colonies[0].owner = 1;
    state.colonies[0].morale = 4;
    MOX::stardate = 35035;
    loyalty::Turn();
    DOCTEST_CHECK(loyalty::Of(3) == 72);  // service and mood
    state.colonies[0].morale = -4;
    MOX::stardate = 35040;
    loyalty::Turn();
    DOCTEST_CHECK(loyalty::Of(3) == 72);  // service +1, mood -1
}

DOCTEST_TEST_CASE("captains at the battle's star win or lose with it") {
    s_loyalty_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::NUM_SHIPS = 3;
    state.leaders[3].status = 1;
    state.leaders[3].location = 0;  // ship 0
    state.ships[0].location = 5;
    s_leader_data& enemy = state.leaders[4];
    enemy.player_index = 2;
    enemy.status = 1;
    enemy.type = SHIP_LEADER;
    enemy.location = 1;
    state.ships[1].location = 5;
    s_leader_data& elsewhere = state.leaders[5];
    elsewhere.player_index = 1;
    elsewhere.status = 1;
    elsewhere.type = SHIP_LEADER;
    elsewhere.location = 2;
    state.ships[2].location = 9;
    loyalty::Turn();
    loyalty::On_Battle(5, 1, 2);
    DOCTEST_CHECK(loyalty::Of(3) == 55);
    DOCTEST_CHECK(loyalty::Of(4) == 45);
    DOCTEST_CHECK(loyalty::Of(5) == 50);
}

DOCTEST_TEST_CASE("an idle, more experienced leader of the same kind is passed over") {
    s_loyalty_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.leaders[3].xp = 150;  // level 2, idle in reserve
    s_leader_data& fresh = state.leaders[4];
    fresh.player_index = 1;
    fresh.status = 0;
    fresh.type = SHIP_LEADER;
    s_leader_data& governor = state.leaders[5];
    governor.player_index = 1;
    governor.status = 0;
    governor.type = COLONY_LEADER;
    governor.xp = 1000;
    loyalty::Turn();
    loyalty::On_Assigned(4);
    DOCTEST_CHECK(loyalty::Of(3) == 45);
    DOCTEST_CHECK(loyalty::Of(5) == 50);  // another kind of post
    DOCTEST_CHECK(loyalty::Of(4) == 50);
}

DOCTEST_TEST_CASE("off: nothing is tracked") {
    s_loyalty_scope scope;
    test::s_game_state state;
    Setup_(&state);
    loyalty::enabled = false;
    loyalty::Turn();
    loyalty::On_Assigned(3);
    DOCTEST_CHECK(loyalty::Of(3) == loyalty::UNTRACKED);
    DOCTEST_CHECK(loyalty::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_loyalty_scope scope;
    test::s_game_state state;
    Setup_(&state);
    loyalty::Turn();
    loyalty::Record(3)->loyalty = 63;
    auto buffer = std::make_unique<uint8_t[]>(loyalty::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), loyalty::Save_Size(), "test overflow");
    loyalty::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == loyalty::Save_Size());
    loyalty::Clear();
    SERIAL::Reader_ reader(buffer.get(), loyalty::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(loyalty::Parse_Save(&reader, &error));
    DOCTEST_CHECK(loyalty::Empty());
    loyalty::Commit_Save();
    DOCTEST_CHECK(loyalty::Of(3) == 63);
    DOCTEST_CHECK(loyalty::Record(3)->owner == 1);
    buffer[2] = 101;  // leader 0's loyalty
    SERIAL::Reader_ bad(buffer.get(), loyalty::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(loyalty::Parse_Save(&bad, &error));
}

}
