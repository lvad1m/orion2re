#include "pch.h"

#include "game/world_events.h"

#include "game/bill.h"
#include "game/colcalc.h"
#include "game/diplomac.h"
#include "game/intrigue.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/ships.h"
#include "game/techdata.h"

#include <algorithm>

// The new events (events-catalog.md 6 and 4.1); the table, the modifiers the engine reads and the
// save section are in world_events_state.cpp. game_random is drawn only with events new_events on.
namespace world_events {

    namespace {
        constexpr int8_t TREATY_FIRST_WAR = 4;
        constexpr int32_t DISCOVER_STARS = 6;
        constexpr int32_t PROPHECY_STARS = 8;

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
            const int32_t richer = std::min<int32_t>(MINERAL_RESOURCE_ULTRA_RICH, MOX::planet[planet].mineral_class + classes);
            MOX::planet[planet].mineral_class = static_cast<int8_t>(richer);
            const int16_t colony = MOX::planet[planet].colony_index;
            if (colony != -1) COLCALC::Colony_Calculation(colony);
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
    } // namespace

    bool Start_Galaxy_Event(e_kind kind) {
        if (!enabled) return false;
        s_entry entry;
        entry.kind = kind;
        entry.started = MOX::stardate;
        switch (kind) {
            case KIND_GOLDEN_AGE:
            case KIND_TOURIST_BOOM:
                if (Find(KIND_GOLDEN_AGE, -1) != nullptr || Find(KIND_TOURIST_BOOM, -1) != nullptr) return false;
                entry.ends = MOX::stardate + GALAXY_MODIFIER_TURNS;
                if (!Add(entry)) return false;
                Log_(kind == KIND_GOLDEN_AGE ? "a golden age begins" : "a tourist boom begins", -1);
                return true;
            case KIND_ORE_RUSH: {
                if (MOX::planet_count <= 0) return false;
                for (int i = 0; i < 3; ++i) Richer_Planet_(static_cast<int16_t>(Pick_(MOX::planet_count)), 2);
                Log_("an ore rush: rich deposits are found on three planets", -1);
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
            case KIND_DISCOVERER: {
                int16_t colonies[MAX_COLONIES];
                int32_t n = 0;
                for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                    if (MOX::colony[c].owner == player) colonies[n++] = c;
                }
                if (n == 0) return false;
                const int32_t found = Reveal_Near_(player, Star_Of_Colony_(colonies[Pick_(n)]), DISCOVER_STARS);
                if (found == 0) return false;
                Log_("a discoverer maps the stars near one of its colonies", player);
                return true;
            }
            case KIND_ANCIENT_WRECK: {
                int16_t apps[TECH_APP_COUNT];
                int32_t n = 0;
                s_player* me = &MOX::player[player];
                for (int16_t a = 0; a < TECH_APP_COUNT; ++a) {
                    if (me->tech_applications[a] == TECH_RESEARCH_STATUS_RESEARCHED || !COLCALC::Player_May_Get_Tech_App(me, a)) continue;
                    for (int16_t o = 0; o < Players_(); ++o) {
                        if (o != player && Alive_(o) && MOX::player[o].tech_applications[a] == TECH_RESEARCH_STATUS_RESEARCHED) {
                            apps[n++] = a;
                            break;
                        }
                    }
                }
                if (n == 0) return false;
                COLCALC::Player_Gets_Tech_App(me, apps[Pick_(n)]);
                Log_("an ancient wreck yields a technology", player);
                return true;
            }
            default: return false;
        }
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

    void Turn() {
        if (!enabled) return;
        Pay_Modifiers_();
        Bankruptcy_();
        // Expired entries free their slots.
        for (int32_t i = 0; i < MAX_ENTRIES; ++i) {
            s_entry& e = Entry_Slots()[i];
            if (e.kind != KIND_NONE && e.ends != 0 && e.ends <= MOX::stardate) e = s_entry{};
        }
        if (galaxy_chance > 0 && static_cast<int32_t>(game_random::Random(1000)) <= galaxy_chance) {
            static const e_kind GALAXY[3] = {KIND_GOLDEN_AGE, KIND_TOURIST_BOOM, KIND_ORE_RUSH};
            Start_Galaxy_Event(GALAXY[Pick_(3)]);
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
            static const e_kind EMPIRE[3] = {KIND_AMBASSADOR_KILLED, KIND_DISCOVERER, KIND_ANCIENT_WRECK};
            Start_Empire_Event(EMPIRE[Pick_(3)], p);
        }
    }

} // namespace world_events
