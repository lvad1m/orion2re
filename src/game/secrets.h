#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Secrets as currency and smear campaigns (docs/LocalLLM/mechanics.md 3.5.2 and 3.2; README.md 10.9,
// the A and B scope).
// - Secrets: an empire's spies in another may learn its secrets - a planned attack on a third empire
//   (intrigue.h), a governor of a third empire it bribed (spy_ops.h). A computer holder then sells the
//   secret to the victim (honorable and pacifist holders), blackmails the owner for silence (ruthless,
//   aggressive, xenophobic: a share of its treasury) or makes it public (the others, and whenever a
//   blackmail is refused); a secret goes stale when the plan or the bribe ends, and after
//   SECRET_TURNS. Warning the victim for a favour is intrigue.h's.
// - Smear: an empire with spies and network points in a rival or an enemy spreads bad rumours about
//   it - a dice check like a false flag; believed, every other computer empire that knows the target
//   thinks worse of it; exposed, the target remembers who smeared it.
// Both off by default; game_random is drawn only by the parts that are on (the learning rolls, the
// smear's dice).
namespace secrets {

    extern bool enabled;          // spy secrets
    extern bool smear;            // spy smear
    extern int32_t smear_cost;    // spy smear_cost: network points

    enum e_kind : uint8_t {
        KIND_NONE = 0,
        KIND_PLAN = 1,     // the owner plans to attack the victim
        KIND_BRIBE = 2,    // the owner bribed a governor of the victim (value: the leader)
        KIND_COUNT = 3,
    };

    // Programming choices (README.md 10.9: tuned on the big bench).
    constexpr int32_t MAX_SECRETS = 32;
    constexpr int32_t LEARN_PER_SPY = 20;        // per mille a turn for each spy (at most 5)
    constexpr int32_t LEARN_PER_RESIDENT = 100;
    constexpr int32_t SECRET_TURNS = 30;
    constexpr int32_t SALE_PRICE = 50;           // BC the victim pays for the secret
    constexpr int32_t BLACKMAIL_PERCENT = 15;    // of the owner's treasury
    constexpr int32_t BLACKMAIL_MIN_BC = 100;
    constexpr int32_t BLACKMAIL_REFUSE_RELATIONS = -50;  // an owner this hostile refuses to pay
    constexpr int32_t BLACKMAIL_OPINION = -25;
    constexpr int32_t EXPOSED_OPINION = -30;     // the owner's memory of whoever made it public
    constexpr int32_t RUMOUR_OPINION = -10;      // each other empire's memory of a smeared or exposed empire
    constexpr int32_t SMEARED_OPINION = -30;
    constexpr int32_t PERIOD = 10;

    struct s_secret {
        uint8_t kind = KIND_NONE;
        int8_t owner = -1;      // whose secret it is
        int8_t victim = -1;     // whom it harms
        int8_t holder = -1;     // who knows it
        int16_t value = -1;     // KIND_BRIBE: the bribed leader
        int32_t learned = 0;
    };

    const s_secret* Secrets(int32_t* count);
    // `holder` learns a secret; false when it knows it already or the store is full.
    bool Learn(int16_t holder, e_kind kind, int16_t owner, int16_t victim, int16_t value);
    // The secret still holds (the plan stands, the bribe lasts) and is not too old.
    bool Still_True(const s_secret& secret);

    // Uses of a secret (Turn picks one by the holder's character; for the tests).
    bool Sell(int32_t index);         // to the victim, for SALE_PRICE BC
    bool Blackmail(int32_t index);    // the owner pays for silence, or refuses and the holder makes it public
    void Make_Public(int32_t index);

    // A smear campaign by `attacker` against `target` (rolls the dice; the tests give them).
    bool Smear(int16_t attacker, int16_t target);
    bool Smear_With_Dice(int16_t attacker, int16_t target, const int32_t* dice_values);

    // Once a turn at the end of the spy step (spy.cpp, after intrigue::Turn): spies learn secrets;
    // every PERIOD turns the computers use theirs and run smear campaigns.
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'SECR' (game/rules_sections.h): payload version 1, then MAX_SECRETS entries of
    // kind (uint8), owner, victim, holder (int8), value (int16), learned (int32).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

    s_secret* Slots();  // MAX_SECRETS slots (secrets_state.cpp)

} // namespace secrets
