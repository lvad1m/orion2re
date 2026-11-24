#include "pch.h"

#include "game/pirate_world.h"

#include "game/colcalc.h"
#include "game/events.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/reputation.h"
#include "game/ships.h"
#include "game/techdata.h"

#include <algorithm>
#include <climits>

// The behaviour of the pirate world (events-catalog.md 3.6 and 3.12); the records and the save section are in
// pirate_world_state.cpp. game_random is drawn only with events pirate_world on: the chances of a new event, the
// pick of a subject, the raids (the classic Pirate_Raid_Amount) and the chances of the lasting ones.
namespace pirate_world {

    namespace {
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

        bool Chance_(int32_t percent) {
            return static_cast<int32_t>(game_random::Random(100)) <= percent;
        }

        void Log_(int16_t player, const char* what) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pirate_world: stardate %d, player %d: %s", (int)MOX::stardate, (int)player, what);
        }

        void Log_Star_(int16_t star, const char* what) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pirate_world: stardate %d, star %d: %s", (int)MOX::stardate, (int)star, what);
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
            r.ends = MOX::stardate + turns;
            return r;
        }

        bool Has_Record_(e_kind kind, int16_t player) {
            const s_state* s = State();
            for (int32_t i = 0; i < s->count; ++i) {
                if (s->record[i].kind == kind && s->record[i].player == player) return true;
            }
            return false;
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

        // The classic pirate raid (events.h): a part of the treasury and some freighters; returns the loss.
        int32_t Raid_(int16_t target, const char* why) {
            s_player& p = MOX::player[target];
            const int16_t loss = p.bc > 0 ? EVENTS::Pirate_Raid_Amount(p.bc) : 0;
            if (loss > 0) EVENTS::Pirate_Raid_Loss(target, loss);
            p.n_freighters = static_cast<int16_t>(std::max<int32_t>(0, p.n_freighters - RAID_FREIGHTERS));
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pirate_world: stardate %d, player %d: pirates raid it (%s), %d BC lost",
                (int)MOX::stardate, (int)target, why, (int)loss);
            return loss;
        }

        // Ships and colonies --------------------------------------------------------------------------

        bool Live_Ship_(int16_t ship, int16_t player) {
            return MOX::ship[ship].owner == player && MOX::ship[ship].status != 5;
        }

        int16_t Colony_Star_(int16_t colony) {
            const int16_t planet = MOX::colony[colony].planet;
            return planet >= 0 ? MOX::planet[planet].star_index : static_cast<int16_t>(-1);
        }

        int32_t Distance2_(int16_t a, int16_t b) {
            const int32_t dx = MOX::star[a].x - MOX::star[b].x;
            const int32_t dy = MOX::star[a].y - MOX::star[b].y;
            return dx * dx + dy * dy;
        }

        // The empire with the most ship points in orbit at the star, -1 when none.
        int16_t Landing_Owner_(int16_t star) {
            int32_t points[MAX_PLAYERS] = {};
            for (int16_t s = 0; s < MOX::NUM_SHIPS && s < MAX_SHIPS; ++s) {
                const s_ship_data& ship = MOX::ship[s];
                if (ship.location != star || ship.status != 0 || ship.owner < 0 || ship.owner >= Players_()) continue;
                points[ship.owner] += ship.d.size + 1;
            }
            int16_t best = -1;
            for (int16_t p = 0; p < Players_(); ++p) {
                if (points[p] > 0 && (best < 0 || points[p] > points[best])) best = p;
            }
            return best;
        }

        // A colony people may be taken from: two or more people, no ship at its star.
        bool Weak_Colony_(int16_t c) {
            const s_colony& colony = MOX::colony[c];
            if (!Alive_(colony.owner) || colony.n_pops < 2 || colony.outpost_flag != 0) return false;
            const int16_t star = Colony_Star_(c);
            return star >= 0 && EVENTS::Ship_Size_Points_At_Star(star) == 0;
        }

        // The weak colony nearest to `from` (any star but `from` and one the slavers already haunt) of `owner`
        // (-1: any empire); -1 when none.
        int16_t Weak_Colony_Near_(int16_t from, int16_t owner) {
            int16_t best = -1;
            int32_t best_distance = INT_MAX;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                if ((owner >= 0 && MOX::colony[c].owner != owner) || !Weak_Colony_(c)) continue;
                const int16_t star = Colony_Star_(c);
                if (star == from || Record_At_Star_(KIND_SLAVERS, star)) continue;
                const int32_t d = from >= 0 ? Distance2_(from, star) : 0;
                if (d < best_distance) {
                    best = c;
                    best_distance = d;
                }
            }
            return best;
        }

        // Technology ------------------------------------------------------------------------------------

        bool Known_(const s_player& p, int32_t app) {
            return p.tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED;
        }

        int64_t App_Cost_(int32_t app) {
            const int16_t field = TECHDATA::technology_applications[app].tech_field_id;
            return field >= 0 && field < TECH_FIELD_COUNT ? static_cast<int64_t>(TECHDATA::technology_fields[field].cost) : INT64_MAX;
        }

        // The cheapest (by field cost) application `player` may get and does not know; -1 when none.
        int32_t Cheapest_Unknown_App_(int16_t player) {
            s_player* me = &MOX::player[player];
            int32_t best = -1;
            for (int32_t app = TECH_APP_NO_TECH + 1; app < TECH_APP_COUNT; ++app) {
                if (Known_(*me, app) || !COLCALC::Player_May_Get_Tech_App(me, app)) continue;
                if (best < 0 || App_Cost_(app) < App_Cost_(best)) best = app;
            }
            return best;
        }

        bool Give_App_(int16_t player) {
            const int32_t app = Cheapest_Unknown_App_(player);
            if (app < 0) return false;
            COLCALC::Player_Gets_Tech_App(&MOX::player[player], static_cast<int16_t>(app));
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pirate_world: stardate %d, player %d: gets application %d", (int)MOX::stardate, (int)player, app);
            return true;
        }

        // Tribute ----------------------------------------------------------------------------------------

        // A computer at war with `player` that can afford to outbid the tribute; the richest, -1 when none.
        int16_t Rival_(int16_t player) {
            int16_t best = -1;
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o == player || !Computer_(o) || !At_War_(o, player) || MOX::player[o].bc < RIVAL_MIN_BC) continue;
                if (best < 0 || MOX::player[o].bc > MOX::player[best].bc) best = o;
            }
            return best;
        }

        bool Start_Tribute_(int16_t player, int32_t option) {
            s_player& me = MOX::player[player];
            if (me.bc < TRIBUTE_MIN) return false;  // a poor empire is not worth the raiders' trouble
            int32_t fee = std::max(TRIBUTE_MIN, me.bc / 100 * TRIBUTE_PERCENT);
            if (option == TRIBUTE_REFUSE) {
                Log_(player, "refuses the tribute of the raiders");
                Raid_(player, "tribute refused");
                return true;
            }
            if (option == TRIBUTE_HAGGLE) {
                fee /= 2;
                if (!Chance_(TRIBUTE_HAGGLE_PERCENT)) {
                    Log_(player, "haggles over the tribute, the raiders will not hear of it");
                    Raid_(player, "haggling failed");
                    return true;
                }
            }
            Add_Bc_(player, -std::min(fee, me.bc));
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pirate_world: stardate %d, player %d: pays %d BC of tribute to the raiders", (int)MOX::stardate,
                (int)player, (int)fee);
            const int16_t rival = Rival_(player);
            if (rival >= 0 && Chance_(RIVAL_PERCENT)) {
                Add_Bc_(rival, -RIVAL_PRICE);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pirate_world: stardate %d, player %d outbids the tribute of player %d for a raid", (int)MOX::stardate,
                    (int)rival, (int)player);
                Raid_(player, "outbid by a rival");
            }
            return true;
        }

        // Privateers -------------------------------------------------------------------------------------

        bool Start_Privateers_(int16_t player) {
            if (!Computer_(player) || Has_Record_(KIND_PRIVATEERS, player)) return false;
            if (MOX::player[player].bc < HIRE_KEEP_BC + PRIVATEER_ESCROW) return false;
            for (int16_t enemy = 0; enemy < Players_(); ++enemy) {
                if (enemy == player || !Alive_(enemy) || !At_War_(player, enemy)) continue;
                s_record r = New_(KIND_PRIVATEERS, player, PRIVATEER_TURNS);
                r.other = enemy;
                r.value = PRIVATEER_ESCROW;
                if (!Record_(r)) return false;
                Add_Bc_(player, -PRIVATEER_ESCROW);
                reputation::Note_Deed(player);  // stains and titles (off by default): toward the corsair
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pirate_world: stardate %d, player %d licenses privateers against player %d (%d BC in escrow)",
                    (int)MOX::stardate, (int)player, (int)enemy, PRIVATEER_ESCROW);
                return true;
            }
            return false;
        }

        // The cheapest ship of `enemy` in orbit at a star without a colony of it; -1 when none.
        int16_t Straggler_(int16_t enemy) {
            int16_t best = -1;
            for (int16_t s = 0; s < MOX::NUM_SHIPS && s < MAX_SHIPS; ++s) {
                const s_ship_data& ship = MOX::ship[s];
                if (!Live_Ship_(s, enemy) || ship.status != 0 || ship.location < 0 || ship.location >= MOX::NUM_STARS) continue;
                if (((MOX::star[ship.location].has_colony >> enemy) & 1) != 0) continue;
                if (best < 0 || ship.d.cost < MOX::ship[best].d.cost) best = s;
            }
            return best;
        }

        // Slavers ----------------------------------------------------------------------------------------

        bool Start_Slavers_(int16_t player) {
            if (State()->count >= MAX_RECORDS) return false;
            int16_t cand[MAX_COLONIES];
            int32_t n = 0;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                if (MOX::colony[c].owner != player || !Weak_Colony_(c) || Record_At_Star_(KIND_SLAVERS, Colony_Star_(c))) continue;
                cand[n++] = c;
            }
            if (n == 0) return false;
            const int16_t c = cand[Pick_(n)];
            s_record r = New_(KIND_SLAVERS, player, SLAVER_TURNS);
            r.target = Colony_Star_(c);
            r.value = r.extra = SLAVER_STRENGTH;
            if (!Record_(r)) return false;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pirate_world: stardate %d, player %d: slavers come to star %d", (int)MOX::stardate, (int)player,
                (int)r.target);
            return true;
        }

        // A computer that buys captives (not honorable or pacifist, with the money) and has a colony with room;
        // the colony is returned in `place`. -1 when none.
        int16_t Slave_Buyer_(int16_t victim, int16_t* place) {
            int16_t best = -1;
            for (int16_t b = 0; b < Players_(); ++b) {
                if (b == victim || !Computer_(b) || MOX::player[b].bc < SLAVER_PRICE) continue;
                const int8_t personality = MOX::player[b].personality;
                if (personality == PLAYER_PERSONALITY_HONORABLE || personality == PLAYER_PERSONALITY_PACIFIST) continue;
                int16_t room = -1;
                for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                    const s_colony& colony = MOX::colony[c];
                    if (colony.owner != b || colony.outpost_flag != 0 || colony.planet < 0 || colony.n_pops == 0 || colony.n_pops >= MAX_COLONY_POPULATION) continue;
                    if (colony.n_pops >= COLCALC::Planet_Max_Population_For_Player(&MOX::planet[colony.planet], b)) continue;
                    if (room < 0 || colony.n_pops < MOX::colony[room].n_pops) room = c;
                }
                if (room < 0) continue;
                if (best < 0 || MOX::player[b].bc > MOX::player[best].bc) {
                    best = b;
                    *place = room;
                }
            }
            return best;
        }

        void Sell_Captive_(int16_t victim, uint32_t captive) {
            int16_t place = -1;
            const int16_t buyer = Slave_Buyer_(victim, &place);
            if (buyer < 0) return;
            Add_Bc_(buyer, -SLAVER_PRICE);
            s_colony& colony = MOX::colony[place];
            colony.pop[colony.n_pops] = captive;
            ++colony.n_pops;
            COLCALC::Colony_Calculation(place);
            reputation::Note_Deed(buyer);
            opinion::Add(victim, buyer, opinion::REASON_RAIDED_US, SLAVER_VICTIM_OPINION);
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o != buyer && o != victim && Alive_(o) && Contact_(o, buyer)) opinion::Add(o, buyer, opinion::REASON_BAD_RUMOURS, SLAVER_OPINION);
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pirate_world: stardate %d, player %d buys a captive of player %d from the slavers (%d BC)",
                (int)MOX::stardate, (int)buyer, (int)victim, SLAVER_PRICE);
        }

        // One turn of the slavers; true when they are done.
        bool Run_Slavers_(s_record& r) {
            if (MOX::stardate >= r.ends) {
                Log_Star_(r.target, "the slavers leave");
                return true;
            }
            const int16_t before = r.value;
            if (!EVENTS::Pirates_At_Star_Turn(r.target, &r.value, std::max<int16_t>(1, r.extra))) {
                Log_Star_(r.target, "the slavers are driven off");
                return true;
            }
            if (r.value < before) return false;  // warships in orbit fight them: no raid this turn
            int16_t victim_colony = -1;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                if (Colony_Star_(c) == r.target && Weak_Colony_(c)) {
                    victim_colony = c;
                    break;
                }
            }
            if (victim_colony < 0) {
                // Nothing to take here (the colony is gone or has been reinforced): move on.
                const int16_t next = Weak_Colony_Near_(r.target, -1);
                if (next < 0) {
                    Log_Star_(r.target, "the slavers find no prey and leave");
                    return true;
                }
                r.target = Colony_Star_(next);
                r.player = MOX::colony[next].owner;
                return false;
            }
            if (!Chance_(SLAVER_PERCENT)) return false;
            s_colony& colony = MOX::colony[victim_colony];
            const int16_t victim = colony.owner;
            const uint32_t captive = colony.pop[colony.n_pops - 1];
            --colony.n_pops;
            COLCALC::Colony_Calculation(victim_colony);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pirate_world: stardate %d, player %d: the slavers carry off a person from colony %d",
                (int)MOX::stardate, (int)victim, (int)victim_colony);
            Sell_Captive_(victim, captive);
            if (++r.option >= SLAVER_MAX_CAPTIVES) {
                Log_Star_(r.target, "the slavers have their fill and leave");
                return true;
            }
            const int16_t next = Weak_Colony_Near_(r.target, -1);
            if (next < 0) {
                Log_Star_(r.target, "the slavers find no more prey and leave");
                return true;
            }
            r.target = Colony_Star_(next);
            r.player = MOX::colony[next].owner;
            return false;
        }

        // Lairs and havens ---------------------------------------------------------------------------------

        bool Start_Lair_() {
            if (Count_(KIND_LAIR) >= LAIR_MAX || State()->count >= MAX_RECORDS) return false;
            int16_t cache[MAX_STARS];
            int16_t plain[MAX_STARS];
            int32_t caches = 0;
            int32_t plains = 0;
            for (int16_t s = 0; s < MOX::NUM_STARS && s < MAX_STARS; ++s) {
                if (MOX::star[s].has_colony != 0 || Record_At_Star_(KIND_LAIR, s) || EVENTS::Star_Has_Event(s) != -1) continue;
                if (MOX::star[s].system_special == SYSTEM_SPECIAL_PIRATE_CACHE) {
                    cache[caches++] = s;
                } else {
                    plain[plains++] = s;
                }
            }
            if (caches + plains == 0) return false;
            const bool at_cache = caches > 0;
            const int16_t star = at_cache ? cache[Pick_(caches)] : plain[Pick_(plains)];
            s_record r = New_(KIND_LAIR, -1, 0);
            r.target = star;
            r.value = r.extra = static_cast<int16_t>(LAIR_STRENGTH + (at_cache ? LAIR_CACHE_BONUS : 0));
            r.ends = 0;
            if (!Record_(r)) return false;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pirate_world: stardate %d, a pirate lair appears at star %d (strength %d)", (int)MOX::stardate,
                (int)star, (int)r.value);
            return true;
        }

        void Lair_Raid_(const s_record& r) {
            int16_t owner = -1;
            int32_t best_distance = INT_MAX;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                const s_colony& colony = MOX::colony[c];
                const int16_t star = Colony_Star_(c);
                if (!Alive_(colony.owner) || star < 0) continue;
                const int32_t d = Distance2_(r.target, star);
                if (d < best_distance) {
                    best_distance = d;
                    owner = colony.owner;
                }
            }
            if (owner < 0 || Licensed(owner)) return;
            if (!Chance_(r.value * 100 / LAIR_CAP)) return;
            Raid_(owner, "from a pirate lair");
        }

        // One turn of a lair; true when it is gone.
        bool Run_Lair_(s_record& r) {
            const int16_t before = r.value;
            if (!EVENTS::Pirates_At_Star_Turn(r.target, &r.value, LAIR_CAP)) {
                const int16_t lander = Landing_Owner_(r.target);
                if (lander >= 0) {
                    Add_Bc_(lander, LAIR_LOOT + LAIR_LOOT_PER_STRENGTH * r.extra);
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pirate_world: stardate %d, player %d storms the pirate lair at star %d and takes the loot",
                        (int)MOX::stardate, (int)lander, (int)r.target);
                } else {
                    Log_Star_(r.target, "the pirate lair is destroyed");
                }
                return true;
            }
            if (r.value < before) return false;  // warships wear it down
            const int32_t age = MOX::stardate - r.started;
            if (age > 0 && age % LAIR_GROW_TURNS == 0 && r.value < LAIR_CAP) {
                const int16_t was = r.value;
                r.value = static_cast<int16_t>(std::min(LAIR_CAP, r.value + LAIR_GROW));
                r.extra = std::max(r.extra, r.value);
                if (was < HAVEN_STRENGTH && r.value >= HAVEN_STRENGTH) Log_Star_(r.target, "the pirate lair grows into a haven");
            }
            if (age > 0 && age % LAIR_RAID_PERIOD == 0) Lair_Raid_(r);
            return false;
        }

        bool Start_Haven_(int16_t player, int32_t option) {
            if (!Has_Haven()) return false;
            s_player& me = MOX::player[player];
            switch (option) {
                case HAVEN_DEFECTORS:
                    if (me.bc < HAVEN_DEFECTORS_BC || Cheapest_Unknown_App_(player) < 0) return false;
                    Add_Bc_(player, -HAVEN_DEFECTORS_BC);
                    Give_App_(player);
                    Log_(player, "takes in defectors from a pirate haven");
                    return true;
                case HAVEN_FLAG: {
                    if (me.bc < HAVEN_FLAG_BC || Licensed(player)) return false;
                    if (!Record_(New_(KIND_LICENCE, player, HAVEN_FLAG_TURNS))) return false;
                    Add_Bc_(player, -HAVEN_FLAG_BC);
                    Log_(player, "buys a flag of convenience from a pirate haven");
                    return true;
                }
                default:
                    if (me.bc < HAVEN_MARKET_BC || Cheapest_Unknown_App_(player) < 0) return false;
                    Add_Bc_(player, -HAVEN_MARKET_BC);
                    for (int32_t i = 0; i < HAVEN_MARKET_APPS; ++i) {
                        if (!Give_App_(player)) break;
                    }
                    reputation::Note_Deed(player);
                    for (int16_t o = 0; o < Players_(); ++o) {
                        if (o != player && Alive_(o) && Contact_(o, player)) opinion::Add(o, player, opinion::REASON_BAD_RUMOURS, HAVEN_MARKET_OPINION);
                    }
                    Log_(player, "buys technologies on the black market of a pirate haven");
                    return true;
            }
        }

        // Syndicate and smuggler ---------------------------------------------------------------------------

        bool Start_Syndicate_(int16_t player, int32_t option) {
            if (option == SYNDICATE_ARREST) {
                Add_Bc_(player, SYNDICATE_LOOT);
                Log_(player, "arrests the secret base of the syndicate and confiscates its money");
                return true;
            }
            if (Has_Record_(KIND_SYNDICATE, player)) return false;
            s_record r = New_(KIND_SYNDICATE, player, SYNDICATE_TURNS);
            r.option = static_cast<int8_t>(option);
            if (!Record_(r)) return false;
            if (option == SYNDICATE_SEIZE) {
                reputation::Note_Deed(player);
                Log_(player, "seizes the secret base of the syndicate");
            } else {
                Log_(player, "infiltrates the secret base of the syndicate");
            }
            return true;
        }

        // The empire a seized base raids: the richest at war with `owner`, else the richest in contact.
        int16_t Raid_Victim_(int16_t owner) {
            int16_t best = -1;
            bool best_war = false;
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o == owner || !Alive_(o) || !Contact_(owner, o)) continue;
                const bool war = At_War_(owner, o);
                if (best < 0 || (war && !best_war) || (war == best_war && MOX::player[o].bc > MOX::player[best].bc)) {
                    best = o;
                    best_war = war;
                }
            }
            return best;
        }

        bool Run_Syndicate_(s_record& r) {
            if (!Alive_(r.player) || MOX::stardate >= r.ends) {
                Log_(r.player, "the secret base of the syndicate is spent");
                return true;
            }
            if (r.option == SYNDICATE_STUDY) {
                Add_Bc_(r.player, STUDY_BC);
                if (Chance_(STUDY_LEAK_PERCENT)) {
                    Log_(r.player, "the syndicate finds the mole, the trade secrets dry up");
                    return true;
                }
                return false;
            }
            Add_Bc_(r.player, SEIZE_BC);
            if ((MOX::stardate - r.started) % SEIZE_PERIOD == 0) {
                const int16_t victim = Raid_Victim_(r.player);
                if (victim >= 0) {
                    const int32_t loss = Raid_(victim, "from a seized base of the syndicate");
                    Add_Bc_(r.player, loss * SEIZE_SHARE_PERCENT / 100);
                    reputation::Note_Deed(r.player);
                }
            }
            return false;
        }

        bool Start_Smuggler_(int16_t player, int32_t option) {
            switch (option) {
                case SMUGGLER_REFUSE:
                    Add_Bc_(player, SMUGGLER_FINE);
                    Log_(player, "turns the repentant smuggler away and jails him");
                    return true;
                case SMUGGLER_AMNESTY: {
                    Add_Bc_(player, AMNESTY_BC);
                    s_state* s = State();
                    for (int32_t i = 0; i < s->count;) {
                        s_record& r = s->record[i];
                        if (r.kind == KIND_LAIR) {
                            r.value = static_cast<int16_t>(r.value - CRACKDOWN_LOSS);
                            if (r.value <= 0) {
                                Log_Star_(r.target, "the pirate lair is destroyed in a crackdown");
                                Remove_Record_(i);
                                continue;
                            }
                        }
                        ++i;
                    }
                    Log_(player, "grants the smuggler an amnesty and cracks down on the crime");
                    if (Chance_(AMNESTY_LAIR_PERCENT)) {
                        Log_(player, "the smuggler deceived it: new pirates appear");
                        Start_Lair_();
                    }
                    return true;
                }
                default:
                    Add_Bc_(player, USE_BC);
                    reputation::Note_Deed(player);
                    Log_(player, "uses the smuggler as a guide to the pirates");
                    Start_Lair_();
                    return true;
            }
        }

        // One turn of a record; true when it ends.
        bool Run_(s_record& r) {
            switch (r.kind) {
                case KIND_LAIR: return Run_Lair_(r);
                case KIND_SLAVERS: return Run_Slavers_(r);
                case KIND_SYNDICATE: return Run_Syndicate_(r);
                case KIND_LICENCE:
                    if (!Alive_(r.player) || MOX::stardate >= r.ends) {
                        Log_(r.player, "the flag of convenience runs out");
                        return true;
                    }
                    return false;
                default: break;
            }
            // KIND_PRIVATEERS
            const int16_t employer = r.player;
            const int16_t enemy = r.other;
            const auto settle = [&](const char* why) {
                if (Alive_(employer)) Add_Bc_(employer, r.value * (100 - PRIVATEER_CUT) / 100);
                Log_(employer, why);
                return true;
            };
            if (!Alive_(employer) || !Alive_(enemy) || !At_War_(employer, enemy)) return settle("the privateers' contract is void");
            if (MOX::stardate >= r.ends || r.value < PRIVATEER_BOUNTY) return settle("the privateers' contract ends");
            if (!Chance_(PRIVATEER_PERCENT)) return false;
            const int16_t ship = Straggler_(enemy);
            if (ship < 0) return false;
            SHIPS::Kill_Ship(ship);
            r.value = static_cast<int16_t>(r.value - PRIVATEER_BOUNTY);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pirate_world: stardate %d, player %d: the privateers sink a ship of player %d (%d BC paid)",
                (int)MOX::stardate, (int)employer, (int)enemy, PRIVATEER_BOUNTY);
            return false;
        }
    } // namespace

    int32_t Choose(e_kind kind, int16_t player) {
        (void)kind;
        if (!enabled || !Computer_(player)) return 0;
        switch (MOX::player[player].personality) {
            case PLAYER_PERSONALITY_HONORABLE:
            case PLAYER_PERSONALITY_PACIFIST: return 0;
            case PLAYER_PERSONALITY_RUTHLESS:
            case PLAYER_PERSONALITY_AGGRESSIVE: return 2;
            default: return 1;
        }
    }

    bool Start(e_kind kind, int16_t player, int32_t option) {
        if (!enabled) return false;
        if (kind == KIND_LAIR) return Start_Lair_();
        if (!Alive_(player)) return false;
        const int32_t chosen = std::clamp(option >= 0 ? option : Choose(kind, player), 0, 2);
        switch (kind) {
            case KIND_TRIBUTE: return Start_Tribute_(player, chosen);
            case KIND_PRIVATEERS: return Start_Privateers_(player);
            case KIND_SLAVERS: return Start_Slavers_(player);
            case KIND_SYNDICATE: return Start_Syndicate_(player, chosen);
            case KIND_SMUGGLER: return Start_Smuggler_(player, chosen);
            case KIND_HAVEN: return Start_Haven_(player, chosen);
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
        if (static_cast<int32_t>(game_random::Random(1000)) <= LAIR_CHANCE && Start_Lair_()) return;
        static const e_kind SUBJECT[6] = {KIND_TRIBUTE, KIND_PRIVATEERS, KIND_SLAVERS, KIND_SYNDICATE, KIND_SMUGGLER, KIND_HAVEN};
        for (int16_t p = 0; p < Players_(); ++p) {
            if (!Alive_(p)) continue;
            if (static_cast<int32_t>(game_random::Random(1000)) > EMPIRE_CHANCE) continue;
            if (Start(SUBJECT[Pick_(6)], p)) return;
        }
    }

} // namespace pirate_world
