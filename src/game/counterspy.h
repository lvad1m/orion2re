#pragma once

#include "game/spy_ops.h"

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Counter-intelligence (docs/LocalLLM/mechanics.md 3.5.5 and 3.2.3; README.md 10.9, wave 2): a mole
// hunt, a canary trap, the interrogation of a caught spy and the guarding of key leaders. Computer
// empires only (the human gets it with the spy screen). Four flags, all off by default; with them
// off nothing here draws from game_random, keeps state or changes a return value of the hooks.
// - Mole hunt: every PERIOD turns (phase 6) an empire that suspects a mole - a spy caught in it, or a
//   secret of it sold or made public, within SUSPECT_TURNS, or heightened or witch-hunt
//   counter-intelligence - checks its assigned leader of the lowest loyalty with a dice check: a bribed
//   one is uncovered, an innocent one jailed when the check fails.
// - Canary trap: an empire whose secret leaked sets a trap; CANARY_TURNS later a bribed leader whose
//   briber has spies or a resident in it is uncovered, with certainty.
// - Interrogation: a computer that catches a spy may learn the owner's plan of attack; the spy of a
//   false-flag operation honestly names the framed empire.
// - Guards: an empire threatened or rich pays guard_fee BC a turn; its key leaders are harder to kill.
namespace counterspy {

    extern bool mole_hunt;         // spy mole_hunt
    extern bool canary_trap;       // spy canary_trap
    extern bool interrogation;     // spy interrogation
    extern bool guards;            // spy guards
    extern int32_t guard_fee;      // spy guard_fee: BC a turn

    // Programming choices (README.md 10.9: tuned on the big bench).
    constexpr int32_t PERIOD = 10;
    constexpr int32_t PHASE = 6;                   // stardate % PERIOD
    constexpr int32_t SUSPECT_TURNS = 20;          // a suspicion lasts this long
    constexpr int32_t HUNT_DIFFICULTY = 12;
    constexpr int32_t HUNT_SKILL_BONUS = 3;        // a spy master, a telepath
    constexpr int32_t HUNT_AGENTS_MAX = 3;         // counter-agents: one point for two, at most this
    constexpr int32_t UNCOVERED_OPINION = -25;     // the owner's memory of the briber
    constexpr int32_t JAILED_LOYALTY = -20;        // the innocent jailed
    constexpr int32_t WITCH_LOYALTY = -3;          // each other leader of the hunter
    constexpr int32_t DEFAULT_LOYALTY = 50;        // an untracked leader
    constexpr int32_t CANARY_TURNS = 10;
    constexpr int32_t INTERROGATION_PERCENT = 30;
    constexpr int32_t GUARD_DIFFICULTY = 3;
    constexpr int32_t GUARD_THREAT_TURNS = 50;     // an attempt this recent calls for guards
    constexpr int32_t GUARD_RICH_BC = 300;
    constexpr int32_t GUARD_LEVEL = 3;             // a leader of this level or more is a key leader

    enum e_hunt : uint8_t {
        HUNT_NONE = 0,       // no suspicion, no suspect, or off
        HUNT_NOTHING = 1,
        HUNT_UNCOVERED = 2,  // a bribed leader found
        HUNT_JAILED = 3,     // an innocent jailed
    };

    // Whether `player` suspects a mole now.
    bool Suspects(int16_t player);
    // The leader `player` would suspect (lowest loyalty among its assigned leaders), -1 if none.
    int16_t Suspect_Leader(int16_t player);

    // `hunter` hunts a mole now (the dice are rolled or given; Turn calls it when Suspects).
    e_hunt Hunt(int16_t hunter);
    e_hunt Hunt_With_Dice(int16_t hunter, const int32_t* dice_values);

    // A secret of `owner` leaked (secrets.cpp sold or made it public, intrigue.cpp revealed a plan):
    // suspicion for the mole hunt, and a canary trap is set.
    void On_Secret_Leaked(int16_t owner);
    // The turn the canary trap of `owner` springs (0: none set).
    int32_t Canary_Turn(int16_t owner);
    // Springs `owner`'s trap now: the first bribed leader whose briber has spies or a resident in
    // the empire is uncovered. True when one was.
    bool Spring_Canary(int16_t owner);

    // A spy of `owner` was caught by `victim` (espionage.cpp On_Spy_Caught): suspicion, and the
    // interrogation. The _With_Roll variant takes the roll (0..99) and returns whether a plan was
    // learned or warned of.
    void On_Spy_Caught(int16_t victim, int16_t owner);
    bool On_Spy_Caught_With_Roll(int16_t victim, int16_t owner, int32_t roll);
    // The empire to blame for an exposed operation (spy_ops.cpp): the false-flag operation's framed
    // empire `framed` when the spy is interrogated, else `attacker`. No dice.
    int16_t Blamed(int16_t target, int16_t attacker, int16_t framed);

    // An assassination, poison or kill-scientist attempt against `target` (spy_ops.cpp): guards.
    // A successful hostile operation `victim` can see makes it suspect a mole (spy mole_hunt): `op` is a
    // spy_ops operation (only those with a visible effect count), OP_COUNT for a classic technology
    // theft or a destroyed building (spy.cpp). Nothing with the flag off or for a human victim.
    void On_Harm(int16_t victim, spy_ops::e_op op = spy_ops::OP_COUNT);
    void On_Attempt(int16_t target, spy_ops::e_op op);
    // Whether `player` guards its leaders now.
    bool Guarded(int16_t player);
    // What guards add to the difficulty of `op` against `target` (0 with the flag off).
    int32_t Guard_Difficulty(int16_t target, spy_ops::e_op op);

    // Once a turn (spy.cpp, after secrets::Turn): guards paid, canary traps sprung, and every PERIOD
    // turns (phase PHASE) the mole hunts.
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'CSPY' (game/rules_sections.h): payload version 1, then per player the stardate the
    // suspicion began (int32, 0 none), the turn the canary trap springs (int32, 0 none), the stardate
    // of the last attempt on its leaders (int32, 0 none) and whether it guards (uint8).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

    struct s_state {
        int32_t suspicion[8] = {};
        int32_t canary[8] = {};
        int32_t attempt[8] = {};
        uint8_t guarded[8] = {};
    };
    s_state* State();

} // namespace counterspy
