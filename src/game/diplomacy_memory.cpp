#include "pch.h"

#include "game/diplomacy_memory.h"

#include <algorithm>

namespace diplomemory {

    namespace {
        // Ship status below this value is a live ship (colcalc Update_Player_Counts, aibuild).
        constexpr int32_t SHIP_STATUS_FIRST_NOT_ALIVE = 5;
        // Treaty states: 1..3 are treaties in force (pact, alliance, peace), 4 and above are war.
        constexpr int32_t TREATY_FIRST_WAR = 4;

        bool Valid_(int16_t player) {
            return player >= 0 && player < MAX_PLAYERS && player < MOX::NUM_PLAYERS;
        }

        int32_t Step_Toward_Zero_(int32_t value) {
            return value > 0 ? value - 1 : value < 0 ? value + 1 : 0;
        }

        // The fleet in the units of ship_war_losses (combat_lifecycle.cpp: 2^(size + 1) per ship).
        int64_t Fleet_Units_(int16_t player) {
            int64_t units = 0;
            for (int32_t i = 0; i < MOX::NUM_SHIPS; ++i) {
                const s_ship_data& ship = MOX::ship[i];
                if (ship.owner != player || ship.status >= SHIP_STATUS_FIRST_NOT_ALIVE) continue;
                const int32_t size = std::clamp<int32_t>(ship.d.size, 0, 6);
                units += int64_t{2} << size;
            }
            return units;
        }
    } // namespace

    void End_Of_Turn() {
        if (!enabled || period < 1 || MOX::stardate % period != 0) return;
        for (int16_t i = 0; i < MOX::NUM_PLAYERS; ++i) {
            s_player& plr = MOX::player[i];
            if (plr.eliminated != 0) continue;
            for (int16_t j = 0; j < MOX::NUM_PLAYERS; ++j) {
                if (j == i || MOX::player[j].eliminated != 0 || plr.contact[j] != 1) continue;
                const int32_t treaty = plr.treaty[j];
                if (treaty >= TREATY_FIRST_WAR) continue;  // no healing while at war
                int32_t trust = plr.trust_worthiness[j];
                if (trust < 0) {
                    ++trust;
                } else if (treaty >= 1 && trust < trust_cap) {
                    ++trust;
                }
                plr.trust_worthiness[j] = static_cast<int8_t>(std::clamp<int32_t>(trust, TRUST_FLOOR, 127));
                plr.broken_treaties_modifier[j] = static_cast<int8_t>(Step_Toward_Zero_(plr.broken_treaties_modifier[j]));
            }
        }
    }

    void Lower_Trust(int16_t player, int16_t target, int32_t amount) {
        if (!Valid_(player) || target < 0 || target >= MAX_PLAYERS) return;
        int8_t& trust = MOX::player[player].trust_worthiness[target];
        if (!enabled) {
            trust = static_cast<int8_t>(trust - amount);  // classic: wraps like the original int8
            return;
        }
        trust = static_cast<int8_t>(std::max<int32_t>(TRUST_FLOOR, trust - amount));
    }

    int32_t War_Weariness(int16_t player, int16_t enemy) {
        if (!enabled || !Valid_(player) || !Valid_(enemy) || player == enemy) return 0;
        const s_player& plr = MOX::player[player];
        if (plr.treaty[enemy] < TREATY_FIRST_WAR) return 0;
        const int64_t ship_losses = std::max<int64_t>(0, plr.ship_war_losses[enemy]);
        const int64_t ship_share = ship_losses * 100 / (ship_losses + Fleet_Units_(player) + 1);
        const int64_t colony_losses = std::max<int64_t>(0, plr.colony_war_losses[enemy]);
        const int64_t colony_share = colony_losses * 100 / (colony_losses + std::max<int64_t>(0, plr.total_pop) + 1);
        return static_cast<int32_t>(std::min<int64_t>(std::max(0, weariness_cap), ship_share / 2 + colony_share));
    }

} // namespace diplomemory
