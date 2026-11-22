#include "pch.h"

#include "game/combat_dice.h"

#include "game/random.h"
#include "game/techdata.h"

// The battle's dice (mechanics.md 3.6). Layer A only reads the rolls the original made; layer C
// draws from game_random on a critical hit only.
namespace combat_dice {

    namespace {
        const char* Weapon_Name_(int16_t weapon_id) {
            return weapon_id >= 0 ? TECHDATA::weapons[weapon_id].name : "weapon";
        }
    } // namespace

    const char* System_Name(e_system system) {
        switch (system) {
            case SYSTEM_SHIELDS: return "shields";
            case SYSTEM_DRIVE: return "drive";
            case SYSTEM_COMPUTER: return "computer";
            default: return "nothing";
        }
    }

    void Log_Beam_Shot(int16_t source, int16_t target, int16_t weapon_id, int32_t natural, int32_t bonus,
        int32_t threshold, int32_t damage, int32_t min_damage, int32_t max_damage) {
        if (!combat_log) return;
        const bool critical = natural > 95;
        const int32_t total = critical ? 100 : natural + bonus;
        if (damage > 0) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "combat_dice: stardate %d, %s: ship %d -> ship %d: d100 = %d%s + %d = %d >= %d - hit, damage %d of %d-%d",
                (int)MOX::stardate, Weapon_Name_(weapon_id), (int)source, (int)target, (int)natural, critical ? " (critical)" : "",
                (int)bonus, (int)total, (int)threshold, (int)damage, (int)min_damage, (int)max_damage);
        } else {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "combat_dice: stardate %d, %s: ship %d -> ship %d: d100 = %d + %d = %d < %d - miss",
                (int)MOX::stardate, Weapon_Name_(weapon_id), (int)source, (int)target, (int)natural, (int)bonus, (int)total, (int)threshold);
        }
    }

    void Log_Ordnance_Hit(int16_t source, int16_t target, int16_t weapon_id, int32_t roll, int32_t damage) {
        if (!combat_log) return;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "combat_dice: stardate %d, %s: ship %d -> ship %d: d100 = %d - hit, damage %d",
            (int)MOX::stardate, Weapon_Name_(weapon_id), (int)source, (int)target, (int)roll, (int)damage);
    }

    void Apply_Failure(int16_t target, e_system system) {
        if (target <= 0 || target >= MOX::combat_total_ship_count) return;
        s_combat_data& ship = MOX::combat_data[target];
        switch (system) {
            case SYSTEM_SHIELDS:
                for (int16_t& arc : ship.shield_arc_current) arc = 0;
                break;
            case SYSTEM_DRIVE: ship.movement_left = 0; break;
            case SYSTEM_COMPUTER:
                for (uint8_t& ready : ship.weapon_ready_flags) ready = 0;
                break;
            default: break;
        }
    }

    e_system On_Critical_Hit(int16_t source, int16_t target) {
        if (!combat_crits || target <= 0 || target >= MOX::combat_total_ship_count) return SYSTEM_COUNT;
        const e_system system = static_cast<e_system>(game_random::Random(SYSTEM_COUNT) - 1);
        Apply_Failure(target, system);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "combat_dice: stardate %d, critical hit: ship %d knocks out the %s of ship %d for the turn",
            (int)MOX::stardate, (int)source, System_Name(system), (int)target);
        return system;
    }

} // namespace combat_dice
