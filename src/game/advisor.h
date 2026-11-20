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

    // Called once per turn by NEXTTURN::Next_Turn_Calc right after AIDATA::Compute_AI_Data and
    // before any AI decision. Does nothing when `enabled` is false.
    void Run_Turn(const game::s_view* world);

    // Clears directives and journal state: new game, failed load, tests.
    void Reset();

} // namespace advisor
