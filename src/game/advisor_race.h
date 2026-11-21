#pragma once

#include <cstdint>

// Race doctrine (docs/LocalLLM/README.md 10.8; flag advisor rules_race). The advisor's decisions
// depended on the leader's character only, so two races with the same character played alike -
// a difference of skin. The race now leans the existing rules a little: each leaning, from the
// race's traits (s_player::traits[]), shifts a threshold, never replaces a rule. The leader's
// character stays the second, independent axis.
namespace advisor {

    struct s_race_leanings {
        int8_t war = 0;        // ship attack, ground combat, warlords, telepathy; - weak ground troops, traders; + repulsive
        int8_t diplomacy = 0;  // charismatic, fantastic traders; -2 repulsive
        int8_t spying = 0;     // spying bonus, uncreative (they steal technology); - spying penalty
        int8_t growth = 0;     // population growth, subterranean, aquatic, large home world, lithovores; - slow growth
        int8_t stealth = 0;    // stealthy ships, trans-dimensional
    };

    // Leanings of a race from its traits (TRAIT_COUNT values, as s_player::traits), each clamped to
    // -2..2. Pure.
    s_race_leanings Race_Leanings(const int8_t* traits);

    // Rules 405 and 406: a contact of `theirs` fighting power is weak for us (`ours`) up to half of
    // ours; up to 2/3 for warlike races (war >= 1), only up to 1/3 for peaceful ones (war <= -1).
    bool Weak_For_Race(int64_t theirs, int64_t ours, const s_race_leanings& race);

    // Rule 408: a neighbour of the would-be winner joins the coalition with this fraction of the
    // winner's fighting power: 1/3, 1/4 for warlike races, 1/2 for peaceful ones (the denominator).
    int32_t Coalition_Join_Denominator(const s_race_leanings& race);

} // namespace advisor
