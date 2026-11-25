#include "pch.h"

#include "game/loyalty.h"

#include "game/diplomacy_memory.h"
#include "game/officer.h"

#include <algorithm>
#include <iterator>

// Design row (mechanics.md 3.5.6): "Соперничество лидеров | продвинули одного — у другого падает лояльность";
// ab-review 4.4: "включить: дёшево, модуль лояльности". Built (flag loyalty leader_rivalry): when a leader is promoted
// (its level rose, seen in Turn), every other leader of the same empire and kind whose level lies from the
// promoted one's old level up to its new one - a peer who was equalled or overtaken - loses RIVAL_PROMOTED per
// level gained. A leader promoted in the same turn is not a rival of the other. Passing over an idle higher-level
// leader at an appointment (On_Assigned) is the older rule and stays.
// The behaviour of leader loyalty (mechanics.md 3.2.5.1); the state and its save section are in
// loyalty_state.cpp. Nothing here draws from game_random.
namespace loyalty {

    namespace {
        constexpr int8_t LEADER_TYPE_SHIP = 0;
        constexpr int8_t STATUS_POOL = 0;
        constexpr int8_t STATUS_ASSIGNED = 1;
        constexpr int8_t STATUS_LIMBO = 2;

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        // Held by an empire: in its reserve, at a post, or between posts.
        bool Owned_(const s_leader_data& leader) {
            return (leader.status == STATUS_POOL || leader.status == STATUS_ASSIGNED || leader.status == STATUS_LIMBO) &&
                Alive_(leader.player_index);
        }

        void Change_(s_record* r, int32_t delta) {
            if (r == nullptr || r->loyalty == UNTRACKED) return;
            r->loyalty = static_cast<int8_t>(std::clamp<int32_t>(r->loyalty + delta, 0, 100));
        }

        // Average morale of the empire's settled colonies, in morale points.
        int32_t Average_Morale_(int16_t player) {
            int32_t sum = 0;
            int32_t count = 0;
            for (int32_t i = 0; i < MOX::NUM_COLONIES && i < MAX_COLONIES; ++i) {
                const s_colony& c = MOX::colony[i];
                if (c.owner != player || c.outpost_flag != 0) continue;
                sum += c.morale;
                ++count;
            }
            return count > 0 ? sum / count : 0;
        }

        int32_t Weariness_(int16_t player) {
            int32_t weariness = 0;
            for (int16_t k = 0; k < Players_(); ++k) {
                if (k != player) weariness = std::max(weariness, diplomemory::War_Weariness(player, k));
            }
            return weariness;
        }

        // Every promotion of the turn lowers the loyalty of the peers it overtook (see the header comment).
        void Rivalry_(const int8_t* old_level) {
            for (int16_t i = 0; i < MAX_LEADERS; ++i) {
                if (old_level[i] < 0 || !Owned_(MOX::leaders[i])) continue;
                const s_leader_data& winner = MOX::leaders[i];
                const int16_t new_level = OFFICER::Owned_Officer_Level(i);
                for (int16_t j = 0; j < MAX_LEADERS; ++j) {
                    const s_leader_data& other = MOX::leaders[j];
                    s_record* r = Record(j);
                    if (j == i || old_level[j] >= 0 || !Owned_(other) || r->loyalty == UNTRACKED) continue;
                    if (other.player_index != winner.player_index || other.type != winner.type) continue;
                    const int16_t level = OFFICER::Owned_Officer_Level(j);
                    if (level < old_level[i] || level > new_level) continue;
                    Change_(r, RIVAL_PROMOTED * (new_level - old_level[i]));
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "loyalty: stardate %d, player %d: leader %d is promoted over leader %d, whose loyalty falls to %d",
                        (int)MOX::stardate, (int)winner.player_index, (int)i, (int)j, (int)r->loyalty);
                }
            }
        }
    } // namespace

    void Turn() {
        if (!enabled) return;
        const bool step = period >= 1 && MOX::stardate % period == 0;
        int8_t old_level[MAX_LEADERS];  // the level before a promotion this turn, -1 when none (rivalry only)
        if (rivalry) std::fill(std::begin(old_level), std::end(old_level), static_cast<int8_t>(-1));
        for (int16_t i = 0; i < MAX_LEADERS; ++i) {
            const s_leader_data& leader = MOX::leaders[i];
            s_record* r = Record(i);
            if (!Owned_(leader)) {
                *r = s_record{};
                continue;
            }
            const int16_t level = OFFICER::Owned_Officer_Level(i);
            if (r->loyalty == UNTRACKED || r->owner != leader.player_index) {
                *r = s_record{};
                r->loyalty = START;
                r->owner = leader.player_index;
                r->last_level = static_cast<int8_t>(level);
                continue;
            }
            if (level > r->last_level) {
                Change_(r, LEVEL_UP * (level - r->last_level));
                if (rivalry) old_level[i] = r->last_level;
            }
            r->last_level = static_cast<int8_t>(level);
            r->idle = leader.status == STATUS_POOL ? static_cast<int16_t>(std::min<int32_t>(r->idle + 1, 30000)) : 0;
            if (!step) continue;
            if (leader.status == STATUS_ASSIGNED) Change_(r, 1);
            if (r->idle > IDLE_TURNS) Change_(r, -1);
            const int32_t morale = Average_Morale_(leader.player_index);
            if (morale >= MORALE_GOOD) {
                Change_(r, 1);
            } else if (morale <= -MORALE_GOOD) {
                Change_(r, -1);
            }
            if (Weariness_(leader.player_index) >= WEARY) Change_(r, -1);
        }
        if (rivalry) Rivalry_(old_level);
    }

    void On_Battle(int16_t star, int16_t winner, int16_t loser) {
        if (!enabled || star < 0) return;
        for (int16_t i = 0; i < MAX_LEADERS; ++i) {
            const s_leader_data& leader = MOX::leaders[i];
            if (leader.type != LEADER_TYPE_SHIP || leader.status != STATUS_ASSIGNED) continue;
            if (leader.player_index != winner && leader.player_index != loser) continue;
            const int16_t ship = leader.location;
            if (ship < 0 || ship >= MOX::NUM_SHIPS || MOX::ship[ship].location != star) continue;
            Change_(Record(i), leader.player_index == winner ? VICTORY : DEFEAT);
        }
    }

    void On_Assigned(int16_t leader_idx) {
        if (!enabled || leader_idx < 0 || leader_idx >= MAX_LEADERS) return;
        const s_leader_data& assigned = MOX::leaders[leader_idx];
        const int16_t level = OFFICER::Owned_Officer_Level(leader_idx);
        for (int16_t i = 0; i < MAX_LEADERS; ++i) {
            const s_leader_data& other = MOX::leaders[i];
            if (i == leader_idx || other.status != STATUS_POOL || other.player_index != assigned.player_index ||
                other.type != assigned.type) {
                continue;
            }
            if (OFFICER::Owned_Officer_Level(i) > level) Change_(Record(i), PASSED_OVER);
        }
    }

} // namespace loyalty
