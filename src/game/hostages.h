#pragma once

#include "game/choices.h"

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
// exchanged, escapes or is executed. Computer empires decide at once. A human owner (pays the ransom,
// offers an exchange, or leaves the leader or spies where they are) and a human holder (executes, releases,
// exchanges or keeps holding) choose in the choice window (choices.h) when events human_choices is on, on the
// module's own phase (stardate % PERIOD == PHASE) and, for a holder, once the prisoner has been held the
// number of turns after which a computer holder decides; otherwise a human owner never pays and a human
// holder never executes or releases, as before. Hostages between computers never involve a human.
// Both flags off by default; with them off nothing here draws from game_random and nothing is kept.
//
// Wave 6 (ab-review 3.1, 2.2, 2.3). The jail: an arrest (a framed or innocent leader, an uncovered bribe,
// the rebel general's trial) keeps the leader out of service for JAIL_TURNS in this same table, held by its
// own empire (holder == owner), instead of the classic dismissal to the hire list; it needs no flag of its
// own (each arrest site keeps its own flag). Spies in prison (flag `diplomacy spy_prison`): a caught spy whose
// fate nobody decided otherwise sits with its catcher, counted per (jailer, owner), and is ransomed,
// exchanged, escapes or is released. A hostage returned home may have turned into a mole of its holder.
namespace hostages {

    extern bool hostage_swaps;  // diplomacy hostages
    extern bool prisoners;      // diplomacy prisoners
    extern bool spy_prison;     // diplomacy spy_prison

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
    constexpr int32_t JAIL_TURNS = 20;              // an arrested leader sits this long, then returns (or is executed)
    constexpr int32_t MOLE_PERCENT = 25;            // a hostage returned home turns mole of its holder with this chance
    constexpr int32_t MAX_SPY_CELLS = 32;           // (jailer, owner) pairs with imprisoned spies
    constexpr int32_t MAX_SPIES_PER_CELL = 8;
    constexpr int32_t SPY_RANSOM = 20;              // BC per imprisoned spy
    constexpr int32_t SPY_ESCAPE_PERCENT = 5;       // a turn per (jailer, owner) pair: one spy escapes
    constexpr int32_t SPY_PRISON_TURNS = 40;        // the longest a spy sits; then released (executed by a ruthless jailer)
    constexpr int32_t EXECUTED_SPY_OPINION = -15;   // REASON_TREATED_OUR_SPY, the owner toward a ruthless jailer
    constexpr int32_t HOME_SPY_LIMIT = 63;          // spies at home: the count is a 6-bit field (BILL::Set_Their_Spy_Number)

    enum e_kind : uint8_t {
        KIND_HOSTAGE = 0,
        KIND_PRISONER = 1,
        KIND_JAIL = 2,      // arrested by its own empire (holder == owner); returns after JAIL_TURNS
        KIND_TRAITOR = 3,   // an uncovered mole, jailed by its own empire; a ruthless owner executes it after the term
    };

    struct s_spies {
        int8_t jailer = -1;
        int8_t owner = -1;
        uint8_t count = 0;
        int32_t since = 0;  // the stardate the pair's term started (restarted when one spy leaves)
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
        int32_t spy_count = 0;
        s_spies spies[MAX_SPY_CELLS];
    };
    s_state* State();

    // Index of the hold record of `leader`, -1 if it is not held.
    int32_t Find(int16_t leader);
    // How many leaders of `owner` the empire `holder` holds (0 when both flags are off).
    int32_t Count_Held(int16_t owner, int16_t holder);

    // Takes `leader` out of service and records it; false when it cannot (limit, already held, not serving).
    bool Hold(int16_t leader, int16_t holder, uint8_t kind);
    // Arrest: `leader` is jailed by its own empire (`traitor`: an uncovered mole). When it cannot be (not in the
    // pool or assigned, table full) the classic dismissal OFFICER::Deassign_Officer is done instead.
    void Arrest(int16_t leader, bool traitor);
    // Jails the leader; false when it cannot be.
    bool Jail(int16_t leader, bool traitor);
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

    // A hostage `leader` of `owner` goes home from `holder`: with MOLE_PERCENT (roll 0-99 under it) it works for
    // the holder from now on (spy_ops' bribed slot). Only with bribery on; true when it turned.
    bool Mole_With_Roll(int16_t leader, int16_t holder, int32_t roll);

    // A spy of `owner` was caught by `jailer` (espionage.cpp On_Spy_Caught): it goes to prison (spy_prison on,
    // and the spy's fate was not decided otherwise by `spy spy_fate`). False when it cannot.
    bool Imprison_Spy(int16_t jailer, int16_t owner);
    // Spies of `owner` in the prison of `jailer` (0 when spy_prison is off).
    int32_t Spies_Held(int16_t jailer, int16_t owner);
    // One spy of the pair escapes home when `roll` (0-99) is under SPY_ESCAPE_PERCENT.
    bool Spy_Escape_With_Roll(int32_t cell, int32_t roll);

    // The ransom a leader costs, in BC.
    int32_t Ransom_Of(int16_t leader);
    // `index` escapes home when `roll` (0-99) is under ESCAPE_PERCENT; only prisoners.
    bool Escape_With_Roll(int32_t index, int32_t roll);

    // The choice window (choices.h, MODULE_HOSTAGES; `kind` is one of CHOICE_*, `player` the human, `other`
    // the other empire, `target` the leader (-1 for spies), `value` the ransom). Both are false/no-ops when
    // the subject is gone.
    enum e_choice : uint8_t {
        CHOICE_LEADER_OWNER = 0,   // pay the ransom / exchange / leave
        CHOICE_LEADER_HOLDER = 1,  // execute / release or exchange / keep
        CHOICE_SPY_OWNER = 2,
        CHOICE_SPY_HOLDER = 3,
    };
    bool Choice_Text(const choices::s_choice& choice, choices::s_text* text);
    void Choice_Apply(const choices::s_choice& choice, int32_t option);

    // Once a turn (diplomac.cpp End_Of_Turn_Diplomacy_Adjustments): hostages come home, prisoners escape
    // (the only draws from game_random) and, on PHASE, are exchanged or ransomed; old prisoners are executed or released.
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'HOST' (game/rules_sections.h): payload version 2, then the count (uint8) and per record
    // leader, owner, holder (int8), kind (uint8), since (int32); then the spy cells: count (uint8) and per cell
    // jailer, owner (int8), count (uint8), since (int32). Version 1 (no spy cells) is still read.
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace hostages
