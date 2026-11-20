#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/diplomacy_memory.h"

namespace {
    struct s_memory_scope {
        test::s_saved_value<bool> enabled{diplomemory::enabled};
        test::s_saved_value<int32_t> period{diplomemory::period};
        test::s_saved_value<int32_t> trust_cap{diplomemory::trust_cap};
        test::s_saved_value<int32_t> weariness_cap{diplomemory::weariness_cap};

        s_memory_scope() {
            diplomemory::enabled = true;
            diplomemory::period = 5;
            diplomemory::trust_cap = 30;
            diplomemory::weariness_cap = 60;
        }
    };

    constexpr int8_t TREATY_PEACE = 3;
    constexpr int8_t TREATY_WAR = 4;

    // Three empires in contact with each other, no treaties, at a stardate that is a step.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 3;
        MOX::stardate = 35010;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                state->players[i].contact[j] = i == j ? 0 : 1;
            }
            state->players[i].total_pop = 40;
        }
    }
} // namespace

DOCTEST_TEST_SUITE("diplomacy memory") {

DOCTEST_TEST_CASE("off: nothing heals and trust wraps like the original") {
    s_memory_scope scope;
    diplomemory::enabled = false;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].trust_worthiness[2] = -40;
    state.players[1].broken_treaties_modifier[2] = 20;

    diplomemory::End_Of_Turn();
    DOCTEST_CHECK(state.players[1].trust_worthiness[2] == -40);
    DOCTEST_CHECK(state.players[1].broken_treaties_modifier[2] == 20);

    state.players[1].trust_worthiness[2] = -120;
    diplomemory::Lower_Trust(1, 2, 20);
    DOCTEST_CHECK(state.players[1].trust_worthiness[2] == 116);  // int8 wrap, as in the original

    state.players[1].treaty[2] = TREATY_WAR;
    state.players[1].ship_war_losses[2] = 100;
    DOCTEST_CHECK(diplomemory::War_Weariness(1, 2) == 0);
}

DOCTEST_TEST_CASE("trust heals, grows under a treaty and stops at war") {
    s_memory_scope scope;
    test::s_game_state state;
    Setup_(&state);
    s_player& plr = state.players[1];
    plr.trust_worthiness[0] = -40;                       // betrayed, now no treaty: heals
    plr.trust_worthiness[2] = 29;                        // treaty in force: grows to the cap
    plr.treaty[2] = TREATY_PEACE;
    state.players[0].trust_worthiness[2] = -40;          // at war: no healing
    state.players[0].treaty[2] = TREATY_WAR;
    state.players[2].trust_worthiness[0] = 10;           // no treaty, positive: stays

    diplomemory::End_Of_Turn();
    DOCTEST_CHECK(plr.trust_worthiness[0] == -39);
    DOCTEST_CHECK(plr.trust_worthiness[2] == 30);
    DOCTEST_CHECK(state.players[0].trust_worthiness[2] == -40);
    DOCTEST_CHECK(state.players[2].trust_worthiness[0] == 10);

    diplomemory::End_Of_Turn();
    DOCTEST_CHECK(plr.trust_worthiness[2] == 30);        // capped

    MOX::stardate = 35011;                               // not a step
    diplomemory::End_Of_Turn();
    DOCTEST_CHECK(plr.trust_worthiness[0] == -38);       // one more step at 35010 above, none now
}

DOCTEST_TEST_CASE("grievances of broken treaties fade toward zero") {
    s_memory_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].broken_treaties_modifier[2] = 3;
    state.players[2].broken_treaties_modifier[1] = -2;
    for (int step = 0; step < 4; ++step) diplomemory::End_Of_Turn();
    DOCTEST_CHECK(state.players[1].broken_treaties_modifier[2] == 0);
    DOCTEST_CHECK(state.players[2].broken_treaties_modifier[1] == 0);
}

DOCTEST_TEST_CASE("lowered trust stops at the floor") {
    s_memory_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].trust_worthiness[2] = -90;
    diplomemory::Lower_Trust(1, 2, 20);
    DOCTEST_CHECK(state.players[1].trust_worthiness[2] == diplomemory::TRUST_FLOOR);
    diplomemory::Lower_Trust(1, 2, 20);
    DOCTEST_CHECK(state.players[1].trust_worthiness[2] == diplomemory::TRUST_FLOOR);
}

DOCTEST_TEST_CASE("war weariness counts lost ships and population against what is left") {
    s_memory_scope scope;
    test::s_game_state state;
    Setup_(&state);
    s_player& plr = state.players[1];
    plr.treaty[2] = TREATY_WAR;
    MOX::NUM_SHIPS = 1;
    state.ships[0].owner = 1;
    state.ships[0].status = 0;
    state.ships[0].d.size = 3;                           // one battleship: 16 units left
    plr.ship_war_losses[2] = 8;                          // 8 * 100 / 25 = 32 percent, half counts
    DOCTEST_CHECK(diplomemory::War_Weariness(1, 2) == 16);

    plr.colony_war_losses[2] = 10;                       // 10 * 100 / 51 = 19 percent
    DOCTEST_CHECK(diplomemory::War_Weariness(1, 2) == 35);

    diplomemory::weariness_cap = 30;
    DOCTEST_CHECK(diplomemory::War_Weariness(1, 2) == 30);

    plr.treaty[2] = TREATY_PEACE;
    DOCTEST_CHECK(diplomemory::War_Weariness(1, 2) == 0);
    DOCTEST_CHECK(diplomemory::War_Weariness(1, 1) == 0);
}

} // DOCTEST_TEST_SUITE
