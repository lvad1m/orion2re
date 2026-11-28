#pragma once

#include "game/choices.h"

#include <cstdint>

// The ambitious governor and council bribery (docs/LocalLLM/mechanics.md 3.5.6 and 3.5.3; README.md
// 10.9, the A and B scope).
// - An ambitious governor: a restless, capable governor of a computer empire (loyalty 20 or less, level 3
//   or more, not at the capital's star) may defect every PERIOD turns. It goes with its colonies to a
//   neighbouring computer empire, or, with no neighbour in contact, deserts to the hire list.
// - Council bribery: a computer nominee with spare money buys the votes of the computer voters that do not
//   hate it, or spends a secret it holds about a voter to blackmail the vote; the other nominee, if it has
//   spies in the briber, may learn of it. The bribes live for one vote only (nothing is saved).
// Both flags off by default; with them off nothing here draws from game_random and nothing is kept.
namespace ambition {

    extern bool ambitious_governors;  // loyalty ambitious_governors (needs loyalty leaders)
    extern bool council_bribery;      // diplomacy council_bribery

    // Programming choices (README.md 10.9: tuned on the big bench).
    constexpr int32_t PERIOD = 10;                 // governors are looked at on stardate % PERIOD == PHASE
    constexpr int32_t PHASE = 7;
    constexpr int32_t MAX_LOYALTY = 20;            // a governor this restless or worse may defect
    constexpr int32_t MIN_LEVEL = 3;
    constexpr int32_t DEFECT_PERCENT = 20;
    constexpr int32_t DISLIKE_RELATIONS = -20;     // a neighbour thought of this badly is preferred as a target
    constexpr int32_t DEFECTION_OPINION = -20;     // REASON_PLOTTED_AGAINST_US, the old owner toward the new one
    // Iteration 1, decision 4 (docs/LocalLLM/iterations.md): "Оживить редкие механики: подкуп совета, вассалы,
    // тайны, события дипломатии." Only the triggers of council bribery are loosened, not what it does: a
    // nominee with half the money (200 -> 100 BC) buys, keeps a smaller reserve (100 -> 50 BC) and so pays
    // more voters, and voters that think worse of it (-30 -> -60) are still bought. The price is unchanged.
    constexpr int32_t MIN_BC_TO_BRIBE = 100;       // a nominee needs this much to start buying votes
    constexpr int32_t BRIBE_PRICE = 25;            // BC to each bought voter
    constexpr int32_t BRIBE_RESERVE = 50;          // the briber keeps this many BC
    constexpr int32_t BRIBE_MIN_RELATIONS = -60;   // only voters thinking of the nominee at least this well are bought
    constexpr int32_t BRIBE_BIAS = 100;
    constexpr int32_t BLACKMAIL_BIAS = 150;
    constexpr int32_t EXPOSURE_PERCENT = 30;
    constexpr int32_t EXPOSURE_OPINION = -20;      // REASON_PLOTTED_AGAINST_US, the other nominee toward the briber

    // The governor `leader` defects (ambitious_governors on, a computer owner, the conditions above hold) when
    // `roll` (0-99) is under DEFECT_PERCENT. True when it left.
    bool Try_Defect(int16_t leader, int32_t roll);
    // Whether `leader` is a governor that may defect at all (flag, owner, loyalty, level, post, not the capital).
    bool Is_Restless(int16_t leader);
    // The computer empire `leader`'s owner would defect to, -1 when none.
    int16_t Pick_Neighbour(int16_t leader);

    // Once a turn (diplomac.cpp End_Of_Turn_Diplomacy_Adjustments): on PHASE, restless governors roll.
    void Turn();

    // Council: the two nominees are fixed, the votes are about to be cast (council.cpp Council_Votes).
    void Before_Council_Vote(int16_t nominee0, int16_t nominee1);
    // Added to a voter's score for a candidate in Vote_Check (0 with the flag off or nothing bought).
    int32_t Council_Vote_Bias(int16_t voter, int16_t candidate);
    // The vote is over: the bribes are forgotten.
    void After_Council_Vote();

    // R2, piece C (human-ui.md section 5): with events human_choices on, a computer nominee that would bribe a
    // computer voter by the rules above (Before_Council_Vote: the same money, reserve and opinion thresholds, no
    // secret to blackmail with) offers the human voter the same BRIBE_PRICE. Called by Council_Votes right after
    // Before_Council_Vote, so the computers' purchases and the exposure roll are as before; nothing is drawn. The
    // window opens at once (choices::Ask_Now). Accepting pays the human and binds its council vote: Council_Player_Vote
    // casts it for the briber without asking (Take_Human_Vote_Promise).
    constexpr uint8_t CHOICE_BRIBE = 1;
    // The offer the nominees would make to `human`, if any (player human, target the briber, other the rival nominee,
    // value the price).
    bool Human_Offer(int16_t human, int16_t nominee0, int16_t nominee1, choices::s_choice* offer);
    void Offer_To_Human(int16_t human, int16_t nominee0, int16_t nominee1);
    // The nominee the human has sold its vote to in this session, -1 if none; reading it forgets it.
    int16_t Take_Human_Vote_Promise();
    bool Choice_Text(const choices::s_choice& choice, choices::s_text* text);
    void Choice_Apply(const choices::s_choice& choice, int32_t option);

} // namespace ambition
