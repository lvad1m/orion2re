#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/beliefs.h"
#include "game/dice.h"
#include "game/espionage.h"
#include "game/intrigue.h"
#include "game/opinion.h"
#include "game/serialization.h"

#include <memory>

namespace {
    struct s_beliefs_scope {
        test::s_saved_value<bool> model{beliefs::neighbour_model};
        test::s_saved_value<bool> disinformation{beliefs::disinformation};
        test::s_saved_value<int32_t> cost{beliefs::disinformation_cost};
        test::s_saved_value<bool> fate{intrigue::spy_fate};
        test::s_saved_value<bool> network{espionage::network};
        test::s_saved_value<bool> ci{espionage::counterintelligence};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<uint8_t> roll_kind{dice::roll_kind};

        s_beliefs_scope() {
            beliefs::Clear();
            intrigue::Clear();
            espionage::Clear();
            opinion::Clear();
            beliefs::neighbour_model = true;
            beliefs::disinformation = true;
            beliefs::disinformation_cost = 30;
            intrigue::spy_fate = true;
            espionage::network = true;
            espionage::counterintelligence = false;
            opinion::enabled = true;
            dice::roll_kind = dice::ROLL_TWO_D10;
        }
        ~s_beliefs_scope() {
            beliefs::Clear();
            intrigue::Clear();
            espionage::Clear();
            opinion::Clear();
        }
    };

    const int32_t TRIUMPH[2] = {10, 10};
    const int32_t CATASTROPHE[2] = {1, 1};

    // Three computer empires that know each other; player 0 is honorable, 1 aggressive, 2 erratic.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 3;
        MOX::NUM_COLONIES = 0;
        MOX::stardate = 35001;
        const uint8_t personalities[3] = {PLAYER_PERSONALITY_HONORABLE, PLAYER_PERSONALITY_AGGRESSIVE, PLAYER_PERSONALITY_ERRATIC};
        for (int i = 0; i < 3; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[i].personality = personalities[i];
            state->players[i].sneak_attack_planet = -1;
            state->players[i].sneak_attack_player = -1;
            for (int j = 0; j < 3; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
    }
} // namespace

DOCTEST_TEST_SUITE("beliefs") {

DOCTEST_TEST_CASE("priors from personality; 50 percent with the model off") {
    s_beliefs_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK(beliefs::Percent(2, 0, beliefs::BELIEF_FAITHFUL) > 70);  // honorable
    DOCTEST_CHECK(beliefs::Percent(0, 2, beliefs::BELIEF_FAITHFUL) < 30);  // erratic
    DOCTEST_CHECK(beliefs::Percent(0, 1, beliefs::BELIEF_HOSTILE) > beliefs::Percent(1, 0, beliefs::BELIEF_HOSTILE));
    DOCTEST_CHECK_FALSE(beliefs::Believes_Faithful(0, 2));
    beliefs::neighbour_model = false;
    DOCTEST_CHECK(beliefs::Percent(0, 2, beliefs::BELIEF_FAITHFUL) == 50);
    DOCTEST_CHECK(beliefs::Believes_Faithful(0, 2));
    DOCTEST_CHECK(beliefs::Trusts(0, 2));
}

DOCTEST_TEST_CASE("opinion memories, broken treaties and lies are evidence; it fades") {
    s_beliefs_scope scope;
    test::s_game_state state;
    Setup_(&state);
    const int32_t before = beliefs::Percent(0, 1, beliefs::BELIEF_HOSTILE);
    opinion::Add(0, 1, opinion::REASON_SNEAK_ATTACK, -40);
    DOCTEST_CHECK(beliefs::Evidence(0, 1, beliefs::BELIEF_HOSTILE) == 3);
    DOCTEST_CHECK(beliefs::Evidence(0, 1, beliefs::BELIEF_FAITHFUL) == -2);
    DOCTEST_CHECK(beliefs::Percent(0, 1, beliefs::BELIEF_HOSTILE) > before);
    beliefs::On_Treaty_Broken(1, 0);
    DOCTEST_CHECK(beliefs::Evidence(0, 1, beliefs::BELIEF_FAITHFUL) == -5);
    DOCTEST_CHECK(beliefs::Evidence(2, 1, beliefs::BELIEF_FAITHFUL) == -1);
    beliefs::On_Lie(2, 1);
    DOCTEST_CHECK(beliefs::Evidence(2, 1, beliefs::BELIEF_HONEST) == -2);
    for (int i = 0; i < 30; ++i) beliefs::Observe(2, 0, beliefs::BELIEF_HONEST, 1);
    DOCTEST_CHECK(beliefs::Evidence(2, 0, beliefs::BELIEF_HONEST) == beliefs::EVIDENCE_LIMIT);
    MOX::stardate = 35010;
    beliefs::Turn();
    DOCTEST_CHECK(beliefs::Evidence(0, 1, beliefs::BELIEF_HOSTILE) == 2);
    DOCTEST_CHECK(beliefs::Evidence(0, 1, beliefs::BELIEF_FAITHFUL) == -4);
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;  // a human holds no beliefs
    beliefs::Observe(0, 2, beliefs::BELIEF_HONEST, 3);
    DOCTEST_CHECK(beliefs::Evidence(0, 2, beliefs::BELIEF_HONEST) == 0);
}

DOCTEST_TEST_CASE("a grudge at its limit is no new evidence") {
    s_beliefs_scope scope;
    test::s_game_state state;
    Setup_(&state);
    for (int i = 0; i < 10; ++i) opinion::Add(0, 1, opinion::REASON_CAUGHT_SPYING, -15);  // -15, -30, then held
    DOCTEST_CHECK(beliefs::Evidence(0, 1, beliefs::BELIEF_HOSTILE) == 2);
    DOCTEST_CHECK(beliefs::Evidence(0, 1, beliefs::BELIEF_HONEST) == -2);
}

DOCTEST_TEST_CASE("disinformation through a turned spy, by the dice, exposed; seen through") {
    s_beliefs_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK(beliefs::Distortion_Percent(1, 0) == 100);
    intrigue::State()->double_agent_until[0][1] = 35031;  // player 0 turned a spy of player 1
    DOCTEST_REQUIRE(beliefs::Plant_Disinformation(0, 1, beliefs::DISINFO_STRONGER));
    DOCTEST_CHECK(beliefs::Distortion_Percent(1, 0) == beliefs::DISINFO_STRONGER);

    state.players[2].spies[1] = 4;
    espionage::State()->points[2][1] = 70;
    DOCTEST_REQUIRE(beliefs::Plant_Disinformation_With_Dice(2, 1, beliefs::DISINFO_WEAKER, TRIUMPH));
    DOCTEST_CHECK(beliefs::Distortion_Percent(1, 2) == beliefs::DISINFO_WEAKER);
    DOCTEST_CHECK(espionage::Points(2, 1) == 40);
    DOCTEST_CHECK_FALSE(beliefs::Plant_Disinformation_With_Dice(2, 1, beliefs::DISINFO_WEAKER, CATASTROPHE));
    DOCTEST_CHECK(state.players[2].spies[1] == 3);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_PLOTTED_AGAINST_US) == beliefs::SEEN_THROUGH_OPINION);

    // Player 1's own spies at player 0 see through the lie (5 spies: 100 per mille a turn).
    state.players[1].spies[0] = 5;
    for (int turn = 0; turn < 200 && beliefs::Distortion_Percent(1, 0) != 100; ++turn) {
        MOX::stardate = 35001 + turn % 3;  // never a planting or fading turn
        beliefs::Turn();
    }
    DOCTEST_CHECK(beliefs::Distortion_Percent(1, 0) == 100);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_PLOTTED_AGAINST_US) == beliefs::SEEN_THROUGH_OPINION);
    // A lie runs out on its own.
    MOX::stardate = 35001 + beliefs::DISINFO_TURNS;
    DOCTEST_CHECK(beliefs::Distortion_Percent(1, 2) == 100);
}

DOCTEST_TEST_CASE("off: nothing is kept or distorted") {
    s_beliefs_scope scope;
    test::s_game_state state;
    Setup_(&state);
    beliefs::neighbour_model = false;
    beliefs::disinformation = false;
    opinion::Add(0, 1, opinion::REASON_SNEAK_ATTACK, -40);
    beliefs::On_Treaty_Broken(1, 0);
    intrigue::State()->double_agent_until[0][1] = 35031;
    DOCTEST_CHECK_FALSE(beliefs::Plant_Disinformation(0, 1, beliefs::DISINFO_STRONGER));
    MOX::stardate = 35010;
    beliefs::Turn();
    DOCTEST_CHECK(beliefs::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_beliefs_scope scope;
    test::s_game_state state;
    Setup_(&state);
    beliefs::Observe(0, 1, beliefs::BELIEF_HOSTILE, 4);
    beliefs::State()->distortion[1][0] = 150;
    beliefs::State()->distortion_until[1][0] = 35021;
    auto buffer = std::make_unique<uint8_t[]>(beliefs::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), beliefs::Save_Size(), "test overflow");
    beliefs::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == beliefs::Save_Size());
    beliefs::Clear();
    SERIAL::Reader_ reader(buffer.get(), beliefs::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(beliefs::Parse_Save(&reader, &error));
    DOCTEST_CHECK(beliefs::Empty());
    beliefs::Commit_Save();
    DOCTEST_CHECK(beliefs::Evidence(0, 1, beliefs::BELIEF_HOSTILE) == 4);
    DOCTEST_CHECK(beliefs::Distortion_Percent(1, 0) == 150);
    buffer[2 + (0 * 8 + 1) * 3] = 99;  // evidence beyond the limit
    SERIAL::Reader_ bad(buffer.get(), beliefs::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(beliefs::Parse_Save(&bad, &error));
    DOCTEST_CHECK(beliefs::Evidence(0, 1, beliefs::BELIEF_HOSTILE) == 4);
}

}
