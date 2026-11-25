#include "pch.h"

#include "game/vassals.h"

#include "game/diplomac.h"
#include "game/diplomacy_memory.h"
#include "game/opinion.h"

#include <algorithm>

// The behaviour of vassalages (mechanics.md 3.1); the state and its save section are in
// vassals_state.cpp. Nothing here draws from game_random; Declare_War does when an overlord defends.
namespace vassals {

    namespace {
        constexpr int8_t TREATY_ALLIANCE = 2;
        constexpr int8_t TREATY_FIRST_WAR = 4;
        bool defending = false;  // Declare_War leads back to On_Aggression

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

        // Fleet strength of `of` against `against` (the original AI's measure).
        int32_t Strength_(int16_t of, int16_t against) {
            return MOX::player[of].total_ship_strength[against];
        }

        bool Has_Vassal_(int16_t player) {
            for (int16_t v = 0; v < Players_(); ++v) {
                if (State()->overlord[v] == player) return true;
            }
            return false;
        }

        // In danger: at war, or beside a hostile neighbour at least THREAT_STRENGTH_PERCENT as strong.
        bool In_Danger_(int16_t v) {
            for (int16_t w = 0; w < Players_(); ++w) {
                if (w == v || !Alive_(w) || MOX::player[v].contact[w] == 0) continue;
                if (At_War_(v, w)) return true;
                if (opinion::Relations(v, w) <= THREAT_RELATIONS && Strength_(w, v) * 100 >= Strength_(v, w) * THREAT_STRENGTH_PERCENT && Strength_(w, v) > 0) {
                    return true;
                }
            }
            return false;
        }

        void Review_() {
            s_state* s = State();
            for (int16_t v = 0; v < Players_(); ++v) {
                const int16_t o = s->overlord[v];
                if (o < 0) continue;
                if (!Alive_(v) || !Alive_(o)) {
                    Free(v, "a party is gone");
                } else if (At_War_(v, o)) {
                    Free(v, "they are at war");
                } else if (Strength_(v, o) * 100 >= Strength_(o, v) * GROWN_PERCENT && Strength_(v, o) > 0) {
                    Free(v, "the vassal has grown strong");
                }
            }
            for (int16_t v = 0; v < Players_(); ++v) {
                if (!Computer_(v) || s->overlord[v] >= 0 || Has_Vassal_(v) || !In_Danger_(v)) continue;
                int16_t best = -1;
                for (int16_t o = 0; o < Players_(); ++o) {
                    if (o == v || !Computer_(o) || s->overlord[o] >= 0) continue;
                    const s_player& po = MOX::player[o];
                    if (po.contact[v] == 0 || po.within_range[v] != 1 || At_War_(o, v)) continue;
                    if (opinion::Relations(v, o) < 0 || opinion::Relations(o, v) < 0) continue;
                    if (Strength_(v, o) * 100 > Strength_(o, v) * WEAK_PERCENT) continue;
                    if (best < 0 || Strength_(o, v) > Strength_(best, v)) best = o;
                }
                if (best >= 0) Bind(v, best);
            }
        }
    } // namespace

    bool Bind(int16_t vassal, int16_t overlord) {
        if (!enabled || !Alive_(vassal) || !Alive_(overlord) || vassal == overlord) return false;
        s_state* s = State();
        if (s->overlord[vassal] >= 0 || s->overlord[overlord] >= 0) return false;
        s->overlord[vassal] = static_cast<int8_t>(overlord);
        s->since[vassal] = MOX::stardate;
        diplomac::Start_Tribute_Treaty(vassal, overlord, VASSAL_TRIBUTE);
        opinion::Add(vassal, overlord, opinion::REASON_OVERLORD, PROTECTED_OPINION);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "vassals: stardate %d, player %d becomes the vassal of player %d",
            (int)MOX::stardate, (int)vassal, (int)overlord);
        return true;
    }

    void Free(int16_t vassal, const char* why) {
        if (vassal < 0 || vassal >= MAX_PLAYERS) return;
        s_state* s = State();
        const int16_t overlord = s->overlord[vassal];
        if (overlord < 0) return;
        s->overlord[vassal] = -1;
        s->since[vassal] = 0;
        if (overlord < MOX::NUM_PLAYERS && MOX::player[vassal].tribute_treaty[overlord] == VASSAL_TRIBUTE) {
            MOX::player[vassal].tribute_treaty[overlord] = 0;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "vassals: stardate %d, player %d is no longer the vassal of player %d: %s",
            (int)MOX::stardate, (int)vassal, (int)overlord, why != nullptr ? why : "");
    }

    void On_Aggression(int16_t aggressor, int16_t victim) {
        if (!enabled || defending || !Alive_(aggressor) || !Alive_(victim) || aggressor == victim) return;
        const int16_t overlord = Overlord(victim);
        if (overlord < 0 || !Alive_(overlord)) return;
        if (overlord == aggressor) {
            opinion::Add(victim, overlord, opinion::REASON_OVERLORD, FAILED_OPINION);
            Free(victim, "the overlord attacked it");
            return;
        }
        const s_player& po = MOX::player[overlord];
        if (At_War_(overlord, aggressor) || po.contact[aggressor] == 0) return;  // already fighting, or no way to
        if (po.treaty[aggressor] == TREATY_ALLIANCE || diplomemory::Too_Weary_For_New_War(overlord)) {
            opinion::Add(victim, overlord, opinion::REASON_OVERLORD, FAILED_OPINION);
            Free(victim, "the overlord did not defend it");
            return;
        }
        defending = true;
        diplomac::Declare_War(overlord, aggressor);
        defending = false;
        opinion::Add(victim, overlord, opinion::REASON_OVERLORD, PROTECTED_OPINION);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "vassals: stardate %d, player %d defends its vassal player %d: war on player %d",
            (int)MOX::stardate, (int)overlord, (int)victim, (int)aggressor);
    }

    void Turn() {
        if (!enabled || MOX::stardate % PERIOD != 5) return;
        Review_();
    }

} // namespace vassals
