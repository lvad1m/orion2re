#include "pch.h"

#include "game/wanderers.h"

#include "game/colcalc.h"
#include "game/events.h"
#include "game/projects.h"
#include "game/random.h"

#include <algorithm>
#include <climits>

// The behaviour of wanderers: the space storm and the wandering trader (events-catalog.md 3.1 and 3.3); the
// records and the save section are in wanderers_state.cpp. game_random is drawn only with events wanderers
// on: the chance of a new wanderer, the choice of its stars, and the storm's hits.
namespace wanderers {

    namespace {
        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        int32_t Pick_(int32_t n) {
            return n <= 1 ? 0 : static_cast<int32_t>(game_random::Random(n)) - 1;
        }

        int32_t Distance2_(int16_t a, int16_t b) {
            const int32_t dx = MOX::star[a].x - MOX::star[b].x;
            const int32_t dy = MOX::star[a].y - MOX::star[b].y;
            return dx * dx + dy * dy;
        }

        int32_t Count_(e_kind kind) {
            int32_t n = 0;
            for (int32_t i = 0; i < State()->count; ++i) {
                if (State()->wanderer[i].kind == kind) ++n;
            }
            return n;
        }

        const char* Name_(uint8_t kind) {
            return kind == KIND_STORM ? "a space storm" : "a wandering trader";
        }

        int16_t Colony_Star_(int16_t colony) {
            const int16_t planet = MOX::colony[colony].planet;
            return planet >= 0 ? MOX::planet[planet].star_index : -1;
        }

        // The distinct stars that hold a colony, at most MAX_STARS.
        int32_t Colony_Stars_(int16_t* stars) {
            int32_t n = 0;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                if (MOX::colony[c].owner < 0) continue;
                const int16_t star = Colony_Star_(c);
                if (star < 0 || star >= MAX_STARS || star >= MOX::NUM_STARS) continue;
                bool seen = false;
                for (int32_t i = 0; i < n && !seen; ++i) seen = stars[i] == star;
                if (!seen) stars[n++] = star;
            }
            return n;
        }

        // Storm: every colony at the star is hit with STORM_HIT_PERCENT (an earthquake's hit).
        void Storm_Arrives_(const s_wanderer& w) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "wanderers: stardate %d, the galaxy: a space storm arrives at star %d", (int)MOX::stardate,
                (int)w.star);
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                const s_colony& colony = MOX::colony[c];
                if (colony.owner < 0 || colony.n_pops == 0 || Colony_Star_(c) != w.star) continue;
                if (static_cast<int32_t>(game_random::Random(100)) > STORM_HIT_PERCENT) continue;
                const int16_t owner = colony.owner;
                int16_t pops = 0;
                int16_t buildings = 0;
                EVENTS::Allocate_Earthquake_Hit(c, &pops, &buildings);
                COLCALC::Colony_Calculation(c);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "wanderers: stardate %d, player %d: the storm hits colony %d at star %d, %d population and %d buildings lost",
                    (int)MOX::stardate, (int)owner, (int)c, (int)w.star, (int)pops, (int)buildings);
            }
        }

        // Trader: every computer empire with a colony here may buy its cheapest unknown application.
        void Trader_Arrives_(s_wanderer& w) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "wanderers: stardate %d, the galaxy: a wandering trader stops at star %d", (int)MOX::stardate,
                (int)w.star);
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!Alive_(p)) continue;
                bool here = false;
                for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES && !here; ++c) {
                    here = MOX::colony[c].owner == p && Colony_Star_(c) == w.star;
                }
                if (!here) continue;
                if (MOX::player[p].objectives == PLAYER_OBJECTIVE_HUMAN) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "wanderers: stardate %d, player %d: a human buys nothing from the trader (no screen yet)",
                        (int)MOX::stardate, (int)p);
                    continue;
                }
                const int16_t app = projects::Cheapest_Unknown_App(p);
                if (app < 0) continue;
                const int32_t price = std::clamp(projects::App_Cost(app) / 2, TRADER_MIN_PRICE, TRADER_MAX_PRICE);
                if (MOX::player[p].bc - price < TRADER_RESERVE) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "wanderers: stardate %d, player %d: cannot afford application %d from the trader (%d BC)",
                        (int)MOX::stardate, (int)p, (int)app, (int)price);
                    continue;
                }
                MOX::player[p].bc -= price;
                w.value += price;
                COLCALC::Player_Gets_Tech_App(&MOX::player[p], app);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "wanderers: stardate %d, player %d: buys application %d from the trader for %d BC",
                    (int)MOX::stardate, (int)p, (int)app, (int)price);
            }
        }

        void Arrive_(int32_t index) {
            s_wanderer& w = State()->wanderer[index];
            if (w.kind == KIND_STORM) {
                Storm_Arrives_(w);
            } else {
                Trader_Arrives_(w);
            }
        }

        void Remove_Record_(int32_t index) {
            s_state* s = State();
            for (int32_t i = index; i + 1 < s->count; ++i) s->wanderer[i] = s->wanderer[i + 1];
            s->wanderer[--s->count] = s_wanderer{};
        }

        int32_t Move_Turns_(uint8_t kind) {
            return kind == KIND_STORM ? STORM_MOVE_TURNS : TRADER_MOVE_TURNS;
        }
    } // namespace

    int32_t Greedy_Route(int16_t from, int16_t to, int16_t* route) {
        if (from < 0 || to < 0 || from >= MOX::NUM_STARS || to >= MOX::NUM_STARS || from >= MAX_STARS || to >= MAX_STARS) return 0;
        int32_t length = 0;
        int16_t current = from;
        route[length++] = current;
        while (length < MAX_ROUTE && current != to) {
            // The STORM_NEIGHBOURS nearest stars of the current one (excluding it), nearest first.
            int16_t near[STORM_NEIGHBOURS];
            int32_t near_count = 0;
            for (int16_t s = 0; s < MOX::NUM_STARS && s < MAX_STARS; ++s) {
                if (s == current) continue;
                const int32_t d = Distance2_(current, s);
                int32_t at = near_count;
                while (at > 0 && Distance2_(current, near[at - 1]) > d) --at;
                if (at >= STORM_NEIGHBOURS) continue;
                const int32_t last = std::min(near_count, STORM_NEIGHBOURS - 1);
                for (int32_t k = last; k > at; --k) near[k] = near[k - 1];
                near[at] = s;
                near_count = std::min(near_count + 1, STORM_NEIGHBOURS);
            }
            // Of them the one nearest the goal, if it brings the storm closer.
            int16_t best = -1;
            for (int32_t k = 0; k < near_count; ++k) {
                if (best < 0 || Distance2_(near[k], to) < Distance2_(best, to)) best = near[k];
            }
            if (best < 0 || Distance2_(best, to) >= Distance2_(current, to)) break;
            current = best;
            route[length++] = current;
        }
        return length;
    }

    int32_t Add(e_kind kind, const int16_t* route, int32_t length, int32_t move_turns) {
        if (!enabled || (kind != KIND_STORM && kind != KIND_TRADER) || route == nullptr) return -1;
        if (length < 2 || length > MAX_ROUTE || move_turns < 1) return -1;
        for (int32_t i = 0; i < length; ++i) {
            if (route[i] < 0 || route[i] >= MAX_STARS || route[i] >= MOX::NUM_STARS) return -1;
        }
        s_state* s = State();
        if (s->count >= MAX_WANDERERS) return -1;
        const int32_t index = s->count++;
        s_wanderer& w = s->wanderer[index];
        w = s_wanderer{};
        w.kind = kind;
        w.length = static_cast<int8_t>(length);
        for (int32_t i = 0; i < length; ++i) w.route[i] = route[i];
        w.star = route[0];
        w.started = MOX::stardate;
        w.next_move = MOX::stardate + move_turns;
        Arrive_(index);
        return index;
    }

    bool Start_Storm() {
        if (!enabled || Count_(KIND_STORM) >= MAX_STORMS || State()->count >= MAX_WANDERERS) return false;
        int16_t stars[MAX_STARS];
        const int32_t n = Colony_Stars_(stars);
        if (n == 0 || MOX::NUM_STARS < 2) return false;
        const int16_t target = stars[Pick_(n)];
        int16_t start = static_cast<int16_t>(Pick_(std::min<int32_t>(MOX::NUM_STARS, MAX_STARS)));
        if (start == target) start = static_cast<int16_t>((start + 1) % std::min<int32_t>(MOX::NUM_STARS, MAX_STARS));
        int16_t route[MAX_ROUTE];
        const int32_t length = Greedy_Route(start, target, route);
        if (length < 2) return false;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "wanderers: stardate %d, the galaxy: a space storm forms at star %d, heading for star %d",
            (int)MOX::stardate, (int)start, (int)target);
        return Add(KIND_STORM, route, length, STORM_MOVE_TURNS) >= 0;
    }

    bool Start_Trader() {
        if (!enabled || Count_(KIND_TRADER) >= MAX_TRADERS || State()->count >= MAX_WANDERERS) return false;
        int16_t stars[MAX_STARS];
        const int32_t n = Colony_Stars_(stars);
        if (n < 2) return false;
        int16_t route[MAX_ROUTE];
        int32_t length = 0;
        route[length++] = stars[Pick_(n)];
        // Nearest-neighbour chain over the colony stars.
        while (length < TRADER_STOPS) {
            int16_t best = -1;
            for (int32_t i = 0; i < n; ++i) {
                bool used = false;
                for (int32_t k = 0; k < length && !used; ++k) used = route[k] == stars[i];
                if (used) continue;
                if (best < 0 || Distance2_(route[length - 1], stars[i]) < Distance2_(route[length - 1], best)) best = stars[i];
            }
            if (best < 0) break;
            route[length++] = best;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "wanderers: stardate %d, the galaxy: a wandering trader sets out from star %d with %d stops",
            (int)MOX::stardate, (int)route[0], (int)length);
        return Add(KIND_TRADER, route, length, TRADER_MOVE_TURNS) >= 0;
    }

    void Turn() {
        if (!enabled) return;
        s_state* s = State();
        for (int32_t i = 0; i < s->count;) {
            s_wanderer& w = s->wanderer[i];
            if (MOX::stardate < w.next_move) {
                ++i;
                continue;
            }
            if (w.step + 1 >= w.length) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "wanderers: stardate %d, the galaxy: %s at star %d is gone, %d BC earned", (int)MOX::stardate,
                    Name_(w.kind), (int)w.star, (int)w.value);
                Remove_Record_(i);
                continue;
            }
            w.trail_star = w.star;
            w.trail_until = MOX::stardate + (w.kind == KIND_STORM ? STORM_TRAIL_TURNS : 0);
            ++w.step;
            w.star = w.route[w.step];
            w.next_move = MOX::stardate + Move_Turns_(w.kind);
            Arrive_(i);
            ++i;
        }
        if (State()->count >= MAX_WANDERERS) return;
        const int32_t roll = static_cast<int32_t>(game_random::Random(1000));
        if (roll <= STORM_CHANCE) {
            Start_Storm();
        } else if (roll <= STORM_CHANCE + TRADER_CHANCE) {
            Start_Trader();
        }
    }

} // namespace wanderers
