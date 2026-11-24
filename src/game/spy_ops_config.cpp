#include "pch.h"

#include "game/spy_ops.h"

// Configuration globals bound from specs/config/main.json5 (namespace "spy"), kept apart from
// spy_ops.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.2.5.3).
namespace spy_ops {

    int32_t operations = 0;
    int32_t cost_unrest = 10;
    int32_t cost_kill_scientist = 20;
    int32_t cost_bribery = 25;
    int32_t cost_frame_leader = 15;
    int32_t cost_data = 5;
    int32_t cost_false_flag = 15;
    int32_t cost_assassination = 40;
    int32_t cost_sabotage = 25;
    bool inside_man = false;

} // namespace spy_ops
