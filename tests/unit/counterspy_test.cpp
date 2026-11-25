#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/counterspy.h"
#include "game/dice.h"
#include "game/espionage.h"
#include "game/hostages.h"
#include "game/loyalty.h"
#include "game/opinion.h"
#include "game/secrets.h"
#include "game/serialization.h"
#include "game/spy_ops.h"

#include <memory>
#include <string>
#include <vector>

namespace {
    struct s_counterspy_scope {
        test::s_saved_value<bool> mole_hunt{counterspy::mole_hunt};
        test::s_saved_value<bool> canary{counterspy::canary_trap};
        test::s_saved_value<bool> interrogation{counterspy::interrogation};
        test::s_saved_value<bool> guards{counterspy::guards};
        test::s_saved_value<int32_t> fee{counterspy::guard_fee};
        test::s_saved_value<bool> secrets_enabled{secrets::enabled};
        test::s_saved_value<bool> network{espionage::network};
        test::s_saved_value<bool> ci{espionage::counterintelligence};
        test::s_saved_value<int32_t> operations{spy_ops::operations};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<bool> loyalty_enabled{loyalty::enabled};
        test::s_saved_value<uint8_t> roll_kind{dice::roll_kind};

        s_counterspy_scope() {
            counterspy::Clear();
            secrets::Clear();
            espionage::Clear();
            spy_ops::Clear();
            opinion::Clear();
            loyalty::Clear();
            counterspy::mole_hunt = true;
            counterspy::canary_trap = true;
            counterspy::interrogation = true;
            counterspy::guards = true;
            counterspy::guard_fee = 5;
            secrets::enabled = true;
            espionage::network = true;
            espionage::counterintelligence = false;
            spy_ops::operations = 16383;
            opinion::enabled = true;
            loyalty::enabled = false;
            dice::roll_kind = dice::ROLL_TWO_D10;
        }
        ~s_counterspy_scope() {
            counterspy::Clear();
            secrets::Clear();
            espionage::Clear();
            spy_ops::Clear();
            opinion::Clear();
            loyalty::Clear();
        }
    };

    const int32_t TRIUMPH[2] = {10, 10};
    const int32_t CATASTROPHE[2] = {1, 1};

    s_leader_data& Leader_(test::s_game_state* state, int index, int owner, int8_t status) {
        s_leader_data& l = state->leaders[index];
        l.player_index = static_cast<int8_t>(owner);
        l.status = status;
        l.location = -1;
        return l;
    }

    // Four computer empires that know each other. Player 2 holds assigned leaders 4 and 5; leader 4 is
    // bribed by player 1, which has spies in player 2. Player 1 plans a sneak attack on player 3.
    // Stardate 35006 (a mole hunt phase).
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 4;
        MOX::NUM_COLONIES = 0;
        MOX::stardate = 35006;
        for (int i = 0; i < 4; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[i].personality = PLAYER_PERSONALITY_ERRATIC;
            state->players[i].sneak_attack_planet = -1;
            state->players[i].sneak_attack_player = -1;
            for (int j = 0; j < 4; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
        state->players[1].sneak_attack_planet = 5;
        state->players[1].sneak_attack_player = 3;
        state->players[1].spies[2] = 4;
        Leader_(state, 4, 2, 1);
        Leader_(state, 5, 2, 1);
        *spy_ops::Bribed_Slot(4) = 1;
    }
} // namespace

DOCTEST_TEST_SUITE("counter-intelligence") {

DOCTEST_TEST_CASE("the hunt finds a mole: the bribe is uncovered") {
    s_counterspy_scope scope;
    test::s_game_state state;
    Setup_(&state);
    counterspy::On_Secret_Leaked(2);  // a secret of player 2 leaked: suspicion
    DOCTEST_REQUIRE(counterspy::Suspects(2));
    DOCTEST_CHECK(counterspy::Suspect_Leader(2) == 4);
    DOCTEST_CHECK(counterspy::Hunt_With_Dice(2, CATASTROPHE) == counterspy::HUNT_NOTHING);  // a failed hunt finds nothing
    DOCTEST_CHECK(spy_ops::Bribed_By(4) == 1);
    counterspy::On_Secret_Leaked(2);
    DOCTEST_CHECK(counterspy::Hunt_With_Dice(2, TRIUMPH) == counterspy::HUNT_UNCOVERED);
    DOCTEST_CHECK(spy_ops::Bribed_By(4) == -1);
    DOCTEST_CHECK(state.leaders[4].status != 1);
    DOCTEST_REQUIRE(hostages::Find(4) >= 0);  // the uncovered mole is jailed as a traitor (ab-review 3.1)
    DOCTEST_CHECK(hostages::State()->held[hostages::Find(4)].kind == hostages::KIND_TRAITOR);
    hostages::Clear();
    DOCTEST_CHECK(state.leaders[5].status == 1);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_BRIBED_OUR_LEADER) == counterspy::UNCOVERED_OPINION);
}

DOCTEST_TEST_CASE("the hunt jails an innocent when the check fails; a success leaves it be") {
    s_counterspy_scope scope;
    test::s_game_state state;
    Setup_(&state);
    *spy_ops::Bribed_Slot(4) = -1;
    loyalty::enabled = true;
    loyalty::Record(4)->loyalty = 40;
    loyalty::Record(5)->loyalty = 60;
    counterspy::On_Secret_Leaked(2);
    DOCTEST_CHECK(counterspy::Suspect_Leader(2) == 4);
    DOCTEST_CHECK(counterspy::Hunt_With_Dice(2, TRIUMPH) == counterspy::HUNT_NOTHING);
    DOCTEST_CHECK(state.leaders[4].status == 1);
    counterspy::On_Secret_Leaked(2);
    DOCTEST_CHECK(counterspy::Hunt_With_Dice(2, CATASTROPHE) == counterspy::HUNT_JAILED);
    DOCTEST_CHECK(state.leaders[4].status != 1);
    DOCTEST_REQUIRE(hostages::Find(4) >= 0);  // the innocent is jailed (ab-review 3.1)
    DOCTEST_CHECK(hostages::State()->held[hostages::Find(4)].kind == hostages::KIND_JAIL);
    hostages::Clear();
    DOCTEST_CHECK(loyalty::Of(4) == 40 + counterspy::JAILED_LOYALTY);
    DOCTEST_CHECK(loyalty::Of(5) == 60 + counterspy::WITCH_LOYALTY);
}

DOCTEST_TEST_CASE("no hunt without suspicion, and a suspicion goes stale") {
    s_counterspy_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK_FALSE(counterspy::Suspects(2));
    counterspy::On_Spy_Caught(2, 3);  // a caught spy is an outsider, not a mole
    DOCTEST_CHECK_FALSE(counterspy::Suspects(2));
    counterspy::Turn();  // phase 6: nobody suspects anyone
    DOCTEST_CHECK(spy_ops::Bribed_By(4) == 1);
    DOCTEST_CHECK(state.leaders[4].status == 1);
    counterspy::On_Secret_Leaked(2);
    DOCTEST_CHECK(counterspy::Suspects(2));
    MOX::stardate += counterspy::SUSPECT_TURNS + 1;
    DOCTEST_CHECK_FALSE(counterspy::Suspects(2));
    espionage::counterintelligence = true;
    espionage::Set_Mode(2, espionage::MODE_HEIGHTENED);
    DOCTEST_CHECK(counterspy::Suspects(2));
    DOCTEST_CHECK_FALSE(counterspy::Suspects(0 + 4));  // a player that does not exist
}

DOCTEST_TEST_CASE("decision 2: one mole hunt per HUNT_COOLDOWN turns for an empire") {
    s_counterspy_scope scope;
    test::s_game_state state;
    Setup_(&state);
    for (int i = 6; i < 10; ++i) Leader_(&state, i, 2, 1);  // enough leaders for a few hunts
    counterspy::On_Secret_Leaked(2);
    counterspy::Turn();  // stardate 35006: phase 6, the hunt is held
    DOCTEST_CHECK(counterspy::State()->last_hunt[2] == 35006);
    MOX::stardate += counterspy::PERIOD;  // 35016: the next phase, 10 turns later
    counterspy::On_Secret_Leaked(2);
    counterspy::Turn();
    DOCTEST_CHECK(counterspy::State()->last_hunt[2] == 35006);  // held back: less than 20 turns
    DOCTEST_CHECK(counterspy::Suspects(2));                     // the suspicion waits
    MOX::stardate += counterspy::PERIOD;  // 35026: 20 turns after
    counterspy::On_Secret_Leaked(2);
    counterspy::Turn();
    DOCTEST_CHECK(counterspy::State()->last_hunt[2] == 35026);
    DOCTEST_CHECK(counterspy::HUNT_COOLDOWN == 20);
}

DOCTEST_TEST_CASE("decision 2: each innocent jailed costs the other leaders more loyalty") {
    s_counterspy_scope scope;
    test::s_game_state state;
    Setup_(&state);
    for (int i = 6; i < 9; ++i) Leader_(&state, i, 2, 1);
    loyalty::enabled = true;
    for (int i = 4; i < 9; ++i) loyalty::Record(static_cast<int16_t>(i))->loyalty = 80;
    counterspy::On_Innocent_Jailed(2, 4);
    DOCTEST_CHECK(loyalty::Of(4) == 80 + counterspy::JAILED_LOYALTY);
    DOCTEST_CHECK(loyalty::Of(5) == 80 + counterspy::WITCH_LOYALTY);  // the first innocent: the old cost
    DOCTEST_CHECK(counterspy::State()->innocents[2] == 1);
    counterspy::On_Innocent_Jailed(2, 6);
    DOCTEST_CHECK(loyalty::Of(5) == 80 + 2 * counterspy::WITCH_LOYALTY + counterspy::INNOCENT_STEP);  // the second: more
    for (int i = 0; i < 10; ++i) counterspy::On_Innocent_Jailed(2, 7);
    loyalty::Record(8)->loyalty = 100;
    counterspy::On_Innocent_Jailed(2, 7);
    DOCTEST_CHECK(loyalty::Of(8) == 100 + counterspy::WITCH_LOYALTY + counterspy::INNOCENT_STEP * counterspy::INNOCENT_CAP);  // capped
    DOCTEST_CHECK(counterspy::State()->innocents[2] == 13);
    // The hunt's own jailing goes through the same cost.
    counterspy::Clear();
    loyalty::Record(5)->loyalty = 80;
    loyalty::Record(4)->loyalty = 40;
    *spy_ops::Bribed_Slot(4) = -1;
    counterspy::On_Secret_Leaked(2);
    DOCTEST_CHECK(counterspy::Hunt_With_Dice(2, CATASTROPHE) == counterspy::HUNT_JAILED);
    DOCTEST_CHECK(counterspy::State()->innocents[2] == 1);
    DOCTEST_CHECK(loyalty::Of(5) == 80 + counterspy::WITCH_LOYALTY);
    hostages::Clear();
}

DOCTEST_TEST_CASE("decision 2: the payload of version 3 round-trips and version 2 is still read") {
    s_counterspy_scope scope;
    test::s_game_state state;
    Setup_(&state);
    counterspy::State()->last_hunt[2] = 35006;
    counterspy::State()->innocents[2] = 4;
    counterspy::State()->canary[1] = 35010;
    const uint32_t size = counterspy::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    counterspy::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    DOCTEST_CHECK(buffer[0] == 3);
    counterspy::Clear();
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(counterspy::Parse_Save(&reader, &error));
    counterspy::Commit_Save();
    DOCTEST_CHECK(counterspy::State()->last_hunt[2] == 35006);
    DOCTEST_CHECK(counterspy::State()->innocents[2] == 4);
    const uint32_t v2_size = size - 8 * 5;
    std::vector<uint8_t> old_buffer(buffer.get(), buffer.get() + v2_size);
    old_buffer[0] = 2;
    old_buffer[1] = 0;
    counterspy::Clear();
    SERIAL::Reader_ old_reader(old_buffer.data(), v2_size, "test underflow", true);
    DOCTEST_REQUIRE(counterspy::Parse_Save(&old_reader, &error));
    counterspy::Commit_Save();
    DOCTEST_CHECK(counterspy::State()->canary[1] == 35010);
    DOCTEST_CHECK(counterspy::State()->last_hunt[2] == 0);
    DOCTEST_CHECK(counterspy::State()->innocents[2] == 0);
    // A negative hunt stardate is refused.
    buffer[size - 8 * 5 + 3] = 0x80;  // player 0's last hunt, high byte
    SERIAL::Reader_ bad(buffer.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(counterspy::Parse_Save(&bad, &error));
}

DOCTEST_TEST_CASE("the canary trap springs on time and never on an innocent") {
    s_counterspy_scope scope;
    test::s_game_state state;
    Setup_(&state);
    counterspy::mole_hunt = false;  // the canary alone
    counterspy::On_Secret_Leaked(2);
    DOCTEST_CHECK(counterspy::Canary_Turn(2) == MOX::stardate + counterspy::CANARY_TURNS);
    const int32_t turn = counterspy::Canary_Turn(2);
    counterspy::On_Secret_Leaked(2);  // one trap at a time
    DOCTEST_CHECK(counterspy::Canary_Turn(2) == turn);
    MOX::stardate = turn - 1;
    counterspy::Turn();
    DOCTEST_CHECK(spy_ops::Bribed_By(4) == 1);  // not yet
    MOX::stardate = turn;
    counterspy::Turn();
    DOCTEST_CHECK(spy_ops::Bribed_By(4) == -1);
    DOCTEST_CHECK(state.leaders[4].status != 1);
    DOCTEST_CHECK(state.leaders[5].status == 1);  // the innocent is untouched
    DOCTEST_CHECK(counterspy::Canary_Turn(2) == 0);

    // A briber without spies or a resident in the empire is not caught; no innocent is jailed either.
    Leader_(&state, 4, 2, 1);
    *spy_ops::Bribed_Slot(4) = 1;
    state.players[1].spies[2] = 0;
    counterspy::On_Secret_Leaked(2);
    DOCTEST_CHECK_FALSE(counterspy::Spring_Canary(2));
    DOCTEST_CHECK(spy_ops::Bribed_By(4) == 1);
    DOCTEST_CHECK(state.leaders[4].status == 1);
    DOCTEST_CHECK(state.leaders[5].status == 1);
}

// 3.5.5 "Ловушка канарейки": "разным подозреваемым - разная ложная информация; смотрим, какая утечёт".
DOCTEST_TEST_CASE("the canary trap feeds each suspect its own false report; only a leaked one is caught") {
    s_counterspy_scope scope;
    test::s_game_state state;
    Setup_(&state);
    counterspy::mole_hunt = false;
    Leader_(&state, 6, 2, 1);
    Leader_(&state, 7, 2, 1);
    *spy_ops::Bribed_Slot(6) = 3;  // bribed by player 3, which has no spies in player 2: cannot listen
    *spy_ops::Bribed_Slot(7) = 1;  // bribed, but the most loyal: not a suspect
    loyalty::enabled = true;
    loyalty::Record(4)->loyalty = 50;
    loyalty::Record(5)->loyalty = 30;
    loyalty::Record(6)->loyalty = 40;
    loyalty::Record(7)->loyalty = 90;
    counterspy::On_Secret_Leaked(2);
    // Three suspects, the least loyal first, one report each.
    DOCTEST_CHECK(counterspy::Canary_Suspect(2, 0) == 5);
    DOCTEST_CHECK(counterspy::Canary_Suspect(2, 1) == 6);
    DOCTEST_CHECK(counterspy::Canary_Suspect(2, 2) == 4);
    DOCTEST_CHECK(counterspy::Canary_Suspect(2, 3) == -1);
    DOCTEST_CHECK(counterspy::Canary_Suspect(1, 0) == -1);
    MOX::stardate = counterspy::Canary_Turn(2);
    counterspy::Turn();
    DOCTEST_CHECK(spy_ops::Bribed_By(4) == -1);   // its report leaked through player 1
    DOCTEST_CHECK(spy_ops::Bribed_By(6) == 3);    // its briber could not listen
    DOCTEST_CHECK(state.leaders[5].status == 1);  // an innocent: untouched
    DOCTEST_CHECK(spy_ops::Bribed_By(7) == 1);    // not fed a report
    DOCTEST_CHECK(state.leaders[7].status == 1);
    DOCTEST_CHECK(counterspy::Canary_Suspect(2, 0) == -1);  // the trap is spent
    hostages::Clear();

    // Two reports leak: both leaders are uncovered.
    Leader_(&state, 4, 2, 1);
    *spy_ops::Bribed_Slot(4) = 1;
    state.players[3].spies[2] = 2;
    counterspy::On_Secret_Leaked(2);
    DOCTEST_CHECK(counterspy::Spring_Canary(2));
    DOCTEST_CHECK(spy_ops::Bribed_By(4) == -1);
    DOCTEST_CHECK(spy_ops::Bribed_By(6) == -1);
    DOCTEST_CHECK(spy_ops::Bribed_By(7) == 1);
    hostages::Clear();
}

DOCTEST_TEST_CASE("the canary trap payload: version 2 keeps the suspects, version 1 is still read") {
    s_counterspy_scope scope;
    test::s_game_state state;
    Setup_(&state);
    counterspy::On_Secret_Leaked(2);
    DOCTEST_REQUIRE(counterspy::Canary_Suspect(2, 0) == 4);
    auto buffer = std::make_unique<uint8_t[]>(counterspy::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), counterspy::Save_Size(), "test overflow");
    counterspy::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == counterspy::Save_Size());
    counterspy::Clear();
    SERIAL::Reader_ reader(buffer.get(), counterspy::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(counterspy::Parse_Save(&reader, &error));
    counterspy::Commit_Save();
    DOCTEST_CHECK(counterspy::Canary_Suspect(2, 0) == 4);
    DOCTEST_CHECK(counterspy::Canary_Suspect(2, 1) == 5);
    DOCTEST_CHECK(counterspy::Canary_Suspect(2, 2) == -1);
    buffer[2 + 13 * 0 + 13] = 0x0F;  // player 0's first suspect: 9999, no such leader
    buffer[2 + 13 * 0 + 14] = 0x27;
    SERIAL::Reader_ bad(buffer.get(), counterspy::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(counterspy::Parse_Save(&bad, &error));

    constexpr uint32_t V1_SIZE = 2 + 8 * 13;
    std::vector<uint8_t> old_buffer(V1_SIZE);
    {
        SERIAL::Writer_ old_writer(old_buffer.data(), V1_SIZE, "test overflow");
        old_writer.Write_Uint16(1);
        for (int p = 0; p < 8; ++p) {
            old_writer.Write_Int32(0);
            old_writer.Write_Int32(p == 2 ? 35016 : 0);
            old_writer.Write_Int32(0);
            old_writer.Write_Uint8(0);
        }
    }
    counterspy::Clear();
    SERIAL::Reader_ old_reader(old_buffer.data(), V1_SIZE, "test underflow", true);
    DOCTEST_REQUIRE(counterspy::Parse_Save(&old_reader, &error));
    counterspy::Commit_Save();
    DOCTEST_CHECK(counterspy::Canary_Turn(2) == 35016);
    DOCTEST_CHECK(counterspy::Canary_Suspect(2, 0) == -1);
}

DOCTEST_TEST_CASE("a leak seen by the secrets module sets the trap") {
    s_counterspy_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(secrets::Learn(0, secrets::KIND_PLAN, 1, 3, -1));
    secrets::Make_Public(0);
    DOCTEST_CHECK(counterspy::Canary_Turn(1) == MOX::stardate + counterspy::CANARY_TURNS);
}

DOCTEST_TEST_CASE("interrogation learns a plan against a third empire, or warns the victim") {
    s_counterspy_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK_FALSE(counterspy::On_Spy_Caught_With_Roll(2, 1, counterspy::INTERROGATION_PERCENT));  // the roll missed
    DOCTEST_CHECK(secrets::Empty());
    DOCTEST_REQUIRE(counterspy::On_Spy_Caught_With_Roll(2, 1, 0));
    DOCTEST_CHECK(secrets::Slots()[0].kind == secrets::KIND_PLAN);
    DOCTEST_CHECK(secrets::Slots()[0].holder == 2);
    DOCTEST_CHECK(secrets::Slots()[0].owner == 1);
    DOCTEST_CHECK(secrets::Slots()[0].victim == 3);
    // A plan against the interrogator itself.
    state.players[1].sneak_attack_player = 2;
    DOCTEST_REQUIRE(counterspy::On_Spy_Caught_With_Roll(2, 1, 0));
    DOCTEST_CHECK(state.players[1].sneak_attack_planet == -1);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_PLOTTED_AGAINST_US) < 0);
    // No plan: nothing to learn.
    DOCTEST_CHECK_FALSE(counterspy::On_Spy_Caught_With_Roll(2, 1, 0));
}

DOCTEST_TEST_CASE("an interrogated false-flag spy names the framed empire") {
    s_counterspy_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK(counterspy::Blamed(2, 1, 3) == 3);
    DOCTEST_CHECK(counterspy::Blamed(2, 1, -1) == 1);
    DOCTEST_CHECK(counterspy::Blamed(2, 1, 1) == 1);
    espionage::State()->points[1][2] = 100;
    DOCTEST_REQUIRE(spy_ops::Execute_With_Dice(1, spy_ops::OP_FALSE_FLAG, 2, 3, CATASTROPHE) == spy_ops::RESULT_EXPOSED);
    DOCTEST_CHECK(opinion::Modifier(2, 3, opinion::REASON_CAUGHT_SPYING) < 0);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_CAUGHT_SPYING) == 0);
    // Off: the real attacker is blamed.
    counterspy::interrogation = false;
    opinion::Clear();
    espionage::State()->points[1][2] = 100;
    DOCTEST_CHECK(counterspy::Blamed(2, 1, 3) == 1);
    DOCTEST_REQUIRE(spy_ops::Execute_With_Dice(1, spy_ops::OP_FALSE_FLAG, 2, 3, CATASTROPHE) == spy_ops::RESULT_EXPOSED);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_CAUGHT_SPYING) < 0);
    DOCTEST_CHECK(opinion::Modifier(2, 3, opinion::REASON_CAUGHT_SPYING) == 0);
}

DOCTEST_TEST_CASE("guards raise the difficulty of killing and cost BC") {
    s_counterspy_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[2].bc = 100;
    const int32_t plain = spy_ops::Difficulty(1, spy_ops::OP_ASSASSINATION, 2);
    counterspy::Turn();  // no threat, too poor, no key leader
    DOCTEST_CHECK_FALSE(counterspy::Guarded(2));
    DOCTEST_CHECK(state.players[2].bc == 100);
    counterspy::On_Attempt(2, spy_ops::OP_POISON_FEAST);
    counterspy::Turn();
    DOCTEST_REQUIRE(counterspy::Guarded(2));
    DOCTEST_CHECK(state.players[2].bc == 100 - counterspy::guard_fee);
    DOCTEST_CHECK(spy_ops::Difficulty(1, spy_ops::OP_ASSASSINATION, 2) == plain + counterspy::GUARD_DIFFICULTY);
    DOCTEST_CHECK(counterspy::Guard_Difficulty(2, spy_ops::OP_KILL_SCIENTIST) == counterspy::GUARD_DIFFICULTY);
    DOCTEST_CHECK(counterspy::Guard_Difficulty(2, spy_ops::OP_DATA) == 0);
    DOCTEST_CHECK(counterspy::Guard_Difficulty(1, spy_ops::OP_ASSASSINATION) == 0);
    state.players[2].bc = 2;  // cannot pay: the guards go
    counterspy::Turn();
    DOCTEST_CHECK_FALSE(counterspy::Guarded(2));
    DOCTEST_CHECK(state.players[2].bc == 2);
    // A rich empire with a leader of level 3 guards without a threat.
    counterspy::Clear();
    state.leaders[5].xp = 3000;
    state.players[2].bc = 400;
    counterspy::Turn();
    DOCTEST_CHECK(counterspy::Guarded(2));
    // The threat is forgotten after GUARD_THREAT_TURNS.
    counterspy::Clear();
    state.players[2].bc = 100;
    counterspy::On_Attempt(2, spy_ops::OP_ASSASSINATION);
    MOX::stardate += counterspy::GUARD_THREAT_TURNS + 1;
    counterspy::Turn();
    DOCTEST_CHECK_FALSE(counterspy::Guarded(2));
}

DOCTEST_TEST_CASE("all flags off: no effect, no state") {
    s_counterspy_scope scope;
    test::s_game_state state;
    Setup_(&state);
    counterspy::mole_hunt = false;
    counterspy::canary_trap = false;
    counterspy::interrogation = false;
    counterspy::guards = false;
    state.players[2].bc = 1000;
    const int32_t plain = spy_ops::Difficulty(1, spy_ops::OP_ASSASSINATION, 2);
    counterspy::On_Spy_Caught(2, 1);
    counterspy::On_Secret_Leaked(2);
    counterspy::On_Attempt(2, spy_ops::OP_ASSASSINATION);
    counterspy::Turn();
    DOCTEST_CHECK(counterspy::Empty());
    DOCTEST_CHECK_FALSE(counterspy::Suspects(2));
    DOCTEST_CHECK(counterspy::Hunt_With_Dice(2, TRIUMPH) == counterspy::HUNT_NONE);
    DOCTEST_CHECK_FALSE(counterspy::On_Spy_Caught_With_Roll(2, 1, 0));
    DOCTEST_CHECK_FALSE(counterspy::Spring_Canary(2));
    DOCTEST_CHECK(counterspy::Blamed(2, 1, 3) == 1);
    DOCTEST_CHECK(counterspy::Guard_Difficulty(2, spy_ops::OP_ASSASSINATION) == 0);
    DOCTEST_CHECK(spy_ops::Difficulty(1, spy_ops::OP_ASSASSINATION, 2) == plain);
    DOCTEST_CHECK(state.players[2].bc == 1000);
    DOCTEST_CHECK(spy_ops::Bribed_By(4) == 1);
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_counterspy_scope scope;
    test::s_game_state state;
    Setup_(&state);
    counterspy::State()->suspicion[2] = 35000;
    counterspy::State()->canary[1] = 35010;
    counterspy::State()->attempt[3] = 34990;
    counterspy::State()->guarded[0] = 1;
    auto buffer = std::make_unique<uint8_t[]>(counterspy::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), counterspy::Save_Size(), "test overflow");
    counterspy::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == counterspy::Save_Size());
    counterspy::Clear();
    DOCTEST_CHECK(counterspy::Empty());
    SERIAL::Reader_ reader(buffer.get(), counterspy::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(counterspy::Parse_Save(&reader, &error));
    DOCTEST_CHECK(counterspy::Empty());  // parsed, not committed
    counterspy::Commit_Save();
    DOCTEST_CHECK(counterspy::State()->suspicion[2] == 35000);
    DOCTEST_CHECK(counterspy::State()->canary[1] == 35010);
    DOCTEST_CHECK(counterspy::State()->attempt[3] == 34990);
    DOCTEST_CHECK(counterspy::State()->guarded[0] == 1);
    buffer[2 + 12] = 7;  // player 0's guard flag
    SERIAL::Reader_ bad(buffer.get(), counterspy::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(counterspy::Parse_Save(&bad, &error));
    DOCTEST_CHECK(counterspy::State()->guarded[0] == 1);
}

DOCTEST_TEST_CASE("a hostile operation the victim sees makes it suspect a mole; unseen ones do not") {
    s_counterspy_scope scope;
    test::s_game_state state;
    Setup_(&state);
    counterspy::On_Harm(2, spy_ops::OP_DATA);
    counterspy::On_Harm(2, spy_ops::OP_BRIBERY);
    DOCTEST_CHECK_FALSE(counterspy::Suspects(2));
    counterspy::On_Harm(2, spy_ops::OP_SABOTAGE_SHIPYARD);
    DOCTEST_CHECK(counterspy::Suspects(2));
    MOX::stardate += counterspy::SUSPECT_TURNS + 1;
    DOCTEST_CHECK_FALSE(counterspy::Suspects(2));
    counterspy::On_Harm(2);  // a classic theft or a destroyed building
    DOCTEST_CHECK(counterspy::Suspects(2));
    MOX::stardate += counterspy::SUSPECT_TURNS + 1;
    counterspy::mole_hunt = false;
    counterspy::On_Harm(2, spy_ops::OP_ASSASSINATION);
    counterspy::mole_hunt = true;
    DOCTEST_CHECK_FALSE(counterspy::Suspects(2));
}

}
