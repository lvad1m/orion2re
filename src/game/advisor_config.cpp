#include "pch.h"

#include "game/advisor.h"

// Configuration globals bound from specs/config/main.json5 (namespace "advisor"). Kept apart
// from advisor.cpp, like the other *_config.cpp files, so every target that links the
// generated config bindings gets them without the rest of the advisor.
namespace advisor {

    bool enabled = false;
    uint8_t journal = JOURNAL_OFF;
    bool rules_objectives = true;
    bool rules_spies = true;
    bool rules_expansion = true;
    bool rules_doctrine = true;
    bool rules_counter_design = true;
    bool rules_seen_ships = true;
    bool rules_outposts = false;
    bool rules_exploration = true;
    int32_t active_players = 255;
    bool rules_lanchester = true;
    bool rules_balance = true;
    int32_t rules_period = 5;
    int32_t objective_min_turns = 10;
    int32_t spy_target_weight_percent = 300;
    int32_t doctrine_parts = DOCTRINE_PART_DEFAULT;
    bool rules_phases = false;
    int32_t phase_parts = PHASE_PART_ALL;

} // namespace advisor
