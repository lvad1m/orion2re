#include "pch.h"

#include "game/ultimatums.h"

#include "game/diplomac.h"
#include "game/diplomacy_memory.h"
#include "game/opinion.h"
#include "game/pacts.h"

#include <algorithm>
#include <climits>

// The behaviour of the ultimatum with a deadline (header: the design row, quoted); the records, the query of a
// lawful cause and the save section are in ultimatums_state.cpp. Nothing here draws from game_random itself
// (Declare_War does, as it always has).
namespace ultimatums {

    namespace {
        constexpr int8_t TREATY_ALLIANCE = 2;
        constexpr int8_t TREATY_FIRST_WAR = 4;

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        bool Computer_(int16_t player) {
            return Alive_(player) && MOX::player[player].objectives != PLAYER_OBJECTIVE_HUMAN;
        }

        bool At_War_(int16_t a, int16_t b) {
            return MOX::player[a].treaty[b] >= TREATY_FIRST_WAR;
        }

        bool Contact_(int16_t a, int16_t b) {
            return MOX::player[a].contact[b] != 0 && MOX::player[b].contact[a] != 0;
        }

        int32_t Strength_(int16_t of, int16_t against) {
            return MOX::player[of].total_ship_strength[against];
        }

        void Remove_Record_(int32_t index) {
            s_state* s = State();
            for (int32_t i = index; i + 1 < s->count; ++i) s->record[i] = s->record[i + 1];
            s->record[--s->count] = s_record{};
        }

        bool Has_Open_(int16_t demander, int16_t victim) {
            const s_state* s = State();
            for (int32_t i = 0; i < s->count; ++i) {
                if (s->record[i].demander == demander || s->record[i].victim == victim) return true;
            }
            return false;
        }

        // How a computer victim answers a demand for BC by its character: the proud (xenophobic, ruthless,
        // aggressive, honorable) bow only to a fleet BOW_RATIO times their own, the others pay.
        bool Pays_(const s_record& r) {
            const s_player& victim = MOX::player[r.victim];
            if (r.amount <= 0 || victim.bc < r.amount) return false;
            switch (victim.personality) {
                case PLAYER_PERSONALITY_XENOPHOBIC:
                case PLAYER_PERSONALITY_RUTHLESS:
                case PLAYER_PERSONALITY_AGGRESSIVE:
                case PLAYER_PERSONALITY_HONORABLE:
                    return static_cast<int64_t>(Strength_(r.demander, r.victim)) >= static_cast<int64_t>(BOW_RATIO) * std::max(1, Strength_(r.victim, r.demander));
                default:
                    return true;
            }
        }

        // Runs one record a turn; true when it ends.
        bool Run_(s_record& r) {
            const int16_t d = r.demander;
            const int16_t v = r.victim;
            if (!Alive_(d) || !Alive_(v)) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ultimatums: stardate %d, the ultimatum of player %d to player %d is void, a party is gone",
                    (int)MOX::stardate, (int)d, (int)v);
                return true;
            }
            if (At_War_(d, v)) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ultimatums: stardate %d, players %d and %d are at war, the ultimatum is closed",
                    (int)MOX::stardate, (int)d, (int)v);
                return true;
            }
            if (r.state == STATE_OPEN) {
                if (Computer_(v) && Pays_(r)) {
                    MOX::player[v].bc -= r.amount;
                    MOX::player[d].bc = static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(MOX::player[d].bc) + r.amount, INT32_MAX / 2));
                    opinion::Add(v, d, opinion::REASON_BLACKMAILED_US, PAID_OPINION);
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ultimatums: stardate %d, player %d meets the ultimatum of player %d: pays %d BC",
                        (int)MOX::stardate, (int)v, (int)d, (int)r.amount);
                    return true;
                }
                if (MOX::stardate < r.deadline) return false;
                r.state = STATE_CAUSE;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ultimatums: stardate %d, the ultimatum of player %d to player %d runs out unmet: a lawful cause for war",
                    (int)MOX::stardate, (int)d, (int)v);
            }
            if (MOX::stardate >= r.deadline + CAUSE_TURNS) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ultimatums: stardate %d, the lawful cause of player %d against player %d lapses unused",
                    (int)MOX::stardate, (int)d, (int)v);
                return true;
            }
            if (!Computer_(d) || diplomemory::Too_Weary_For_New_War(d) || MOX::player[d].treaty[v] == TREATY_ALLIANCE) return false;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ultimatums: stardate %d, player %d declares a lawful war on player %d for its unmet ultimatum",
                (int)MOX::stardate, (int)d, (int)v);
            diplomac::Declare_War(d, v);  // Break_Treaties and On_Sneak_Attack see the cause (Has_Cause) and charge less
            pacts::On_Aggression(d, v);
            return true;
        }

        void Run_Records_() {
            s_state* s = State();
            for (int32_t i = 0; i < s->count;) {
                const s_record copy = s->record[i];  // Declare_War may reach back here: work on a copy, then find the record again
                s_record r = copy;
                const bool ends = Run_(r);
                const int32_t at = Find(copy.demander, copy.victim);
                if (at < 0) continue;  // gone meanwhile: look at the same index again
                if (ends) {
                    Remove_Record_(at);
                } else {
                    s->record[at] = r;
                    ++i;
                }
            }
        }

        // Every computer that is not honorable or pacifist and not too weary, with no demand open, demands tribute
        // of the richest weaker computer neighbour in contact that it thinks ill of.
        void Make_Demands_() {
            for (int16_t d = 0; d < Players_(); ++d) {
                if (!Computer_(d)) continue;
                const int32_t personality = MOX::player[d].personality;
                if (personality == PLAYER_PERSONALITY_HONORABLE || personality == PLAYER_PERSONALITY_PACIFIST) continue;
                if (diplomemory::Too_Weary_For_New_War(d)) continue;
                int16_t best = -1;
                int32_t best_amount = 0;
                for (int16_t v = 0; v < Players_(); ++v) {
                    if (v == d || !Computer_(v) || !Contact_(d, v) || Has_Open_(d, v)) continue;
                    const int8_t treaty = MOX::player[d].treaty[v];
                    if (treaty >= TREATY_FIRST_WAR || treaty == TREATY_ALLIANCE || At_War_(v, d)) continue;
                    if (opinion::Relations(d, v) >= 0) continue;
                    if (Strength_(d, v) < STRENGTH_RATIO * std::max(1, Strength_(v, d))) continue;
                    const int32_t amount = std::max(DEMAND_MIN, MOX::player[v].bc / 100 * DEMAND_PERCENT);
                    if (MOX::player[v].bc < amount) continue;
                    if (best < 0 || MOX::player[v].bc > MOX::player[best].bc) {
                        best = v;
                        best_amount = amount;
                    }
                }
                if (best >= 0) Issue(d, best, best_amount);
            }
        }
    } // namespace

    bool Issue(int16_t demander, int16_t victim, int32_t amount) {
        if (!enabled || !Computer_(demander) || !Alive_(victim) || demander == victim || amount < 0) return false;
        s_state* s = State();
        if (s->count >= MAX_RECORDS || Find(demander, victim) >= 0) return false;
        s_record r;
        r.demander = static_cast<int8_t>(demander);
        r.victim = static_cast<int8_t>(victim);
        r.state = STATE_OPEN;
        r.issued = MOX::stardate;
        r.deadline = MOX::stardate + DEADLINE_TURNS;
        r.amount = amount;
        s->record[s->count++] = r;
        if (amount > 0) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ultimatums: stardate %d, player %d makes an ultimatum to player %d: %d BC by stardate %d",
                (int)MOX::stardate, (int)demander, (int)victim, (int)amount, (int)r.deadline);
        } else {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ultimatums: stardate %d, player %d makes a demand of the original game to player %d: answer by stardate %d",
                (int)MOX::stardate, (int)demander, (int)victim, (int)r.deadline);
        }
        return true;
    }

    void On_Demand(int16_t demander, int16_t victim) {
        if (!enabled) return;
        Issue(demander, victim, 0);
    }

    void On_Demand_Met(int16_t demander, int16_t victim) {
        if (!enabled) return;
        const int32_t i = Find(demander, victim);
        if (i < 0 || State()->record[i].state != STATE_OPEN) return;
        Remove_Record_(i);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ultimatums: stardate %d, player %d meets the demand of player %d in time",
            (int)MOX::stardate, (int)victim, (int)demander);
    }

    void Turn() {
        if (!enabled) return;
        Run_Records_();
        if (MOX::stardate % PERIOD == PHASE) Make_Demands_();
    }

} // namespace ultimatums
