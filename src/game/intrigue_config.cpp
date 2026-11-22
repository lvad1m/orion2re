#include "pch.h"

#include "game/intrigue.h"

// Configuration globals bound from specs/config/main.json5 (namespaces "spy" and "diplomacy"),
// kept apart from intrigue.cpp like the other *_config.cpp files (docs/LocalLLM/mechanics.md 3.5.9).
namespace intrigue {

    bool reveal_plans = false;
    int32_t reveal_chance = 30;
    bool forged_letters = false;
    int32_t forged_letter_cost = 25;
    bool spy_fate = false;
    bool proxy_wars = false;
    int32_t proxy_payment_percent = 20;
    bool favours = false;
    int32_t favour_vote_bonus = 150;

} // namespace intrigue
