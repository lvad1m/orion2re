#pragma once

#include <cstdint>

#include "game/world_view.h"

// Advisor: a layer above the original AI. Once per turn it builds each computer empire's view of
// the galaxy (advisor_view.h), asks directive sources for a directive (advisor_directive.h),
// validates and stores it, and writes the turn journal (advisor_journal.h).
//
// Sources: rules (stage 2, in this repository) and a local LLM (stub only; see
// docs/LocalLLM/README.md, sections 4 and 10). With `enabled == false` nothing runs and games
// are unchanged, so the turn oracle stays green.
//
// Determinism: the advisor never draws from game_random (random.cpp). Directives are recorded
// in the save (optional section), so replays and multiplayer clients never re-ask a source.
namespace advisor {

    enum e_journal_mode : uint8_t {
        JOURNAL_OFF = 0,
        JOURNAL_DECISIONS = 1,
        JOURNAL_FULL = 2,
    };

    // Configuration, bound from specs/config/main.json5 (namespace "advisor").
    // `enabled` is in the gameplay group: saved with the game and pushed by the host.
    // `journal` is in the interface group: local to this machine.
    extern bool enabled;
    extern uint8_t journal;  // e_journal_mode

    // Stage 2: rules advisor and its effects (gameplay group). Effective only while `enabled`.
    extern bool rules_objectives;            // rules may change an empire's objective
    extern bool rules_spies;                 // rules may steer spy targets and missions
    extern bool rules_expansion;             // rules may keep a colony ship in production (stage 2v)
    extern bool rules_doctrine;              // Byzantine doctrine: allies against enemies, buy peace, deterrence
    extern bool rules_counter_design;        // advised empires design ships against their real enemies
    extern bool rules_outposts;              // rules may build and settle outposts
    extern bool rules_exploration;           // rules may push range research (rule 501)
    extern int32_t active_players;           // bit i: the advisor steers player i (0..255, default 255)
    extern bool rules_lanchester;            // advised empires weigh fleets by Lanchester's square law
    extern bool rules_balance;               // rules may turn empires against a runaway leader (rule 401)
    extern int32_t rules_period;             // turns a rules directive stays active (1..50)
    extern int32_t objective_min_turns;      // minimum turns between objective changes (1..100)
    extern int32_t spy_target_weight_percent;  // weight multiplier for the directive's spy target (100..1000)
    extern int32_t doctrine_parts;           // e_doctrine_part bits active under rules_doctrine (0..63)

    // Parts of the Byzantine doctrine (Luttwak's seven rules, README.md 10.6), one bit each in
    // `doctrine_parts`, so each can be measured on its own. Rules II (intelligence), III (decisive
    // force) and VII's counter-design have their own flags: rules_spies, rules_lanchester,
    // rules_counter_design.
    enum e_doctrine_part : int32_t {
        DOCTRINE_PART_PEACE = 1,       // I: peace bias toward neighbours that are dangerous to fight
        DOCTRINE_PART_READINESS = 2,   // I: fleet target of the strongest neighbour; no peace dividend while out-gunned
        DOCTRINE_PART_RAIDS = 4,       // IV: raids on undefended colonies rather than battles
        DOCTRINE_PART_ALLIANCES = 8,   // V: befriend our enemies' enemies, court our enemies' allies
        DOCTRINE_PART_BUY_PEACE = 16,  // VI: peace early when losing a war
        DOCTRINE_PART_SPARE = 32,      // VII: peace with a beaten enemy that does not lead
        DOCTRINE_PART_ALL = 63,
    };

    // True when rules_doctrine is on and `part` is in doctrine_parts.
    inline bool Doctrine(int32_t part) {
        return rules_doctrine && (doctrine_parts & part) != 0;
    }

    // Called once per turn by NEXTTURN::Next_Turn_Calc right after AIDATA::Compute_AI_Data and
    // before any AI decision. Does nothing when `enabled` is false.
    void Run_Turn(const game::s_view* world);

    // Clears directives and journal state: new game, failed load, tests.
    void Reset();

} // namespace advisor
