#pragma once

#include "game/consts.h"

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Ultimatum with a deadline (docs/LocalLLM/mechanics.md 3.5.3; README.md 10.9, wave 6; ab-review.md 4.1).
// The design row, in its own words:
//   "Ультиматум со сроком | невыполнение за N ходов — законный повод для войны | `Get_Demand_Response`
//    (`diplomac.cpp:2799`), `threats[]`"
// Built literally: a demand made by a computer empire carries a deadline of DEADLINE_TURNS (the N). If the
// demanded empire has not complied by then, the demander holds a lawful cause for war (a casus belli) for
// CAUSE_TURNS; a war it declares with the cause costs it less with the others than a war without one
// (diplomac.cpp Break_Treaties: no oathbreaker stain, no lost trust among the others; opinion.cpp
// On_Sneak_Attack: no "attacked us without declaring war"). A computer demander that is not too weary
// declares that war at once when the deadline passes.
// Where an ultimatum starts:
// - the original game's own demands (diplomac.cpp NPC_Sneak_Attacks: a demand, an ultimatum to a human, and
//   the demand that follows a second threat): On_Demand, amount unknown (0). The human accepts through the
//   diplomacy screen (dip_scrn_main.cpp Accepted_Npc_Demand_Resolution calls On_Demand_Met);
// - a computer's own demand for tribute from a weaker computer neighbour it thinks ill of (Turn, every PERIOD
//   turns on PHASE): the sum in BC; the victim pays by its character while the time lasts.
// Off by default (diplomacy ultimatums); with the flag off nothing here draws from game_random, nothing is kept
// and every hook returns at once. Departure from the row: `Get_Demand_Response` answers a demand made BY a
// human TO a computer, and the original has no demand of a computer on a computer, so the computers' own
// ultimatums use their own rule of answering (Pays_), not that function.
namespace ultimatums {

    extern bool enabled;  // diplomacy ultimatums

    constexpr int32_t MAX_RECORDS = 16;
    constexpr int32_t DEADLINE_TURNS = 10;    // the N of the row
    constexpr int32_t CAUSE_TURNS = 20;       // a lawful cause lasts this long (if the demander is too weary to use it)
    constexpr int32_t PERIOD = 10;            // computers make their own demands on stardate % PERIOD == PHASE
    constexpr int32_t PHASE = 4;
    constexpr int32_t DEMAND_PERCENT = 15;    // of the victim's treasury
    constexpr int32_t DEMAND_MIN = 50;        // BC
    constexpr int32_t STRENGTH_RATIO = 2;     // the demander's fleet against the victim's, at least
    constexpr int32_t BOW_RATIO = 3;          // a fleet this much stronger makes even a proud victim pay
    constexpr int32_t PAID_OPINION = -15;     // REASON_BLACKMAILED_US: the victim remembers the extortion

    enum e_state : uint8_t {
        STATE_OPEN = 1,   // the deadline runs
        STATE_CAUSE = 2,  // the deadline passed unmet: a lawful cause for war
    };

    struct s_record {
        int8_t demander = -1;
        int8_t victim = -1;
        uint8_t state = 0;
        int32_t issued = 0;
        int32_t deadline = 0;
        int32_t amount = 0;   // BC demanded; 0 for a demand of the original game (its content is not known here)
    };

    struct s_state {
        int32_t count = 0;
        s_record record[MAX_RECORDS];
    };
    s_state* State();

    // The record of the pair, or -1.
    int32_t Find(int16_t demander, int16_t victim);

    // `demander` holds a lawful cause for war against `victim` now (false with the flag off).
    bool Has_Cause(int16_t demander, int16_t victim);

    // Opens an ultimatum of `demander` (a computer) to `victim`; amount 0 = a demand of the original game.
    // False when it cannot (flag off, a human demander, a record of the pair exists, the table is full).
    bool Issue(int16_t demander, int16_t victim, int32_t amount);

    // The original game made a demand of `victim` (diplomac.cpp): opens an ultimatum without a sum.
    void On_Demand(int16_t demander, int16_t victim);
    // `victim` accepted the demand of `demander` (dip_scrn_main.cpp): the ultimatum is met and closed.
    void On_Demand_Met(int16_t demander, int16_t victim);

    // Once a turn (diplomac.cpp End_Of_Turn_Diplomacy_Adjustments): the records run (a computer victim pays by its
    // character, a deadline passes into a cause, a computer demander declares its lawful war, old causes lapse) and,
    // on PHASE, computers make their own demands.
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'ULTM' (game/rules_sections.h): payload version 1, then the count (uint8), per record demander
    // and victim (int8), state (uint8), issued, deadline and amount (int32).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace ultimatums
