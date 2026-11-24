#pragma once

#include <cstdint>
#include <string>

struct s_colony;

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Spy operations (docs/LocalLLM/mechanics.md 3.2.3 and 3.2.5.3; README.md 10.9, tier 3B): unrest,
// killing a scientist, bribery, framing an honest leader, data collection and a false-flag
// operation. Each spends network points (espionage.h) and is a dice check (dice.h). Enabled by the
// bits of spy operations; needs spy network. Off by default.
namespace spy_ops {

    extern int32_t operations;             // spy operations: e_op bits
    extern int32_t cost_unrest;            // spy op_cost_unrest (network points)
    extern int32_t cost_kill_scientist;    // spy op_cost_kill_scientist
    extern int32_t cost_bribery;           // spy op_cost_bribery
    extern int32_t cost_frame_leader;      // spy op_cost_frame_leader
    extern int32_t cost_data;              // spy op_cost_data
    extern int32_t cost_false_flag;        // spy op_cost_false_flag
    extern int32_t cost_assassination;     // spy op_cost_assassination (assassination and poison at a feast)
    extern int32_t cost_sabotage;          // spy op_cost_sabotage (the six sabotage kinds)
    extern bool inside_man;                // spy inside_man

    enum e_op : uint8_t {
        OP_UNREST = 0,
        OP_KILL_SCIENTIST = 1,
        OP_BRIBERY = 2,
        OP_FRAME_LEADER = 3,
        OP_DATA = 4,
        OP_FALSE_FLAG = 5,
        // Wave 2 (mechanics.md 3.2.2, 3.5.4).
        OP_ASSASSINATION = 6,
        OP_POISON_FEAST = 7,
        OP_SABOTAGE_SHIPYARD = 8,
        OP_SABOTAGE_RESEARCH = 9,
        OP_SABOTAGE_FOOD = 10,
        OP_SABOTAGE_TRADE = 11,
        OP_STRIKE = 12,
        OP_POISON_WATER = 13,
        OP_COUNT = 14,
    };

    constexpr int32_t UNREST_PER_OPERATION = 6;
    constexpr int32_t UNREST_MAX = 15;
    constexpr int32_t BRIBED_GOVERNOR_MORALE = -2;
    constexpr int32_t UNCOVER_PER_MILLE[3] = {20, 50, 100};  // by the target's counterintelligence mode
    constexpr int32_t AI_PERIOD = 10;
    constexpr int32_t ASSASSINATION_DIFFICULTY = 3;   // added to the check
    constexpr int32_t SABOTAGE_TURNS = 5;             // a stalled shipyard or factory, a famine
    constexpr int32_t FAMINE_FOOD2 = -2;              // half-food units per farmer while the famine lasts
    constexpr int32_t POISON_WATER_BIO_HITS = 2;      // biological hits on the poisoned colony
    constexpr int32_t POISON_WATER_MIN_POPS = 3;      // never wipes the colony out
    constexpr int32_t INSIDE_MAN_NUMERATOR = 1;       // the defenders' troops count 1/2
    constexpr int32_t INSIDE_MAN_DENOMINATOR = 2;

    bool Enabled(e_op op);
    int32_t Cost(e_op op);

    // The result of an operation (for the journal and the tests).
    enum e_result : uint8_t {
        RESULT_NOT_POSSIBLE = 0,   // off, too few points, no target for it
        RESULT_FAILED = 1,
        RESULT_EXPOSED = 2,        // failed, the spy caught: an incident
        RESULT_DONE = 3,
    };

    // `attacker` runs operation `op` against `target`; `third` is the empire to blame (false flag).
    // Spends the points, rolls the check and applies the effect. Logs "spy_ops: ...".
    e_result Execute(int16_t attacker, e_op op, int16_t target, int16_t third = -1);

    // The same with the dice already rolled (as many values as the configured dice kind has dice):
    // for tests and replays; the game always rolls.
    e_result Execute_With_Dice(int16_t attacker, e_op op, int16_t target, int16_t third, const int32_t* dice_values);

    // The difficulty of the check (without the operation's dice) and the attacker's modifiers:
    // for the rules and the tests.
    int32_t Difficulty(int16_t attacker, e_op op, int16_t target);
    int32_t Bonus(int16_t attacker, int16_t target);

    // Unrest in a colony (stability.cpp subtracts it).
    int32_t Unrest(int16_t colony);
    // A bribed governor at the colony's star lowers its morale (Colony_Morale).
    int32_t Morale_Effect(const s_colony* colony);
    // Who bribed the leader, -1 if no one.
    int16_t Bribed_By(int16_t leader);

    // Famine (sabotage of food): half-food units per farmer to add in Colony_Food2_Per_Farmer;
    // 0 when the operation is off or no famine runs there. Reads only state.
    int32_t Food2_Penalty(const s_colony* colony);
    // Whether the colony's production is stalled by sabotage or a strike.
    bool Stalled(int16_t colony);

    // Inside man (spy inside_man): a bribed governor of `colony` whose briber is `invader` opens the
    // gates. Inside_Man_Troops returns `troops` halved in that case, else unchanged (no side
    // effects, no dice); Inside_Man_Spend then spends the bribe (slot cleared, governor deassigned,
    // logged) and returns whether it did. Both are called once per real invasion (invasion.cpp Invade).
    int16_t Inside_Man_Troops(const s_colony* colony, int16_t invader, int16_t troops);
    bool Inside_Man_Spend(s_colony* colony, int16_t invader);

    // Once a turn (spy.cpp, after the spy step): unrest fades, bribes may be uncovered or end in a
    // colony's handover, and every AI_PERIOD turns the computer empires choose operations.
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'SPYO' (game/rules_sections.h): payload version 2 (1 is still read), then unrest per colony
    // (uint8 x MAX_COLONIES), per leader (MAX_LEADERS) who bribed it (int8) and whether it is
    // immune to framing (uint8), and (v2) per colony the turns left of a stalled production and of
    // a famine (uint8 x MAX_COLONIES each).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

    // State access for the behaviour code (spy_ops_state.cpp).
    uint8_t* Unrest_Slot(int16_t colony);
    int8_t* Bribed_Slot(int16_t leader);
    uint8_t* Immune_Slot(int16_t leader);
    uint8_t* Stall_Slot(int16_t colony);
    uint8_t* Famine_Slot(int16_t colony);

} // namespace spy_ops
