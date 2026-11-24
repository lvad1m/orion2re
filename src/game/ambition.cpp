#include "pch.h"

#include "game/ambition.h"

#include "game/bill.h"
#include "game/invasion.h"
#include "game/loyalty.h"
#include "game/officer.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/secrets.h"

#include <algorithm>

// The behaviour of the ambitious governor and council bribery (mechanics.md 3.5.6, 3.5.3). Nothing is saved:
// a defection is final at once and the bribes live for one council vote. game_random is drawn only with
// ambitious_governors on (one roll per restless governor on PHASE) and with council_bribery on (one roll per
// briber that has a rival with spies in it).
namespace ambition {

    namespace {
        constexpr int8_t TREATY_FIRST_WAR = 4;
        constexpr int8_t LEADER_TYPE_COLONY = 1;
        constexpr int8_t STATUS_POOL = 0;
        constexpr int8_t STATUS_ASSIGNED = 1;

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        bool Computer_(int16_t player) {
            return Alive_(player) && MOX::player[player].objectives != PLAYER_OBJECTIVE_HUMAN;
        }

        int16_t Star_Of_(const s_colony& colony) {
            if (colony.planet < 0 || colony.planet >= MAX_PLANETS) return -1;
            return MOX::planet[colony.planet].star_index;
        }

        int16_t Capital_Star_(int16_t player) {
            const int16_t home = MOX::player[player].home_planet_id;
            if (home < 0 || home >= MAX_PLANETS) return -1;
            return MOX::planet[home].star_index;
        }

        // Squared distance between two stars.
        int32_t Distance_(int16_t a, int16_t b) {
            const int32_t dx = MOX::star[a].x - MOX::star[b].x;
            const int32_t dy = MOX::star[a].y - MOX::star[b].y;
            return dx * dx + dy * dy;
        }

        // The squared distance from `star` to the nearest settled colony of `empire`, -1 when it has none.
        int32_t Nearest_Colony_(int16_t empire, int16_t star) {
            int32_t best = -1;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                const s_colony& colony = MOX::colony[c];
                if (colony.owner != empire || colony.outpost_flag != 0) continue;
                const int16_t s = Star_Of_(colony);
                if (s < 0 || s >= MAX_STARS) continue;
                const int32_t d = Distance_(star, s);
                if (best < 0 || d < best) best = d;
            }
            return best;
        }

        void Defect_All_() {
            for (int16_t i = 0; i < MAX_LEADERS; ++i) {
                if (!Is_Restless(i)) continue;
                Try_Defect(i, static_cast<int32_t>(game_random::Random(100)));
            }
        }

        // The secret that `holder` keeps about `owner`, -1 when there is none that still holds.
        int32_t Secret_About_(int16_t holder, int16_t owner) {
            if (!secrets::enabled) return -1;
            int32_t count = 0;
            const secrets::s_secret* slots = secrets::Secrets(&count);
            for (int32_t i = 0; i < count; ++i) {
                const secrets::s_secret& s = slots[i];
                if (s.kind != secrets::KIND_NONE && s.holder == holder && s.owner == owner && secrets::Still_True(s)) return i;
            }
            return -1;
        }

        // Transient: what each voter was given for each candidate in this vote.
        int16_t bias[MAX_PLAYERS][MAX_PLAYERS];
    } // namespace

    bool Is_Restless(int16_t leader) {
        if (!ambitious_governors || !loyalty::enabled || leader < 0 || leader >= MAX_LEADERS) return false;
        const s_leader_data& l = MOX::leaders[leader];
        if (l.type != LEADER_TYPE_COLONY || l.status != STATUS_ASSIGNED || !Computer_(l.player_index)) return false;
        if (l.location < 0 || l.location >= MAX_STARS || l.location == Capital_Star_(l.player_index)) return false;
        const int32_t loyal = loyalty::Of(leader);
        if (loyal == loyalty::UNTRACKED || loyal > MAX_LOYALTY) return false;
        return OFFICER::Owned_Officer_Level(leader) >= MIN_LEVEL;
    }

    int16_t Pick_Neighbour(int16_t leader) {
        if (leader < 0 || leader >= MAX_LEADERS) return -1;
        const s_leader_data& l = MOX::leaders[leader];
        const int16_t owner = l.player_index;
        if (!Computer_(owner) || l.location < 0 || l.location >= MAX_STARS) return -1;
        int16_t best = -1;
        int32_t best_preference = -1;
        int32_t best_distance = 0;
        for (int16_t e = 0; e < Players_(); ++e) {
            if (e == owner || !Computer_(e) || MOX::player[owner].contact[e] == 0) continue;
            const int32_t distance = Nearest_Colony_(e, l.location);
            if (distance < 0) continue;
            int32_t preference = 0;
            if (MOX::player[owner].treaty[e] >= TREATY_FIRST_WAR) {
                preference = 2;
            } else if (opinion::Relations(owner, e) <= DISLIKE_RELATIONS) {
                preference = 1;
            }
            if (best < 0 || preference > best_preference || (preference == best_preference && distance < best_distance)) {
                best = e;
                best_preference = preference;
                best_distance = distance;
            }
        }
        return best;
    }

    bool Try_Defect(int16_t leader, int32_t roll) {
        if (!Is_Restless(leader) || roll >= DEFECT_PERCENT) return false;
        s_leader_data& l = MOX::leaders[leader];
        const int16_t owner = l.player_index;
        const int16_t star = l.location;
        const int16_t target = Pick_Neighbour(leader);
        if (target < 0) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ambition: stardate %d, governor %d of player %d deserts",
                (int)MOX::stardate, (int)leader, (int)owner);
            OFFICER::Kill_Officer(leader);  // post cleared, colonies recalculated
            l.status = -1;                  // back to the hire list
            l.player_index = -1;
            l.location = -1;
            l.eta = 0;
            return true;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ambition: stardate %d, governor %d of player %d defects with its colonies to player %d",
            (int)MOX::stardate, (int)leader, (int)owner, (int)target);
        OFFICER::Deassign_Officer(leader);
        for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
            s_colony& colony = MOX::colony[c];
            if (colony.owner != owner || Star_Of_(colony) != star) continue;
            invasion::Change_Colony_Ownership(&colony, static_cast<uint32_t>(target), nullptr);
        }
        l.player_index = static_cast<int8_t>(target);  // it joins the new owner's reserve
        l.status = STATUS_POOL;
        l.location = -1;
        l.eta = 0;
        opinion::Add(owner, target, opinion::REASON_PLOTTED_AGAINST_US, DEFECTION_OPINION);
        return true;
    }

    void Turn() {
        if (!ambitious_governors || !loyalty::enabled || MOX::stardate % PERIOD != PHASE) return;
        Defect_All_();
    }

    void Before_Council_Vote(int16_t nominee0, int16_t nominee1) {
        std::fill(&bias[0][0], &bias[0][0] + MAX_PLAYERS * MAX_PLAYERS, int16_t{0});
        if (!council_bribery) return;
        const int16_t nominees[2] = {nominee0, nominee1};
        for (int n = 0; n < 2; ++n) {
            const int16_t briber = nominees[n];
            const int16_t rival = nominees[1 - n];
            if (!Computer_(briber) || briber == rival) continue;
            const bool rich = MOX::player[briber].bc >= MIN_BC_TO_BRIBE;  // checked once, before the first purchase
            int32_t bought = 0;
            for (int16_t voter = 0; voter < Players_(); ++voter) {
                if (voter == nominee0 || voter == nominee1 || !Computer_(voter)) continue;
                if (bias[voter][nominee0] != 0 || bias[voter][nominee1] != 0) continue;  // already bought
                const int32_t secret = Secret_About_(briber, voter);
                if (secret >= 0) {
                    secrets::Slots()[secret] = secrets::s_secret{};
                    bias[voter][briber] = static_cast<int16_t>(BLACKMAIL_BIAS);
                    ++bought;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ambition: stardate %d, player %d blackmails player %d for its council vote",
                        (int)MOX::stardate, (int)briber, (int)voter);
                    continue;
                }
                s_player& payer = MOX::player[briber];
                if (!rich || payer.bc - BRIBE_PRICE < BRIBE_RESERVE ||
                    opinion::Relations(voter, briber) < BRIBE_MIN_RELATIONS) {
                    continue;
                }
                payer.bc -= BRIBE_PRICE;
                MOX::player[voter].bc += BRIBE_PRICE;
                bias[voter][briber] = static_cast<int16_t>(BRIBE_BIAS);
                ++bought;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ambition: stardate %d, player %d bribes player %d with %d BC for its council vote",
                    (int)MOX::stardate, (int)briber, (int)voter, (int)BRIBE_PRICE);
            }
            if (bought > 0 && Alive_(rival) && BILL::Get_Their_Spy_Number(rival, briber) > 0 &&
                static_cast<int32_t>(game_random::Random(100)) < EXPOSURE_PERCENT) {
                opinion::Add(rival, briber, opinion::REASON_PLOTTED_AGAINST_US, EXPOSURE_OPINION);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ambition: stardate %d, player %d learns of the council bribes of player %d",
                    (int)MOX::stardate, (int)rival, (int)briber);
            }
        }
    }

    int32_t Council_Vote_Bias(int16_t voter, int16_t candidate) {
        if (!council_bribery || voter < 0 || voter >= MAX_PLAYERS || candidate < 0 || candidate >= MAX_PLAYERS) return 0;
        return bias[voter][candidate];
    }

    void After_Council_Vote() {
        if (!council_bribery) return;
        std::fill(&bias[0][0], &bias[0][0] + MAX_PLAYERS * MAX_PLAYERS, int16_t{0});
    }

} // namespace ambition
