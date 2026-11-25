#include "pch.h"

#include "game/beasts.h"

#include "game/events.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/ships.h"

#include <algorithm>
#include <climits>

// The behaviour of the beasts (events-catalog.md 3.7 and 3.12); the records and the save section are in
// beasts_state.cpp. game_random is drawn only with events beasts on: the chances of a new event, the pick of a
// star or a subject, the swallowing of a ship and the guardians of the wormhole.
namespace beasts {

    namespace {
        constexpr int16_t STOLEN_TYPE = NONPLAYER_SHIP_TYPE_CRYSTAL;
        constexpr int16_t UNLEASHED_TYPE = NONPLAYER_SHIP_TYPE_HYDRA;
        constexpr int8_t SHIP_STATUS_ORBIT = 0;
        constexpr int8_t SHIP_STATUS_GONE = 5;

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        bool Computer_(int16_t player) {
            return Alive_(player) && MOX::player[player].objectives != PLAYER_OBJECTIVE_HUMAN;
        }

        int32_t Pick_(int32_t n) {
            return n <= 1 ? 0 : static_cast<int32_t>(game_random::Random(n)) - 1;
        }

        bool Chance_(int32_t percent) {
            return static_cast<int32_t>(game_random::Random(100)) <= percent;
        }

        void Log_Star_(int16_t star, const char* what) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "beasts: stardate %d, star %d: %s", (int)MOX::stardate, (int)star, what);
        }

        void Log_Player_(int16_t player, int16_t star, const char* what) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "beasts: stardate %d, player %d, star %d: %s", (int)MOX::stardate, (int)player, (int)star, what);
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

        s_record New_(e_kind kind, int16_t player, int32_t turns) {
            s_record r;
            r.kind = kind;
            r.player = static_cast<int8_t>(player);
            r.started = MOX::stardate;
            r.ends = turns > 0 ? MOX::stardate + turns : 0;
            return r;
        }

        int32_t Count_(e_kind kind) {
            const s_state* s = State();
            int32_t n = 0;
            for (int32_t i = 0; i < s->count; ++i) n += s->record[i].kind == kind ? 1 : 0;
            return n;
        }

        bool Record_At_Star_(e_kind kind, int16_t star) {
            const s_state* s = State();
            for (int32_t i = 0; i < s->count; ++i) {
                if (s->record[i].kind == kind && s->record[i].target == star) return true;
            }
            return false;
        }

        bool Has_Player_Record_(e_kind kind, int16_t player) {
            const s_state* s = State();
            for (int32_t i = 0; i < s->count; ++i) {
                if (s->record[i].kind == kind && s->record[i].player == player) return true;
            }
            return false;
        }

        int32_t Distance2_(int16_t a, int16_t b) {
            const int32_t dx = MOX::star[a].x - MOX::star[b].x;
            const int32_t dy = MOX::star[a].y - MOX::star[b].y;
            return dx * dx + dy * dy;
        }

        // Monsters are ships whose owner is their type (as in world_events).
        bool Monster_At_(int16_t type, int16_t star) {
            for (int16_t i = 0; i < MOX::NUM_SHIPS && i < MAX_SHIPS; ++i) {
                const s_ship_data& ship = MOX::ship[i];
                if (ship.owner == type && ship.status < SHIP_STATUS_GONE && ship.location == star) return true;
            }
            return false;
        }

        bool Any_Monster_At_(int16_t star) {
            for (int16_t i = 0; i < MOX::NUM_SHIPS && i < MAX_SHIPS; ++i) {
                const s_ship_data& ship = MOX::ship[i];
                if (ship.owner >= MAX_PLAYERS && ship.status < SHIP_STATUS_GONE && ship.location == star) return true;
            }
            return false;
        }

        // An empire's ship idle in orbit at a star.
        bool Idle_Ship_(int16_t s, int16_t owner) {
            const s_ship_data& ship = MOX::ship[s];
            return ship.owner == owner && ship.status == SHIP_STATUS_ORBIT && ship.location >= 0 && ship.location < MOX::NUM_STARS;
        }

        // The empire with the most ship points in orbit at the star, -1 when none.
        int16_t Landing_Owner_(int16_t star) {
            int32_t points[MAX_PLAYERS] = {};
            for (int16_t s = 0; s < MOX::NUM_SHIPS && s < MAX_SHIPS; ++s) {
                const s_ship_data& ship = MOX::ship[s];
                if (ship.location != star || ship.status != SHIP_STATUS_ORBIT || ship.owner < 0 || ship.owner >= Players_()) continue;
                points[ship.owner] += ship.d.size + 1;
            }
            int16_t best = -1;
            for (int16_t p = 0; p < Players_(); ++p) {
                if (points[p] > 0 && (best < 0 || points[p] > points[best])) best = p;
            }
            return best;
        }

        bool Colony_Star_(int16_t star) {
            return MOX::star[star].has_colony != 0;
        }

        // The colony star nearest `from` (not `from`) that harvesters do not haunt; -1 when none.
        int16_t Nearest_Free_Colony_Star_(int16_t from) {
            int16_t best = -1;
            int32_t best_distance = INT_MAX;
            for (int16_t s = 0; s < MOX::NUM_STARS && s < MAX_STARS; ++s) {
                if (s == from || !Colony_Star_(s) || Record_At_Star_(KIND_HARVESTERS, s)) continue;
                const int32_t d = Distance2_(from, s);
                if (d < best_distance) {
                    best = s;
                    best_distance = d;
                }
            }
            return best;
        }

        bool Is_Belt_(int16_t star) {
            for (int32_t p = 0; p < MOX::planet_count && p < MAX_PLANETS; ++p) {
                if (MOX::planet[p].star_index == star && MOX::planet[p].planet_type == PLANET_TYPE_ASTEROID) return true;
            }
            return false;
        }

        // Harvesters ---------------------------------------------------------------------------------------

        // The base of the harvesters, nullptr when there is none.
        s_record* Base_() {
            s_state* s = State();
            for (int32_t i = 0; i < s->count; ++i) {
                if (s->record[i].kind == KIND_BASE) return &s->record[i];
            }
            return nullptr;
        }

        bool Spawn_Swarm_(int16_t star, int32_t strength) {
            if (Count_(KIND_HARVESTERS) >= MAX_HARVESTERS || State()->count >= MAX_RECORDS || Record_At_Star_(KIND_HARVESTERS, star)) return false;
            s_record r = New_(KIND_HARVESTERS, -1, 0);
            r.target = star;
            r.value = static_cast<int16_t>(strength);
            return Record_(r);
        }

        // The nearest star without a colony to `from`; -1 when none.
        int16_t Nearest_Empty_Star_(int16_t from) {
            int16_t best = -1;
            int32_t best_distance = INT_MAX;
            for (int16_t s = 0; s < MOX::NUM_STARS && s < MAX_STARS; ++s) {
                if (s == from || Colony_Star_(s)) continue;
                const int32_t d = Distance2_(from, s);
                if (d < best_distance) {
                    best = s;
                    best_distance = d;
                }
            }
            return best;
        }

        // The harvesters at `star` are destroyed: while their base stands it grows a level and a stronger swarm
        // is on its way.
        void Strike_Back_(int16_t star) {
            s_record* base = Base_();
            if (base == nullptr) return;
            base->extra = static_cast<int16_t>(std::min<int32_t>(COUNTER_MAX_LEVEL, base->extra + 1));
            base->other = star;
            base->ends = MOX::stardate + COUNTER_DELAY;
            Log_Star_(base->target, "the base of the harvesters answers the loss: the next swarm will be stronger");
        }

        bool Start_Harvesters_() {
            if (Count_(KIND_HARVESTERS) >= MAX_HARVESTERS || State()->count >= MAX_RECORDS) return false;
            int16_t cand[MAX_STARS];
            int32_t n = 0;
            for (int16_t s = 0; s < MOX::NUM_STARS && s < MAX_STARS; ++s) {
                if (Colony_Star_(s) && !Record_At_Star_(KIND_HARVESTERS, s)) cand[n++] = s;
            }
            if (n == 0) return false;
            s_record r = New_(KIND_HARVESTERS, -1, 0);
            r.target = cand[Pick_(n)];
            const s_record* base = Base_();
            r.value = static_cast<int16_t>(HARVESTER_STRENGTH + COUNTER_STEP * (base != nullptr ? base->extra : 0));
            if (!Record_(r)) return false;
            Log_Star_(r.target, "self-replicating harvesters arrive");
            // Their base: a star without colonies, the nearest to where they arrive.
            if (Base_() == nullptr && State()->count < MAX_RECORDS) {
                const int16_t at = Nearest_Empty_Star_(r.target);
                if (at >= 0) {
                    s_record b = New_(KIND_BASE, -1, 0);
                    b.target = at;
                    b.value = BASE_STRENGTH;
                    if (Record_(b)) Log_Star_(at, "the base of the self-replicating harvesters is set up");
                }
            }
            return true;
        }

        // One turn of a harvester record; true when it is gone.
        bool Run_Harvesters_(s_record& r) {
            const int16_t points = EVENTS::Ship_Size_Points_At_Star(r.target);
            if (points > 0) {
                r.value = static_cast<int16_t>(r.value - points);
                if (r.value <= 0) {
                    const int16_t lander = Landing_Owner_(r.target);
                    if (lander >= 0) {
                        Add_Bc_(lander, SALVAGE_BC);
                        Log_Player_(lander, r.target, "destroys the harvesters and salvages them");
                    } else {
                        Log_Star_(r.target, "the harvesters are destroyed");
                    }
                    Strike_Back_(r.target);
                    return true;
                }
            }
            if (!Colony_Star_(r.target)) {
                const int16_t next = Nearest_Free_Colony_Star_(r.target);
                if (next < 0) {
                    Log_Star_(r.target, "the harvesters find nothing to strip and fall silent");
                    return true;
                }
                Log_Star_(next, "the harvesters move on to a colony");
                r.target = next;
                return false;
            }
            const int32_t age = MOX::stardate - r.started;
            if (age > 0 && age % HARVEST_PERIOD == 0) {
                for (int16_t p = 0; p < Players_(); ++p) {
                    if (((MOX::star[r.target].has_colony >> p) & 1) == 0 || !Alive_(p)) continue;
                    const int32_t loss = std::min<int32_t>(HARVEST_BC, MOX::player[p].bc);
                    Add_Bc_(p, -loss);
                    Log_Player_(p, r.target, "the harvesters strip its colony");
                }
                if (r.value < HARVESTER_CAP) ++r.value;  // a swarm stronger than the cap (a counter-strike) does not shrink
            }
            if (age >= SPLIT_TURNS && r.value >= 2 && Count_(KIND_HARVESTERS) < MAX_HARVESTERS) {
                const int16_t dest = Nearest_Free_Colony_Star_(r.target);
                if (dest >= 0) {
                    s_record child = New_(KIND_HARVESTERS, -1, 0);
                    child.target = dest;
                    child.value = static_cast<int16_t>(r.value / 2);
                    if (Record_(child)) {
                        r.value = static_cast<int16_t>(r.value - child.value);
                        r.started = MOX::stardate;
                        Log_Star_(dest, "the harvesters split in two");
                    }
                }
            }
            return false;
        }

        // One turn of the base; true when warships have destroyed it.
        bool Run_Base_(s_record& r) {
            const int16_t points = EVENTS::Ship_Size_Points_At_Star(r.target);
            if (points > 0) {
                r.value = static_cast<int16_t>(r.value - points);
                if (r.value <= 0) {
                    const int16_t lander = Landing_Owner_(r.target);
                    if (lander >= 0) {
                        Add_Bc_(lander, BASE_BC);
                        Log_Player_(lander, r.target, "destroys the base of the harvesters");
                    } else {
                        Log_Star_(r.target, "the base of the harvesters is destroyed");
                    }
                    return true;
                }
            }
            if (r.ends > 0 && MOX::stardate >= r.ends) {
                int16_t dest = r.other;
                if (dest < 0 || !Colony_Star_(dest) || Record_At_Star_(KIND_HARVESTERS, dest)) {
                    dest = Nearest_Free_Colony_Star_(dest >= 0 ? dest : r.target);
                }
                if (dest >= 0 && Spawn_Swarm_(dest, HARVESTER_STRENGTH + COUNTER_STEP * r.extra)) {
                    Log_Star_(dest, "a stronger swarm of the harvesters strikes back");
                    r.ends = 0;
                }
            }
            return false;
        }

        // Ambush -------------------------------------------------------------------------------------------

        bool Start_Ambush_() {
            if (Count_(KIND_AMBUSH) >= MAX_AMBUSHES || State()->count >= MAX_RECORDS) return false;
            int16_t cand[MAX_STARS];
            int32_t n = 0;
            for (int16_t s = 0; s < MOX::NUM_STARS && s < MAX_STARS; ++s) {
                if (Colony_Star_(s) || !Is_Belt_(s) || Record_At_Star_(KIND_AMBUSH, s) || Any_Monster_At_(s)) continue;
                cand[n++] = s;
            }
            if (n == 0) return false;
            s_record r = New_(KIND_AMBUSH, -1, AMBUSH_TURNS);
            r.target = cand[Pick_(n)];
            if (!Record_(r)) return false;
            Log_Star_(r.target, "a predator lurks in the asteroid belt");
            return true;
        }

        // The first ship of an empire that is the only one of its owner at the star; -1 when none.
        int16_t Lone_Ship_At_(int16_t star) {
            for (int16_t s = 0; s < MOX::NUM_SHIPS && s < MAX_SHIPS; ++s) {
                const s_ship_data& ship = MOX::ship[s];
                if (ship.location != star || ship.status != SHIP_STATUS_ORBIT || !Alive_(ship.owner)) continue;
                int32_t mine = 0;
                for (int16_t o = 0; o < MOX::NUM_SHIPS && o < MAX_SHIPS; ++o) {
                    mine += (MOX::ship[o].owner == ship.owner && MOX::ship[o].location == star && MOX::ship[o].status == SHIP_STATUS_ORBIT) ? 1 : 0;
                }
                if (mine == 1) return s;
            }
            return -1;
        }

        void Free_Ship_(s_record& r) {
            r.option = 0;
            r.player = -1;
            r.other = -1;
            r.value = 0;
        }

        // One turn of an ambush; true when the predator is gone.
        bool Run_Ambush_(s_record& r) {
            if (r.option == 1) {
                const int16_t ship = r.other;
                if (!Idle_Ship_(ship, r.player) || MOX::ship[ship].location != r.target) {
                    Log_Player_(r.player, r.target, "the swallowed ship breaks free");
                    Free_Ship_(r);
                    return false;
                }
                for (int16_t s = 0; s < MOX::NUM_SHIPS && s < MAX_SHIPS; ++s) {
                    if (s != ship && Idle_Ship_(s, r.player) && MOX::ship[s].location == r.target) {
                        Log_Player_(r.player, r.target, "a rescuer drives the predator off, the swallowed ship is freed");
                        return true;
                    }
                }
                if (--r.value <= 0) {
                    SHIPS::Kill_Ship(ship);
                    Log_Player_(r.player, r.target, "the predator devours the swallowed ship");
                    Free_Ship_(r);
                }
                return false;
            }
            if (MOX::stardate >= r.ends) {
                Log_Star_(r.target, "the predator leaves the belt");
                return true;
            }
            const int16_t victim = Lone_Ship_At_(r.target);
            if (victim < 0 || !Chance_(AMBUSH_PERCENT)) return false;
            r.option = 1;
            r.player = static_cast<int8_t>(MOX::ship[victim].owner);
            r.other = victim;
            r.value = AMBUSH_SAVE_TURNS;
            Log_Player_(r.player, r.target, "a predator swallows its ship in the asteroid belt");
            return false;
        }

        // Stolen flagship ----------------------------------------------------------------------------------

        bool Start_Hijack_(int16_t player) {
            if (!Computer_(player) || Has_Player_Record_(KIND_HIJACK, player) || State()->count >= MAX_RECORDS) return false;
            int16_t best = -1;
            for (int16_t s = 0; s < MOX::NUM_SHIPS && s < MAX_SHIPS; ++s) {
                if (!Idle_Ship_(s, player) || MOX::ship[s].d.cost < HIJACK_MIN_COST) continue;
                if (best < 0 || MOX::ship[s].d.cost > MOX::ship[best].d.cost) best = s;
            }
            if (best < 0) return false;
            const int16_t star = MOX::ship[best].location;
            const int32_t cost = MOX::ship[best].d.cost;
            if (SHIPS::Build_Nonplayer_Ship(STOLEN_TYPE, star, 0) < 0) return false;
            SHIPS::Kill_Ship(best);
            s_record r = New_(KIND_HIJACK, player, HIJACK_TURNS);
            r.target = star;
            r.value = static_cast<int16_t>(std::min<int32_t>(cost, 30000));
            if (!Record_(r)) return true;  // the ship is stolen all the same, nobody will claim it back
            Log_Player_(player, star, "its flagship is stolen and roams as a rogue");
            return true;
        }

        // One turn of a stolen flagship; true when it is settled.
        bool Run_Hijack_(s_record& r) {
            if (!Monster_At_(STOLEN_TYPE, r.target)) {
                const int16_t lander = Landing_Owner_(r.target);
                if (lander < 0) {
                    Log_Star_(r.target, "the stolen flagship is gone");
                    return true;
                }
                const bool own = lander == r.player;
                Add_Bc_(lander, HIJACK_REWARD + (own ? r.value : r.value / 2));
                if (!own && Alive_(r.player)) opinion::Add(r.player, lander, opinion::REASON_DID_US_A_SERVICE, HIJACK_OPINION);
                Log_Player_(lander, r.target, own ? "wins back its stolen flagship's worth" : "destroys a stolen flagship and is rewarded");
                return true;
            }
            if (MOX::stardate >= r.ends) {
                if (SHIPS::Build_Nonplayer_Ship(UNLEASHED_TYPE, r.target, 0) >= 0) {
                    Log_Star_(r.target, "the thieves unleash a monster of their own");
                } else {
                    Log_Star_(r.target, "the thieves of the flagship vanish");
                }
                return true;
            }
            return false;
        }

        // Wormhole -----------------------------------------------------------------------------------------

        bool Start_Wormhole_(int16_t player) {
            if (!Computer_(player) || Has_Player_Record_(KIND_WORMHOLE, player) || State()->count >= MAX_RECORDS) return false;
            int16_t entrances[MAX_STARS];
            int32_t ne = 0;
            int16_t exits[MAX_STARS];
            int32_t nx = 0;
            for (int16_t s = 0; s < MOX::NUM_STARS && s < MAX_STARS; ++s) {
                if (((MOX::star[s].has_colony >> player) & 1) != 0) entrances[ne++] = s;
                else if (((MOX::star[s].visited >> player) & 1) == 0) exits[nx++] = s;
            }
            if (ne == 0 || nx == 0) return false;
            s_record r = New_(KIND_WORMHOLE, player, WORM_TURNS);
            r.target = entrances[Pick_(ne)];
            r.other = exits[Pick_(nx)];
            if (!Record_(r)) return false;
            Log_Player_(player, r.target, "an unstable wormhole opens");
            return true;
        }

        int32_t Worm_Percent_(int16_t player) {
            switch (MOX::player[player].personality) {
                case PLAYER_PERSONALITY_HONORABLE:
                case PLAYER_PERSONALITY_PACIFIST: return WORM_PERCENT_SAFE;
                case PLAYER_PERSONALITY_RUTHLESS:
                case PLAYER_PERSONALITY_AGGRESSIVE: return WORM_PERCENT_BOLD;
                default: return WORM_PERCENT;
            }
        }

        // The nearest star `player` has not visited to `star` (the exit itself counts): it becomes known.
        void Reveal_Near_(int16_t player, int16_t star) {
            const uint8_t bit = static_cast<uint8_t>(1u << player);
            MOX::star[star].visited = static_cast<uint8_t>(MOX::star[star].visited | bit);
            int16_t best = -1;
            int32_t best_distance = INT_MAX;
            for (int16_t s = 0; s < MOX::NUM_STARS && s < MAX_STARS; ++s) {
                if ((MOX::star[s].visited & bit) != 0) continue;
                const int32_t d = Distance2_(star, s);
                if (d < best_distance) {
                    best = s;
                    best_distance = d;
                }
            }
            if (best >= 0) MOX::star[best].visited = static_cast<uint8_t>(MOX::star[best].visited | bit);
        }

        // One turn of a wormhole; true when it collapses.
        bool Run_Wormhole_(s_record& r) {
            const int16_t player = r.player;
            if (!Alive_(player) || MOX::stardate >= r.ends || ((MOX::star[r.target].has_colony >> player) & 1) == 0) {
                Log_Player_(player, r.target, "the wormhole collapses");
                return true;
            }
            int16_t ship = -1;
            for (int16_t s = 0; s < MOX::NUM_SHIPS && s < MAX_SHIPS; ++s) {
                if (!Idle_Ship_(s, player) || MOX::ship[s].location != r.target) continue;
                if (ship < 0 || MOX::ship[s].d.cost > MOX::ship[ship].d.cost) ship = s;
            }
            if (ship < 0 || !Chance_(Worm_Percent_(player))) return false;
            const int32_t danger = WORM_DANGER + WORM_DANGER_STEP * r.value;
            if (Chance_(danger)) {
                SHIPS::Kill_Ship(ship);
                Log_Player_(player, r.target, "the guardians beyond the wormhole destroy its ship");
            } else {
                const int32_t reward = WORM_REWARD + WORM_REWARD_STEP * r.value;
                Add_Bc_(player, reward);
                Reveal_Near_(player, r.other);
                Log_Player_(player, r.target, "its ship returns through the wormhole with a reward");
            }
            ++r.value;
            if (r.value >= WORM_MAX_USES) {
                Log_Player_(player, r.target, "the wormhole collapses after its last use");
                return true;
            }
            return false;
        }

        // One turn of a record; true when it ends.
        bool Run_(s_record& r) {
            switch (r.kind) {
                case KIND_HARVESTERS: return Run_Harvesters_(r);
                case KIND_AMBUSH: return Run_Ambush_(r);
                case KIND_HIJACK: return Run_Hijack_(r);
                case KIND_WORMHOLE: return Run_Wormhole_(r);
                case KIND_BASE: return Run_Base_(r);
                default: return true;
            }
        }
    } // namespace

    bool Start(e_kind kind, int16_t player) {
        if (!enabled) return false;
        switch (kind) {
            case KIND_HARVESTERS: return Start_Harvesters_();
            case KIND_AMBUSH: return Start_Ambush_();
            case KIND_HIJACK: return Start_Hijack_(player);
            case KIND_WORMHOLE: return Start_Wormhole_(player);
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
        if (static_cast<int32_t>(game_random::Random(1000)) <= HARVESTER_CHANCE && Start_Harvesters_()) return;
        if (static_cast<int32_t>(game_random::Random(1000)) <= AMBUSH_CHANCE && Start_Ambush_()) return;
        for (int16_t p = 0; p < Players_(); ++p) {
            if (!Alive_(p)) continue;
            if (static_cast<int32_t>(game_random::Random(1000)) > EMPIRE_CHANCE) continue;
            if (Start(Pick_(2) == 0 ? KIND_HIJACK : KIND_WORMHOLE, p)) return;
        }
    }

} // namespace beasts
