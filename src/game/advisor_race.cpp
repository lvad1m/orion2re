#include "pch.h"

#include "game/advisor_race.h"

#include <algorithm>

namespace advisor {
    namespace {
        int32_t Sign_(int8_t value) {
            return value > 0 ? 1 : value < 0 ? -1 : 0;
        }

        int8_t Clamp_(int32_t value) {
            return static_cast<int8_t>(std::clamp<int32_t>(value, -2, 2));
        }
    } // namespace

    s_race_leanings Race_Leanings(const int8_t* traits) {
        s_race_leanings race;
        if (traits == nullptr) return race;
        const auto has = [&](int trait) { return traits[trait] != 0 ? 1 : 0; };
        const auto bonus = [&](int trait) { return traits[trait] > 0 ? 1 : 0; };
        const auto penalty = [&](int trait) { return traits[trait] < 0 ? 1 : 0; };
        const int32_t repulsive = has(TRAIT_REPULSIVE);
        race.war = Clamp_(bonus(TRAIT_SHIP_ATTACK) + bonus(TRAIT_GROUND_COMBAT_BONUS) + has(TRAIT_WARLORD) +
            has(TRAIT_TELEPATHIC) - penalty(TRAIT_GROUND_COMBAT_BONUS) - has(TRAIT_FANTASTIC_TRADERS) + repulsive);
        race.diplomacy = Clamp_(has(TRAIT_CHARISMATIC) + has(TRAIT_FANTASTIC_TRADERS) - 2 * repulsive);
        race.spying = Clamp_(Sign_(traits[TRAIT_SPYING_BONUS]) + has(TRAIT_UNCREATIVE));
        race.growth = Clamp_(Sign_(traits[TRAIT_POP_GROWTH_BONUS]) + has(TRAIT_SUBTERRANEAN) + has(TRAIT_AQUATIC) +
            has(TRAIT_LARGE_HOME_WORLD) + has(TRAIT_EATS_MINERALS));
        race.stealth = Clamp_(has(TRAIT_STEALTHY_SHIPS) + has(TRAIT_TRANS_DIMENSIONAL));
        return race;
    }

    bool Weak_For_Race(int64_t theirs, int64_t ours, const s_race_leanings& race) {
        if (ours <= 0) return false;
        if (race.war >= 1) return 3 * theirs <= 2 * ours;
        if (race.war <= -1) return 3 * theirs <= ours;
        return 2 * theirs <= ours;
    }

    int32_t Coalition_Join_Denominator(const s_race_leanings& race) {
        if (race.war >= 1) return 4;
        if (race.war <= -1) return 2;
        return 3;
    }

} // namespace advisor
