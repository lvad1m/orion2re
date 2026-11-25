#include "pch.h"

#include "game/moo_world.h"

#include "game/bill.h"
#include "game/events.h"
#include "game/opinion.h"
#include "game/pirate_world.h"
#include "game/random.h"
#include "game/world_events.h"

#include <algorithm>
#include <climits>
#include <iterator>

// The behaviour of the world events (events-catalog.md 4.2); the records and the save section are in
// moo_world_state.cpp. game_random is drawn only with events moo_world on: the chances of a new event, the length
// of an epidemic, the king's raids and the haggling over his tribute.
namespace moo_world {

    namespace {
        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        bool Computer_(int16_t player) {
            return Alive_(player) && MOX::player[player].objectives != PLAYER_OBJECTIVE_HUMAN;
        }

        bool Contact_(int16_t a, int16_t b) {
            return MOX::player[a].contact[b] != 0 || MOX::player[b].contact[a] != 0;
        }

        int32_t Pick_(int32_t n) {
            return n <= 1 ? 0 : static_cast<int32_t>(game_random::Random(n)) - 1;
        }

        bool Chance_(int32_t percent) {
            return static_cast<int32_t>(game_random::Random(100)) <= percent;
        }

        void Log_(int16_t player, const char* what) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "moo_world: stardate %d, player %d: %s", (int)MOX::stardate, (int)player, what);
        }

        void Log_Star_(int16_t star, const char* what) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "moo_world: stardate %d, star %d: %s", (int)MOX::stardate, (int)star, what);
        }

        void Add_Bc_(int16_t player, int32_t delta) {
            s_player& me = MOX::player[player];
            me.bc = static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(me.bc) + delta, 0, INT32_MAX / 2));
        }

        void Remove_Record_(int32_t index) {
            s_state* s = State();
            for (int32_t i = index; i + 1 < s->count; ++i) s->record[i] = s->record[i + 1];
            s->record[--s->count] = s_record{};
        }

        bool Record_(const s_record& record) {
            s_state* s = State();
            if (s->count >= MAX_RECORDS) return false;
            s->record[s->count++] = record;
            return true;
        }

        bool Has_King_() {
            const s_state* s = State();
            for (int32_t i = 0; i < s->count; ++i) {
                if (s->record[i].kind == KIND_PIRATE_KING) return true;
            }
            return false;
        }

        int64_t Distance2_(int16_t a, int16_t b) {
            const int64_t dx = MOX::star[a].x - MOX::star[b].x;
            const int64_t dy = MOX::star[a].y - MOX::star[b].y;
            return dx * dx + dy * dy;
        }

        int16_t Colony_Star_(int16_t colony) {
            const int16_t planet = MOX::colony[colony].planet;
            return planet >= 0 ? MOX::planet[planet].star_index : static_cast<int16_t>(-1);
        }

        bool Ship_In_Orbit_(const s_ship_data& ship) {
            return ship.status == 0 && ship.owner >= 0 && ship.owner < Players_() && ship.location >= 0 && ship.location < MAX_STARS &&
                   ship.location < MOX::NUM_STARS;
        }

        // The empire with the most ship points in orbit at the star, -1 when none.
        int16_t Landing_Owner_(int16_t star) {
            int32_t points[MAX_PLAYERS] = {};
            for (int16_t s = 0; s < MOX::NUM_SHIPS && s < MAX_SHIPS; ++s) {
                const s_ship_data& ship = MOX::ship[s];
                if (!Ship_In_Orbit_(ship) || ship.location != star) continue;
                points[ship.owner] += ship.d.size + 1;
            }
            int16_t best = -1;
            for (int16_t p = 0; p < Players_(); ++p) {
                if (points[p] > 0 && (best < 0 || points[p] > points[best])) best = p;
            }
            return best;
        }

        // Pirate king -------------------------------------------------------------------------------------

        // The classic pirate raid (events.h): a part of the treasury and some freighters.
        void Raid_(int16_t target, const char* why) {
            s_player& p = MOX::player[target];
            const int16_t loss = p.bc > 0 ? EVENTS::Pirate_Raid_Amount(p.bc) : 0;
            if (loss > 0) EVENTS::Pirate_Raid_Loss(target, loss);
            p.n_freighters = static_cast<int16_t>(std::max<int32_t>(0, p.n_freighters - KING_FREIGHTERS));
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "moo_world: stardate %d, player %d: the pirate king raids it (%s), %d BC lost",
                (int)MOX::stardate, (int)target, why, (int)loss);
        }

        // The king asks every empire with a colony within his reach for tribute.
        void Demand_(const s_record& r) {
            bool near_king[MAX_PLAYERS] = {};
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                const int16_t owner = MOX::colony[c].owner;
                const int16_t star = Colony_Star_(c);
                if (!Alive_(owner) || star < 0 || star >= MAX_STARS) continue;
                if (Distance2_(r.target, star) <= KING_REACH2) near_king[owner] = true;
            }
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!near_king[p] || !Alive_(p)) continue;
                s_player& me = MOX::player[p];
                if (me.bc < KING_TRIBUTE_MIN) {
                    Raid_(p, "too poor for tribute");
                    continue;
                }
                const int32_t option = Choose(p);
                int32_t fee = std::max(KING_TRIBUTE_MIN, me.bc / 100 * KING_TRIBUTE_PERCENT);
                if (option == 2) {
                    Log_(p, "refuses the tribute of the pirate king");
                    Raid_(p, "tribute refused");
                    continue;
                }
                if (option == 1) {
                    fee /= 2;
                    if (!Chance_(KING_HAGGLE_PERCENT)) {
                        Log_(p, "haggles over the tribute, the pirate king will not hear of it");
                        Raid_(p, "haggling failed");
                        continue;
                    }
                }
                fee = std::min(fee, me.bc);
                Add_Bc_(p, -fee);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "moo_world: stardate %d, player %d: pays %d BC of tribute to the pirate king",
                    (int)MOX::stardate, (int)p, (int)fee);
            }
        }

        // One turn of the king; true when he is gone.
        bool Run_King_(s_record& r) {
            if (MOX::stardate >= r.ends) {
                Log_Star_(r.target, "the horde of the pirate king breaks up");
                return true;
            }
            if (!EVENTS::Pirates_At_Star_Turn(r.target, &r.value, static_cast<int16_t>(std::max<int32_t>(1, r.extra)))) {
                const int16_t lander = Landing_Owner_(r.target);
                if (lander >= 0) {
                    Add_Bc_(lander, KING_LOOT + KING_LOOT_PER_STRENGTH * r.extra);
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "moo_world: stardate %d, player %d storms the stronghold of the pirate king at star %d and takes the loot",
                        (int)MOX::stardate, (int)lander, (int)r.target);
                } else {
                    Log_Star_(r.target, "the pirate king is destroyed");
                }
                return true;
            }
            const int32_t age = MOX::stardate - r.started;
            if (age > 0 && age % KING_PERIOD == 0) Demand_(r);
            return false;
        }

        struct s_presence {
            int16_t star = -1;
            int16_t value = 0;
            bool lair = false;
        };

        // Lairs of the pirate world and hired pirates that are there now.
        int32_t Presences_(s_presence* out, int32_t max) {
            int32_t n = 0;
            if (pirate_world::enabled) {
                const pirate_world::s_state* ps = pirate_world::State();
                for (int32_t i = 0; i < ps->count && n < max; ++i) {
                    const pirate_world::s_record& r = ps->record[i];
                    if (r.kind == pirate_world::KIND_LAIR && r.target >= 0 && r.value > 0) out[n++] = {r.target, r.value, true};
                }
            }
            const world_events::s_entry* entries = world_events::Entry_Slots();
            for (int32_t i = 0; i < world_events::MAX_ENTRIES && n < max; ++i) {
                const world_events::s_entry& e = entries[i];
                if (e.kind == world_events::KIND_HIRED_PIRATES && e.target >= 0 && e.value > 0 && (e.ends == 0 || e.ends > MOX::stardate)) {
                    out[n++] = {e.target, e.value, false};
                }
            }
            return n;
        }

        // The presences at `star` go into the king.
        void Absorb_(int16_t star) {
            pirate_world::s_state* ps = pirate_world::State();
            for (int32_t i = 0; i < ps->count;) {
                if (ps->record[i].kind == pirate_world::KIND_LAIR && ps->record[i].target == star) {
                    for (int32_t j = i; j + 1 < ps->count; ++j) ps->record[j] = ps->record[j + 1];
                    ps->record[--ps->count] = pirate_world::s_record{};
                } else {
                    ++i;
                }
            }
            world_events::s_entry* entries = world_events::Entry_Slots();
            for (int32_t i = 0; i < world_events::MAX_ENTRIES; ++i) {
                if (entries[i].kind == world_events::KIND_HIRED_PIRATES && entries[i].target == star) entries[i] = world_events::s_entry{};
            }
        }

        // The pirates count the turns they have stood together; when it is long enough a king rises.
        void Unite_() {
            s_state* s = State();
            s_presence presence[world_events::MAX_ENTRIES + pirate_world::MAX_RECORDS];
            const int32_t n = Presences_(presence, static_cast<int32_t>(std::size(presence)));
            if (n < 2 || Has_King_()) {
                s->unite_turns = 0;
                return;
            }
            if (++s->unite_turns < UNITE_TURNS) return;
            int32_t strongest = 0;
            int32_t sum = 0;
            for (int32_t i = 0; i < n; ++i) {
                sum += presence[i].value;
                if (presence[i].value > presence[strongest].value) strongest = i;
            }
            const int16_t star = presence[strongest].star;
            // Every presence goes into the king, wherever it stood.
            for (int32_t i = 0; i < n; ++i) Absorb_(presence[i].star);
            s->unite_turns = 0;
            Raise_King(star, static_cast<int16_t>(std::min<int32_t>(sum, 9999)));
        }

        // Epidemic in a fleet ----------------------------------------------------------------------------

        // The star where `player` keeps the most ships in orbit (at least FLEET_MIN_SHIPS, not yet grounded).
        int16_t Fleet_Star_(int16_t player) {
            int16_t count[MAX_STARS] = {};
            for (int16_t s = 0; s < MOX::NUM_SHIPS && s < MAX_SHIPS; ++s) {
                const s_ship_data& ship = MOX::ship[s];
                if (ship.owner == player && Ship_In_Orbit_(ship)) ++count[ship.location];
            }
            int16_t best = -1;
            for (int16_t star = 0; star < MOX::NUM_STARS && star < MAX_STARS; ++star) {
                if (count[star] < FLEET_MIN_SHIPS || Star_Grounded(star)) continue;
                if (best < 0 || count[star] > count[best]) best = star;
            }
            return best;
        }

        // A new event may start; at most one a turn. The scandal first, then the epidemics.
        void Start_New_() {
            for (int16_t owner = 0; owner < Players_(); ++owner) {
                if (!Alive_(owner)) continue;
                for (int16_t target = 0; target < Players_(); ++target) {
                    if (target == owner || !Alive_(target)) continue;
                    const int32_t spies = BILL::Get_Their_Spy_Number(owner, target);
                    if (spies <= 0) continue;
                    if (static_cast<int32_t>(game_random::Random(1000)) > spies * SCANDAL_PER_SPY) continue;
                    if (Start_Scandal(owner, target)) return;
                }
            }
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!Alive_(p)) continue;
                const int16_t star = Fleet_Star_(p);
                if (star < 0) continue;
                if (static_cast<int32_t>(game_random::Random(1000)) > EPIDEMIC_CHANCE) continue;
                if (Start_Epidemic(p, star, EPIDEMIC_MIN_TURNS + Pick_(EPIDEMIC_SPAN_TURNS + 1))) return;
            }
        }
    } // namespace

    int32_t Choose(int16_t player) {
        if (!enabled || !Computer_(player)) return 0;
        switch (MOX::player[player].personality) {
            case PLAYER_PERSONALITY_HONORABLE:
            case PLAYER_PERSONALITY_PACIFIST: return 0;
            case PLAYER_PERSONALITY_RUTHLESS:
            case PLAYER_PERSONALITY_AGGRESSIVE: return 2;
            default: return 1;
        }
    }

    bool Raise_King(int16_t star, int16_t strength) {
        if (!enabled || star < 0 || star >= MAX_STARS || strength <= 0 || Has_King_()) return false;
        s_record r;
        r.kind = KIND_PIRATE_KING;
        r.target = star;
        r.started = MOX::stardate;
        r.ends = MOX::stardate + KING_TURNS;
        r.value = strength;
        r.extra = strength;
        if (!Record_(r)) return false;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "moo_world: stardate %d, star %d: the pirates unite, a pirate king rises (strength %d)",
            (int)MOX::stardate, (int)star, (int)strength);
        return true;
    }

    bool Start_Epidemic(int16_t player, int16_t star, int32_t turns) {
        if (!enabled || !Alive_(player) || star < 0 || star >= MAX_STARS || turns <= 0 || Star_Grounded(star)) return false;
        s_record r;
        r.kind = KIND_FLEET_EPIDEMIC;
        r.player = static_cast<int8_t>(player);
        r.target = star;
        r.started = MOX::stardate;
        r.ends = MOX::stardate + turns;
        if (!Record_(r)) return false;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "moo_world: stardate %d, player %d: an epidemic in the fleet at star %d, no ship leaves it for %d turns",
            (int)MOX::stardate, (int)player, (int)star, (int)turns);
        return true;
    }

    bool Start_Scandal(int16_t owner, int16_t target) {
        if (!enabled || !Alive_(owner) || !Alive_(target) || owner == target) return false;
        if (BILL::Get_Their_Spy_Number(owner, target) <= 0) return false;
        BILL::Set_Their_Spy_Number(owner, target, 0);
        opinion::Add(target, owner, opinion::REASON_CAUGHT_SPYING, SCANDAL_OPINION);
        for (int16_t o = 0; o < Players_(); ++o) {
            if (o == owner || o == target || !Alive_(o) || !Contact_(o, owner)) continue;
            opinion::Add(o, owner, opinion::REASON_BAD_RUMOURS, SCANDAL_RUMOUR);
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "moo_world: stardate %d, player %d: a spy scandal, its network in player %d is exposed and lost",
            (int)MOX::stardate, (int)owner, (int)target);
        return true;
    }

    void Run_Records() {
        if (!enabled) return;
        s_state* s = State();
        for (int32_t i = 0; i < s->count;) {
            s_record& r = s->record[i];
            bool gone = false;
            if (r.kind == KIND_PIRATE_KING) {
                gone = Run_King_(r);
            } else if (r.kind == KIND_FLEET_EPIDEMIC) {
                gone = MOX::stardate >= r.ends || !Alive_(r.player);
                if (gone) Log_(r.player, "the epidemic in the fleet is over");
            }
            if (gone) {
                Remove_Record_(i);
            } else {
                ++i;
            }
        }
    }

    void Turn() {
        if (!enabled) return;
        Run_Records();
        Unite_();
        Start_New_();
    }

} // namespace moo_world
