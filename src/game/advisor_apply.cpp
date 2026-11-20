#include "pch.h"

#include "game/advisor_apply.h"

#include "game/advisor.h"
#include "game/advisor_directive.h"

// Nothing in this file draws from game_random.
namespace advisor {
    namespace {
        constexpr uint8_t LAST_OBJECTIVE = PLAYER_OBJECTIVE_ECOLOGIST;

        // The directive of `player` when the spy mechanic is on and the directive is active and
        // aimed at `target`; nullptr otherwise.
        const s_directive* Spy_Directive_(uint8_t player, uint8_t target) {
            if (!enabled || !rules_spies) return nullptr;
            const s_directive* directive = Get_Directive(player);
            if (directive == nullptr || target == NO_PLAYER || directive->spy_target != target) return nullptr;
            return Is_Active(directive, game::current.Stardate()) ? directive : nullptr;
        }
    } // namespace

    void Apply_Directives(const game::s_view* world) {
        if (!enabled || !rules_objectives || world == nullptr || world->players == nullptr) return;
        const int32_t stardate = world->Stardate();
        int32_t player_count = world->Player_Count();
        if (player_count > MAX_PLAYERS) player_count = MAX_PLAYERS;

        for (int32_t i = 0; i < player_count; i++) {
            // The view's pointer is to non-const players; the pointer to the view is what is const.
            s_player* player = &world->players[i];
            if (player->eliminated != 0 || player->objectives == PLAYER_OBJECTIVE_HUMAN) continue;
            const uint8_t index = (uint8_t)i;

            const s_directive* directive = Get_Directive(index);
            if (directive == nullptr || !Is_Active(directive, stardate)) continue;
            if (directive->objective == OBJECTIVE_KEEP || directive->objective > LAST_OBJECTIVE) continue;
            if (directive->objective == player->objectives) continue;

            const int32_t last = Last_Objective_Change(index);
            if (last != 0 && stardate - last < objective_min_turns) continue;

            player->objectives = directive->objective;
            Set_Last_Objective_Change(index, stardate);
        }
    }

    uint32_t Spy_Target_Weight_Percent(uint8_t player, uint8_t target) {
        if (Spy_Directive_(player, target) == nullptr) return 100;
        return (uint32_t)spy_target_weight_percent;
    }

    bool Colonization_Push(uint8_t player) {
        if (!enabled || !rules_expansion || player >= MAX_PLAYERS) return false;
        if (player >= game::current.Player_Count()) return false;
        const s_player& p = game::current.players[player];
        if (p.eliminated != 0 || p.objectives == PLAYER_OBJECTIVE_HUMAN) return false;
        const s_directive* directive = Get_Directive(player);
        if (directive == nullptr || !Is_Active(directive, game::current.Stardate())) return false;
        const uint8_t level = directive->weights[WEIGHT_COLONIZATION];
        return level >= LEVEL_HIGH && level < LEVEL_COUNT;
    }

    bool Preferred_Spy_Mission(uint8_t player, uint8_t target, int8_t* engine_mission) {
        const s_directive* directive = Spy_Directive_(player, target);
        if (directive == nullptr || engine_mission == nullptr) return false;
        if (directive->spy_mission == SPY_STEAL_TECH) {
            *engine_mission = 0;
            return true;
        }
        if (directive->spy_mission == SPY_SABOTAGE) {
            *engine_mission = 1;
            return true;
        }
        return false;
    }

} // namespace advisor
