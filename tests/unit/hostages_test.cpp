#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "../fixtures/texts_fixture.h"
#include "game/choices.h"
#include "game/espionage.h"
#include "game/hostages.h"
#include "game/intrigue.h"
#include "game/opinion.h"
#include "game/spy_ops.h"
#include "game/serialization.h"

#include <memory>
#include <string>

namespace {
    struct s_hostages_scope {
        test::s_saved_value<bool> swaps{hostages::hostage_swaps};
        test::s_saved_value<bool> prisoners{hostages::prisoners};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<bool> spy_prison{hostages::spy_prison};
        test::s_saved_value<bool> spy_fate{intrigue::spy_fate};
        test::s_saved_value<int32_t> operations{spy_ops::operations};

        s_hostages_scope() {
            hostages::Clear();
            opinion::Clear();
            hostages::hostage_swaps = true;
            hostages::prisoners = true;
            hostages::spy_prison = true;
            intrigue::spy_fate = false;
            spy_ops::operations = 0;
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
            for (int i = 0; i < 3; ++i) calm = calm && game_random::Random(100) >= static_cast<uint32_t>(hostages::SPY_ESCAPE_PERCENT);
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
    hostages::spy_prison = false;
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
    DOCTEST_CHECK_FALSE(hostages::Imprison_Spy(1, 0));
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 0);
    DOCTEST_CHECK_FALSE(hostages::Spy_Escape_With_Roll(0, 0));
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


DOCTEST_TEST_CASE("an arrest jails the leader in its own empire (no flag needed); it returns after the term")
{
    s_hostages_scope scope;
    test::s_game_state state;
    Setup_(&state);
    hostages::hostage_swaps = false;
    hostages::prisoners = false;
    hostages::spy_prison = false;
    const uint32_t seed = game_random::Get_Random_Seed();
    hostages::Arrest(2, false);
    DOCTEST_REQUIRE(hostages::State()->count == 1);
    DOCTEST_CHECK(hostages::State()->held[0].leader == 2);
    DOCTEST_CHECK(hostages::State()->held[0].owner == 0);
    DOCTEST_CHECK(hostages::State()->held[0].holder == 0);
    DOCTEST_CHECK(hostages::State()->held[0].kind == hostages::KIND_JAIL);
    DOCTEST_CHECK(state.leaders[2].status == -2);
    DOCTEST_CHECK(state.leaders[2].level == 2);  // not promoted onto the hire list as the dismissal does
    DOCTEST_CHECK_FALSE(hostages::Empty());

    MOX::stardate += hostages::JAIL_TURNS - 1;
    hostages::Turn();
    DOCTEST_CHECK(hostages::State()->count == 1);
    MOX::stardate += 1;
    hostages::Turn();
    DOCTEST_CHECK(hostages::Empty());
    DOCTEST_CHECK(state.leaders[2].status == 0);
    DOCTEST_CHECK(state.leaders[2].player_index == 0);  // back in its own pool
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);  // the jail draws nothing
}

DOCTEST_TEST_CASE("a jailed traitor is executed by a ruthless empire after the term, released by the others")
{
    s_hostages_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].personality = PLAYER_PERSONALITY_RUTHLESS;
    hostages::Arrest(0, true);
    DOCTEST_REQUIRE(hostages::State()->held[0].kind == hostages::KIND_TRAITOR);
    MOX::stardate += hostages::JAIL_TURNS;
    hostages::Turn();
    DOCTEST_CHECK(hostages::Empty());
    DOCTEST_CHECK(state.leaders[0].status == -2);  // stays dead

    state.players[0].personality = PLAYER_PERSONALITY_HONORABLE;
    hostages::Arrest(1, true);
    MOX::stardate += hostages::JAIL_TURNS;
    hostages::Turn();
    DOCTEST_CHECK(hostages::Empty());
    DOCTEST_CHECK(state.leaders[1].status == 0);  // an honest leader (framed as a traitor) comes back
}

DOCTEST_TEST_CASE("a full table or a leader out of service falls back to the classic dismissal")
{
    s_hostages_scope scope;
    test::s_game_state state;
    Setup_(&state);
    hostages::State()->count = hostages::MAX_HELD;
    for (int i = 0; i < hostages::MAX_HELD; ++i) hostages::State()->held[i].leader = static_cast<int8_t>(50 + i);
    DOCTEST_CHECK_FALSE(hostages::Jail(2, false));
    hostages::Arrest(2, false);
    DOCTEST_CHECK(hostages::State()->count == hostages::MAX_HELD);
    DOCTEST_CHECK(hostages::Find(2) == -1);  // not jailed: the classic dismissal was done
    hostages::State()->count = 0;
    state.leaders[1].status = -2;
    DOCTEST_CHECK_FALSE(hostages::Jail(1, false));
}

DOCTEST_TEST_CASE("a jailed leader of an empire that is gone is dropped")
{
    s_hostages_scope scope;
    test::s_game_state state;
    Setup_(&state);
    hostages::Arrest(2, false);
    state.players[0].eliminated = 1;
    hostages::Turn();
    DOCTEST_CHECK(hostages::Empty());
}

DOCTEST_TEST_CASE("a caught spy sits in prison unless spy_fate decided its fate; escape, ransom, exchange, term")
{
    s_hostages_scope scope;
    test::s_game_state state;
    Setup_(&state);
    // The classic path: spy_fate off, so espionage::On_Spy_Caught imprisons the spy of player 0 with player 1.
    espionage::On_Spy_Caught(1, 0);
    espionage::On_Spy_Caught(1, 0);
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 2);
    DOCTEST_CHECK(hostages::Spies_Held(0, 1) == 0);
    DOCTEST_CHECK_FALSE(hostages::Empty());
    // spy_fate on: a computer catcher decides the fate (execute, expel, send home, turn): no prison.
    intrigue::spy_fate = true;
    espionage::On_Spy_Caught(1, 0);
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 2);
    intrigue::spy_fate = false;

    // Escape on a low roll: one spy goes home.
    state.players[0].spies[0] = 0;
    DOCTEST_CHECK_FALSE(hostages::Spy_Escape_With_Roll(0, hostages::SPY_ESCAPE_PERCENT));
    DOCTEST_CHECK(hostages::Spy_Escape_With_Roll(0, 0));
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 1);
    DOCTEST_CHECK((state.players[0].spies[0] & 0x3F) == 1);

    // Ransom at the phase: 20 BC to the jailer; the last spy leaves, the cell is gone.
    Calm_();
    MOX::stardate = 35107;
    hostages::Turn();
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 1);
    MOX::stardate = 35108;
    hostages::Turn();
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 0);
    DOCTEST_CHECK(state.players[0].bc == 1000 - hostages::SPY_RANSOM);
    DOCTEST_CHECK(state.players[1].bc == 1000 + hostages::SPY_RANSOM);
    DOCTEST_CHECK((state.players[0].spies[0] & 0x3F) == 2);

    // A poor owner does not pay; a human owner does not pay.
    state.players[0].bc = hostages::RANSOM_RESERVE;
    DOCTEST_REQUIRE(hostages::Imprison_Spy(1, 0));
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 1);
    state.players[0].bc = 1000;
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 1);
    state.players[0].objectives = PLAYER_OBJECTIVE_MILITARIST;

    // Exchange one for one: each side holds the other's spy, no BC moves.
    DOCTEST_REQUIRE(hostages::Imprison_Spy(0, 1));
    state.players[0].bc = state.players[1].bc = 1000;
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 0);
    DOCTEST_CHECK(hostages::Spies_Held(0, 1) == 0);
    DOCTEST_CHECK(state.players[0].bc == 1000);
    DOCTEST_CHECK(state.players[1].bc == 1000);
}

DOCTEST_TEST_CASE("a spy held too long is released, or executed by a ruthless jailer; the pair is capped")
{
    s_hostages_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].bc = 0;  // cannot pay
    DOCTEST_REQUIRE(hostages::Imprison_Spy(1, 0));
    MOX::stardate += hostages::SPY_PRISON_TURNS;
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 0);
    DOCTEST_CHECK((state.players[0].spies[0] & 0x3F) == 1);

    state.players[1].personality = PLAYER_PERSONALITY_RUTHLESS;
    DOCTEST_REQUIRE(hostages::Imprison_Spy(1, 0));
    MOX::stardate += hostages::SPY_PRISON_TURNS;
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 0);
    DOCTEST_CHECK((state.players[0].spies[0] & 0x3F) == 1);  // executed: no spy came home
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_TREATED_OUR_SPY) == hostages::EXECUTED_SPY_OPINION);

    for (int i = 0; i < hostages::MAX_SPIES_PER_CELL; ++i) DOCTEST_CHECK(hostages::Imprison_Spy(1, 0));
    DOCTEST_CHECK_FALSE(hostages::Imprison_Spy(1, 0));
    DOCTEST_CHECK_FALSE(hostages::Imprison_Spy(1, 1));
}

DOCTEST_TEST_CASE("a hostage returned home may turn mole of its holder (bribery on); otherwise nothing is drawn")
{
    s_hostages_scope scope;
    test::s_game_state state;
    Setup_(&state);
    spy_ops::operations = 63;
    DOCTEST_CHECK(hostages::Mole_With_Roll(0, 1, hostages::MOLE_PERCENT - 1));
    DOCTEST_CHECK(spy_ops::Bribed_By(0) == 1);
    *spy_ops::Bribed_Slot(0) = -1;
    DOCTEST_CHECK_FALSE(hostages::Mole_With_Roll(0, 1, hostages::MOLE_PERCENT));
    DOCTEST_CHECK(spy_ops::Bribed_By(0) == -1);
    spy_ops::operations = 0;
    DOCTEST_CHECK_FALSE(hostages::Mole_With_Roll(0, 1, 0));

    // The term's end with bribery off: no draw. With it on: one draw, and the leader is home either way.
    Ally_(&state);
    DOCTEST_REQUIRE(hostages::State()->count == 1);
    const int16_t ward = hostages::State()->held[0].leader;
    MOX::stardate += hostages::HOSTAGE_TURNS;
    const uint32_t seed = game_random::Get_Random_Seed();
    hostages::Turn();
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
    DOCTEST_CHECK(state.leaders[ward].status == 0);

    spy_ops::operations = 63;
    Ally_(&state);
    DOCTEST_REQUIRE(hostages::State()->count == 1);
    MOX::stardate += hostages::HOSTAGE_TURNS;
    hostages::Turn();
    DOCTEST_CHECK(game_random::Get_Random_Seed() != seed);  // the mole roll
    DOCTEST_CHECK(state.leaders[ward].status == 0);
    DOCTEST_CHECK(state.leaders[ward].player_index == 0);
    *spy_ops::Bribed_Slot(ward) = -1;
}

DOCTEST_TEST_CASE("the payload with jail records and spy cells round-trips; version 1 is still read; bad cells are refused")
{
    s_hostages_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(hostages::Jail(2, false));
    DOCTEST_REQUIRE(hostages::Jail(1, true));
    DOCTEST_REQUIRE(hostages::Imprison_Spy(1, 0));
    DOCTEST_REQUIRE(hostages::Imprison_Spy(1, 0));
    DOCTEST_REQUIRE(hostages::Imprison_Spy(0, 2));
    const uint32_t size = hostages::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    hostages::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    hostages::Clear();
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(hostages::Parse_Save(&reader, &error));
    hostages::Commit_Save();
    DOCTEST_CHECK(hostages::State()->count == 2);
    DOCTEST_CHECK(hostages::State()->held[1].kind == hostages::KIND_TRAITOR);
    DOCTEST_CHECK(hostages::State()->held[1].owner == hostages::State()->held[1].holder);
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 2);
    DOCTEST_CHECK(hostages::Spies_Held(0, 2) == 1);

    // A spy cell at the end: jailer, owner, count, since (4 bytes). Offsets from the end: the last cell is 7 bytes.
    auto same = std::make_unique<uint8_t[]>(size);
    std::copy(buffer.get(), buffer.get() + size, same.get());
    same[size - 6] = same[size - 7];  // owner = jailer
    SERIAL::Reader_ r1(same.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(hostages::Parse_Save(&r1, &error));

    auto zero = std::make_unique<uint8_t[]>(size);
    std::copy(buffer.get(), buffer.get() + size, zero.get());
    zero[size - 5] = 0;  // a cell of no spies
    SERIAL::Reader_ r2(zero.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(hostages::Parse_Save(&r2, &error));

    // A jail record held by another empire than its owner is refused.
    auto wrong = std::make_unique<uint8_t[]>(size);
    std::copy(buffer.get(), buffer.get() + size, wrong.get());
    wrong[5] = 1;  // the first record (a jail): holder != owner
    SERIAL::Reader_ r3(wrong.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(hostages::Parse_Save(&r3, &error));

    // Version 1: no spy cells; the records are read as before.
    uint8_t v1[] = {1, 0, 1, 3, 0, 1, hostages::KIND_PRISONER, 0, 0, 0, 0};
    SERIAL::Reader_ r4(v1, sizeof(v1), "test underflow", true);
    DOCTEST_REQUIRE(hostages::Parse_Save(&r4, &error));
    hostages::Commit_Save();
    DOCTEST_CHECK(hostages::State()->count == 1);
    DOCTEST_CHECK(hostages::State()->held[0].leader == 3);
    DOCTEST_CHECK(hostages::State()->spy_count == 0);
}

}

// R2, piece B (human-ui.md section 5): a human owner or holder of prisoners and imprisoned spies chooses in
// the choice window.
namespace {
    struct s_choice_scope {
        test::s_saved_value<bool> choices_on{choices::enabled};
        test::s_saved_value<uint8_t> language{text_language::language};
        test::s_saved_value<int8_t> game_type{MOX::game_type};
        test::s_saved_value<decltype(NETMOX::net.is_network_active)> network{NETMOX::net.is_network_active};

        s_choice_scope() {
            choices::Clear();
            choices::enabled = true;
            MOX::game_type = 0;
            NETMOX::net.is_network_active = 0;
        }
        ~s_choice_scope() { choices::Clear(); }
    };

    // Every text of a choice in both languages: no placeholder left and no '?' (CP866 clean).
    void Check_Texts_(const choices::s_choice& c, int expected_options) {
        for (const uint8_t language : {text_language::LANGUAGE_EN, text_language::LANGUAGE_RU}) {
            text_language::language = language;
            choices::s_text t;
            DOCTEST_REQUIRE(hostages::Choice_Text(c, &t));
            DOCTEST_CHECK(t.count == expected_options);
            std::string all = std::string(t.title) + t.body;
            for (int i = 0; i < t.count; ++i) all += t.option[i];
            DOCTEST_CHECK(all.find('{') == std::string::npos);
            DOCTEST_CHECK(all.find('?') == std::string::npos);
        }
    }
} // namespace

DOCTEST_TEST_SUITE("hostages human choices") {

DOCTEST_TEST_CASE("a human owner of a prisoner chooses: pay the ransom, exchange, or leave; off and computers as before")
{
    s_hostages_scope scope;
    s_choice_scope cscope;
    test::s_game_state state;
    Setup_(&state);
    test::Load_Base_Texts();
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_REQUIRE(hostages::Hold(0, 1, hostages::KIND_PRISONER));  // level 3: 125 BC
    state.players[0].bc = 500;
    // Setting off: the human is not asked and does not pay.
    choices::enabled = false;
    MOX::stardate = 35108;
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(hostages::State()->count == 1);
    // Not the phase: nothing; on the phase: one pending choice and no effect.
    choices::enabled = true;
    MOX::stardate = 35107;
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(choices::Empty());
    MOX::stardate = 35108;
    Calm_();
    hostages::Turn();
    DOCTEST_REQUIRE(choices::Count(0) == 1);
    DOCTEST_CHECK(hostages::State()->count == 1);
    DOCTEST_CHECK(state.players[0].bc == 500);
    Calm_();
    hostages::Turn();  // asked once, not again while it waits
    DOCTEST_CHECK(choices::Count(0) == 1);
    const choices::s_choice ask = choices::State()->choice[0];
    DOCTEST_CHECK(ask.module == choices::MODULE_HOSTAGES);
    DOCTEST_CHECK(ask.value == 125);
    Check_Texts_(ask, 2);
    text_language::language = text_language::LANGUAGE_EN;
    choices::s_text en;
    DOCTEST_REQUIRE(hostages::Choice_Text(ask, &en));
    DOCTEST_CHECK(std::string(en.option[0]) == "Pay 125 BC (the leader returns to your pool)");
    // Pay: the computer's ransom path.
    DOCTEST_REQUIRE(choices::Apply_Pending(0, 0));
    DOCTEST_CHECK(hostages::Empty());
    DOCTEST_CHECK(state.players[0].bc == 375);
    DOCTEST_CHECK(state.players[1].bc == 1125);
    DOCTEST_CHECK(state.leaders[0].status == 0);
    DOCTEST_CHECK(state.leaders[0].player_index == 0);
    // Leave: nothing changes.
    DOCTEST_REQUIRE(hostages::Hold(1, 1, hostages::KIND_PRISONER));  // level 1: 75 BC
    Calm_();
    hostages::Turn();
    DOCTEST_REQUIRE(choices::Count(0) == 1);
    DOCTEST_REQUIRE(choices::Apply_Pending(0, 1));
    DOCTEST_CHECK(hostages::State()->count == 1);
    DOCTEST_CHECK(state.players[0].bc == 375);
    // Too poor and no exchange: not asked; asked but the sum gone by the time: nothing happens.
    state.players[0].bc = 10;
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(choices::Empty());
    state.players[0].bc = 500;
    Calm_();
    hostages::Turn();
    DOCTEST_REQUIRE(choices::Count(0) == 1);
    const choices::s_choice stale = choices::State()->choice[0];
    state.players[0].bc = 10;
    choices::s_text poor;
    DOCTEST_REQUIRE(hostages::Choice_Text(stale, &poor));
    DOCTEST_CHECK(std::string(poor.option[0]).find("not enough BC") != std::string::npos);
    DOCTEST_REQUIRE(choices::Apply_Pending(0, 0));
    DOCTEST_CHECK(hostages::State()->count == 1);
    DOCTEST_CHECK(state.players[0].bc == 10);
    // A gone leader (released elsewhere) makes the text false.
    state.players[0].bc = 500;
    Calm_();
    hostages::Turn();
    DOCTEST_REQUIRE(choices::Count(0) == 1);
    const choices::s_choice gone = choices::State()->choice[0];
    hostages::Release(0, 0, "test");
    DOCTEST_CHECK_FALSE(hostages::Choice_Text(gone, &en));
    choices::Clear();
    // A full queue: nothing, as today.
    DOCTEST_REQUIRE(hostages::Hold(1, 1, hostages::KIND_PRISONER));
    for (int i = 0; i < choices::MAX_PENDING; ++i) {
        choices::s_choice filler;
        filler.module = choices::MODULE_INCIDENTS;
        filler.player = 2;
        DOCTEST_REQUIRE(choices::Add(filler));
    }
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(choices::Count(0) == 0);
    DOCTEST_CHECK(hostages::State()->count == 1);
    choices::Clear();
    // A computer owner pays at once and queues nothing.
    state.players[0].objectives = PLAYER_OBJECTIVE_MILITARIST;
    state.players[0].bc = 1000;
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(hostages::Empty());
}

DOCTEST_TEST_CASE("a human owner may offer an exchange when it holds a leader of the holder")
{
    s_hostages_scope scope;
    s_choice_scope cscope;
    test::s_game_state state;
    Setup_(&state);
    test::Load_Base_Texts();
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_REQUIRE(hostages::Hold(0, 1, hostages::KIND_PRISONER));   // ours, held by player 1
    DOCTEST_REQUIRE(hostages::Hold(10, 0, hostages::KIND_PRISONER));  // theirs, held by us
    state.players[0].bc = 10;                                         // cannot pay, can exchange
    state.players[1].bc = 10;                                         // a computer owner would buy its own back first
    MOX::stardate = 35108;
    Calm_();
    hostages::Turn();
    DOCTEST_REQUIRE(choices::Count(0) >= 1);
    const choices::s_choice ask = choices::State()->choice[0];
    DOCTEST_CHECK(ask.kind == hostages::CHOICE_LEADER_OWNER);
    Check_Texts_(ask, 3);
    DOCTEST_REQUIRE(choices::Apply_Pending(0, 1));
    DOCTEST_CHECK(hostages::Empty());
    DOCTEST_CHECK(state.leaders[0].player_index == 0);
    DOCTEST_CHECK(state.leaders[0].status == 0);
    DOCTEST_CHECK(state.leaders[10].player_index == 1);
    DOCTEST_CHECK(state.leaders[10].status == 0);
    DOCTEST_CHECK(state.players[0].bc == 10);
    DOCTEST_CHECK(state.players[1].bc == 10);
}

DOCTEST_TEST_CASE("a human holder of a prisoner chooses after the term: execute, release, exchange or keep")
{
    s_hostages_scope scope;
    s_choice_scope cscope;
    test::s_game_state state;
    Setup_(&state);
    test::Load_Base_Texts();
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;  // player 1 holds player 0's leaders
    state.players[0].bc = 0;                               // a computer owner buys its leaders back at once if it can
    MOX::stardate = 35058;
    DOCTEST_REQUIRE(hostages::Hold(0, 1, hostages::KIND_PRISONER));
    DOCTEST_REQUIRE(hostages::Hold(1, 1, hostages::KIND_PRISONER));
    // Before the term nothing is asked; off: nothing at the term; a human never executes by itself.
    MOX::stardate = 35098;
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(choices::Empty());
    MOX::stardate = 35108;
    choices::enabled = false;
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(hostages::State()->count == 2);
    choices::enabled = true;
    Calm_();
    hostages::Turn();
    DOCTEST_REQUIRE(choices::Count(1) == 2);
    DOCTEST_CHECK(hostages::State()->count == 2);
    const choices::s_choice first = choices::State()->choice[0];
    DOCTEST_CHECK(first.kind == hostages::CHOICE_LEADER_HOLDER);
    Check_Texts_(first, 3);
    // Execute: the computer holder's executing path (opinion of the owner), the record is gone.
    DOCTEST_REQUIRE(choices::Apply_Pending(1, 0));
    DOCTEST_CHECK(hostages::State()->count == 1);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_HARMED_A_HOSTAGE) == hostages::EXECUTED_PRISONER_OPINION);
    DOCTEST_CHECK(state.leaders[0].status == -2);
    // Release: the leader goes home.
    DOCTEST_REQUIRE(choices::Apply_Pending(1, 1));
    DOCTEST_CHECK(hostages::Empty());
    DOCTEST_CHECK(state.leaders[1].status == 0);
    DOCTEST_CHECK(state.leaders[1].player_index == 0);
    // Keep: nothing changes, and the question comes again on the next phase.
    DOCTEST_REQUIRE(hostages::Hold(2, 1, hostages::KIND_PRISONER));
    state.players[0].bc = 0;  // a computer owner could not pay
    MOX::stardate = 35058;
    hostages::State()->held[0].since = 35058;
    MOX::stardate = 35108;
    Calm_();
    hostages::Turn();
    DOCTEST_REQUIRE(choices::Count(1) == 1);
    DOCTEST_REQUIRE(choices::Apply_Pending(1, 2));
    DOCTEST_CHECK(hostages::State()->count == 1);
    Calm_();
    MOX::stardate = 35118;
    hostages::Turn();
    DOCTEST_CHECK(choices::Count(1) == 1);
    choices::Clear();
    hostages::Clear();
    state.leaders[2].status = 0;  // back in the pool
    // Exchange: the computer owner holds a leader of ours too; the pair is offered once and goes home together.
    MOX::stardate = 35058;
    DOCTEST_REQUIRE(hostages::Hold(2, 1, hostages::KIND_PRISONER));
    MOX::stardate = 35107;
    DOCTEST_REQUIRE(hostages::Hold(10, 0, hostages::KIND_PRISONER));
    MOX::stardate = 35108;
    Calm_();
    hostages::Turn();
    DOCTEST_REQUIRE(choices::Count(1) == 2);
    const choices::s_choice holder_ask = choices::State()->choice[0];
    const choices::s_choice owner_ask = choices::State()->choice[1];
    DOCTEST_CHECK(holder_ask.kind == hostages::CHOICE_LEADER_HOLDER);
    Check_Texts_(holder_ask, 3);
    DOCTEST_REQUIRE(choices::Apply_Pending(1, 1));
    DOCTEST_CHECK(hostages::Empty());
    DOCTEST_CHECK(state.leaders[2].player_index == 0);
    DOCTEST_CHECK(state.leaders[2].status == 0);
    DOCTEST_CHECK(state.leaders[10].player_index == 1);
    DOCTEST_CHECK(state.leaders[10].status == 0);
    choices::s_text gone;
    DOCTEST_CHECK_FALSE(hostages::Choice_Text(owner_ask, &gone));
}

DOCTEST_TEST_CASE("imprisoned spies: a human owner pays or exchanges, a human jailer executes, releases or keeps")
{
    s_hostages_scope scope;
    s_choice_scope cscope;
    test::s_game_state state;
    Setup_(&state);
    test::Load_Base_Texts();
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.players[0].spies[0] = 0;
    MOX::stardate = 35108;
    DOCTEST_REQUIRE(hostages::Imprison_Spy(1, 0));
    state.players[0].bc = 500;
    // Off: not asked.
    choices::enabled = false;
    Calm_();
    hostages::Turn();
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 1);
    // Owner: pay for one spy.
    choices::enabled = true;
    Calm_();
    hostages::Turn();
    DOCTEST_REQUIRE(choices::Count(0) == 1);
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 1);
    const choices::s_choice ask = choices::State()->choice[0];
    DOCTEST_CHECK(ask.kind == hostages::CHOICE_SPY_OWNER);
    Check_Texts_(ask, 2);
    DOCTEST_REQUIRE(choices::Apply_Pending(0, 0));
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 0);
    DOCTEST_CHECK(state.players[0].bc == 500 - hostages::SPY_RANSOM);
    DOCTEST_CHECK(state.players[1].bc == 1000 + hostages::SPY_RANSOM);
    DOCTEST_CHECK((state.players[0].spies[0] & 0x3F) == 1);
    // Owner: exchange one spy each when it holds one of theirs.
    DOCTEST_REQUIRE(hostages::Imprison_Spy(1, 0));
    DOCTEST_REQUIRE(hostages::Imprison_Spy(0, 1));
    state.players[0].bc = 10;
    state.players[1].bc = 10;
    Calm_();
    hostages::Turn();
    DOCTEST_REQUIRE(choices::Count(0) == 1);
    Check_Texts_(choices::State()->choice[0], 3);
    DOCTEST_REQUIRE(choices::Apply_Pending(0, 1));
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 0);
    DOCTEST_CHECK(hostages::Spies_Held(0, 1) == 0);
    DOCTEST_CHECK(state.players[0].bc == 10);
    DOCTEST_CHECK(state.players[1].bc == 10);
    // A gone cell makes the text false.
    DOCTEST_REQUIRE(hostages::Imprison_Spy(1, 0));
    state.players[0].bc = 500;
    Calm_();
    hostages::Turn();
    DOCTEST_REQUIRE(choices::Count(0) == 1);
    const choices::s_choice stale = choices::State()->choice[0];
    hostages::Clear();
    choices::s_text t;
    DOCTEST_CHECK_FALSE(hostages::Choice_Text(stale, &t));
    choices::Clear();
    // Jailer: after the term (40 turns) a human is asked; execute (the owner's opinion), release (home), keep.
    state.players[0].objectives = PLAYER_OBJECTIVE_MILITARIST;
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.players[0].bc = 0;  // a computer owner cannot pay
    state.players[0].spies[0] = 0;
    MOX::stardate = 35068;
    DOCTEST_REQUIRE(hostages::Imprison_Spy(1, 0));
    DOCTEST_REQUIRE(hostages::Imprison_Spy(1, 0));
    DOCTEST_REQUIRE(hostages::Imprison_Spy(1, 0));
    MOX::stardate = 35108;
    Calm_();
    hostages::Turn();
    DOCTEST_REQUIRE(choices::Count(1) == 1);
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 3);
    const choices::s_choice hold = choices::State()->choice[0];
    DOCTEST_CHECK(hold.kind == hostages::CHOICE_SPY_HOLDER);
    Check_Texts_(hold, 3);
    DOCTEST_REQUIRE(choices::Apply_Pending(1, 0));
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 2);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_TREATED_OUR_SPY) == hostages::EXECUTED_SPY_OPINION);
    DOCTEST_CHECK((state.players[0].spies[0] & 0x3F) == 0);
    hostages::State()->spies[0].since = 35068;  // a spy leaving restarts the term
    Calm_();
    hostages::Turn();
    DOCTEST_REQUIRE(choices::Count(1) == 1);
    DOCTEST_REQUIRE(choices::Apply_Pending(1, 1));
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 1);
    DOCTEST_CHECK((state.players[0].spies[0] & 0x3F) == 1);
    hostages::State()->spies[0].since = 35068;
    Calm_();
    hostages::Turn();
    DOCTEST_REQUIRE(choices::Count(1) == 1);
    DOCTEST_REQUIRE(choices::Apply_Pending(1, 2));
    DOCTEST_CHECK(hostages::Spies_Held(1, 0) == 1);
}

}
