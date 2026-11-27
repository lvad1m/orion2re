#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/council_ai.h"
#include "game/random.h"

namespace {
    // Player 0 has population 100 (10 votes), player 1 60 (6), players 2 and 3 30 and 20 (3, 2):
    // the nominees are 0 and 1; 21 votes in all, 14 needed.
    void Setup_Council_() {
        MOX::NUM_PLAYERS = 4;
        MOX::stardate = 35200;
        const int16_t population[4] = {100, 60, 30, 20};
        for (int i = 0; i < 4; ++i) {
            MOX::player[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            MOX::player[i].total_pop = population[i];
            MOX::player[i].council_voted_for = -1;
        }
        diplodef::council_winner = -1;
    }

    void Treaty_(int voter, int candidate, int8_t treaty) {
        MOX::player[voter].treaty[candidate] = treaty;
    }
} // namespace

DOCTEST_TEST_SUITE("council without humans") {

DOCTEST_TEST_CASE("allies elect a nominee with two thirds of the votes") {
    test::s_game_state state;
    Setup_Council_();
    for (int member = 2; member < 4; ++member) {
        Treaty_(member, 0, 2);  // an ally always votes for its ally (Vote_Check: 200)
        Treaty_(member, 1, 4);  // a war enemy never
    }
    DOCTEST_CHECK(council::Hold_Council_Without_Humans() == 0);
    DOCTEST_CHECK(council::council_nominee_[0] == 0);
    DOCTEST_CHECK(council::council_nominee_[1] == 1);
    DOCTEST_CHECK(council::council_total_votes == 21);
    DOCTEST_CHECK(council::council_nominee_vote_total[0] == 10 + 3 + 2);
    DOCTEST_CHECK(council::council_nominee_vote_total[1] == 6);
    DOCTEST_CHECK(diplodef::council_winner == 0);
    DOCTEST_CHECK(MOX::player[0].won_council_vote == 1);
    DOCTEST_CHECK(MOX::player[2].council_voted_for == 0);
    diplodef::council_winner = -1;
}

DOCTEST_TEST_CASE("without two thirds nobody is elected") {
    test::s_game_state state;
    Setup_Council_();
    for (int member = 2; member < 4; ++member) {
        Treaty_(member, 0, 4);  // at war with both nominees: abstain
        Treaty_(member, 1, 4);
    }
    DOCTEST_CHECK(council::Hold_Council_Without_Humans() == -1);
    DOCTEST_CHECK(council::council_nominee_vote_total[0] == 10);
    DOCTEST_CHECK(diplodef::council_winner == -1);
    DOCTEST_CHECK(MOX::player[2].council_voted_for == -1);
}

// Iteration 7, piece A: Vote_Score is Vote_Check's score without the draw; Vote_Check keeps its one draw.
DOCTEST_TEST_CASE("Vote_Score is the pure score; Vote_Check draws once and compares") {
    test::s_game_state state;
    Setup_Council_();
    Treaty_(2, 0, 2);   // peace: always
    Treaty_(2, 1, 4);   // war: never
    DOCTEST_CHECK(council::Vote_Score(0, 2, 1) == 200);
    DOCTEST_CHECK(council::Vote_Score(1, 2, 0) == -1);
    Treaty_(3, 0, 1);   // non-aggression pact: +50 on the base
    const int16_t base = council::Vote_Score(0, 3, 1);
    Treaty_(3, 1, 4);   // allied-or-at-war with the opponent: +100
    DOCTEST_CHECK(council::Vote_Score(0, 3, 1) == base + 100);

    const uint32_t seed = game_random::Get_Random_Seed();
    for (int voter = 2; voter < 4; ++voter) {
        for (int candidate = 0; candidate < 2; ++candidate) {
            const int opponent = 1 - candidate;
            game_random::Set_Random_Seed(777);
            const int16_t score = council::Vote_Score((int16_t)candidate, (int16_t)voter, (int16_t)opponent);
            DOCTEST_CHECK(game_random::Get_Random_Seed() == 777);  // no draw
            const int32_t draw = (int32_t)game_random::Random(200);
            const uint32_t after = game_random::Get_Random_Seed();
            game_random::Set_Random_Seed(777);
            const bool cast = council::Vote_Check((int16_t)candidate, (int16_t)voter, (int16_t)opponent);
            DOCTEST_CHECK(cast == (draw <= score));
            DOCTEST_CHECK(game_random::Get_Random_Seed() == after);  // exactly one draw
        }
    }
    game_random::Set_Random_Seed(seed);
}

DOCTEST_TEST_CASE("the predicted nominees are the council's; the session window") {
    test::s_game_state state;
    Setup_Council_();
    uint8_t nominees[2];
    DOCTEST_REQUIRE(council::Predict_Nominees(nominees));
    DOCTEST_CHECK(nominees[0] == 0);
    DOCTEST_CHECK(nominees[1] == 1);
    const int16_t flag = diplodef::COUNCIL_FLAG;
    const int16_t next = diplodef::next_council_meeting_turn;
    diplodef::COUNCIL_FLAG = 1;
    MOX::stardate = 35200;  // turn offset 200
    diplodef::next_council_meeting_turn = 210;
    DOCTEST_CHECK(council::Session_Due_Within(10));
    DOCTEST_CHECK_FALSE(council::Session_Due_Within(9));
    diplodef::next_council_meeting_turn = 200;  // today: no longer "ahead"
    DOCTEST_CHECK_FALSE(council::Session_Due_Within(10));
    diplodef::next_council_meeting_turn = 260;
    DOCTEST_CHECK_FALSE(council::Session_Due_Within(10));
    diplodef::COUNCIL_FLAG = 0;  // no session yet and the delay is over: it sits now
    diplodef::next_council_meeting_turn = 210;
    DOCTEST_CHECK_FALSE(council::Session_Due_Within(10));
    MOX::stardate = 35020;  // before the first session: it waits for the delay (25)
    DOCTEST_CHECK(council::Session_Due_Within(10));
    DOCTEST_CHECK_FALSE(council::Session_Due_Within(4));
    MOX::player[3].eliminated = 1;
    MOX::player[2].eliminated = 1;  // two empires left: no council
    DOCTEST_CHECK_FALSE(council::Session_Due_Within(10));
    MOX::player[3].eliminated = 0;
    MOX::player[2].eliminated = 0;
    diplodef::COUNCIL_FLAG = flag;
    diplodef::next_council_meeting_turn = next;
}

} // DOCTEST_TEST_SUITE
