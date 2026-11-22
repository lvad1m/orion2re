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
    extern bool rules_seen_ships;            // advised empires design against the ships they saw in battle
    extern bool rules_race;                  // the race's traits lean the rules a little (advisor_race.h)
    extern bool rules_production;            // rule 120: weights for defence, economy, food, research (tier 6)
    extern bool rules_war_goals;             // the main rival's colonies are the first targets (tier 6)
    extern bool rules_tactics;               // a tactical doctrine for battles (tier 6, README.md 5.6)
    extern bool rules_outposts;              // rules may build and settle outposts
    extern bool rules_exploration;           // rules may push range research (rule 501)
    extern int32_t active_players;           // bit i: the advisor steers player i (0..255, default 255)
    extern bool rules_lanchester;            // advised empires weigh fleets by Lanchester's square law
    extern bool rules_balance;               // rules may turn empires against a runaway leader (rule 401)
    extern int32_t rules_period;             // turns a rules directive stays active (1..50)
    extern int32_t objective_min_turns;      // minimum turns between objective changes (1..100)
    extern int32_t spy_target_weight_percent;  // weight multiplier for the directive's spy target (100..1000)
    extern int32_t doctrine_parts;           // e_doctrine_part bits active under rules_doctrine (0..511)
    extern bool rules_phases;                // phase strategy: a leader plays for victory (rule 406),
                                             // everyone resists an empire nearing a council win (407)
    extern int32_t phase_parts;              // e_phase_part bits active under rules_phases (0..63)

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
        DOCTRINE_PART_THIRD_PARTY = 64,  // III-V, VII: other empires' wars (rule 405); spare only a needed counterweight
        DOCTRINE_PART_TRIBUTE = 128,   // VI: a losing empire buys peace with tribute and stops paying once clearly stronger
        DOCTRINE_PART_DIVIDE = 256,    // V: spies frame a rival for their thefts and sabotage (divide and rule)
        DOCTRINE_PART_ALL = 511,
        // The default (189): readiness cost advised empires their expansion in 400-turn games
        // against the original AI (README.md 10.4.4), so it stays opt-in, and so does the
        // third-party part (10.4.5). Tribute is on since its bench (10.4.12: a bought peace holds
        // longer and the payer survives more often); divide and rule stays opt-in, as framing
        // redirected blame but changed nothing measurable.
        DOCTRINE_PART_DEFAULT = DOCTRINE_PART_ALL & ~DOCTRINE_PART_READINESS & ~DOCTRINE_PART_THIRD_PARTY &
            ~DOCTRINE_PART_DIVIDE,
    };

    // Parts of the phase strategy (README.md 10.7), one bit each in `phase_parts`.
    enum e_phase_part : int32_t {
        PHASE_PART_LEADER = 1,          // rule 406: a leader conquers and courts votes; its fleet weight
        PHASE_PART_AGAINST_WINNER = 2,  // rule 407: resist a council nominee nearing a win
        PHASE_PART_COUNCIL_VOTES = 4,   // council votes follow the directive's attitudes
        PHASE_PART_COALITION = 8,       // rule 408: war on a nominee holding half the votes; a leader that
                                        // holds half grows in peace
        PHASE_PART_JOIN_LAST = 16,      // rule 408: ruthless, erratic and pacifist leaders join the
                                        // coalition last, once the would-be winner is outmatched
        PHASE_PART_REPORTS = 32,        // military reports between empires at war (advisor_reports.h);
                                        // rule 408 counts reported help near us
        PHASE_PART_ALL = 63,
    };

    // True when rules_phases is on and `part` is in phase_parts.
    inline bool Phase(int32_t part) {
        return rules_phases && (phase_parts & part) != 0;
    }

    // True when rules_doctrine is on and `part` is in doctrine_parts.
    inline bool Doctrine(int32_t part) {
        return rules_doctrine && (doctrine_parts & part) != 0;
    }

    // Called once per turn by NEXTTURN::Next_Turn_Calc right after AIDATA::Compute_AI_Data and
    // before any AI decision. Does nothing when `enabled` is false.
    void Run_Turn(const game::s_view* world);

    // Clears directives, the intel memory and journal state: new game, failed load, tests.
    void Reset();

} // namespace advisor
