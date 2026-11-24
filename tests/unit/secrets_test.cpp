#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/dice.h"
#include "game/espionage.h"
#include "game/opinion.h"
#include "game/secrets.h"
#include "game/serialization.h"
#include "game/spy_ops.h"

#include <memory>
#include <string>

namespace {
    struct s_secrets_scope {
        test::s_saved_value<bool> enabled{secrets::enabled};
        test::s_saved_value<bool> smear{secrets::smear};
        test::s_saved_value<int32_t> cost{secrets::smear_cost};
        test::s_saved_value<bool> network{espionage::network};
        test::s_saved_value<bool> ci{espionage::counterintelligence};
        test::s_saved_value<int32_t> operations{spy_ops::operations};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<uint8_t> roll_kind{dice::roll_kind};

        s_secrets_scope() {
            secrets::Clear();
            espionage::Clear();
            spy_ops::Clear();
            opinion::Clear();
            secrets::enabled = true;
            secrets::smear = true;
            secrets::smear_cost = 20;
            espionage::network = true;
            espionage::counterintelligence = false;
            spy_ops::operations = 63;
            opinion::enabled = true;
            dice::roll_kind = dice::ROLL_TWO_D10;
        }
        ~s_secrets_scope() {
            secrets::Clear();
            espionage::Clear();
            spy_ops::Clear();
            opinion::Clear();
        }
    };

    const int32_t TRIUMPH[2] = {10, 10};
    const int32_t CATASTROPHE[2] = {1, 1};

    // Four computer empires that know each other. Player 0 plans a sneak attack on player 2.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 4;
        MOX::stardate = 35101;
        for (int i = 0; i < 4; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[i].personality = PLAYER_PERSONALITY_ERRATIC;
            state->players[i].sneak_attack_planet = -1;
            state->players[i].sneak_attack_player = -1;
            for (int j = 0; j < 4; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
        state->players[0].sneak_attack_planet = 5;
        state->players[0].sneak_attack_player = 2;
    }
} // namespace

DOCTEST_TEST_SUITE("secrets and smear") {

DOCTEST_TEST_CASE("selling a secret: the victim pays and learns the plan; the surprise is lost") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_CHECK_FALSE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));  // known already
    DOCTEST_CHECK(secrets::Still_True(secrets::Slots()[0]));
    DOCTEST_CHECK_FALSE(secrets::Sell(0));  // the victim cannot pay
    state.players[2].bc = 80;
    DOCTEST_REQUIRE(secrets::Sell(0));
    DOCTEST_CHECK(state.players[2].bc == 80 - secrets::SALE_PRICE);
    DOCTEST_CHECK(state.players[1].bc == secrets::SALE_PRICE);
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == -1);
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_PLOTTED_AGAINST_US) < 0);
    DOCTEST_CHECK(secrets::Empty());
}

DOCTEST_TEST_CASE("blackmail: the owner pays for silence, or refuses and the secret is made public") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].bc = 1000;
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_REQUIRE(secrets::Blackmail(0));
    DOCTEST_CHECK(state.players[0].bc == 1000 - 1000 * secrets::BLACKMAIL_PERCENT / 100);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_BLACKMAILED_US) == secrets::BLACKMAIL_OPINION);
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == 5);  // silence kept
    state.players[0].bc = 50;  // too poor: refuses
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_CHECK_FALSE(secrets::Blackmail(0));
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == -1);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_EXPOSED_US) == secrets::EXPOSED_OPINION);
    DOCTEST_CHECK(opinion::Modifier(3, 0, opinion::REASON_BAD_RUMOURS) == secrets::RUMOUR_OPINION);
}

DOCTEST_TEST_CASE("a bribed governor is a secret too; it goes stale when the bribe ends") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.leaders[4].player_index = 2;
    *spy_ops::Bribed_Slot(4) = 0;
    DOCTEST_REQUIRE(secrets::Learn(3, secrets::KIND_BRIBE, 0, 2, 4));
    DOCTEST_CHECK(secrets::Still_True(secrets::Slots()[0]));
    secrets::Make_Public(0);
    DOCTEST_CHECK(spy_ops::Bribed_By(4) == -1);
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_BRIBED_OUR_LEADER) < 0);
    DOCTEST_REQUIRE(secrets::Learn(3, secrets::KIND_PLAN, 0, 2, -1));
    MOX::stardate = 35101 + secrets::SECRET_TURNS;
    DOCTEST_CHECK_FALSE(secrets::Still_True(secrets::Slots()[0]));
}

DOCTEST_TEST_CASE("smear: believed, the others think worse of the target; exposed, the target remembers") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].spies[2] = 4;
    espionage::State()->points[1][2] = 50;
    DOCTEST_REQUIRE(secrets::Smear_With_Dice(1, 2, TRIUMPH));
    DOCTEST_CHECK(espionage::Points(1, 2) == 30);
    DOCTEST_CHECK(opinion::Modifier(0, 2, opinion::REASON_BAD_RUMOURS) == secrets::RUMOUR_OPINION);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_BAD_RUMOURS) == 0);
    DOCTEST_CHECK_FALSE(secrets::Smear_With_Dice(1, 2, CATASTROPHE));
    DOCTEST_CHECK(state.players[1].spies[2] == 3);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_SMEARED_US) == secrets::SMEARED_OPINION);
    DOCTEST_CHECK_FALSE(secrets::Smear_With_Dice(1, 2, TRIUMPH));  // 10 points left
}

DOCTEST_TEST_CASE("off: nothing is learned, nothing smeared") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    secrets::enabled = false;
    secrets::smear = false;
    state.players[1].spies[0] = 5;
    DOCTEST_CHECK_FALSE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    state.players[1].spies[2] = 4;
    espionage::State()->points[1][2] = 50;
    DOCTEST_CHECK_FALSE(secrets::Smear_With_Dice(1, 2, TRIUMPH));
    secrets::Turn();
    DOCTEST_CHECK(secrets::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    auto buffer = std::make_unique<uint8_t[]>(secrets::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), secrets::Save_Size(), "test overflow");
    secrets::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == secrets::Save_Size());
    secrets::Clear();
    SERIAL::Reader_ reader(buffer.get(), secrets::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(secrets::Parse_Save(&reader, &error));
    DOCTEST_CHECK(secrets::Empty());
    secrets::Commit_Save();
    DOCTEST_CHECK(secrets::Slots()[0].kind == secrets::KIND_PLAN);
    buffer[2] = 9;  // an unknown kind
    SERIAL::Reader_ bad(buffer.get(), secrets::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(secrets::Parse_Save(&bad, &error));
    DOCTEST_CHECK(secrets::Slots()[0].kind == secrets::KIND_PLAN);
}

}
