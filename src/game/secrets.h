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
//
// Wave 6 (ab-review 2.1), mechanics.md 3.5.2 "Тайны как валюта": "Шпионы добывают тайны: запланированная
// внезапная атака, тайный договор, подкупленный губернатор, применение биологического оружия,
// спрятанный флот." and the table "Предупредить жертву - жертва должна вам услугу; Шантажировать
// владельца - дань, технология или голос в совете за молчание; Раскрыть публично (GNN, совет) - удар
// по репутации владельца у всех; Продать третьей стороне - BC или услуга; Придержать - тайна
// устаревает". Built now: the kinds a secret treaty (covenants.h) and a use of biological weapons
// (the deed is noted by On_Bio_Weapon), blackmail for tribute, a technology or a council vote
// (ambition.h spends the secret as a vote), and the sale to a third party.
namespace secrets {

    extern bool enabled;          // spy secrets
    extern bool smear;            // spy smear
    extern int32_t smear_cost;    // spy smear_cost: network points

    enum e_kind : uint8_t {
        KIND_NONE = 0,
        KIND_PLAN = 1,     // the owner plans to attack the victim
        KIND_BRIBE = 2,    // the owner bribed a governor of the victim (value: the leader)
        KIND_TREATY = 3,   // the owner guarantees `value` against the victim in a secret clause (covenants.h)
        KIND_BIOWEAPON = 4,  // the owner used biological weapons against the victim's colony
        KIND_COUNT = 5,
    };

    // What a holder demands of the owner for its silence.
    enum e_demand : uint8_t {
        DEMAND_TRIBUTE = 0,  // BLACKMAIL_PERCENT of the owner's treasury
        DEMAND_TECH = 1,     // the cheapest technology the owner has and the holder lacks
        DEMAND_VOTE = 2,     // the owner's vote in the council (ambition.h spends the secret at the vote)
    };

    // Programming choices (README.md 10.9: tuned on the big bench).
    constexpr int32_t MAX_SECRETS = 32;
    // Iteration 1, decision 4 (docs/LocalLLM/iterations.md): "Оживить редкие механики: подкуп совета, вассалы,
    // тайны, события дипломатии." Only the learning rates are doubled (20 -> 40 per spy, 100 -> 200 per
    // resident), so a spy finds a secret twice as fast; what a secret is and how it is used is unchanged.
    constexpr int32_t LEARN_PER_SPY = 40;        // per mille a turn for each spy (at most 5)
    constexpr int32_t LEARN_PER_RESIDENT = 200;
    constexpr int32_t SECRET_TURNS = 30;
    constexpr int32_t SALE_PRICE = 50;           // BC the victim pays for the secret
    constexpr int32_t BLACKMAIL_PERCENT = 15;    // of the owner's treasury
    constexpr int32_t BLACKMAIL_MIN_BC = 100;
    constexpr int32_t BLACKMAIL_REFUSE_RELATIONS = -50;  // an owner this hostile refuses to pay
    constexpr int32_t BLACKMAIL_OPINION = -25;
    constexpr int32_t THIRD_PARTY_MAX_RELATIONS = -20;  // a third party buys a secret about an empire it thinks this ill of
    constexpr int32_t EXPOSED_OPINION = -30;     // the owner's memory of whoever made it public
    constexpr int32_t RUMOUR_OPINION = -10;      // each other empire's memory of a smeared or exposed empire
    constexpr int32_t SMEARED_OPINION = -30;
    constexpr int32_t PERIOD = 10;

    struct s_secret {
        uint8_t kind = KIND_NONE;
        int8_t owner = -1;      // whose secret it is
        int8_t victim = -1;     // whom it harms
        int8_t holder = -1;     // who knows it
        int16_t value = -1;     // KIND_BRIBE: the bribed leader; KIND_TREATY: the protege
        int32_t learned = 0;
        uint8_t demanded = 0;   // 1: the owner pays with its council vote; the secret waits for the vote
    };

    // The last use of biological weapons by each empire (what KIND_BIOWEAPON secrets are about).
    struct s_bio {
        int32_t turn = 0;       // 0: none
        int8_t victim = -1;     // the owner of the colony hit
    };
    s_bio* Bio();               // MAX_PLAYERS entries
    // `owner` used biological weapons at `star` (combat_lifecycle.cpp Apply_Bio_Weapon_Penalty): noted
    // when a colony of another empire is there. Nothing with the flag off.
    void On_Bio_Weapon(int16_t owner, int16_t star);

    const s_secret* Secrets(int32_t* count);
    // `holder` learns a secret; false when it knows it already or the store is full.
    bool Learn(int16_t holder, e_kind kind, int16_t owner, int16_t victim, int16_t value);
    // The secret still holds (the plan stands, the bribe lasts) and is not too old.
    bool Still_True(const s_secret& secret);

    // Uses of a secret (Turn picks one by the holder's character; for the tests).
    bool Sell(int32_t index);         // to the victim, for SALE_PRICE BC
    bool Blackmail(int32_t index);    // the owner pays tribute for silence, or refuses and the holder makes it public
    // The same with the demand chosen: a technology (falls back to tribute when the owner has none to
    // give), or a council vote (needs `diplomacy council_bribery`, else tribute; the secret is kept).
    bool Blackmail_For(int32_t index, e_demand demand);
    void Make_Public(int32_t index);
    // Sells the secret to a third empire (not the owner, the victim or the holder) that thinks ill of
    // the owner: for SALE_PRICE BC, or for a favour when it is poor (intrigue.h). The buyer now holds it.
    bool Sell_To_Third(int32_t index, int32_t* new_index = nullptr);

    // A smear campaign by `attacker` against `target` (rolls the dice; the tests give them).
    bool Smear(int16_t attacker, int16_t target);
    bool Smear_With_Dice(int16_t attacker, int16_t target, const int32_t* dice_values);

    // Once a turn at the end of the spy step (spy.cpp, after intrigue::Turn): spies learn secrets;
    // every PERIOD turns the computers use theirs and run smear campaigns.
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'SECR' (game/rules_sections.h): payload version 2, then MAX_SECRETS entries of
    // kind (uint8), owner, victim, holder (int8), value (int16), learned (int32), demanded (uint8);
    // then per player the stardate of its last biological attack (int32) and its victim (int8).
    // Version 1 (entries without `demanded`, no deeds) is still read.
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

    s_secret* Slots();  // MAX_SECRETS slots (secrets_state.cpp)

} // namespace secrets
