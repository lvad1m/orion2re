#include "pch.h"

#include "game/stability.h"

#include "game/diplomacy_memory.h"
#include "game/invasion.h"
#include "game/msg.h"
#include "game/pop.h"
#include "game/random.h"

#include <algorithm>

// The behaviour of colony stability (mechanics.md 3.2.5.1); the state and its save section are in
// stability_state.cpp. game_random is drawn only for a colony long in unrest.
namespace stability {

    namespace {
        constexpr int8_t TREATY_FIRST_WAR = 4;

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        // A settled colony of a living empire (not an outpost).
        bool Settled_(int16_t colony) {
            if (colony < 0 || colony >= MOX::NUM_COLONIES || colony >= MAX_COLONIES) return false;
            const s_colony& c = MOX::colony[colony];
            return Alive_(c.owner) && c.outpost_flag == 0;
        }

        int Government_Family_(int16_t player) {
            return MOX::player[player].traits[TRAIT_CURRENT_GOVERNMENT] / 2;
        }

        bool Is_Capitol_(const s_colony& c) {
            return c.buildings[BUILDING_CAPITOL] != 0;
        }

        bool Has_Barracks_(const s_colony& c) {
            return c.buildings[BUILDING_MARINE_BARRACKS] != 0 || c.buildings[BUILDING_ARMOR_BARRACKS] != 0;
        }
    } // namespace

    int32_t Compute(int16_t colony) {
        if (!Settled_(colony)) return 0;
        const s_colony& c = MOX::colony[colony];
        const int16_t owner = c.owner;
        const s_player& p = MOX::player[owner];

        int32_t weariness = 0;
        bool at_war = false;
        for (int16_t k = 0; k < Players_(); ++k) {
            if (k == owner || !Alive_(k) || p.treaty[k] < TREATY_FIRST_WAR) continue;
            at_war = true;
            weariness = std::max(weariness, diplomemory::War_Weariness(owner, k));
        }
        int32_t negative = std::min<int32_t>(10, weariness / 6);
        negative += std::clamp<int32_t>(p.food_shortage_duration, 0, 10);
        negative += p.tax_rate / 10;
        if (p.surplus_bc < 0) negative += 5;

        int32_t positive = 0;
        if (!at_war) positive += 3;
        const int32_t* victory = Last_Victory_Slot(owner);
        if (victory != nullptr && *victory > 0 && MOX::stardate - *victory <= VICTORY_TURNS) positive += 2;
        int32_t growth = 0;
        for (int16_t g : c.pop_growth) growth += g;
        if (growth > 0) positive += 1;
        if (Has_Barracks_(c)) positive += 3;
        if (c.buildings[BUILDING_HOLO_SIMULATOR] != 0 || c.buildings[BUILDING_PLEASURE_DOME] != 0 ||
            p.tech_applications[TECH_APP_VIRTUAL_REALITY_NETWORK] == TECH_RESEARCH_STATUS_RESEARCHED) {
            positive += 2;
        }
        if (Is_Capitol_(c)) positive += 5;

        // The government: dictatorships hold their people harder, democracies feel more.
        const int family = Government_Family_(owner);
        if (family == GOVERNMENT_DICTATORSHIP / 2) {
            negative /= 2;
        } else if (family == GOVERNMENT_DEMOCRACY / 2) {
            negative = negative * 3 / 2;
        }
        int32_t value = positive - negative;
        if (family == GOVERNMENT_UNIFICATION / 2) value = std::max<int32_t>(0, value);
        return std::clamp<int32_t>(value, MIN_STABILITY, MAX_STABILITY);
    }

    int32_t Morale_Effect(const s_colony* colony) {
        if (!enabled || colony == nullptr) return 0;
        const int32_t value = Of(static_cast<int16_t>(colony - MOX::colony));
        return value < 0 ? std::max<int32_t>(MORALE_EFFECT_MIN, value / 2) : 0;
    }

    void Note_Victory(int16_t player) {
        int32_t* slot = Last_Victory_Slot(player);
        if (enabled && slot != nullptr) *slot = MOX::stardate;
    }

    int16_t Leaning_Neighbour(int16_t colony) {
        if (!Settled_(colony)) return -1;
        const s_colony& c = MOX::colony[colony];
        const int16_t owner = c.owner;
        int16_t best = -1;
        int32_t best_score = 0;
        for (int16_t k = 0; k < Players_(); ++k) {
            if (k == owner || !Alive_(k) || MOX::player[owner].contact[k] == 0 || MOX::player[k].within_range[owner] != 1) continue;
            int32_t score = 0;
            for (int i = 0; i < c.n_pops && i < MAX_COLONY_POPULATION; ++i) {
                if (static_cast<int16_t>(c.pop[i] & POP::MASK_RACE) == k) score += 3;
            }
            if (Government_Family_(k) == GOVERNMENT_DEMOCRACY / 2) score += 2;
            if (score < 2) continue;  // only the race or a democracy draws a people; peace adds to it
            if (MOX::player[owner].treaty[k] < TREATY_FIRST_WAR) score += 1;
            if (score > best_score) {
                best = k;
                best_score = score;
            }
        }
        return best;
    }

    void Turn() {
        if (!enabled) return;
        const int16_t colonies = static_cast<int16_t>(std::min<int32_t>(MOX::NUM_COLONIES, MAX_COLONIES));
        for (int16_t i = 0; i < colonies; ++i) {
            int8_t* stored = Stability_Slot(i);
            uint8_t* unrest = Unrest_Slot(i);
            if (!Settled_(i)) {
                *stored = 0;
                *unrest = 0;
                continue;
            }
            const int32_t value = Compute(i);
            *stored = static_cast<int8_t>(value);
            if (value > rebellion_threshold) {
                *unrest = 0;
                continue;
            }
            if (*unrest < 255) ++*unrest;
            s_colony& c = MOX::colony[i];
            if (*unrest < UNREST_TURNS || Is_Capitol_(c)) continue;
            int32_t chance = rebellion_chance * (rebellion_threshold - value + 1);
            if (Has_Barracks_(c)) chance /= 2;
            if (static_cast<int32_t>(game_random::Random(1000)) > chance) continue;
            const int16_t target = Leaning_Neighbour(i);
            if (target < 0) continue;
            const int16_t owner = c.owner;
            s_MSG msg = {};
            msg.msg_type = MSG_TYPE_COLONY;
            msg.u.colony.planet_idx = c.planet;
            msg.u.colony.subtype = MSG_COLONY_SUBTYPE_REBELLION;
            msg.u.colony.c.rebels.owner_player = static_cast<int8_t>(target);
            msg.u.colony.c.rebels.rebels = static_cast<int8_t>(c.n_pops);
            msg.u.colony.c.rebels.lost = 0;
            MSG_::Add_Msg(msg, owner);
            MSG_::Add_Msg(msg, target);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "stability: stardate %d, colony %d of player %d secedes to player %d (stability %d)",
                (int)MOX::stardate, (int)i, (int)owner, (int)target, (int)value);
            invasion::Change_Colony_Ownership(&c, static_cast<uint32_t>(target), nullptr);
            *unrest = 0;
        }
    }

} // namespace stability
