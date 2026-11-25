#include "pch.h"

#include "game/moo_world.h"

#include "game/bill.h"
#include "game/colcalc.h"
#include "game/events.h"
#include "game/mox.h"
#include "game/officer.h"
#include "game/opinion.h"
#include "game/pacts.h"
#include "game/pirate_world.h"
#include "game/pop.h"
#include "game/random.h"
#include "game/reputation.h"
#include "game/stability.h"
#include "game/techdata.h"
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

        // The statuses of the leader records and the first treaty that means war (s_player::treaty).
        constexpr int8_t STATUS_POOL = 0;
        constexpr int8_t STATUS_ASSIGNED = 1;
        constexpr int8_t STATUS_LIMBO = 2;
        constexpr int8_t TREATY_FIRST_WAR = 4;

        bool Holds_(const s_leader_data& leader, int16_t player) {
            return leader.player_index == player &&
                (leader.status == STATUS_POOL || leader.status == STATUS_ASSIGNED || leader.status == STATUS_LIMBO);
        }

        // Famous exile --------------------------------------------------------------------------------------

        void Exile_Step_() {
            for (int16_t p = 0; p < Players_(); ++p) {
                if (MOX::player[p].eliminated != 0) Start_Exile(p);
            }
        }

        // Scientist defection -------------------------------------------------------------------------------

        // The leader of `owner` with a science skill (the spy operations' scientist), the highest level; -1 when none.
        int16_t Scientist_(int16_t owner) {
            int16_t best = -1;
            for (int16_t i = 0; i < MAX_LEADERS; ++i) {
                const s_leader_data& l = MOX::leaders[i];
                if (!Holds_(l, owner)) continue;
                if ((l.special_skills & LEADER_COLONY_SKILL_SCIENCE_LEADER) == 0 && (l.general_skills & LEADER_GENERAL_SKILL_RESEARCHER) == 0) continue;
                if (best < 0 || OFFICER::Owned_Officer_Level(i) > OFFICER::Owned_Officer_Level(best)) best = i;
            }
            return best;
        }

        // A colony of `owner` in unrest: stability at DEFECT_STABILITY or worse (morale when stability is off).
        bool Unstable_(int16_t owner) {
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                const s_colony& colony = MOX::colony[c];
                if (colony.owner != owner || colony.outpost_flag != 0) continue;
                if (stability::enabled ? stability::Of(c) <= DEFECT_STABILITY : colony.morale <= DEFECT_MORALE) return true;
            }
            return false;
        }

        // The cheapest (by field cost) application `from` has and `to` lacks and may have; -1 when none.
        int16_t Gift_Tech_(int16_t from, int16_t to) {
            const auto cost = [](int16_t app) {
                const int16_t field = TECHDATA::technology_applications[app].tech_field_id;
                return field >= 0 && field < TECH_FIELD_COUNT ? static_cast<int64_t>(TECHDATA::technology_fields[field].cost) : INT64_MAX;
            };
            s_player* giver = &MOX::player[from];
            s_player* taker = &MOX::player[to];
            int16_t best = -1;
            for (int32_t app = TECH_APP_NO_TECH + 1; app < TECH_APP_COUNT; ++app) {
                if (giver->tech_applications[app] != TECH_RESEARCH_STATUS_RESEARCHED) continue;
                if (taker->tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED || !COLCALC::Player_May_Get_Tech_App(taker, app)) continue;
                if (best < 0 || cost(static_cast<int16_t>(app)) < cost(best)) best = static_cast<int16_t>(app);
            }
            return best;
        }

        // Council's demand -----------------------------------------------------------------------------------

        bool Guilty_(int16_t player) {
            for (int16_t j = 0; j < MAX_PLAYERS; ++j) {
                if (MOX::player[player].biological_weapons_flag[j] != 0) return true;
            }
            return reputation::Has(player, reputation::TITLE_ATROCITIES);
        }

        void Council_Step_() {
            s_state* s = State();
            for (int16_t p = 0; p < Players_(); ++p) {
                const uint16_t bit = static_cast<uint16_t>(1u << p);
                if (!Alive_(p)) continue;
                const bool guilty = Guilty_(p);
                const bool was = (s->guilty_mask & bit) != 0;
                if (guilty && !was) {
                    s->guilty_mask = static_cast<uint16_t>(s->guilty_mask | bit);
                    Council_Demand(p);
                } else if (!guilty && was) {
                    s->guilty_mask = static_cast<uint16_t>(s->guilty_mask & ~bit);
                }
            }
        }

        // Antaran scout ----------------------------------------------------------------------------------------

        void Scout_Step_() {
            const s_state* s = State();
            if (MOX::antarans.attack_pressure < SCOUT_PRESSURE) return;
            if (s->last_scout != 0 && MOX::stardate - s->last_scout < SCOUT_COOLDOWN) return;
            int16_t colonies[MAX_COLONIES];
            int32_t n = 0;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                const s_colony& colony = MOX::colony[c];
                const int16_t star = Colony_Star_(c);
                if (Alive_(colony.owner) && colony.outpost_flag == 0 && star >= 0 && star < MAX_STARS) colonies[n++] = c;
            }
            if (n == 0) return;
            const int16_t c = colonies[Pick_(n)];
            Start_Scout(MOX::colony[c].owner, Colony_Star_(c));
        }

        // The neighbours of a scouted colony answer: a pair of computers not at war with each other that think well
        // of each other signs a pact (or leans to each other when the pacts are off).
        void Neighbours_Answer_(int16_t victim, int16_t star) {
            bool near_star[MAX_PLAYERS] = {};
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                const int16_t owner = MOX::colony[c].owner;
                const int16_t at = Colony_Star_(c);
                if (!Computer_(owner) || owner == victim || at < 0 || at >= MAX_STARS) continue;
                if (Distance2_(star, at) <= SCOUT_REACH2) near_star[owner] = true;
            }
            for (int16_t a = 0; a < Players_(); ++a) {
                for (int16_t b = static_cast<int16_t>(a + 1); b < Players_(); ++b) {
                    if (!near_star[a] || !near_star[b]) continue;
                    if (MOX::player[a].treaty[b] >= TREATY_FIRST_WAR || MOX::player[b].treaty[a] >= TREATY_FIRST_WAR) continue;
                    if (opinion::Relations(a, b) < 0 || opinion::Relations(b, a) < 0) continue;
                    if (pacts::enabled) {
                        if (pacts::Has_Pact(a, b)) continue;
                        pacts::Sign_Pact(a, b, -1);
                    } else {
                        opinion::Add(a, b, opinion::REASON_DID_US_A_SERVICE, SCOUT_OPINION);
                        opinion::Add(b, a, opinion::REASON_DID_US_A_SERVICE, SCOUT_OPINION);
                    }
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "moo_world: stardate %d, players %d and %d draw together against the Antarans (%s)",
                        (int)MOX::stardate, (int)a, (int)b, pacts::enabled ? "a defence pact" : "good will");
                }
            }
        }

        // Refugees ----------------------------------------------------------------------------------------------

        // The refugees of `r` reach the nearest colony of a third empire with room for them; true when done.
        bool Run_Refugees_(const s_record& r) {
            if (MOX::stardate < r.ends) return false;
            int16_t best = -1;
            int64_t best_distance = INT64_MAX;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                const s_colony& colony = MOX::colony[c];
                const int16_t star = Colony_Star_(c);
                if (!Alive_(colony.owner) || colony.owner == r.player || colony.owner == r.option || colony.outpost_flag != 0) continue;
                if (star < 0 || star >= MAX_STARS || colony.n_pops == 0 || colony.n_pops >= MAX_COLONY_POPULATION) continue;
                if (colony.n_pops >= COLCALC::Planet_Max_Population_For_Player(&MOX::planet[colony.planet], colony.owner)) continue;
                const int64_t d = Distance2_(r.target, star);
                if (d < best_distance) {
                    best_distance = d;
                    best = c;
                }
            }
            if (best < 0) {
                Log_(r.player, "its refugees find nobody to take them in");
                return true;
            }
            s_colony& colony = MOX::colony[best];
            const int16_t host = colony.owner;
            if (Refugee_Choice(host) == 0) {
                uint32_t pop = 0;
                POP::Set_Race(&pop, static_cast<uint32_t>(r.value));
                POP::Set_Original_Owner(&pop, r.value < MAX_PLAYERS ? static_cast<uint32_t>(r.value) : static_cast<uint32_t>(r.player));
                POP::Set_Prof(&pop, POP::PROF_WORKER);
                colony.pop[colony.n_pops] = pop;
                ++colony.n_pops;
                COLCALC::Colony_Calculation(best);
                opinion::Add(r.option, host, opinion::REASON_BAD_RUMOURS, REFUGEE_AGGRESSOR_OPINION);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "moo_world: stardate %d, player %d takes in the refugees of player %d (driven out by player %d)",
                    (int)MOX::stardate, (int)host, (int)r.player, (int)r.option);
            } else {
                if (Alive_(r.player)) opinion::Add(r.player, host, opinion::REASON_ABANDONED_US, REFUGEE_REFUSED_OPINION);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "moo_world: stardate %d, player %d turns the refugees of player %d away",
                    (int)MOX::stardate, (int)host, (int)r.player);
            }
            return true;
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

        // A new event may start; at most one a turn. The scandal first, then the defection, then the epidemics.
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
                if (!Computer_(p) || Scientist_(p) < 0 || !Unstable_(p)) continue;
                if (static_cast<int32_t>(game_random::Random(1000)) > DEFECT_CHANCE) continue;
                if (Start_Defection(p)) return;
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

    int32_t Refugee_Choice(int16_t player) {
        if (!enabled || !Computer_(player)) return 0;
        switch (MOX::player[player].personality) {
            case PLAYER_PERSONALITY_XENOPHOBIC:
            case PLAYER_PERSONALITY_RUTHLESS:
            case PLAYER_PERSONALITY_AGGRESSIVE: return 1;
            default: return 0;
        }
    }

    bool Start_Exile(int16_t dead) {
        if (!enabled || dead < 0 || dead >= Players_() || MOX::player[dead].eliminated == 0) return false;
        s_state* s = State();
        const uint16_t bit = static_cast<uint16_t>(1u << dead);
        if ((s->exile_mask & bit) != 0) return false;
        s->exile_mask = static_cast<uint16_t>(s->exile_mask | bit);
        int16_t best = -1;
        for (int16_t i = 0; i < MAX_LEADERS; ++i) {
            if (!Holds_(MOX::leaders[i], dead)) continue;
            if (best < 0 || OFFICER::Owned_Officer_Level(i) > OFFICER::Owned_Officer_Level(best)) best = i;
        }
        if (best < 0) return false;
        OFFICER::Deassign_Officer(best);  // onto the hire list
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "moo_world: stardate %d, player %d: leader %d of the fallen empire offers service as an exile",
            (int)MOX::stardate, (int)dead, (int)best);
        int16_t suitor = -1;
        int32_t suitor_opinion = INT_MIN;
        int32_t suitor_cost = 0;
        for (int16_t q = 0; q < Players_(); ++q) {
            if (q == dead || !Computer_(q) || !OFFICER::Player_Can_Hire_More_Officers(q)) continue;
            const int32_t cost = OFFICER::Officer_Cost(q, best);
            if (MOX::player[q].bc < cost) continue;
            const int32_t liking = opinion::Relations(q, dead);
            if (liking > suitor_opinion) {
                suitor = q;
                suitor_opinion = liking;
                suitor_cost = cost;
            }
        }
        if (suitor < 0) return true;
        Add_Bc_(suitor, -suitor_cost);
        OFFICER::Set_Officer_To_Player(best, suitor, 1);
        if (MOX::leaders[best].player_index != suitor) {
            Add_Bc_(suitor, suitor_cost);  // no room after all: the money stays
            return true;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "moo_world: stardate %d, player %d hires leader %d, the exile of player %d (%d BC)",
            (int)MOX::stardate, (int)suitor, (int)best, (int)dead, (int)suitor_cost);
        return true;
    }

    bool Start_Defection(int16_t owner) {
        if (!enabled || !Computer_(owner) || !Unstable_(owner)) return false;
        const int16_t leader = Scientist_(owner);
        if (leader < 0) return false;
        int16_t target = -1;
        int16_t app = -1;
        for (int16_t q = 0; q < Players_(); ++q) {
            if (q == owner || !Alive_(q) || !Contact_(owner, q) || !OFFICER::Player_Can_Hire_More_Officers(q)) continue;
            const int16_t gift = Gift_Tech_(owner, q);
            if (gift < 0) continue;
            if (target < 0 || opinion::Relations(owner, q) > opinion::Relations(owner, target)) {
                target = q;
                app = gift;
            }
        }
        if (target < 0) return false;
        OFFICER::Deassign_Officer(leader);
        OFFICER::Set_Officer_To_Player(leader, target, 1);
        if (MOX::leaders[leader].player_index != target) return false;
        COLCALC::Player_Gets_Tech_App(&MOX::player[target], app);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "moo_world: stardate %d, player %d: scientist %d defects to player %d and brings application %d",
            (int)MOX::stardate, (int)owner, (int)leader, (int)target, (int)app);
        if (BILL::Get_Their_Spy_Number(owner, target) > 0) opinion::Add(owner, target, opinion::REASON_PLOTTED_AGAINST_US, DEFECT_OPINION);
        return true;
    }

    bool Council_Demand(int16_t player) {
        if (!enabled || !Alive_(player)) return false;
        s_state* s = State();
        const uint16_t bit = static_cast<uint16_t>(1u << player);
        const auto tell_all = [&](opinion::e_reason reason, int32_t value) {
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o != player && Alive_(o) && Contact_(o, player)) opinion::Add(o, player, reason, value);
            }
        };
        if ((s->agreed_mask & bit) != 0) {
            s->agreed_mask = static_cast<uint16_t>(s->agreed_mask & ~bit);
            tell_all(opinion::REASON_BROKE_ITS_WORD, COUNCIL_BROKE_OPINION);
            Log_(player, "uses biological weapons again after promising the council to stop");
            return true;
        }
        const int8_t personality = MOX::player[player].personality;
        const bool refuses = Computer_(player) && (personality == PLAYER_PERSONALITY_XENOPHOBIC || personality == PLAYER_PERSONALITY_RUTHLESS ||
                                                   personality == PLAYER_PERSONALITY_AGGRESSIVE);
        if (refuses) {
            tell_all(opinion::REASON_BAD_RUMOURS, COUNCIL_REFUSE_OPINION);
            Log_(player, "refuses the council's demand to stop using biological weapons");
        } else {
            s->agreed_mask = static_cast<uint16_t>(s->agreed_mask | bit);
            tell_all(opinion::REASON_DID_US_A_SERVICE, COUNCIL_AGREE_OPINION);
            Log_(player, "agrees to the council's demand to stop using biological weapons");
        }
        return true;
    }

    bool Start_Scout(int16_t player, int16_t star) {
        if (!enabled || !Alive_(player) || star < 0 || star >= MAX_STARS) return false;
        s_record r;
        r.kind = KIND_ANTARAN_SCOUT;
        r.player = static_cast<int8_t>(player);
        r.target = star;
        r.started = MOX::stardate;
        r.ends = MOX::stardate + SCOUT_TURNS;
        if (!Record_(r)) return false;
        State()->last_scout = MOX::stardate;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "moo_world: stardate %d, player %d: an Antaran scout is seen near its colony at star %d",
            (int)MOX::stardate, (int)player, (int)star);
        Neighbours_Answer_(player, star);
        return true;
    }

    void On_Colony_Taken(const s_colony* colony, int16_t old_owner, int16_t new_owner) {
        if (!enabled || colony == nullptr || colony->outpost_flag != 0 || colony->n_pops == 0) return;
        if (old_owner < 0 || old_owner >= Players_() || new_owner < 0 || new_owner >= Players_() || old_owner == new_owner) return;
        const int16_t star = Colony_Star_(static_cast<int16_t>(colony - MOX::colony));
        if (star < 0 || star >= MAX_STARS) return;
        // The race of the colony's first population unit when it is one of the empires', else the old owner's.
        uint32_t race = POP::Get_Race(POP::Load(&colony->pop[0]));
        if (race >= static_cast<uint32_t>(MAX_PLAYERS)) race = static_cast<uint32_t>(old_owner);
        s_record r;
        r.kind = KIND_REFUGEES;
        r.player = static_cast<int8_t>(old_owner);
        r.option = static_cast<int8_t>(new_owner);
        r.target = star;
        r.value = static_cast<int16_t>(race);
        r.started = MOX::stardate;
        r.ends = MOX::stardate + REFUGEE_DELAY;
        if (!Record_(r)) return;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "moo_world: stardate %d, player %d: refugees flee the colony at star %d taken by player %d",
            (int)MOX::stardate, (int)old_owner, (int)star, (int)new_owner);
    }

    void On_Colony_Destroyed(const s_colony* colony, int16_t attacker) {
        if (!enabled || colony == nullptr) return;
        On_Colony_Taken(colony, colony->owner, attacker);
    }

    void On_Bio_Weapon(int16_t attacker, int16_t star) {
        if (!enabled || attacker < 0 || attacker >= Players_() || star < 0 || star >= MAX_STARS) return;
        int16_t victim = attacker;
        for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
            const int16_t owner = MOX::colony[c].owner;
            if (owner == attacker || !Alive_(owner) || Colony_Star_(c) != star) continue;
            victim = owner;
            break;
        }
        MOX::player[attacker].biological_weapons_flag[victim] = 1;
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
            } else if (r.kind == KIND_ANTARAN_SCOUT) {
                gone = MOX::stardate >= r.ends;
            } else if (r.kind == KIND_REFUGEES) {
                gone = Run_Refugees_(r);
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
        Exile_Step_();
        Council_Step_();
        Scout_Step_();
        Unite_();
        Start_New_();
    }

} // namespace moo_world
