#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/council_ai.h"

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

} // DOCTEST_TEST_SUITE
