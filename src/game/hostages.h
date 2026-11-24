#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

struct s_colony;

// Hostages and wards, captured leaders and ransom (docs/LocalLLM/mechanics.md 3.5.3, events-catalog 3.12;
// README.md 10.9, the A and B scope). A hostage: the weaker partner of a new alliance gives one of its
// leaders to the stronger. A prisoner: the governor of a colony taken by invasion is captured by the
// invader. A held leader is out of service (status -2, as for a dead one) until it is returned, ransomed,
// exchanged, escapes or is executed. Only computer empires decide; a human's leader may be captured, but
// a human owner never pays and a human holder never executes or releases (no interface step yet).
// Both flags off by default; with them off nothing here draws from game_random and nothing is kept.
namespace hostages {

    extern bool hostage_swaps;  // diplomacy hostages
    extern bool prisoners;      // diplomacy prisoners

    constexpr int32_t MAX_HELD = 32;
    constexpr int32_t HOSTAGE_TURNS = 40;           // a hostage is returned after this long
    constexpr int32_t RETURNED_OPINION = 10;        // REASON_FAMILY_TIES, the giver toward the holder (kept well)
    constexpr int32_t EXECUTED_HOSTAGE_OPINION = -40;  // REASON_HARMED_A_HOSTAGE, the giver toward the holder
    constexpr int32_t PERIOD = 10;                  // ransom and exchange on stardate % PERIOD == PHASE
    constexpr int32_t PHASE = 8;
    constexpr int32_t RANSOM_BASE = 50;             // BC
    constexpr int32_t RANSOM_PER_LEVEL = 25;        // BC per leader level
    constexpr int32_t RANSOM_RESERVE = 100;         // the owner must keep this many BC after paying
    constexpr int32_t PRISONER_TURNS = 50;          // then executed (ruthless holder) or released
    constexpr int32_t EXECUTED_PRISONER_OPINION = -30;  // REASON_HARMED_A_HOSTAGE, the owner toward the holder
    constexpr int32_t ESCAPE_PERCENT = 3;           // a turn per prisoner

    enum e_kind : uint8_t {
        KIND_HOSTAGE = 0,
        KIND_PRISONER = 1,
    };

    struct s_held {
        int8_t leader = -1;     // index in MOX::leaders
        int8_t owner = -1;      // the empire the leader serves
        int8_t holder = -1;     // the empire holding it
        uint8_t kind = KIND_HOSTAGE;
        int32_t since = 0;
    };

    struct s_state {
        int32_t count = 0;
        s_held held[MAX_HELD];
    };
    s_state* State();

    // Index of the hold record of `leader`, -1 if it is not held.
    int32_t Find(int16_t leader);
    // How many leaders of `owner` the empire `holder` holds (0 when both flags are off).
    int32_t Count_Held(int16_t owner, int16_t holder);

    // Takes `leader` out of service and records it; false when it cannot (limit, already held, not serving).
    bool Hold(int16_t leader, int16_t holder, uint8_t kind);
    // Puts the leader back into the pool of `to` and drops the record (the later records move up).
    void Release(int32_t index, int16_t to, const char* how);
    // The leader stays dead; the record is dropped.
    void Execute(int32_t index, const char* why);

    // A hostage: the lowest-level leader of `giver` in the pool or assigned, never its capital's governor;
    // -1 when none.
    int16_t Pick_Hostage(int16_t giver);
    // An alliance of a and b was made (diplomac.cpp Start_Treaty): the weaker gives a hostage to the stronger.
    void On_Alliance(int16_t a, int16_t b);
    // `breaker` broke a treaty with `victim` (diplomac.cpp Break_Treaties): the holder of the breaker's hostage
    // decides by personality - executes, sends home, or keeps it as a ward.
    void On_Treaty_Broken(int16_t breaker, int16_t victim);

    // The colony is about to change hands by invasion (invasion.cpp Change_Colony_Ownership, before the pops
    // change hands): the old owner's governor there is captured by `new_owner`. True when one was captured.
    bool On_Colony_Invaded(const s_colony* colony, int16_t new_owner);

    // The ransom a leader costs, in BC.
    int32_t Ransom_Of(int16_t leader);
    // `index` escapes home when `roll` (0-99) is under ESCAPE_PERCENT; only prisoners.
    bool Escape_With_Roll(int32_t index, int32_t roll);

    // Once a turn (diplomac.cpp End_Of_Turn_Diplomacy_Adjustments): hostages come home, prisoners escape
    // (the only draws from game_random) and, on PHASE, are exchanged or ransomed; old prisoners are executed or released.
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'HOST' (game/rules_sections.h): payload version 1, then the count (uint8) and per record
    // leader, owner, holder (int8), kind (uint8), since (int32).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace hostages
