#include "pch.h"

#include "game/hostages.h"

#include "game/colcalc.h"
#include "game/officer.h"
#include "game/opinion.h"
#include "game/random.h"

#include <algorithm>

// The behaviour of hostages and captured leaders (mechanics.md 3.5.3, events-catalog 3.12); the held
// leaders and the save section are in hostages_state.cpp. game_random is drawn only with diplomacy
// prisoners on and a prisoner held (the escape roll), once a turn per prisoner.
namespace hostages {

    namespace {
        constexpr int8_t LEADER_DEAD = -2;
        constexpr int8_t LEADER_POOL = 0;
        constexpr int8_t LEADER_ASSIGNED = 1;

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        bool Computer_(int16_t player) {
            return Alive_(player) && MOX::player[player].objectives != PLAYER_OBJECTIVE_HUMAN;
        }

        int32_t Strength_(int16_t of, int16_t against) {
            return MOX::player[of].total_ship_strength[against];
        }

        bool Kind_On_(uint8_t kind) {
            return kind == KIND_HOSTAGE ? hostage_swaps : prisoners;
        }

        const char* Kind_Name_(uint8_t kind) {
            return kind == KIND_HOSTAGE ? "hostage" : "prisoner";
        }

        // The holder's mind about a hostage whose giver broke a treaty: 0 execute, 1 send home, 2 keep as a ward.
        int Fate_(int16_t holder) {
            switch (MOX::player[holder].personality) {
                case PLAYER_PERSONALITY_RUTHLESS:
                case PLAYER_PERSONALITY_AGGRESSIVE:
                    return 0;
                case PLAYER_PERSONALITY_HONORABLE:
                case PLAYER_PERSONALITY_PACIFIST:
                    return 1;
                default:
                    return 2;
            }
        }

        void Remove_Record_(int32_t index) {
            s_state* s = State();
            for (int32_t i = index; i + 1 < s->count; ++i) s->held[i] = s->held[i + 1];
            s->held[--s->count] = s_held{};
        }

        void Exchange_And_Ransom_() {
            s_state* s = State();
            // Exchanges: one for one between two computers that hold each other's leaders.
            for (int32_t i = 0; i < s->count; ++i) {
                const s_held a = s->held[i];
                if (a.kind != KIND_PRISONER || !Computer_(a.owner) || !Computer_(a.holder)) continue;
                for (int32_t j = i + 1; j < s->count; ++j) {
                    const s_held b = s->held[j];
                    if (b.kind != KIND_PRISONER || b.owner != a.holder || b.holder != a.owner) continue;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, players %d and %d exchange leaders %d and %d",
                        (int)MOX::stardate, (int)a.owner, (int)a.holder, (int)a.leader, (int)b.leader);
                    Release(j, b.owner, "exchanged");  // the higher index first: the records move up
                    Release(i, a.owner, "exchanged");
                    --i;  // the record at i is a new one now
                    break;
                }
            }
            // Ransom: a computer owner that keeps a reserve buys its leader back.
            for (int32_t i = 0; i < s->count;) {
                const s_held h = s->held[i];
                const int32_t ransom = Ransom_Of(h.leader);
                if (h.kind != KIND_PRISONER || !Computer_(h.owner) || !Alive_(h.holder) ||
                    MOX::player[h.owner].bc - ransom < RANSOM_RESERVE) {
                    ++i;
                    continue;
                }
                MOX::player[h.owner].bc -= ransom;
                MOX::player[h.holder].bc += ransom;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, player %d pays %d BC to player %d for leader %d",
                    (int)MOX::stardate, (int)h.owner, (int)ransom, (int)h.holder, (int)h.leader);
                Release(i, h.owner, "ransomed");
            }
        }
    } // namespace

    bool Hold(int16_t leader, int16_t holder, uint8_t kind) {
        if (!Kind_On_(kind) || leader < 0 || leader >= MAX_LEADERS || !Alive_(holder)) return false;
        s_state* s = State();
        if (s->count >= MAX_HELD || Find(leader) >= 0) return false;
        s_leader_data& l = MOX::leaders[leader];
        const int16_t owner = l.player_index;
        if (owner < 0 || owner >= MAX_PLAYERS || owner == holder) return false;
        if (l.status != LEADER_POOL && l.status != LEADER_ASSIGNED) return false;
        OFFICER::Kill_Officer(leader);  // out of service: post cleared, colonies recalculated, status -2
        s->held[s->count++] = {static_cast<int8_t>(leader), static_cast<int8_t>(owner), static_cast<int8_t>(holder), kind, MOX::stardate};
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, player %d holds leader %d of player %d as a %s",
            (int)MOX::stardate, (int)holder, (int)leader, (int)owner, Kind_Name_(kind));
        return true;
    }

    void Release(int32_t index, int16_t to, const char* how) {
        s_state* s = State();
        if (index < 0 || index >= s->count) return;
        const s_held h = s->held[index];
        Remove_Record_(index);
        s_leader_data& l = MOX::leaders[h.leader];
        l.status = LEADER_POOL;
        l.location = -1;
        l.eta = 0;
        l.player_index = static_cast<int8_t>(to);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, leader %d of player %d (held by player %d) goes to player %d: %s",
            (int)MOX::stardate, (int)h.leader, (int)h.owner, (int)h.holder, (int)to, how != nullptr ? how : "");
    }

    void Execute(int32_t index, const char* why) {
        s_state* s = State();
        if (index < 0 || index >= s->count) return;
        const s_held h = s->held[index];
        Remove_Record_(index);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, player %d executes leader %d of player %d: %s",
            (int)MOX::stardate, (int)h.holder, (int)h.leader, (int)h.owner, why != nullptr ? why : "");
    }

    int16_t Pick_Hostage(int16_t giver) {
        if (giver < 0 || giver >= MAX_PLAYERS) return -1;
        int16_t capital_governor = -1;
        const int16_t home = MOX::player[giver].home_planet_id;
        if (home >= 0 && home < MAX_PLANETS) {
            const int16_t star = MOX::planet[home].star_index;
            if (star >= 0 && star < MAX_STARS) capital_governor = MOX::star[star].officer_index[giver];
        }
        int16_t best = -1;
        for (int16_t i = 0; i < MAX_LEADERS; ++i) {
            const s_leader_data& l = MOX::leaders[i];
            if (l.player_index != giver || (l.status != LEADER_POOL && l.status != LEADER_ASSIGNED) || i == capital_governor || Find(i) >= 0) continue;
            if (best < 0 || l.level < MOX::leaders[best].level) best = i;
        }
        return best;
    }

    void On_Alliance(int16_t a, int16_t b) {
        if (!hostage_swaps || a == b || !Computer_(a) || !Computer_(b)) return;
        const int16_t giver = Strength_(a, b) <= Strength_(b, a) ? a : b;
        const int16_t holder = giver == a ? b : a;
        const s_state* s = State();
        for (int32_t i = 0; i < s->count; ++i) {
            if (s->held[i].kind == KIND_HOSTAGE && s->held[i].owner == giver && s->held[i].holder == holder) return;  // one is already held
        }
        const int16_t leader = Pick_Hostage(giver);
        if (leader >= 0) Hold(leader, holder, KIND_HOSTAGE);
    }

    void On_Treaty_Broken(int16_t breaker, int16_t victim) {
        if (!hostage_swaps || !Computer_(victim)) return;
        s_state* s = State();
        for (int32_t i = 0; i < s->count;) {
            const s_held h = s->held[i];
            if (h.kind != KIND_HOSTAGE || h.owner != breaker || h.holder != victim) {
                ++i;
                continue;
            }
            switch (Fate_(victim)) {
                case 0:
                    opinion::Add(breaker, victim, opinion::REASON_HARMED_A_HOSTAGE, EXECUTED_HOSTAGE_OPINION);
                    Execute(i, "the giver broke a treaty");
                    break;
                case 1:
                    Release(i, breaker, "the holder sends the hostage home");
                    break;
                default:
                    Release(i, victim, "the holder keeps the hostage as a ward");
                    break;
            }
        }
    }

    bool On_Colony_Invaded(const s_colony* colony, int16_t new_owner) {
        if (!prisoners || colony == nullptr) return false;
        const int16_t owner = colony->owner;
        if (owner == new_owner || owner < 0 || owner >= MAX_PLAYERS || !Alive_(new_owner)) return false;
        const int16_t star = MOX::planet[colony->planet].star_index;
        // The governor is lost only with the owner's last colony at the star (as BOMB::Handle_Leader_At_Lost_Colony).
        for (int32_t slot = 0; slot < 5; ++slot) {
            const int16_t p = MOX::star[star].planet_index[slot];
            if (p == -1) continue;
            const int16_t c = MOX::planet[p].colony_index;
            if (c != -1 && &MOX::colony[c] != colony && MOX::colony[c].owner == owner) return false;
        }
        const int leader = COLCALC::Colony_Officer(const_cast<s_colony*>(colony));
        if (leader < 0) return false;
        return Hold(static_cast<int16_t>(leader), new_owner, KIND_PRISONER);
    }

    int32_t Ransom_Of(int16_t leader) {
        if (leader < 0 || leader >= MAX_LEADERS) return RANSOM_BASE;
        return RANSOM_BASE + RANSOM_PER_LEVEL * static_cast<int32_t>(MOX::leaders[leader].level);
    }

    bool Escape_With_Roll(int32_t index, int32_t roll) {
        if (!prisoners) return false;
        const s_state* s = State();
        if (index < 0 || index >= s->count || s->held[index].kind != KIND_PRISONER || roll >= ESCAPE_PERCENT) return false;
        Release(index, s->held[index].owner, "escaped");
        return true;
    }

    void Turn() {
        s_state* s = State();
        for (int32_t i = 0; i < s->count;) {
            const s_held h = s->held[i];
            if (!Kind_On_(h.kind)) {
                ++i;
                continue;
            }
            if (!Alive_(h.owner)) {
                if (Alive_(h.holder)) {
                    Release(i, h.holder, "its owner is gone");
                } else {
                    Execute(i, "both empires are gone");
                }
                continue;
            }
            if (!Alive_(h.holder)) {
                Release(i, h.owner, "its holder is gone");
                continue;
            }
            if (h.kind == KIND_HOSTAGE) {
                if (MOX::stardate - h.since >= HOSTAGE_TURNS) {
                    opinion::Add(h.owner, h.holder, opinion::REASON_FAMILY_TIES, RETURNED_OPINION);
                    Release(i, h.owner, "the term is over");
                    continue;
                }
                ++i;
                continue;
            }
            if (Escape_With_Roll(i, static_cast<int32_t>(game_random::Random(100)))) continue;
            if (MOX::stardate - h.since >= PRISONER_TURNS && Computer_(h.holder)) {
                if (MOX::player[h.holder].personality == PLAYER_PERSONALITY_RUTHLESS) {
                    opinion::Add(h.owner, h.holder, opinion::REASON_HARMED_A_HOSTAGE, EXECUTED_PRISONER_OPINION);
                    Execute(i, "held too long");
                } else {
                    Release(i, h.owner, "held too long, released");
                }
                continue;
            }
            ++i;
        }
        if (prisoners && MOX::stardate % PERIOD == PHASE) Exchange_And_Ransom_();
    }

} // namespace hostages
