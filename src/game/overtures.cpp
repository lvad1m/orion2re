#include "pch.h"

#include "game/overtures.h"

#include "game/colcalc.h"
#include "game/diplomac.h"
#include "game/diplomacy_memory.h"
#include "game/intrigue.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/ships.h"
#include "game/techdata.h"

#include <algorithm>
#include <climits>

// The behaviour of the four overtures (events-catalog.md 3.5 and 3.12); the records and the save section are
// in overtures_state.cpp. game_random is drawn only with events overtures on: the chance of a new event, the
// pick of a leaked application, the chance of peace each turn, and Declare_War when an ally goes to war.
namespace overtures {

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
            return MOX::player[a].contact[b] != 0 || MOX::player[b].contact[a] != 0;
        }

        int32_t Pick_(int32_t n) {
            return n <= 1 ? 0 : static_cast<int32_t>(game_random::Random(n)) - 1;
        }

        void Log_(int16_t player, const char* what) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "overtures: stardate %d, player %d: %s", (int)MOX::stardate, (int)player, what);
        }

        void Remove_Record_(int32_t index) {
            s_state* s = State();
            for (int32_t i = index; i + 1 < s->count; ++i) s->record[i] = s->record[i + 1];
            s->record[--s->count] = s_record{};
        }

        void Add_Bc_(int16_t player, int32_t delta) {
            s_player& me = MOX::player[player];
            me.bc = static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(me.bc) + delta, 0, INT32_MAX / 2));
        }

        bool Known_(const s_player& p, int32_t app) {
            return p.tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED;
        }

        int64_t App_Cost_(int32_t app) {
            const int16_t field = TECHDATA::technology_applications[app].tech_field_id;
            return field >= 0 && field < TECH_FIELD_COUNT ? static_cast<int64_t>(TECHDATA::technology_fields[field].cost) : INT64_MAX;
        }

        // A ship that is still alive: SHIPS::Kill_Ship leaves the owner and sets status 5 (retired).
        bool Live_Ship_(int16_t ship, int16_t player) {
            return MOX::ship[ship].owner == player && MOX::ship[ship].status != 5;
        }

        int32_t Ship_Count_(int16_t player) {
            int32_t n = 0;
            for (int16_t s = 0; s < MOX::NUM_SHIPS && s < MAX_SHIPS; ++s) {
                if (Live_Ship_(s, player)) ++n;
            }
            return n;
        }

        // Scraps up to `count` of the cheapest ships of `player` (the lowest index among equals); returns how many.
        int32_t Scrap_Cheapest_(int16_t player, int32_t count) {
            int32_t done = 0;
            for (; done < count; ++done) {
                int16_t best = -1;
                for (int16_t s = 0; s < MOX::NUM_SHIPS && s < MAX_SHIPS; ++s) {
                    if (!Live_Ship_(s, player)) continue;
                    if (best < 0 || MOX::ship[s].d.cost < MOX::ship[best].d.cost) best = s;
                }
                if (best < 0) break;
                SHIPS::Kill_Ship(best);
            }
            return done;
        }

        // Unknown applications `player` may get, the cheapest (by field cost) first; returns how many.
        int32_t Unknown_Apps_(int16_t player, int16_t* out, int32_t capacity) {
            s_player* me = &MOX::player[player];
            int32_t n = 0;
            for (int32_t app = TECH_APP_NO_TECH + 1; app < TECH_APP_COUNT && n < capacity; ++app) {
                if (!Known_(*me, app) && COLCALC::Player_May_Get_Tech_App(me, app)) out[n++] = static_cast<int16_t>(app);
            }
            std::stable_sort(out, out + n, [](int16_t a, int16_t b) { return App_Cost_(a) < App_Cost_(b) || (App_Cost_(a) == App_Cost_(b) && a < b); });
            return n;
        }

        void Give_App_(int16_t player, int32_t app) {
            COLCALC::Player_Gets_Tech_App(&MOX::player[player], static_cast<int16_t>(app));
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "overtures: stardate %d, player %d: gets application %d", (int)MOX::stardate, (int)player, app);
        }

        bool Start_Hand_(int16_t player, int32_t option) {
            if (Ship_Count_(player) <= HAND_SHIPS) return false;
            bool contact = false;
            for (int16_t o = 0; o < Players_(); ++o) contact = contact || (o != player && Alive_(o) && Contact_(player, o));
            if (!contact) return false;
            int16_t apps[TECH_APP_COUNT];
            const int32_t n = Unknown_Apps_(player, apps, TECH_APP_COUNT);
            if (n == 0) return false;
            switch (option) {
                case HAND_ACCEPT: {
                    Scrap_Cheapest_(player, HAND_SHIPS);
                    const int32_t count = std::min(n, HAND_APPS);
                    for (int32_t i = 0; i < count; ++i) Give_App_(player, apps[i]);
                    Log_(player, "accepts the outstretched hand: the fleet is cut, the scientists share their work");
                    break;
                }
                case HAND_BARGAIN:
                    Scrap_Cheapest_(player, 1);
                    Give_App_(player, apps[0]);
                    Add_Bc_(player, HAND_BARGAIN_BC);
                    Log_(player, "bargains with the scientists: one ship for one discovery and some money");
                    break;
                default:
                    Add_Bc_(player, HAND_LOOT);
                    for (int16_t o = 0; o < Players_(); ++o) {
                        if (o != player && Alive_(o) && Contact_(player, o)) opinion::Add(o, player, opinion::REASON_BAD_RUMOURS, HAND_SEIZE_OPINION);
                    }
                    Log_(player, "seizes the delegation of scientists");
                    break;
            }
            return true;
        }

        bool Start_Leak_(int16_t player, int32_t option) {
            const s_player& me = MOX::player[player];
            // Applications known here and missing at an empire in contact.
            int16_t apps[TECH_APP_COUNT];
            int32_t n = 0;
            for (int32_t app = TECH_APP_NO_TECH + 1; app < TECH_APP_COUNT; ++app) {
                if (!Known_(me, app)) continue;
                for (int16_t o = 0; o < Players_(); ++o) {
                    if (o != player && Alive_(o) && Contact_(player, o) && !Known_(MOX::player[o], app) &&
                        COLCALC::Player_May_Get_Tech_App(&MOX::player[o], app)) {
                        apps[n++] = static_cast<int16_t>(app);
                        break;
                    }
                }
            }
            if (n == 0) return false;
            const int16_t app = apps[Pick_(n)];
            int16_t takers[MAX_PLAYERS];
            int32_t t = 0;
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o != player && Alive_(o) && Contact_(player, o) && !Known_(MOX::player[o], app) &&
                    COLCALC::Player_May_Get_Tech_App(&MOX::player[o], app)) {
                    takers[t++] = o;
                }
            }
            switch (option) {
                case LEAK_SHARE:
                    for (int32_t i = 0; i < t && i < LEAK_MAX_SHARE; ++i) {
                        Give_App_(takers[i], app);
                        opinion::Add(takers[i], player, opinion::REASON_DID_US_A_SERVICE, LEAK_SHARE_OPINION);
                    }
                    Log_(player, "shares a discovery that was about to leak");
                    break;
                case LEAK_SELL: {
                    int16_t buyer = -1;
                    for (int32_t i = 0; i < t; ++i) {
                        if (MOX::player[takers[i]].bc >= LEAK_PRICE && (buyer < 0 || MOX::player[takers[i]].bc > MOX::player[buyer].bc)) buyer = takers[i];
                    }
                    if (buyer < 0) {
                        Log_(player, "finds no buyer for a discovery that was about to leak");
                        break;
                    }
                    Add_Bc_(buyer, -LEAK_PRICE);
                    Add_Bc_(player, LEAK_PRICE);
                    Give_App_(buyer, app);
                    Log_(player, "sells a discovery that was about to leak");
                    break;
                }
                default:
                    Add_Bc_(player, -std::min(LEAK_SUPPRESS_COST, MOX::player[player].bc));
                    Log_(player, "suppresses a discovery that was about to leak");
                    break;
            }
            return true;
        }

        bool Pair_Busy_(int16_t a, int16_t b) {
            const s_state* s = State();
            for (int32_t i = 0; i < s->count; ++i) {
                const s_record& r = s->record[i];
                if (r.kind == KIND_MEDIATOR && ((r.target == a && r.other == b) || (r.target == b && r.other == a))) return true;
            }
            return false;
        }

        bool Record_(const s_record& record) {
            s_state* s = State();
            if (s->count >= MAX_RECORDS) return false;
            s->record[s->count++] = record;
            return true;
        }

        bool Start_Mediator_(int16_t player) {
            if (!Computer_(player)) return false;
            for (int16_t enemy = 0; enemy < Players_(); ++enemy) {
                if (enemy == player || !Computer_(enemy) || !At_War_(player, enemy) || Pair_Busy_(player, enemy)) continue;
                int16_t mediator = -1;
                for (int16_t m = 0; m < Players_(); ++m) {
                    if (m == player || m == enemy || !Computer_(m)) continue;
                    if (!Contact_(m, player) || !Contact_(m, enemy) || At_War_(m, player) || At_War_(m, enemy)) continue;
                    if (opinion::Relations(m, player) < 0 || opinion::Relations(m, enemy) < 0) continue;
                    if (mediator < 0 || MOX::player[m].total_ship_strength[player] > MOX::player[mediator].total_ship_strength[player]) mediator = m;
                }
                if (mediator < 0) continue;
                s_record r;
                r.kind = KIND_MEDIATOR;
                r.player = static_cast<int8_t>(mediator);
                r.target = player;
                r.other = enemy;
                r.started = MOX::stardate;
                r.ends = MOX::stardate + PEACE_TURNS;
                if (!Record_(r)) return false;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "overtures: stardate %d, player %d offers to mediate between players %d and %d",
                    (int)MOX::stardate, (int)mediator, (int)player, (int)enemy);
                return true;
            }
            return false;
        }

        bool Start_Offensive_(int16_t payer, int32_t option) {
            if (!Computer_(payer)) return false;
            const int32_t payment = std::max(PAYMENT_MIN, MOX::player[payer].bc / 100 * PAYMENT_PERCENT[option]);
            if (MOX::player[payer].bc < payment) return false;
            for (int16_t enemy = 0; enemy < Players_(); ++enemy) {
                if (enemy == payer || !Alive_(enemy) || !At_War_(payer, enemy)) continue;
                int16_t ally = -1;
                int32_t best = INT_MIN;
                for (int16_t a = 0; a < Players_(); ++a) {
                    if (a == payer || a == enemy || !Computer_(a)) continue;
                    if (!Contact_(a, payer) || !Contact_(a, enemy) || At_War_(a, payer)) continue;
                    if (MOX::player[a].treaty[enemy] == TREATY_ALLIANCE || diplomemory::Too_Weary_For_New_War(a)) continue;
                    const int32_t liking = opinion::Relations(a, payer);
                    if (liking < 0) continue;
                    const int32_t score = liking - opinion::Relations(a, enemy);
                    if (score > best) {
                        best = score;
                        ally = a;
                    }
                }
                if (ally < 0) continue;
                Add_Bc_(payer, -payment);
                Add_Bc_(ally, payment);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "overtures: stardate %d, player %d pays %d BC to player %d for an offensive against player %d",
                    (int)MOX::stardate, (int)payer, (int)payment, (int)ally, (int)enemy);
                if (!At_War_(ally, enemy)) diplomac::Declare_War(ally, enemy);
                s_record r;
                r.kind = KIND_OFFENSIVE;
                r.player = static_cast<int8_t>(payer);
                r.target = ally;
                r.other = enemy;
                r.started = MOX::stardate;
                r.ends = MOX::stardate + OFFENSIVE_TURNS;
                r.option = static_cast<int8_t>(option);
                r.value = static_cast<int16_t>(std::min<int32_t>(payment, INT16_MAX));
                Record_(r);  // a full table only means the deal is not judged later
                return true;
            }
            return false;
        }

        // Runs one record a turn; true when it ends.
        bool Run_(s_record& r) {
            if (r.kind == KIND_MEDIATOR) {
                const int16_t a = r.target;
                const int16_t b = r.other;
                if (!Alive_(r.player) || !Alive_(a) || !Alive_(b) || !At_War_(a, b)) {
                    Log_(r.player, "the mediation is void, the war or a party is gone");
                    return true;
                }
                const int32_t weariness = (diplomemory::War_Weariness(a, b) + diplomemory::War_Weariness(b, a)) / 2;
                const int32_t chance = std::min(PEACE_MAX, PEACE_BASE + weariness);
                if (static_cast<int32_t>(game_random::Random(100)) <= chance) {
                    diplomac::Declare_Peace(a, b);
                    opinion::Add(a, r.player, opinion::REASON_DID_US_A_SERVICE, PEACE_OPINION);
                    opinion::Add(b, r.player, opinion::REASON_DID_US_A_SERVICE, PEACE_OPINION);
                    // "за услуги от обоих": each side now owes the mediator a favour (nothing without diplomacy favours).
                    intrigue::Owe(a, r.player);
                    intrigue::Owe(b, r.player);
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "overtures: stardate %d, the mediator, player %d, brings peace between players %d and %d; both owe it a favour",
                        (int)MOX::stardate, (int)r.player, (int)a, (int)b);
                    return true;
                }
                if (MOX::stardate < r.ends) return false;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "overtures: stardate %d, the mediator, player %d, gives up on players %d and %d, the war goes on",
                    (int)MOX::stardate, (int)r.player, (int)a, (int)b);
                return true;
            }
            // KIND_OFFENSIVE
            const int16_t payer = r.player;
            const int16_t ally = r.target;
            const int16_t enemy = r.other;
            if (!Alive_(payer) || !Alive_(ally) || !Alive_(enemy)) {
                Log_(payer, "the paid offensive is void, a party is gone");
                return true;
            }
            if (MOX::stardate < r.ends) return false;
            if (At_War_(ally, enemy)) {
                opinion::Add(payer, ally, opinion::REASON_HELPED_IN_WAR, OFFENSIVE_OPINION);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "overtures: stardate %d, player %d: player %d kept its war on player %d, the money was well spent",
                    (int)MOX::stardate, (int)payer, (int)ally, (int)enemy);
            } else {
                opinion::Add(payer, ally, opinion::REASON_BROKE_ITS_WORD, -OFFENSIVE_OPINION);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "overtures: stardate %d, player %d: player %d took the money and made peace with player %d",
                    (int)MOX::stardate, (int)payer, (int)ally, (int)enemy);
            }
            return true;
        }
    } // namespace

    int32_t Choose(e_kind kind, int16_t player) {
        (void)kind;
        if (!enabled || player < 0 || player >= Players_() || !Computer_(player)) return 0;
        switch (MOX::player[player].personality) {
            case PLAYER_PERSONALITY_HONORABLE:
            case PLAYER_PERSONALITY_PACIFIST: return 0;
            case PLAYER_PERSONALITY_RUTHLESS:
            case PLAYER_PERSONALITY_AGGRESSIVE: return 2;
            default: return 1;
        }
    }

    bool Start(e_kind kind, int16_t player, int32_t option) {
        if (!enabled || !Alive_(player)) return false;
        const int32_t chosen = std::clamp(option >= 0 ? option : Choose(kind, player), 0, 2);
        switch (kind) {
            case KIND_HAND: return Start_Hand_(player, chosen);
            case KIND_LEAK: return Start_Leak_(player, chosen);
            case KIND_MEDIATOR: return Start_Mediator_(player);
            case KIND_OFFENSIVE: return Start_Offensive_(player, chosen);
            default: return false;
        }
    }

    void Run_Records() {
        if (!enabled) return;
        s_state* s = State();
        for (int32_t i = 0; i < s->count;) {
            if (Run_(s->record[i])) {
                Remove_Record_(i);
            } else {
                ++i;
            }
        }
    }

    void Turn() {
        if (!enabled) return;
        Run_Records();
        for (int16_t p = 0; p < Players_(); ++p) {
            if (!Alive_(p)) continue;
            if (static_cast<int32_t>(game_random::Random(1000)) > EMPIRE_CHANCE) continue;
            const e_kind kind = static_cast<e_kind>(Pick_(KIND_LAST) + 1);
            if (Start(kind, p)) return;
        }
    }

} // namespace overtures
