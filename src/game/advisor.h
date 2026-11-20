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

    // Called once per turn by NEXTTURN::Next_Turn_Calc right after AIDATA::Compute_AI_Data and
    // before any AI decision. Does nothing when `enabled` is false.
    void Run_Turn(const game::s_view* world);

    // Clears directives and journal state: new game, failed load, tests.
    void Reset();

} // namespace advisor
