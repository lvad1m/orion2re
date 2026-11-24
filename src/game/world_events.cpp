#include "pch.h"

#include "game/world_events.h"

#include "game/bill.h"
#include "game/colcalc.h"
#include "game/dice.h"
#include "game/diplomac.h"
#include "game/events.h"
#include "game/intrigue.h"
#include "game/invasion.h"
#include "game/officer.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/reputation.h"
#include "game/spy.h"
#include "game/spy_ops.h"
#include "game/spy_ranks.h"
#include "game/ships.h"
#include "game/techdata.h"

#include <algorithm>
#include <climits>

// The new events (events-catalog.md 6 and 4.1); the table, the modifiers the engine reads and the
// save section are in world_events_state.cpp. game_random is drawn only with events new_events on.
namespace world_events {

    namespace {
        constexpr int8_t TREATY_FIRST_WAR = 4;
        constexpr int32_t PROPHECY_STARS = 8;
        constexpr int16_t GHOST_TYPE = NONPLAYER_SHIP_TYPE_EEL;
        constexpr int16_t BEAST_TYPE = NONPLAYER_SHIP_TYPE_HYDRA;
        constexpr int8_t LEADER_TYPE_COLONY = 1;
        constexpr int8_t LEADER_STATUS_ASSIGNED = 1;
        constexpr int8_t SHIP_STATUS_GONE = 5;

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        // A random number 0..n-1 (game_random::Random(n) is 1..n).
        int32_t Pick_(int32_t n) {
            return n <= 1 ? 0 : static_cast<int32_t>(game_random::Random(n)) - 1;
        }

        int16_t Capital_(int16_t player) {
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                const s_colony& colony = MOX::colony[c];
                if (colony.owner == player && colony.buildings[BUILDING_CAPITOL] != 0) return c;
            }
            return -1;
        }

        int16_t Star_Of_Colony_(int16_t colony) {
            const int16_t planet = MOX::colony[colony].planet;
            return planet >= 0 ? MOX::planet[planet].star_index : -1;
        }

        int32_t Trade_Treaties_(int16_t player) {
            int32_t count = 0;
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o != player && MOX::player[player].trade_treaty[o] != 0) ++count;
            }
            return count;
        }

        void Log_(const char* what, int16_t player) {
            if (player >= 0) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d: %s", (int)MOX::stardate, (int)player, what);
            } else {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, the galaxy: %s", (int)MOX::stardate, what);
            }
        }

        // The `count` unvisited stars nearest `star` become known to `player`.
        int32_t Reveal_Near_(int16_t player, int16_t star, int32_t count) {
            if (star < 0) return 0;
            const uint8_t bit = static_cast<uint8_t>(1u << player);
            int32_t revealed = 0;
            for (int32_t n = 0; n < count; ++n) {
                int16_t best = -1;
                int32_t best_distance = 0;
                for (int16_t s = 0; s < MOX::NUM_STARS; ++s) {
                    if ((MOX::star[s].visited & bit) != 0) continue;
                    const int32_t dx = MOX::star[s].x - MOX::star[star].x;
                    const int32_t dy = MOX::star[s].y - MOX::star[star].y;
                    const int32_t distance = dx * dx + dy * dy;
                    if (best < 0 || distance < best_distance) {
                        best = s;
                        best_distance = distance;
                    }
                }
                if (best < 0) break;
                MOX::star[best].visited = static_cast<uint8_t>(MOX::star[best].visited | bit);
                ++revealed;
            }
            return revealed;
        }

        void Richer_Planet_(int16_t planet, int32_t classes) {
            EVENTS::Enrich_Planet(planet, static_cast<int16_t>(classes));  // as the classic mineral deposit
        }

        // A star without colonies and without monsters, at random; -1 when there is none.
        int16_t Empty_Star_() {
            int16_t stars[MAX_STARS];
            int32_t n = 0;
            for (int16_t s = 0; s < MOX::NUM_STARS && s < MAX_STARS; ++s) {
                if (MOX::star[s].has_colony != 0 || MOX::star[s].owner != -1) continue;
                bool monster = false;
                for (int16_t i = 0; i < MOX::NUM_SHIPS && !monster; ++i) {
                    const s_ship_data& ship = MOX::ship[i];
                    monster = ship.owner >= MAX_PLAYERS && ship.status < SHIP_STATUS_GONE && ship.location == s;
                }
                if (!monster) stars[n++] = s;
            }
            return n == 0 ? -1 : stars[Pick_(n)];
        }

        bool Monster_At_(int16_t type, int16_t star) {
            for (int16_t i = 0; i < MOX::NUM_SHIPS; ++i) {
                const s_ship_data& ship = MOX::ship[i];
                if (ship.owner == type && ship.status < SHIP_STATUS_GONE && ship.location == star) return true;
            }
            return false;
        }

        s_entry* Find_Slot_(e_kind kind, int16_t player, int16_t target) {
            for (int32_t i = 0; i < MAX_ENTRIES; ++i) {
                s_entry& e = Entry_Slots()[i];
                if (e.kind == kind && e.player == player && (target < 0 || e.target == target) &&
                    (e.ends == 0 || e.ends > MOX::stardate)) {
                    return &e;
                }
            }
            return nullptr;
        }

        int32_t Distance2_(int16_t colony_a, int16_t colony_b) {
            const int16_t a = Star_Of_Colony_(colony_a);
            const int16_t b = Star_Of_Colony_(colony_b);
            if (a < 0 || b < 0) return INT32_MAX;
            const int32_t dx = MOX::star[a].x - MOX::star[b].x;
            const int32_t dy = MOX::star[a].y - MOX::star[b].y;
            return dx * dx + dy * dy;
        }

        bool Infected_(int16_t colony) {
            return EVENTS::Event_Check_Plague(colony) != 0 || Epidemic(colony);
        }

        bool Start_Epidemic_(int16_t colony) {
            const int16_t owner = MOX::colony[colony].owner;
            if (owner < 0 || Infected_(colony)) return false;
            if (MOX::player[owner].tech_applications[TECH_APP_UNIVERSAL_ANTIDOTE] == TECH_RESEARCH_STATUS_RESEARCHED) return false;
            s_entry entry;
            entry.kind = KIND_EPIDEMIC;
            entry.player = static_cast<int8_t>(owner);
            entry.target = colony;
            entry.started = MOX::stardate;
            entry.ends = MOX::stardate + EPIDEMIC_TURNS;
            entry.value = static_cast<int16_t>(EPIDEMIC_CURE_BASE + 10 * MOX::colony[colony].n_pops);
            if (!Add(entry)) return false;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d: an epidemic breaks out at colony %d",
                (int)MOX::stardate, (int)owner, (int)colony);
            return true;
        }

        // Epidemics: the colony's research works on a cure; an infected colony (the classic plague
        // or ours) may pass it to its owner's nearest healthy colony; the universal antidote ends it.
        void Epidemics_() {
            for (int32_t i = 0; i < MAX_ENTRIES; ++i) {
                s_entry& e = Entry_Slots()[i];
                if (e.kind != KIND_EPIDEMIC || (e.ends != 0 && e.ends <= MOX::stardate)) continue;
                const s_colony& colony = MOX::colony[e.target];
                const bool cured = colony.owner != e.player ||
                    MOX::player[e.player].tech_applications[TECH_APP_UNIVERSAL_ANTIDOTE] == TECH_RESEARCH_STATUS_RESEARCHED;
                e.value = static_cast<int16_t>(e.value - std::max<int32_t>(1, colony.production[ECON_RESEARCH]));
                if (cured || e.value <= 0) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d: the epidemic at colony %d is over",
                        (int)MOX::stardate, (int)e.player, (int)e.target);
                    e = s_entry{};
                }
            }
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                if (MOX::colony[c].owner < 0 || !Infected_(c)) continue;
                if (Pick_(100) >= EPIDEMIC_SPREAD_PERCENT) continue;
                Spread_Epidemic(c);
            }
        }

        void Ghosts_() {
            for (int32_t i = 0; i < MAX_ENTRIES; ++i) {
                s_entry& e = Entry_Slots()[i];
                if (e.kind != KIND_GHOST_SHIP) continue;
                if (Monster_At_(GHOST_TYPE, e.target)) {
                    e.started = 0;
                    continue;
                }
                if (e.value <= 0) {
                    Log_("the ghost ship is gone for good", -1);
                    e = s_entry{};
                } else if (e.started == 0) {
                    e.started = MOX::stardate;
                    Log_("the ghost ship is destroyed, for now", -1);
                } else if (MOX::stardate - e.started >= GHOST_RETURN_TURNS) {
                    if (SHIPS::Build_Nonplayer_Ship(GHOST_TYPE, e.target, 0) >= 0) {
                        --e.value;
                        e.started = 0;
                        Log_("the ghost ship returns", -1);
                    }
                }
            }
        }

        void Raid_(int16_t target, const char* why) {
            s_player& p = MOX::player[target];
            // As the classic raid; an empire in debt has nothing to lose (and no debt is paid off).
            const int16_t loss = p.bc > 0 ? EVENTS::Pirate_Raid_Amount(p.bc) : 0;
            if (loss > 0) EVENTS::Pirate_Raid_Loss(target, loss);
            p.n_freighters = static_cast<int16_t>(std::max<int32_t>(0, p.n_freighters - 2));
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d: pirates raid it (%s), %d BC lost",
                (int)MOX::stardate, (int)target, why, (int)loss);
        }

        // Bounties (events-catalog.md 3.6): every 10 turns computers at war put a price on their
        // enemy's head; pirates strike the empire with the highest price, then offer it protection.
        void Bounties_() {
            const int32_t phase = MOX::stardate % 10;
            if (phase == 2) {
                for (int16_t payer = 0; payer < Players_(); ++payer) {
                    if (!Alive_(payer) || MOX::player[payer].objectives == PLAYER_OBJECTIVE_HUMAN) continue;
                    if (MOX::player[payer].bc < BOUNTY_PAYER_MIN_BC) continue;
                    for (int16_t target = 0; target < Players_(); ++target) {
                        if (target == payer || !Alive_(target) || MOX::player[payer].treaty[target] < TREATY_FIRST_WAR) continue;
                        const int32_t amount = std::max<int32_t>(BOUNTY_MIN, MOX::player[payer].bc * BOUNTY_PERCENT / 100);
                        s_entry* bounty = Find_Slot_(KIND_BOUNTY, target, -1);
                        if (bounty == nullptr) {
                            s_entry entry;
                            entry.kind = KIND_BOUNTY;
                            entry.player = static_cast<int8_t>(target);
                            entry.started = MOX::stardate;
                            if (!Add(entry)) break;
                            bounty = Find_Slot_(KIND_BOUNTY, target, -1);
                            if (bounty == nullptr) break;
                        }
                        MOX::player[payer].bc -= amount;
                        reputation::Note_Deed(payer);  // stains and titles (off by default)
                        bounty->value = static_cast<int16_t>(std::min<int32_t>(30000, bounty->value + amount));
                        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d puts %d BC on the head of player %d",
                            (int)MOX::stardate, (int)payer, (int)amount, (int)target);
                        break;  // one bounty a payer
                    }
                }
            } else if (phase == 7) {
                s_entry* best = nullptr;
                for (int32_t i = 0; i < MAX_ENTRIES; ++i) {
                    s_entry& e = Entry_Slots()[i];
                    if (e.kind != KIND_BOUNTY || e.value < BOUNTY_RAID_MIN || !Alive_(e.player)) continue;
                    if (Find(KIND_PROTECTION, e.player) != nullptr) continue;
                    if (best == nullptr || e.value > best->value) best = &e;
                }
                if (best == nullptr) return;
                const int16_t target = best->player;
                *best = s_entry{};
                Raid_(target, "a price on its head");
                const bool computer = MOX::player[target].objectives != PLAYER_OBJECTIVE_HUMAN;
                if (computer && MOX::player[target].bc >= 2 * PROTECTION_FEE) {
                    s_entry protection;
                    protection.kind = KIND_PROTECTION;
                    protection.player = static_cast<int8_t>(target);
                    protection.started = MOX::stardate;
                    protection.ends = MOX::stardate + PROTECTION_TURNS;
                    protection.value = PROTECTION_FEE;
                    if (Add(protection)) Log_("pays the pirates for protection", target);
                }
            }
        }

        void Protection_() {
            for (int32_t i = 0; i < MAX_ENTRIES; ++i) {
                s_entry& e = Entry_Slots()[i];
                if (e.kind != KIND_PROTECTION || (e.ends != 0 && e.ends <= MOX::stardate) || !Alive_(e.player)) continue;
                s_player& p = MOX::player[e.player];
                if (p.bc >= e.value) {
                    p.bc -= e.value;
                    continue;
                }
                const int16_t target = e.player;
                e = s_entry{};
                Raid_(target, "it stopped paying for protection");
            }
        }

        // Hired pirates (mechanics.md 3.2.1) -------------------------------------------------------

        bool Knows_Pirate_Cache_(int16_t player) {
            for (int16_t s = 0; s < MOX::NUM_STARS && s < MAX_STARS; ++s) {
                const s_star_data& star = MOX::star[s];
                if (star.system_special == SYSTEM_SPECIAL_PIRATE_CACHE && ((star.visited >> player) & 1) != 0) return true;
            }
            return false;
        }

        bool Pirates_At_(int16_t star) {
            for (int32_t i = 0; i < MAX_ENTRIES; ++i) {
                const s_entry& e = Entry_Slots()[i];
                if (e.kind == KIND_HIRED_PIRATES && e.target == star) return true;
            }
            return false;
        }

        // The star of `target`'s most populous colony where the hirer has no colony and no event sits.
        int16_t Pirate_Star_(int16_t hirer, int16_t target) {
            int16_t best = -1;
            int32_t best_pops = -1;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                const s_colony& colony = MOX::colony[c];
                if (colony.owner != target || colony.outpost_flag != 0 || colony.planet < 0) continue;
                const int16_t star = MOX::planet[colony.planet].star_index;
                if (star < 0 || ((MOX::star[star].has_colony >> hirer) & 1) != 0) continue;
                if (EVENTS::Star_Has_Event(star) != -1 || Pirates_At_(star)) continue;
                if (colony.n_pops > best_pops) {
                    best = star;
                    best_pops = colony.n_pops;
                }
            }
            return best;
        }

        // Every turn: hired pirates prey on their star as the classic pirate activity does.
        void Hired_Pirates_Turn_() {
            for (int32_t i = 0; i < MAX_ENTRIES; ++i) {
                s_entry& e = Entry_Slots()[i];
                if (e.kind != KIND_HIRED_PIRATES) continue;
                if (e.target < 0 || (e.ends != 0 && e.ends <= MOX::stardate)) {
                    Log_("the hired pirates leave", e.player);
                    e = s_entry{};
                    continue;
                }
                if (!EVENTS::Pirates_At_Star_Turn(e.target, &e.value, static_cast<int16_t>(std::max<int32_t>(1, e.extra)))) {
                    Log_("the hired pirates are driven off", e.player);
                    e = s_entry{};
                }
            }
        }

        // Every HIRE_PERIOD turns a computer pays pirates against its worst enemy: the fierce arm
        // them, the others pay for a raid.
        void Hire_All_() {
            for (int16_t hirer = 0; hirer < Players_(); ++hirer) {
                const s_player& p = MOX::player[hirer];
                if (!Alive_(hirer) || p.objectives == PLAYER_OBJECTIVE_HUMAN || p.bc < HIRE_MIN_BC + RAID_PRICE) continue;
                int16_t best = -1;
                int32_t worst = INT_MAX;
                for (int16_t t = 0; t < Players_(); ++t) {
                    if (t == hirer || !Alive_(t) || p.contact[t] == 0) continue;
                    const bool war = p.treaty[t] >= TREATY_FIRST_WAR;
                    int32_t score = opinion::Relations(hirer, t);
                    if (!war && score > HIRE_RELATIONS) continue;
                    if (war) score -= 200;
                    if (score < worst) {
                        worst = score;
                        best = t;
                    }
                }
                if (best < 0) continue;
                const bool fierce = p.personality == PLAYER_PERSONALITY_AGGRESSIVE || p.personality == PLAYER_PERSONALITY_RUTHLESS ||
                    p.personality == PLAYER_PERSONALITY_XENOPHOBIC;
                if (fierce && p.bc >= HIRE_MIN_BC + ARM_PRICE && Hire_Pirates(hirer, best, HIRE_ARM) != HIRE_NOT_POSSIBLE) continue;
                Hire_Pirates(hirer, best, HIRE_RAID);
            }
        }

        // A random star with a colony and no entry of `kind` yet; -1 when there is none.
        int16_t Colony_Star_Without_(e_kind kind) {
            int16_t stars[MAX_COLONIES];
            int32_t n = 0;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                const s_colony& colony = MOX::colony[c];
                if (colony.owner < 0 || colony.planet < 0) continue;
                const int16_t star = Star_Of_Colony_(c);
                if (star < 0 || Find_Slot_(kind, -1, star) != nullptr) continue;
                bool seen = false;
                for (int32_t i = 0; i < n && !seen; ++i) seen = stars[i] == star;
                if (!seen) stars[n++] = star;
            }
            return n == 0 ? -1 : stars[Pick_(n)];
        }

        bool Galaxy_Modifier_Active_() {
            return Find(KIND_GOLDEN_AGE, -1) != nullptr || Find(KIND_TOURIST_BOOM, -1) != nullptr ||
                Find(KIND_GALACTIC_SLOWDOWN, -1) != nullptr || Find(KIND_SENSOR_FAILURE, -1) != nullptr;
        }

        // Every turn: a flare burns the turn's industry of the colonies at its star (as a strike does).
        void Flares_() {
            for (int32_t i = 0; i < MAX_ENTRIES; ++i) {
                const s_entry& e = Entry_Slots()[i];
                if (e.kind != KIND_STAR_FLARE || e.target < 0 || (e.ends != 0 && e.ends <= MOX::stardate)) continue;
                for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                    s_colony& colony = MOX::colony[c];
                    if (colony.owner < 0 || colony.planet < 0 || Star_Of_Colony_(c) != e.target) continue;
                    colony.production_spent = static_cast<int16_t>(std::max<int32_t>(0, colony.production_spent - colony.production[ECON_INDUSTRY]));
                }
            }
        }

        void Log_Star_(const char* what, int16_t star) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, the galaxy: %s at star %d", (int)MOX::stardate, what, (int)star);
        }

        // Every turn: the galaxy modifiers pay; Klackon workers speed the capital's build.
        void Pay_Modifiers_() {
            const s_entry* golden = Find(KIND_GOLDEN_AGE, -1);
            const s_entry* tourists = Find(KIND_TOURIST_BOOM, -1);
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!Alive_(p)) continue;
                s_player& player = MOX::player[p];
                if (golden != nullptr && player.surplus_bc > 0) player.bc += player.surplus_bc * GOLDEN_AGE_PERCENT / 100;
                if (tourists != nullptr) player.bc += TOURIST_BC_PER_TREATY * Trade_Treaties_(p);
                if (Find(KIND_RACE_EVENT, p, RACE_WORKER_SWARM) != nullptr) {
                    const int16_t capital = Capital_(p);
                    if (capital >= 0) {
                        s_colony& colony = MOX::colony[capital];
                        colony.production_spent = static_cast<int16_t>(colony.production_spent + colony.production[ECON_INDUSTRY] / 4);
                    }
                }
            }
        }

        // The bankruptcy scale (events-catalog.md 3.3): 10 a turn in debt, -5 out of it; at 75 a
        // building of the capital is sold, at 100 a quarter of the warships are scrapped.
        void Bankruptcy_() {
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!Alive_(p)) continue;
                s_player& player = MOX::player[p];
                s_entry* scale = const_cast<s_entry*>(Find(KIND_BANKRUPTCY, p));
                if (scale == nullptr) {
                    if (player.bc >= 0) continue;
                    s_entry entry;
                    entry.kind = KIND_BANKRUPTCY;
                    entry.player = static_cast<int8_t>(p);
                    entry.started = MOX::stardate;
                    if (!Add(entry)) continue;
                    scale = const_cast<s_entry*>(Find(KIND_BANKRUPTCY, p));
                    if (scale == nullptr) continue;
                    Log_("falls into debt: the bankruptcy scale starts", p);
                }
                const int32_t before = scale->value;
                scale->value = static_cast<int16_t>(std::clamp<int32_t>(before + (player.bc < 0 ? BANKRUPTCY_STEP : -BANKRUPTCY_RECOVERY), 0, 100));
                if (scale->value == 0) {
                    *scale = s_entry{};
                    Log_("is out of debt: the bankruptcy scale ends", p);
                    continue;
                }
                if (before < BANKRUPTCY_EMERGENCY && scale->value >= BANKRUPTCY_EMERGENCY) {
                    const int16_t capital = Capital_(p);
                    int16_t sold = -1;
                    if (capital >= 0) {
                        for (int16_t b = 0; b < BUILDING_COUNT; ++b) {
                            if (b == BUILDING_CAPITOL || MOX::colony[capital].buildings[b] == 0) continue;
                            if (sold < 0 || TECHDATA::buildings[b].cost > TECHDATA::buildings[sold].cost) sold = b;
                        }
                    }
                    if (sold >= 0) {
                        MOX::colony[capital].buildings[sold] = 0;
                        player.bc += TECHDATA::buildings[sold].cost / 2;
                        COLCALC::Colony_Calculation(capital);
                    }
                    Log_("sells a building of its capital to pay its debts", p);
                }
                if (scale->value >= 100) {
                    int32_t warships = 0;
                    for (int16_t s = 0; s < MOX::NUM_SHIPS; ++s) {
                        if (MOX::ship[s].owner == p && MOX::ship[s].status < 5) ++warships;
                    }
                    int32_t scrap = warships / 4;
                    for (int16_t s = MOX::NUM_SHIPS - 1; s >= 0 && scrap > 0; --s) {
                        if (MOX::ship[s].owner != p || MOX::ship[s].status >= 5) continue;
                        SHIPS::Kill_Ship(s);
                        --scrap;
                    }
                    player.bc = std::max<int32_t>(player.bc, 0);
                    scale->value = 50;
                    Log_("goes bankrupt: a quarter of its ships are scrapped and its debts written off", p);
                }
            }
        }

        // The mutiny is the engine's own rebellion (invasion.cpp Check_Rebellion) with the chance forced: a
        // colony of the empire has no conquered rebels, so the rebel side is another empire (one in contact
        // first) with up to a colony's population in rebels; the ground fight, the messages and, when the
        // rebels win, the change of owner (Change_Colony_Ownership) and the garrison are the classic ones.
        // Closest equivalent: Check_Rebellion itself rolls its chance from conquered pops only.
        bool Mutiny_Rebellion_(int16_t colony_index) {
            s_colony* colony = &MOX::colony[colony_index];
            const int16_t owner = colony->owner;
            int16_t rebels[MAX_PLAYERS];
            int32_t n = 0;
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o != owner && Alive_(o) && MOX::player[owner].contact[o] != 0) rebels[n++] = o;
            }
            if (n == 0) {
                for (int16_t o = 0; o < Players_(); ++o) {
                    if (o != owner && Alive_(o)) rebels[n++] = o;
                }
            }
            if (n == 0) return false;  // nobody to take the colony: only the unrest
            const int16_t rebel_player = rebels[Pick_(n)];
            const int32_t rebel_count = static_cast<int32_t>(game_random::Random(colony->n_pops));
            s_invasion_side_info rebel_side;
            s_invasion_side_info defender_side;
            invasion::Get_Rebellion_Info(rebel_player, static_cast<int16_t>(rebel_count), colony, &rebel_side, &defender_side);
            invasion::Resolve_Ground_Combat(&rebel_side, &defender_side);

            s_MSG msg = {};
            msg.msg_type = MSG_TYPE_COLONY;
            msg.u.colony.planet_idx = colony->planet;
            msg.u.colony.subtype = MSG_COLONY_SUBTYPE_REBELLION;
            msg.u.colony.c.rebels.owner_player = static_cast<int8_t>(rebel_player);
            msg.u.colony.c.rebels.rebels = static_cast<int8_t>(rebel_count);
            msg.u.colony.c.rebels.lost = static_cast<int8_t>(rebel_count - rebel_side.unit_counts[3]);
            MSG_::Add_Msg(msg, owner);
            MSG_::Add_Msg(msg, rebel_player);

            if (rebel_side.current_troop_type_idx < 4) {
                invasion::Change_Colony_Ownership(colony, rebel_side.player_id, 0);
                invasion::Resolve_Rebellion_Troops(rebel_side);
                colony->just_produced = COLONY_PRODUCTION_JUST_LIBERATED;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d: the rebels of colony %d (%d) take it for player %d",
                    (int)MOX::stardate, (int)owner, (int)colony_index, (int)rebel_count, (int)rebel_player);
                return true;
            } else {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d: the rebels of colony %d (%d) are crushed",
                    (int)MOX::stardate, (int)owner, (int)colony_index, (int)rebel_count);
            }
            return false;
        }

        // Discoverers (events-catalog 3.8): the first empire to explore a star is paid for each of its planets.
        // "First" without engine hooks: the stars somebody explored are kept in the state; the first Turn
        // only takes the stars visited so far (a new game, an old save) without paying.
        void Discoverers_() {
            const uint8_t empires = static_cast<uint8_t>((1u << Players_()) - 1u);
            const bool baseline = !Explored_Valid();
            for (int16_t s = 0; s < MOX::NUM_STARS && s < MAX_STARS; ++s) {
                if (Star_Explored(s)) continue;
                const uint8_t visited = static_cast<uint8_t>(MOX::star[s].visited & empires);
                if (visited == 0) continue;
                Mark_Explored(s);
                if (baseline) continue;
                int16_t finder = -1;
                for (int16_t p = 0; p < Players_() && finder < 0; ++p) {  // the lower bit wins a tie
                    if (((visited >> p) & 1) != 0 && Alive_(p)) finder = p;
                }
                if (finder < 0) continue;
                int32_t bonus = 0;
                for (int16_t pl = 0; pl < MOX::planet_count; ++pl) {
                    const s_planet_data& planet = MOX::planet[pl];
                    if (planet.star_index != s) continue;
                    const bool special = MOX::star[s].system_special != SYSTEM_SPECIAL_NO_SPECIAL ||
                        planet.planet_special != SYSTEM_SPECIAL_NO_SPECIAL;
                    bonus += special ? DISCOVERER_SPECIAL_BC : DISCOVERER_BC;
                }
                if (bonus == 0) continue;
                MOX::player[finder].bc += bonus;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d: discoverer of star %d, %d BC",
                    (int)MOX::stardate, (int)finder, (int)s, (int)bonus);
            }
            Mark_Explored_Valid();
        }
    } // namespace

    bool Start_Galaxy_Event(e_kind kind) {
        if (!enabled) return false;
        s_entry entry;
        entry.kind = kind;
        entry.started = MOX::stardate;
        switch (kind) {
            case KIND_GOLDEN_AGE:
            case KIND_TOURIST_BOOM:
                if (Galaxy_Modifier_Active_()) return false;
                entry.ends = MOX::stardate + GALAXY_MODIFIER_TURNS;
                if (!Add(entry)) return false;
                Log_(kind == KIND_GOLDEN_AGE ? "a golden age begins" : "a tourist boom begins", -1);
                return true;
            case KIND_GALACTIC_SLOWDOWN:
            case KIND_SENSOR_FAILURE: {
                if (Galaxy_Modifier_Active_()) return false;
                const bool slow = kind == KIND_GALACTIC_SLOWDOWN;
                entry.ends = MOX::stardate + (slow ? SLOWDOWN_MIN_TURNS + Pick_(SLOWDOWN_SPAN_TURNS + 1)
                                                   : SENSOR_MIN_TURNS + Pick_(SENSOR_SPAN_TURNS + 1));
                if (!Add(entry)) return false;
                Log_(slow ? "a galactic slowdown begins: every ship is a quarter slower" : "a sensor failure begins: scanner range is halved", -1);
                return true;
            }
            case KIND_STAR_FLARE:
            case KIND_SYSTEM_EMP: {
                const int16_t star = Colony_Star_Without_(kind);
                if (star < 0) return false;
                const bool flare = kind == KIND_STAR_FLARE;
                entry.target = star;
                entry.ends = MOX::stardate + (flare ? FLARE_MIN_TURNS + Pick_(FLARE_SPAN_TURNS + 1)
                                                    : EMP_MIN_TURNS + Pick_(EMP_SPAN_TURNS + 1));
                if (!Add(entry)) return false;
                Log_Star_(flare ? "a star flare begins: no building, no ship leaves" : "a system EMP begins: its colonies are dark, its ships stand", star);
                return true;
            }
            case KIND_ORE_RUSH: {
                if (MOX::planet_count <= 0) return false;
                for (int i = 0; i < 3; ++i) Richer_Planet_(static_cast<int16_t>(Pick_(MOX::planet_count)), 2);
                Log_("an ore rush: rich deposits are found on three planets", -1);
                return true;
            }
            case KIND_GHOST_SHIP: {
                const int16_t star = Empty_Star_();
                if (star < 0 || SHIPS::Build_Nonplayer_Ship(GHOST_TYPE, star, 0) < 0) return false;
                entry.target = star;
                entry.started = 0;
                entry.value = GHOST_RETURNS;
                if (!Add(entry)) return false;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, the galaxy: a ghost ship haunts star %d",
                    (int)MOX::stardate, (int)star);
                return true;
            }
            case KIND_TERRITORIAL_BEAST: {
                const int16_t star = Empty_Star_();
                if (star < 0 || SHIPS::Build_Nonplayer_Ship(BEAST_TYPE, star, 0) < 0) return false;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, the galaxy: a territorial beast settles at star %d",
                    (int)MOX::stardate, (int)star);
                return true;
            }
            default: return false;
        }
    }

    bool Start_Empire_Event(e_kind kind, int16_t player) {
        if (!enabled || !Alive_(player)) return false;
        switch (kind) {
            case KIND_AMBASSADOR_KILLED: {
                int16_t contacts[MAX_PLAYERS];
                int32_t n = 0;
                for (int16_t o = 0; o < Players_(); ++o) {
                    if (o != player && Alive_(o) && MOX::player[player].contact[o] != 0) contacts[n++] = o;
                }
                if (n == 0) return false;
                const int16_t envoy = contacts[Pick_(n)];
                diplomac::Change_Relations(AMBASSADOR_RELATIONS, player, envoy, 0, 0, 0);
                diplomac::Change_Relations(AMBASSADOR_RELATIONS, envoy, player, 0, 0, 0);
                // Graded incidents (events-catalog 3.12): a killed diplomat is remembered worse than a skirmish.
                opinion::Add(envoy, player, opinion::REASON_KILLED_OUR_DIPLOMAT, opinion::DIPLOMAT_KILLED_OPINION);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d: the ambassador of player %d is killed",
                    (int)MOX::stardate, (int)player, (int)envoy);
                const bool computer = MOX::player[envoy].objectives != PLAYER_OBJECTIVE_HUMAN;
                if (computer && MOX::player[envoy].treaty[player] < TREATY_FIRST_WAR &&
                    opinion::Relations(envoy, player) <= AMBASSADOR_WAR_RELATIONS) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d goes to war over its ambassador",
                        (int)MOX::stardate, (int)envoy);
                    diplomac::Declare_War(envoy, player);
                }
                return true;
            }
            case KIND_DISCOVERER: return false;  // retired: a rule now (Discoverers_ in Turn)
            case KIND_ANCIENT_WRECK: {
                // Unknown weapon and shield applications, the cheapest (by field cost) first: a windfall, not a
                // leap to the end of the tree.
                int16_t apps[static_cast<int>(WEAPON_COUNT) + static_cast<int>(SHIELD_COUNT)];
                int32_t n = 0;
                s_player* me = &MOX::player[player];
                const auto add = [&](int32_t app) {
                    if (app <= TECH_APP_NO_TECH || app >= TECH_APP_COUNT) return;
                    if (me->tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED || !COLCALC::Player_May_Get_Tech_App(me, app)) return;
                    for (int32_t i = 0; i < n; ++i) {
                        if (apps[i] == app) return;
                    }
                    apps[n++] = static_cast<int16_t>(app);
                };
                for (int32_t i = 0; i < WEAPON_COUNT; ++i) add(TECHDATA::weapons[i].tech_app_id);
                for (int32_t i = 0; i < SHIELD_COUNT; ++i) add(TECHDATA::shields[i].tech_app_id);
                if (n == 0) return false;
                const auto cost = [](int16_t app) {
                    const int16_t field = TECHDATA::technology_applications[app].tech_field_id;
                    return field >= 0 && field < TECH_FIELD_COUNT ? static_cast<int64_t>(TECHDATA::technology_fields[field].cost) : INT64_MAX;
                };
                std::stable_sort(apps, apps + n, [&](int16_t a, int16_t b) { return cost(a) < cost(b) || (cost(a) == cost(b) && a < b); });
                const int32_t count = std::min<int32_t>(n, 1 + Pick_(WRECK_MAX_APPS));
                for (int32_t i = 0; i < count; ++i) {
                    COLCALC::Player_Gets_Tech_App(me, apps[i]);
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d: an ancient wreck yields application %d",
                        (int)MOX::stardate, (int)player, (int)apps[i]);
                }
                Log_("an ancient wreck yields weapon and shield technologies", player);
                return true;
            }
            case KIND_MUTINY: {
                int16_t colonies[MAX_COLONIES];
                int32_t n = 0;
                for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                    const s_colony& colony = MOX::colony[c];
                    if (colony.owner == player && colony.n_pops > 0 && colony.buildings[BUILDING_CAPITOL] == 0 &&
                        colony.occupation_policy != 4 && Find_Slot_(KIND_MUTINY, player, c) == nullptr) {
                        colonies[n++] = c;
                    }
                }
                if (n == 0) return false;
                s_entry entry;
                entry.kind = KIND_MUTINY;
                entry.player = static_cast<int8_t>(player);
                entry.target = colonies[Pick_(n)];
                entry.started = MOX::stardate;
                entry.ends = MOX::stardate + MODIFIER_TURNS;
                if (!Add(entry)) return false;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d: colony %d mutinies",
                    (int)MOX::stardate, (int)player, (int)entry.target);
                if (Mutiny_Rebellion_(entry.target)) {  // lost to the rebels: the unrest goes with the old owner
                    if (s_entry* slot = Find_Slot_(KIND_MUTINY, player, entry.target)) *slot = s_entry{};
                }
                COLCALC::Colony_Calculation(entry.target);
                return true;
            }
            case KIND_WARM_PLACE: {
                int16_t leaders[MAX_LEADERS];
                int32_t n = 0;
                for (int16_t l = 0; l < MAX_LEADERS; ++l) {
                    const s_leader_data& leader = MOX::leaders[l];
                    if (leader.player_index == player && leader.type == LEADER_TYPE_COLONY && leader.status == LEADER_STATUS_ASSIGNED &&
                        !Leader_Locked(l)) {
                        leaders[n++] = l;
                    }
                }
                if (n == 0) return false;
                s_entry entry;
                entry.kind = KIND_WARM_PLACE;
                entry.player = static_cast<int8_t>(player);
                entry.target = leaders[Pick_(n)];
                entry.started = MOX::stardate;
                entry.ends = MOX::stardate + WARM_PLACE_TURNS;
                if (!Add(entry)) return false;
                s_leader_data& leader = MOX::leaders[entry.target];
                leader.xp = static_cast<int16_t>(std::min<int32_t>(32767, leader.xp + WARM_PLACE_EXPERIENCE));
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d: leader %d finds a warm place",
                    (int)MOX::stardate, (int)player, (int)entry.target);
                return true;
            }
            case KIND_EPIDEMIC: {
                int16_t colonies[MAX_COLONIES];
                int32_t n = 0;
                for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                    if (MOX::colony[c].owner == player && MOX::colony[c].n_pops > 0 && !Infected_(c)) colonies[n++] = c;
                }
                return n > 0 && Start_Epidemic_(colonies[Pick_(n)]);
            }
            case KIND_COMET_ORE: {
                // The classic comet strikes a colony without warning and leaves a richer deposit.
                int16_t colonies[MAX_COLONIES];
                int32_t n = 0;
                for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                    const s_colony& colony = MOX::colony[c];
                    if (colony.owner == player && colony.n_pops > 0 && colony.planet >= 0) colonies[n++] = c;
                }
                if (n == 0) return false;
                const int16_t target = colonies[Pick_(n)];
                const int16_t planet = MOX::colony[target].planet;
                EVENTS::Allocate_Comet_Hit(target);
                if (MOX::colony[target].owner == player) Richer_Planet_(planet, 1);  // the hit may have wiped the colony out
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d: a comet strikes colony %d and leaves ore",
                    (int)MOX::stardate, (int)player, (int)target);
                return true;
            }
            default: return false;
        }
    }

    bool Spread_Epidemic(int16_t from) {
        if (!enabled || from < 0 || from >= MOX::NUM_COLONIES || MOX::colony[from].owner < 0 || !Infected_(from)) return false;
        int16_t nearest = -1;
        for (int16_t o = 0; o < MOX::NUM_COLONIES && o < MAX_COLONIES; ++o) {
            if (o == from || MOX::colony[o].owner != MOX::colony[from].owner || MOX::colony[o].n_pops == 0 || Infected_(o)) continue;
            if (nearest < 0 || Distance2_(from, o) < Distance2_(from, nearest)) nearest = o;
        }
        return nearest >= 0 && Start_Epidemic_(nearest);
    }

    bool Start_Race_Event(int16_t player, e_race_event event) {
        if (!enabled || !Alive_(player) || event >= RACE_EVENT_COUNT) return false;
        s_player& me = MOX::player[player];
        s_entry modifier;
        modifier.kind = KIND_RACE_EVENT;
        modifier.player = static_cast<int8_t>(player);
        modifier.started = MOX::stardate;
        modifier.ends = MOX::stardate + MODIFIER_TURNS;
        modifier.value = event;
        const int16_t capital = Capital_(player);
        switch (event) {
            case RACE_FAIR_WIND:
            case RACE_DUEL_OF_HONOUR:
            case RACE_WORKER_SWARM:
            case RACE_WARM_CURRENT:
                if (Find(KIND_RACE_EVENT, player, event) != nullptr || !Add(modifier)) return false;
                break;
            case RACE_DOUBLE_AGENT: {
                int16_t owner = -1;
                for (int16_t o = 0; o < Players_(); ++o) {
                    if (o == player || !Alive_(o) || BILL::Get_Their_Spy_Number(o, player) == 0) continue;
                    if (owner < 0 || BILL::Get_Their_Spy_Number(o, player) > BILL::Get_Their_Spy_Number(owner, player)) owner = o;
                }
                if (owner < 0) return false;
                intrigue::State()->double_agent_until[player][owner] = MOX::stardate + intrigue::DOUBLE_AGENT_TURNS;
                break;
            }
            case RACE_PROPHECY:
                if (capital < 0 || Reveal_Near_(player, Star_Of_Colony_(capital), PROPHECY_STARS) == 0) return false;
                break;
            case RACE_GOOD_BARGAIN:
                me.bc += std::max<int32_t>(50, 25 * Trade_Treaties_(player));
                break;
            case RACE_BREAKTHROUGH:
                for (int16_t o = 0; o < Players_(); ++o) {
                    if (o != player && Alive_(o)) diplomac::Change_Relations(10, player, o, 0, 0, 0);
                }
                break;
            case RACE_NETWORK_FAILURE:
                me.research_accumulated = Pick_(4) == 0 ? me.research_accumulated * 2 : me.research_accumulated / 2;
                break;
            case RACE_GREAT_HUNT: {
                const int16_t star = capital >= 0 ? Star_Of_Colony_(capital) : -1;
                if (star < 0) return false;
                for (int16_t s = 0; s < MOX::NUM_SHIPS; ++s) {
                    s_ship_data& ship = MOX::ship[s];
                    if (ship.owner != player || ship.status >= 5 || ship.location != star) continue;
                    ship.crew_experience = static_cast<uint16_t>(std::min<int32_t>(65535, ship.crew_experience + GREAT_HUNT_EXPERIENCE));
                }
                break;
            }
            case RACE_INSIGHT: {
                const int8_t field = me.current_research_field;
                if (field <= 0) return false;
                COLCALC::Give_Player_Field(&me, field, 1);
                const bool keep_points = COLCALC::keep_research_on_breakthrough != 0 && me.research_breakthrough != 0;
                COLCALC::Resolve_Research_Completion(&me, keep_points, 0);
                break;
            }
            case RACE_CLUTCH: {
                int32_t boomed = 0;
                for (int32_t round = 0; round < 3; ++round) {
                    int16_t best = -1;
                    for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                        const s_colony& colony = MOX::colony[c];
                        if (colony.owner != player || colony.n_pops == 0 || Population_Boom(c)) continue;
                        if (best < 0 || colony.n_pops > MOX::colony[best].n_pops) best = c;
                    }
                    if (best < 0) break;
                    s_entry boom = modifier;
                    boom.target = best;
                    if (!Add(boom)) break;
                    ++boomed;
                }
                if (boomed == 0) return false;
                break;
            }
            case RACE_PLATE_SHIFT: {
                int16_t best = -1;
                for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                    const s_colony& colony = MOX::colony[c];
                    if (colony.owner != player || colony.planet < 0) continue;
                    if (MOX::planet[colony.planet].mineral_class >= MINERAL_RESOURCE_ULTRA_RICH) continue;
                    if (best < 0 || colony.n_pops > MOX::colony[best].n_pops) best = c;
                }
                if (best < 0) return false;
                Richer_Planet_(MOX::colony[best].planet, 1);
                break;
            }
            default: return false;
        }
        static const char* const NAMES[RACE_EVENT_COUNT] = {"a fair wind speeds its ships", "a duel of honour lifts its people",
            "a double agent turns a neighbour's spy", "a prophecy reveals the stars near its capital", "a good bargain fills its treasury",
            "a diplomatic breakthrough warms every contact", "a worker swarm speeds its capital", "a network failure strikes its research",
            "a great hunt seasons its crews", "an insight completes its research", "a clutch booms its colonies",
            "a plate shift enriches a world", "a warm current feeds its ocean worlds"};
        Log_(NAMES[event], player);
        return true;
    }

    e_hire_result Hire_Pirates(int16_t hirer, int16_t target, e_hire how) {
        return Hire_Pirates_With_Dice(hirer, target, how, nullptr);
    }

    e_hire_result Hire_Pirates_With_Dice(int16_t hirer, int16_t target, e_hire how, const int32_t* dice_values) {
        if (!hired_pirates || !Alive_(hirer) || !Alive_(target) || hirer == target) return HIRE_NOT_POSSIBLE;
        const int32_t spies = BILL::Get_Their_Spy_Number(hirer, target);
        if (spies == 0 && !Knows_Pirate_Cache_(hirer)) return HIRE_NOT_POSSIBLE;
        s_player& payer = MOX::player[hirer];
        int16_t star = -1;
        int32_t price = RAID_PRICE;
        if (how == HIRE_ARM) {
            star = Pirate_Star_(hirer, target);
            if (star < 0) return HIRE_NOT_POSSIBLE;
            int32_t free_slots = 0;
            for (int32_t i = 0; i < MAX_ENTRIES; ++i) free_slots += Entry_Slots()[i].kind == KIND_NONE ? 1 : 0;
            if (free_slots == 0) return HIRE_NOT_POSSIBLE;
            price = std::clamp<int32_t>(payer.bc * ARM_PERCENT / 100, ARM_PRICE, ARM_PRICE_MAX);
        } else if (MOX::player[target].bc < RAID_MIN_BC) {
            return HIRE_NOT_POSSIBLE;
        }
        if (payer.bc < price) return HIRE_NOT_POSSIBLE;
        payer.bc -= price;

        int16_t offense = 0;
        int16_t defense = 0;
        spy::Compute_Spy_Bonuses(&payer, &offense, &defense);
        const dice::s_modifier modifiers[3] = {{"spies", std::min<int32_t>(5, spies / 2)}, {"spy skill", offense / 10},
            {"resident", spy_ranks::Resident_Bonus(hirer, target)}};
        const int32_t difficulty = spy_ops::Difficulty(hirer, spy_ops::OP_DATA, target);
        const dice::s_check check = dice_values != nullptr
            ? dice::Resolve_Check(static_cast<dice::e_roll_kind>(dice::roll_kind), dice_values, modifiers, 3, difficulty)
            : dice::Roll_Check(modifiers, 3, difficulty);
        const char* what = how == HIRE_ARM ? "arms pirates" : "pays for a pirate raid";
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d %s against player %d (%d BC): %s",
            (int)MOX::stardate, (int)hirer, what, (int)target, (int)price, dice::Describe_Check(what, check, modifiers, 3).c_str());

        if (check.outcome >= dice::OUTCOME_SUCCESS) {
            reputation::Note_Deed(hirer);  // stains and titles (off by default): toward the corsair
            if (how == HIRE_RAID) {
                const int16_t loss = EVENTS::Pirate_Raid_Amount(MOX::player[target].bc);
                EVENTS::Pirate_Raid_Loss(target, loss);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d: hired pirates raid it, %d BC lost",
                    (int)MOX::stardate, (int)target, (int)loss);
                return HIRE_DONE;
            }
            const int32_t strength = std::clamp<int32_t>(EVENTS::Event_Pirate_Activity_Strength(star) + price / 2, 1, 30000);
            s_entry entry;
            entry.kind = KIND_HIRED_PIRATES;
            entry.player = static_cast<int8_t>(target);
            entry.target = star;
            entry.started = MOX::stardate;
            entry.ends = MOX::stardate + HIRED_PIRATE_TURNS;
            entry.value = static_cast<int16_t>(strength);
            entry.extra = static_cast<int16_t>(strength);
            entry.hirer = static_cast<int8_t>(hirer);
            Add(entry);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "world_events: stardate %d, player %d: hired pirates (strength %d) sit at star %d",
                (int)MOX::stardate, (int)target, (int)strength, (int)star);
            return HIRE_DONE;
        }
        if (check.outcome != dice::OUTCOME_EXPOSED && check.outcome != dice::OUTCOME_CATASTROPHE) return HIRE_FAILED;
        if (spies > 0) BILL::Set_Their_Spy_Number(hirer, target, static_cast<uint8_t>(spies - 1));
        opinion::Add(target, hirer, opinion::REASON_RAIDED_US, opinion::RAIDED_OPINION);
        return HIRE_EXPOSED;
    }

    void Turn() {
        // Hired pirates (spy hired_pirates) work with or without the other new events.
        if (hired_pirates) {
            Hired_Pirates_Turn_();
            if (MOX::stardate % HIRE_PERIOD == 4) Hire_All_();
        }
        if (!enabled) return;
        Pay_Modifiers_();
        Bankruptcy_();
        Epidemics_();
        Ghosts_();
        Bounties_();
        Protection_();
        Flares_();
        Discoverers_();
        // Expired entries free their slots.
        for (int32_t i = 0; i < MAX_ENTRIES; ++i) {
            s_entry& e = Entry_Slots()[i];
            if (e.kind != KIND_NONE && e.ends != 0 && e.ends <= MOX::stardate) {
                switch (e.kind) {
                    case KIND_STAR_FLARE: Log_Star_("a star flare ends", e.target); break;
                    case KIND_SYSTEM_EMP: Log_Star_("a system EMP ends", e.target); break;
                    case KIND_GALACTIC_SLOWDOWN: Log_("the galactic slowdown ends", -1); break;
                    case KIND_SENSOR_FAILURE: Log_("the sensor failure ends", -1); break;
                    default: break;
                }
                e = s_entry{};
            }
        }
        if (galaxy_chance > 0 && static_cast<int32_t>(game_random::Random(1000)) <= galaxy_chance) {
            static const e_kind GALAXY[9] = {KIND_GOLDEN_AGE, KIND_TOURIST_BOOM, KIND_ORE_RUSH, KIND_GHOST_SHIP,
                KIND_TERRITORIAL_BEAST, KIND_STAR_FLARE, KIND_SYSTEM_EMP, KIND_GALACTIC_SLOWDOWN, KIND_SENSOR_FAILURE};
            Start_Galaxy_Event(GALAXY[Pick_(9)]);
        }
        if (empire_chance <= 0) return;
        for (int16_t p = 0; p < Players_(); ++p) {
            if (!Alive_(p) || static_cast<int32_t>(game_random::Random(1000)) > empire_chance) continue;
            // Half the empire events are the race's own (stock races only).
            const uint8_t race = MOX::player[p].race;
            if (race < STOCK_RACE_COUNT && Pick_(2) == 0) {
                Start_Race_Event(p, static_cast<e_race_event>(race));
                continue;
            }
            static const e_kind EMPIRE[6] = {KIND_AMBASSADOR_KILLED, KIND_ANCIENT_WRECK, KIND_MUTINY,
                KIND_WARM_PLACE, KIND_EPIDEMIC, KIND_COMET_ORE};
            Start_Empire_Event(EMPIRE[Pick_(6)], p);
        }
    }

} // namespace world_events
