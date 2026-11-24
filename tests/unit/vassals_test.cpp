#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/opinion.h"
#include "game/serialization.h"
#include "game/vassals.h"

#include <memory>
#include <string>

namespace {
    struct s_vassals_scope {
        test::s_saved_value<bool> enabled{vassals::enabled};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};

        s_vassals_scope() {
            vassals::Clear();
            opinion::Clear();
            vassals::enabled = true;
            opinion::enabled = true;
        }
        ~s_vassals_scope() {
            vassals::Clear();
            opinion::Clear();
        }
    };

    constexpr int8_t TREATY_WAR = 4;

    // Three computer empires: player 0 strong, player 1 weak and at war with player 2.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 3;
        MOX::stardate = 35105;
        for (int i = 0; i < 3; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            for (int j = 0; j < 3; ++j) {
                state->players[i].contact[j] = i == j ? 0 : 1;
                state->players[i].within_range[j] = i == j ? 0 : 1;
            }
        }
        state->players[0].total_ship_strength[1] = 3000;
        state->players[0].total_ship_strength[2] = 3000;
        state->players[1].total_ship_strength[0] = 500;
        state->players[1].total_ship_strength[2] = 500;
        state->players[2].total_ship_strength[1] = 1000;
        state->players[2].total_ship_strength[0] = 1100;
        state->players[1].treaty[2] = state->players[2].treaty[1] = TREATY_WAR;
    }
} // namespace

DOCTEST_TEST_SUITE("vassals") {

DOCTEST_TEST_CASE("a weak empire at war becomes the vassal of the strongest friendly empire and pays tribute") {
    s_vassals_scope scope;
    test::s_game_state state;
    Setup_(&state);
    vassals::Turn();
    DOCTEST_CHECK(vassals::Overlord(1) == 0);
    DOCTEST_CHECK(state.players[1].tribute_treaty[0] == vassals::VASSAL_TRIBUTE);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_OVERLORD) == vassals::PROTECTED_OPINION);
    DOCTEST_CHECK(vassals::Overlord(2) == -1);  // not weak enough against player 0 (1100 vs 3000 > 34 %)
    MOX::stardate = 35106;
    vassals::Turn();  // only every 10 turns
    DOCTEST_CHECK_FALSE(vassals::Bind(2, 1));  // an overlord's vassal cannot be an overlord here, nor a vassal twice
}

DOCTEST_TEST_CASE("the overlord defends its vassal, or fails it and the vassal goes free") {
    s_vassals_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].treaty[2] = state.players[2].treaty[1] = 0;
    DOCTEST_REQUIRE(vassals::Bind(1, 0));
    vassals::On_Aggression(2, 1);
    DOCTEST_CHECK(state.players[0].treaty[2] >= TREATY_WAR);
    DOCTEST_CHECK(vassals::Overlord(1) == 0);
    vassals::Clear();
    opinion::Clear();
    test::s_game_state state2;
    Setup_(&state2);
    state2.players[0].treaty[2] = state2.players[2].treaty[0] = 2;  // the overlord is allied to the aggressor
    DOCTEST_REQUIRE(vassals::Bind(1, 0));
    vassals::On_Aggression(2, 1);
    DOCTEST_CHECK(vassals::Overlord(1) == -1);
    DOCTEST_CHECK(state2.players[1].tribute_treaty[0] == 0);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_OVERLORD) == vassals::PROTECTED_OPINION + vassals::FAILED_OPINION);
}

DOCTEST_TEST_CASE("an overlord attacking its vassal, or a grown vassal, ends the vassalage") {
    s_vassals_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(vassals::Bind(1, 0));
    vassals::On_Aggression(0, 1);
    DOCTEST_CHECK(vassals::Overlord(1) == -1);
    DOCTEST_REQUIRE(vassals::Bind(1, 0));
    state.players[1].total_ship_strength[0] = 2100;  // two thirds of 3000
    MOX::stardate = 35115;
    vassals::Turn();
    DOCTEST_CHECK(vassals::Overlord(1) == -1);
}

DOCTEST_TEST_CASE("off: nothing binds") {
    s_vassals_scope scope;
    test::s_game_state state;
    Setup_(&state);
    vassals::enabled = false;
    DOCTEST_CHECK_FALSE(vassals::Bind(1, 0));
    vassals::Turn();
    DOCTEST_CHECK(vassals::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_vassals_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(vassals::Bind(1, 0));
    auto buffer = std::make_unique<uint8_t[]>(vassals::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), vassals::Save_Size(), "test overflow");
    vassals::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == vassals::Save_Size());
    vassals::Clear();
    SERIAL::Reader_ reader(buffer.get(), vassals::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(vassals::Parse_Save(&reader, &error));
    DOCTEST_CHECK(vassals::Empty());
    vassals::Commit_Save();
    DOCTEST_CHECK(vassals::Overlord(1) == 0);
    buffer[2 + 2 * 5] = 2;  // player 2 its own overlord
    SERIAL::Reader_ bad(buffer.get(), vassals::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(vassals::Parse_Save(&bad, &error));
    DOCTEST_CHECK(vassals::Overlord(1) == 0);
}

}
