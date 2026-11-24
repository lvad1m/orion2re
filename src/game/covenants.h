#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Secret clauses and dynastic marriages (docs/LocalLLM/mechanics.md 3.5.3; README.md 10.9, the A and B
// scope). A secret clause: guarantor A promises protected B to go to war with C if C attacks B; kept
// secret until C's spies learn of it or it is called on. A dynastic marriage: two friendly computer
// empires at peace marry; the bond keeps their opinion up and breaking it costs the breaker dearly.
// Only computer empires for now. Both flags off by default; with them off nothing here draws from
// game_random and nothing is kept.
namespace covenants {

    extern bool secret_clauses;  // diplomacy secret_clauses
    extern bool marriages;       // diplomacy marriages

    constexpr int32_t PERIOD = 10;                  // reviews on stardate % PERIOD == PHASE
    constexpr int32_t PHASE = 3;
    constexpr int32_t MAX_CLAUSES = 32;
    constexpr int32_t CLAUSE_TURNS = 50;            // a clause lasts this long
    constexpr int32_t SIGN_FRIENDSHIP = 20;         // A thinks of B at least this well
    constexpr int32_t SIGN_HATRED = -20;            // both think of C this ill or worse
    constexpr int32_t DEFENDED_OPINION = 15;        // REASON_DEFENDED_US when a clause is honoured
    constexpr int32_t BETRAYED_OPINION = -30;       // REASON_BETRAYED_A_SECRET_PACT when it is not
    constexpr int32_t REVEALED_OPINION = -30;       // REASON_PLOTTED_AGAINST_US, the aggressor toward both parties
    constexpr int32_t SPY_PER_MILLE = 20;           // a turn per spy of the aggressor in a party
    constexpr int32_t SPY_CAP = 5;
    constexpr int32_t MARRY_FRIENDSHIP = 30;        // each thinks of the other at least this well
    constexpr int32_t MARRY_PERCENT = 50;           // chance per candidate pair per review
    constexpr int32_t FAMILY_OPINION = 25;          // REASON_FAMILY_TIES, made and kept at least this high
    constexpr int32_t BROKEN_OPINION = -60;         // REASON_BROKE_ITS_WORD, the wronged side toward the breaker
    constexpr int32_t RUMOUR_OPINION = -10;         // REASON_BAD_RUMOURS, every other empire that knows the breaker

    struct s_clause {
        int8_t guarantor = -1;
        int8_t protege = -1;
        int8_t target = -1;     // the empire the clause is aimed at
        uint8_t revealed = 0;
        int32_t since = 0;
    };

    struct s_state {
        int32_t count = 0;
        s_clause clauses[MAX_CLAUSES];
        int8_t spouse[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
        int32_t married_since[8] = {};
    };
    s_state* State();

    // Clauses.
    // True when A may sign a clause pair with B against C (the conditions only, not the limits).
    bool Can_Sign(int16_t a, int16_t b, int16_t c);
    // Signs the pair (A guards B and B guards A against C); false when it cannot (limit, duplicate).
    bool Sign(int16_t a, int16_t b, int16_t c);
    // The clause index guarding `protege` against `target` by `guarantor`, -1 if none.
    int32_t Find_Clause(int16_t guarantor, int16_t protege, int16_t target);
    // True when a clause aimed at `target` involves `party` (as guarantor or protege).
    bool Clause_Against(int16_t party, int16_t target);
    // Reveals clause `index` (and its twin of the same pair): the aggressor remembers it against both.
    void Reveal(int32_t index, const char* how);
    // The aggressor's spies in a party learn of clause `index`: revealed when `roll` (0-999) is under
    // the chance; false when there are no spies, it is already revealed or the roll is too high.
    bool Spy_Reveal_With_Roll(int32_t index, int32_t roll);
    // Ends clause `index` (the later ones move up).
    void End_Clause(int32_t index, const char* why);

    // Marriages.
    int16_t Spouse(int16_t player);                 // -1 when none or the flag is off
    bool Can_Marry(int16_t a, int16_t b);           // the conditions, not the dice
    bool Marry(int16_t a, int16_t b);               // false when either is not free
    // Ends the marriage; `breaker` >= 0 is the side that broke it (the other is wronged), -1 for none.
    void Divorce(int16_t a, int16_t b, int16_t breaker, const char* why);

    // `aggressor` attacked `victim` (pacts.cpp On_Aggression, after the vassals): guarantors of clauses
    // aimed at the aggressor answer, and a marriage between them is broken.
    void On_Aggression(int16_t aggressor, int16_t victim);
    // A treaty between them was broken by `breaker` (diplomac.cpp Break_Treaties): a marriage ends.
    void On_Treaty_Broken(int16_t breaker, int16_t victim);

    // Once a turn (diplomac.cpp End_Of_Turn_Diplomacy_Adjustments): spies roll for clauses aimed at
    // their empire; every PERIOD turns on PHASE clauses expire and are signed, marriages are kept and made.
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'CVNT' (game/rules_sections.h): payload version 1, then the clause count (uint8) and
    // per clause guarantor, protege, target (int8), revealed (uint8), since (int32); then per player its
    // spouse (int8, -1 none) and the stardate of the marriage (int32).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace covenants
