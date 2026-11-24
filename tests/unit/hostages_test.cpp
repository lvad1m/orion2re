#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/hostages.h"
#include "game/opinion.h"
#include "game/serialization.h"

#include <memory>
#include <string>

namespace {
    struct s_hostages_scope {
        test::s_saved_value<bool> swaps{hostages::hostage_swaps};
        test::s_saved_value<bool> prisoners{hostages::prisoners};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};

        s_hostages_scope() {
            hostages::Clear();
            opinion::Clear();
            hostages::hostage_swaps = true;
            hostages::prisoners = true;
            opinion::enabled = true;
        }
        ~s_hostages_scope() {
            hostages::Clear();
            opinion::Clear();
        }
    };

    // Three computer empires. Player 0 is weak, player 1 strong. Leaders 0-2 serve player 0 (levels 3, 1, 2,
    // in the pool), leader 3 is the governor of player 0's capital (level 0), leaders 10-11 serve player 1.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 3;
        MOX::stardate = 35100;
        for (int i = 0; i < 3; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[i].personality = PLAYER_PERSONALITY_ERRATIC;
            state->players[i].bc = 1000;
        }
        state->players[0].total_ship_strength[1] = 10;
        state->players[1].total_ship_strength[0] = 100;
        const int levels[3] = {3, 1, 2};
        for (int i = 0; i < 3; ++i) {
            state->leaders[i].player_index = 0;
            state->leaders[i].status = 0;
            state->leaders[i].type = LEADER_TYPE_SHIP;
            state->leaders[i].level = static_cast<uint8_t>(levels[i]);
            state->leaders[i].location = -1;
        }
        for (int i = 10; i < 12; ++i) {
            state->leaders[i].player_index = 1;
            state->leaders[i].status = 0;
            state->leaders[i].type = LEADER_TYPE_SHIP;
            state->leaders[i].level = 1;
            state->leaders[i].location = -1;
        }
        for (int s = 0; s < 3; ++s) {
            for (int slot = 0; slot < 5; ++slot) state->stars[s].planet_index[slot] = -1;
            for (int p = 0; p < 8; ++p) state->stars[s].officer_index[p] = -1;
            state->planets[s].star_index = static_cast<int16_t>(s);
            state->planets[s].colony_index = -1;
        }
        // The capital of player 0: planet 0 at star 0, governed by leader 3.
        state->players[0].home_planet_id = 0;
        state->leaders[3].player_index = 0;
        state->leaders[3].status = 1;
        state->leaders[3].type = LEADER_TYPE_COLONY;
        state->leaders[3].level = 0;
        state->leaders[3].location = 0;
        state->leaders[3].eta = 0;
        state->stars[0].officer_index[0] = 3;
    }

    // Seeds the dice so that the next three escape draws all miss (an escape is 3 per cent a prisoner a turn).
    void Calm_() {
        for (uint32_t seed = 1; seed < 100000; ++seed) {
            game_random::Set_Random_Seed(seed);
            bool calm = true;
            for (int i = 0; i < 3; ++i) calm = calm && game_random::Random(100) >= static_cast<uint32_t>(hostages::ESCAPE_PERCENT);
            if (calm) {
                game_random::Set_Random_Seed(seed);
                return;
            }
        }
    }

    void Ally_(test::s_game_state* state) {
        state->players[0].treaty[1] = state->players[1].treaty[0] = 2;
        hostages::On_Alliance(0, 1);
    }
} // namespace

DOCTEST_TEST_SUITE("hostages") {

DOCTEST_TEST_CASE("the weaker ally gives its lowest-level leader (not the capital's governor), returned after the term")
{
    s_hostages_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Ally_(&state);
    DOCTEST_REQUIRE(hostages::State()->count == 1);
    DOCTEST_CHECK(hostages::State()->held[0].leader == 1);  // level 1; leader 3 (level 0) is the governor
    DOCTEST_CHECK(hostages::State()->held[0].owner == 0);
    DOCTEST_CHECK(hostages::State()->held[0].holder == 1);
    DOCTEST_CHECK(state.leaders[1].status == -2);
    DOCTEST_CHECK(state.leaders[3].status == 1);
    DOCTEST_CHECK(hostages::Count_Held(0, 1) == 1);
    DOCTEST_CHECK(hostages::Count_Held(1, 0) == 0);
    hostages::On_Alliance(0, 1);  // one is enough
    DOCTEST_CHECK(hostages::State()->count == 1);

    MOX::stardate += hostages::HOSTAGE_TURNS - 1;
    hostages::Turn();
    DOCTEST_CHECK(hostages::State()->count == 1);
    MOX::stardate += 1;
    hostages::Turn();
    DOCTEST_CHECK(hostages::Empty());
    DOCTEST_CHECK(state.leaders[1].status == 0);
    DOCTEST_CHECK(state.leaders[1].player_index == 0);
    DOCTEST_CHECK(state.leaders[1].location == -1);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_FAMILY_TIES) == hostages::RETURNED_OPINION);
}

DOCTEST_TEST_CASE("a treaty broken while the hostage is held: executed, sent home or kept as a ward by personality")
{
    s_hostages_scope scope;
    test::s_game_state state;
    Setup_(&state);

    state.players[1].personality = PLAYER_PERSONALITY_RUTHLESS;
    Ally_(&state);
    hostages::On_Treaty_Broken(0, 1);
    DOCTEST_CHECK(hostages::Empty());
    DOCTEST_CHECK(state.leaders[1].status == -2);  // stays dead
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_HARMED_A_HOSTAGE) == hostages::EXECUTED_HOSTAGE_OPINION);

    state.leaders[1].status = 0;  // revive for the next case
    state.players[1].personality = PLAYER_PERSONALITY_HONORABLE;
    Ally_(&state);
    DOCTEST_REQUIRE(hostages::State()->count == 1);
    hostages::On_Treaty_Broken(0, 1);
    DOCTEST_CHECK(hostages::Empty());
    DOCTEST_CHECK(state.leaders[1].status == 0);
    DOCTEST_CHECK(state.leaders[1].player_index == 0);

    state.players[1].personality = PLAYER_PERSONALITY_ERRATIC;
    Ally_(&state);
    DOCTEST_REQUIRE(hostages::State()->count == 1);
    hostages::On_Treaty_Broken(2, 1);  // somebody else's break: nothing
    DOCTEST_CHECK(hostages::State()->count == 1);
    hostages::On_Treaty_Broken(0, 1);
    DOCTEST_CHECK(hostages::Empty());
    DOCTEST_CHECK(state.leaders[1].status == 0);
    DOCTEST_CHECK(state.leaders[1].player_index == 1);  // a ward: it joined the holder
}

DOCTEST_TEST_CASE("the governor of an invaded colony is captured; a hand-over or a second colony at the star is not")
{
    s_hostages_scope scope;
    test::s_game_state state;
    Setup_(&state);
    // Player 2's colony 0 on planet 1 at star 1, governed by leader 20; player 2 has one colony there.
    state.stars[1].planet_index[0] = 1;
    state.planets[1].colony_index = 0;
    state.colonies[0].planet = 1;
    state.colonies[0].owner = 2;
    state.leaders[20].player_index = 2;
    state.leaders[20].status = 1;
    state.leaders[20].type = LEADER_TYPE_COLONY;
    state.leaders[20].level = 4;
    state.leaders[20].location = 1;
    state.leaders[20].eta = 0;
    state.stars[1].officer_index[2] = 20;

    // A second colony of the owner at the star keeps the governor in place.
    state.stars[1].planet_index[1] = 2;
    state.planets[2].star_index = 1;
    state.planets[2].colony_index = 1;
    state.colonies[1].planet = 2;
    state.colonies[1].owner = 2;
    DOCTEST_CHECK_FALSE(hostages::On_Colony_Invaded(&state.colonies[0], 1));
    DOCTEST_CHECK(state.leaders[20].status == 1);
    state.planets[2].colony_index = -1;

    DOCTEST_CHECK_FALSE(hostages::On_Colony_Invaded(&state.colonies[0], 2));  // its own empire
    DOCTEST_REQUIRE(hostages::On_Colony_Invaded(&state.colonies[0], 1));
    DOCTEST_CHECK(state.leaders[20].status == -2);
    DOCTEST_CHECK(state.stars[1].officer_index[2] == -1);
    DOCTEST_CHECK(hostages::Count_Held(2, 1) == 1);
    DOCTEST_CHECK(hostages::State()->held[0].kind == hostages::KIND_PRISONER);
    DOCTEST_CHECK_FALSE(hostages::On_Colony_Invaded(&state.colonies[0], 1));  // no governor left
}

DOCTEST_TEST_CASE("ransom: paid at the phase when the owner keeps a reserve; a poor or human owner does not pay")
{
    s_hostages_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(hostages::Hold(0, 1, hostages::KIND_PRISONER));  // leader 0, level 3 -> 125 BC
    DOCTEST_CHECK(hostages::Ransom_Of(0) == 125);
    state.players[0].bc = 224;
    MOX::stardate = 35107;
    Calm_();
    hostages::Turn();  // not the phase
    DOCTEST_CHECK(hostages::State()->count == 1);
    MOX::stardate = 35108;
    state.players[0].bc = 224;  // leaves 99
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(hostages::State()->count == 1);
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.players[0].bc = 5000;
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(hostages::State()->count == 1);  // a human never pays
    state.players[0].objectives = PLAYER_OBJECTIVE_MILITARIST;
    state.players[0].bc = 225;
    state.players[1].bc = 1000;
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(hostages::Empty());
    DOCTEST_CHECK(state.players[0].bc == 100);
    DOCTEST_CHECK(state.players[1].bc == 1125);
    DOCTEST_CHECK(state.leaders[0].status == 0);
    DOCTEST_CHECK(state.leaders[0].player_index == 0);
}

DOCTEST_TEST_CASE("two computers that hold each other's leaders exchange them one for one, no BC changes hands")
{
    s_hostages_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(hostages::Hold(0, 1, hostages::KIND_PRISONER));
    DOCTEST_REQUIRE(hostages::Hold(10, 0, hostages::KIND_PRISONER));
    DOCTEST_REQUIRE(hostages::Hold(11, 0, hostages::KIND_PRISONER));
    state.players[0].bc = 50;  // too poor to ransom anything
    state.players[1].bc = 50;
    MOX::stardate = 35108;
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(state.leaders[0].status == 0);
    DOCTEST_CHECK(state.leaders[0].player_index == 0);
    DOCTEST_CHECK(state.leaders[10].status == 0);
    DOCTEST_CHECK(state.leaders[10].player_index == 1);
    DOCTEST_CHECK(state.leaders[11].status == -2);  // the odd one stays held
    DOCTEST_CHECK(hostages::State()->count == 1);
    DOCTEST_CHECK(state.players[0].bc == 50);
    DOCTEST_CHECK(state.players[1].bc == 50);
}

DOCTEST_TEST_CASE("a prisoner escapes on a low roll; after the limit a ruthless holder executes, others release")
{
    s_hostages_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].bc = 0;  // no ransom
    DOCTEST_REQUIRE(hostages::Hold(0, 1, hostages::KIND_PRISONER));
    DOCTEST_CHECK_FALSE(hostages::Escape_With_Roll(0, hostages::ESCAPE_PERCENT));
    DOCTEST_CHECK(hostages::Escape_With_Roll(0, hostages::ESCAPE_PERCENT - 1));
    DOCTEST_CHECK(hostages::Empty());
    DOCTEST_CHECK(state.leaders[0].status == 0);
    DOCTEST_CHECK_FALSE(hostages::Escape_With_Roll(0, 0));  // nothing left

    DOCTEST_REQUIRE(hostages::Hold(0, 1, hostages::KIND_PRISONER));
    DOCTEST_REQUIRE(hostages::Hold(1, 2, hostages::KIND_PRISONER));
    state.players[1].personality = PLAYER_PERSONALITY_RUTHLESS;
    state.players[2].personality = PLAYER_PERSONALITY_PACIFIST;
    MOX::stardate = 35100 + hostages::PRISONER_TURNS - 1;
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(hostages::State()->count == 2);  // not yet
    Calm_();
    MOX::stardate += 1;
    hostages::Turn();
    DOCTEST_CHECK(hostages::Empty());
    DOCTEST_CHECK(state.leaders[0].status == -2);  // the ruthless holder executed it
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_HARMED_A_HOSTAGE) == hostages::EXECUTED_PRISONER_OPINION);
    DOCTEST_CHECK(state.leaders[1].status == 0);  // the pacifist released it
    DOCTEST_CHECK(state.leaders[1].player_index == 0);
}

DOCTEST_TEST_CASE("off: nothing is held, returned or drawn")
{
    s_hostages_scope scope;
    test::s_game_state state;
    Setup_(&state);
    hostages::hostage_swaps = false;
    hostages::prisoners = false;
    Ally_(&state);
    DOCTEST_CHECK_FALSE(hostages::Hold(0, 1, hostages::KIND_PRISONER));
    DOCTEST_CHECK_FALSE(hostages::Hold(0, 1, hostages::KIND_HOSTAGE));
    DOCTEST_CHECK_FALSE(hostages::On_Colony_Invaded(&state.colonies[0], 1));
    hostages::On_Treaty_Broken(0, 1);
    const uint32_t seed = game_random::Get_Random_Seed();
    MOX::stardate = 35108;
    hostages::Turn();
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
    DOCTEST_CHECK(hostages::Empty());
    DOCTEST_CHECK(state.leaders[1].status == 0);
    DOCTEST_CHECK(hostages::Count_Held(0, 1) == 0);
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused")
{
    s_hostages_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(hostages::Hold(0, 1, hostages::KIND_HOSTAGE));
    DOCTEST_REQUIRE(hostages::Hold(10, 0, hostages::KIND_PRISONER));
    const uint32_t size = hostages::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    hostages::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    hostages::Clear();
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(hostages::Parse_Save(&reader, &error));
    DOCTEST_CHECK(hostages::Empty());
    hostages::Commit_Save();
    DOCTEST_CHECK(hostages::State()->count == 2);
    DOCTEST_CHECK(hostages::State()->held[1].leader == 10);
    DOCTEST_CHECK(hostages::State()->held[1].kind == hostages::KIND_PRISONER);

    auto bad_leader = std::make_unique<uint8_t[]>(size);
    std::copy(buffer.get(), buffer.get() + size, bad_leader.get());
    bad_leader[3] = 120;  // the first record's leader
    SERIAL::Reader_ r1(bad_leader.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(hostages::Parse_Save(&r1, &error));

    auto same_empire = std::make_unique<uint8_t[]>(size);
    std::copy(buffer.get(), buffer.get() + size, same_empire.get());
    same_empire[5] = same_empire[4];  // holder = owner
    SERIAL::Reader_ r2(same_empire.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(hostages::Parse_Save(&r2, &error));

    auto twice = std::make_unique<uint8_t[]>(size);
    std::copy(buffer.get(), buffer.get() + size, twice.get());
    twice[3 + 8] = twice[3];  // the second record holds the first one's leader
    SERIAL::Reader_ r3(twice.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(hostages::Parse_Save(&r3, &error));

    SERIAL::Reader_ r4(buffer.get(), size - 1, "test underflow", true);
    DOCTEST_CHECK_FALSE(hostages::Parse_Save(&r4, &error));
    DOCTEST_CHECK(hostages::State()->count == 2);  // the failed parses left the state alone
}

}
