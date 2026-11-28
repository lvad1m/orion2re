#include "pch.h"

#include "game/incidents.h"

#include "game/bill.h"
#include "game/choices.h"
#include "game/colcalc.h"
#include "game/dice.h"
#include "game/events.h"
#include "game/haccess.h"
#include "game/hostages.h"
#include "game/loyalty.h"
#include "game/officer.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/ships.h"

#include <algorithm>
#include <climits>
#include <cstdarg>

// The behaviour of the six incidents (events-catalog.md 3.2, 3.3 and 3.4); the records, the hooks and the
// save section are in incidents_state.cpp. game_random is drawn only with events incidents on: the chance
// of a new incident, the choice of its subject, its amounts and the chances of the lasting ones.
namespace incidents {

    namespace {
        constexpr int8_t LEADER_ASSIGNED = 1;
        constexpr int8_t TREATY_FIRST_WAR = 4;  // the first s_player::treaty value that means war

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        bool Human_(int16_t player) {
            return MOX::player[player].objectives == PLAYER_OBJECTIVE_HUMAN;
        }

        int32_t Pick_(int32_t n) {
            return n <= 1 ? 0 : static_cast<int32_t>(game_random::Random(n)) - 1;
        }

        void Log_(int16_t player, const char* what) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: %s", (int)MOX::stardate, (int)player, what);
        }

        void Remove_Record_(int32_t index) {
            s_state* s = State();
            for (int32_t i = index; i + 1 < s->count; ++i) s->incident[i] = s->incident[i + 1];
            s->incident[--s->count] = s_incident{};
        }

        void Add_Bc_(int16_t player, int32_t delta) {
            s_player& me = MOX::player[player];
            me.bc = static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(me.bc) + delta, 0, INT32_MAX / 2));
        }

        int16_t Colony_Star_(int16_t colony) {
            const int16_t planet = MOX::colony[colony].planet;
            return planet >= 0 ? MOX::planet[planet].star_index : -1;
        }

        // A colony with a running record of its own (morale, unrest, levy or boom): no second one is started.
        bool Colony_Busy_(int16_t colony) {
            const s_state* s = State();
            for (int32_t i = 0; i < s->count; ++i) {
                const s_incident& r = s->incident[i];
                if (r.kind != KIND_REBEL_GENERAL && r.kind != KIND_TREASURY && r.target == colony) return true;
            }
            return false;
        }

        // The colonies of `player` that can take an event; `rich_only`: a deposit may still be made richer.
        int32_t Colonies_(int16_t player, bool rich_only, int16_t* out) {
            int32_t n = 0;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                const s_colony& colony = MOX::colony[c];
                if (colony.owner != player || colony.n_pops == 0 || Colony_Busy_(c)) continue;
                if (rich_only && (colony.planet < 0 || MOX::planet[colony.planet].mineral_class >= MINERAL_RESOURCE_ULTRA_RICH)) continue;
                out[n++] = c;
            }
            return n;
        }

        bool Record_(const s_incident& record) {
            s_state* s = State();
            if (s->count >= MAX_INCIDENTS) return false;
            s->incident[s->count++] = record;
            return true;
        }

        void Add_Loyalty_(int16_t leader, int32_t delta) {
            if (loyalty::Of(leader) == loyalty::UNTRACKED) return;
            loyalty::s_record* r = loyalty::Record(leader);
            if (r == nullptr) return;
            r->loyalty = static_cast<int8_t>(std::clamp<int32_t>(r->loyalty + delta, 0, 100));
        }

        bool Ship_Captain_(int16_t leader, int16_t player) {
            const s_leader_data& l = MOX::leaders[leader];
            return l.player_index == player && l.type == LEADER_TYPE_SHIP && l.status == LEADER_ASSIGNED && l.location >= 0 &&
                l.location < MAX_SHIPS && MOX::ship[l.location].officer_index == leader;
        }

        // The colony a colony governor governs: the empire's colony at the star it is posted to.
        int16_t Governed_Colony_(int16_t leader, int16_t player) {
            const s_leader_data& l = MOX::leaders[leader];
            if (l.player_index != player || l.type != LEADER_TYPE_COLONY || l.status != LEADER_ASSIGNED || l.location < 0) return -1;
            int16_t best = -1;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                const s_colony& colony = MOX::colony[c];
                if (colony.owner != player || colony.n_pops == 0 || Colony_Star_(c) != l.location) continue;
                if (best < 0 || colony.n_pops > MOX::colony[best].n_pops) best = c;
            }
            return best;
        }

        // The enemy of the empire: the empire at war with it (a treaty of war either way) that it likes least
        // (opinion::Relations, ties to the lowest number); -1 when it is at war with nobody.
        int16_t Enemy_(int16_t player) {
            int16_t best = -1;
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o == player || !Alive_(o)) continue;
                if (MOX::player[player].treaty[o] < TREATY_FIRST_WAR && MOX::player[o].treaty[player] < TREATY_FIRST_WAR) continue;
                if (best < 0 || opinion::Relations(player, o) < opinion::Relations(player, best)) best = o;
            }
            return best;
        }

        // Design row (events-catalog.md 3.4): "Мятежный генерал | генерал не подчиняется: урезонить, игнорировать,
        // казнить; ветки — переворот или суд"; ab-review 3.3: "генерал и корабль гибнут -> доделать: корабль уходит
        // к врагу империи (движок умеет менять владельца корабля — так сделана капитуляция)".
        // The general dies; his ship changes owner to the enemy of the empire as in Do_Surrender_Empire. With no
        // enemy (at war with nobody) the ship is lost, as before.
        void Coup_(int16_t player, int16_t leader) {
            const int16_t ship = MOX::leaders[leader].location;
            OFFICER::Kill_Officer(leader);
            // A ship in a colony's production queue (4: a refit, 6: being built) cannot leave the slip: it stays, the
            // general dies (the queue refers to the slot; scrapping or handing it over corrupted the heap, 27.11.2026).
            const bool ours = ship >= 0 && ship < MAX_SHIPS && MOX::ship[ship].owner == player;
            if (ours && (MOX::ship[ship].status == 4 || MOX::ship[ship].status == 6)) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: the general %d stages a coup and dies, his ship %d stays in the shipyard",
                    (int)MOX::stardate, (int)player, (int)leader, (int)ship);
                return;
            }
            const bool own_ship = ours;
            const int16_t enemy = own_ship ? Enemy_(player) : static_cast<int16_t>(-1);
            if (enemy >= 0) {
                MOX::ship[ship].owner = static_cast<int8_t>(enemy);
                MOX::ship[ship].officer_index = -1;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: the general %d stages a coup, his ship %d goes over to player %d",
                    (int)MOX::stardate, (int)player, (int)leader, (int)ship, (int)enemy);
                return;
            }
            if (own_ship) SHIPS::Kill_Ship(ship);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: the general %d stages a coup, he and his ship are lost (no enemy to go over to)",
                (int)MOX::stardate, (int)player, (int)leader);
        }

        void Trial_(int16_t player, int16_t leader) {
            Add_Loyalty_(leader, -GENERAL_JAILED);
            hostages::Arrest(leader, false);  // the trial jails the general (ab-review 3.1)
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: the general %d is tried and jailed", (int)MOX::stardate,
                (int)player, (int)leader);
        }

        s_incident New_(e_kind kind, int16_t player, int32_t turns) {
            s_incident r;
            r.kind = kind;
            r.player = static_cast<int8_t>(player);
            r.started = MOX::stardate;
            r.ends = MOX::stardate + turns;
            return r;
        }

        // The subject of an event, picked before the option is applied: the colony, the leader or the other empire,
        // the sum (what a pending choice stores).
        struct s_subject {
            int16_t target = -1;
            int16_t other = -1;
            int32_t value = 0;
        };

        // The pick of each kind draws from game_random exactly as the event always did; `option` is the option about
        // to be applied, -1 while a human is yet to choose (the checks that depend on it wait for the apply).
        bool Pick_Painless_(int16_t player, s_subject* s) {
            int16_t colonies[MAX_COLONIES];
            const int32_t n = Colonies_(player, false, colonies);
            if (n == 0 || State()->count >= MAX_INCIDENTS) return false;
            s->target = colonies[Pick_(n)];
            return true;
        }

        void Apply_Painless_(int16_t player, const s_subject& s, int32_t option) {
            const int16_t c = s.target;
            if (option == PAINLESS_CURE && MOX::player[player].bc - PAINLESS_CURE_COST < PAINLESS_RESERVE) option = PAINLESS_QUARANTINE;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: colony %d: a disease without pain, option %d", (int)MOX::stardate,
                (int)player, (int)c, (int)option);
            if (option == PAINLESS_CURE) {
                Add_Bc_(player, -PAINLESS_CURE_COST);
                Log_(player, "the disease is cured");
                return;
            }
            s_incident r = New_(KIND_PAINLESS, player, PAINLESS_TURNS);
            r.target = c;
            r.option = static_cast<int8_t>(option);
            if (option == PAINLESS_QUARANTINE) {
                r.morale = PAINLESS_QUARANTINE_MORALE;
            } else {
                r.morale = PAINLESS_RECRUIT_MORALE;
                r.value = PAINLESS_LEVY_TURNS;
            }
            Record_(r);
            COLCALC::Colony_Calculation(c);
        }

        bool Pick_Drug_(int16_t player, s_subject* s) {
            return Pick_Painless_(player, s);  // the same pick: a colony that can take an event
        }

        void Apply_Drug_(int16_t player, const s_subject& s, int32_t option) {
            const int16_t c = s.target;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: colony %d: a drug of the natives, option %d", (int)MOX::stardate,
                (int)player, (int)c, (int)option);
            s_incident r = New_(KIND_NATIVE_DRUG, player, DRUG_TURNS);
            r.target = c;
            r.option = static_cast<int8_t>(option);
            const int16_t pops = MOX::colony[c].n_pops;
            if (option == DRUG_TOLERATE) {
                r.morale = DRUG_TOLERATE_MORALE;
                r.value = static_cast<int16_t>(-pops);
            } else if (option == DRUG_TAX) {
                r.morale = DRUG_TAX_MORALE;
                r.value = pops;
            } else {
                r.morale = DRUG_BAN_MORALE;
            }
            Record_(r);
            COLCALC::Colony_Calculation(c);
        }

        bool Pick_Treasury_(int16_t player, int32_t option, s_subject* s) {
            int16_t owners[MAX_PLAYERS];
            int32_t n = 0;
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o != player && Alive_(o) && MOX::player[player].contact[o] != 0) owners[n++] = o;
            }
            if (n == 0) {
                for (int16_t o = 0; o < Players_(); ++o) {
                    if (o != player && Alive_(o)) owners[n++] = o;
                }
            }
            if (n == 0 || (option == TREASURY_KEEP && State()->count >= MAX_INCIDENTS)) return false;
            s->other = owners[Pick_(n)];
            s->value = TREASURY_MIN + Pick_(TREASURY_MAX - TREASURY_MIN + 1);
            return true;
        }

        void Apply_Treasury_(int16_t player, const s_subject& s, int32_t option) {
            const int16_t owner = s.other;
            const int32_t amount = s.value;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: finds %d BC stolen from player %d, option %d", (int)MOX::stardate,
                (int)player, (int)amount, (int)owner, (int)option);
            if (option == TREASURY_RETURN) {
                Add_Bc_(owner, amount);
                opinion::Add(owner, player, opinion::REASON_DID_US_A_SERVICE, TREASURY_RETURN_OPINION);
                Log_(player, "returns the treasury in full");
            } else if (option == TREASURY_COMMISSION) {
                const int32_t commission = amount * TREASURY_COMMISSION_PERCENT / 100;
                Add_Bc_(player, commission);
                Add_Bc_(owner, amount - commission);
                opinion::Add(owner, player, opinion::REASON_DID_US_A_SERVICE, TREASURY_COMMISSION_OPINION);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: returns the treasury and keeps a commission of %d BC",
                    (int)MOX::stardate, (int)player, (int)commission);
            } else {
                Add_Bc_(player, amount);
                s_incident r = New_(KIND_TREASURY, player, TREASURY_SECRET_TURNS);
                r.other = owner;
                r.option = static_cast<int8_t>(option);
                r.value = static_cast<int16_t>(amount);
                Record_(r);
                Log_(player, "keeps the treasury hidden");
            }
        }

        bool Pick_Deposit_(int16_t player, s_subject* s) {
            int16_t colonies[MAX_COLONIES];
            const int32_t n = Colonies_(player, true, colonies);
            if (n == 0 || State()->count >= MAX_INCIDENTS) return false;
            s->target = colonies[Pick_(n)];
            return true;
        }

        void Apply_Deposit_(int16_t player, const s_subject& s, int32_t option) {
            const int16_t c = s.target;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: colony %d: a rare deposit under the colony, option %d",
                (int)MOX::stardate, (int)player, (int)c, (int)option);
            if (option == DEPOSIT_CAREFUL) {
                EVENTS::Enrich_Planet(MOX::colony[c].planet, 1);
                Log_(player, "the deposit is mined with care, the planet is a class richer");
                return;
            }
            s_incident r = New_(KIND_DEPOSIT, player, option == DEPOSIT_LEAVE ? DEPOSIT_TURNS : DEPOSIT_PLUNDER_TURNS);
            r.target = c;
            r.option = static_cast<int8_t>(option);
            if (option == DEPOSIT_LEAVE) {
                r.morale = DEPOSIT_LEAVE_MORALE;
            } else {
                r.morale = DEPOSIT_PLUNDER_MORALE;
                r.unrest = DEPOSIT_PLUNDER_UNREST;
                EVENTS::Enrich_Planet(MOX::colony[c].planet, 2);
                Log_(player, "the deposit is plundered, the planet is two classes richer and the people revolt");
            }
            Record_(r);
            COLCALC::Colony_Calculation(c);
        }

        bool Pick_General_(int16_t player, int32_t option, s_subject* s) {
            int16_t leaders[MAX_LEADERS];
            int32_t n = 0;
            for (int16_t l = 0; l < MAX_LEADERS; ++l) {
                if (Ship_Captain_(l, player)) leaders[n++] = l;
            }
            if (n == 0 || (option == GENERAL_IGNORE && State()->count >= MAX_INCIDENTS)) return false;
            s->other = leaders[Pick_(n)];
            return true;
        }

        void Apply_General_(int16_t player, const s_subject& s, int32_t option, const int32_t* dice_values) {
            const int16_t general = s.other;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: the general %d defies orders, option %d", (int)MOX::stardate,
                (int)player, (int)general, (int)option);
            if (option == GENERAL_REASON) {
                const int32_t loyal = loyalty::Of(general);
                const dice::s_modifier modifiers[1] = {{"loyalty", loyal == loyalty::UNTRACKED ? 0 : (loyal - 50) / 10}};
                const dice::s_check check = dice_values != nullptr
                    ? dice::Resolve_Check(static_cast<dice::e_roll_kind>(dice::roll_kind), dice_values, modifiers, 1, GENERAL_DIFFICULTY)
                    : dice::Roll_Check(modifiers, 1, GENERAL_DIFFICULTY);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: reasoning with the general: %s", (int)MOX::stardate,
                    (int)player, dice::Describe_Check("reason", check, modifiers, 1).c_str());
                if (check.outcome >= dice::OUTCOME_SUCCESS) {
                    Add_Loyalty_(general, GENERAL_LOYALTY_GAIN);
                    Log_(player, "the general is talked round");
                } else {
                    Coup_(player, general);
                }
            } else if (option == GENERAL_EXECUTE) {
                OFFICER::Kill_Officer(general);
                for (int16_t l = 0; l < MAX_LEADERS; ++l) {
                    if (MOX::leaders[l].player_index == player && l != general) Add_Loyalty_(l, -GENERAL_FEAR);
                }
                Log_(player, "the general is executed, the other leaders are afraid");
            } else {
                s_incident r = New_(KIND_REBEL_GENERAL, player, GENERAL_RIPEN_TURNS);
                r.other = general;
                r.option = static_cast<int8_t>(option);
                Record_(r);
                Log_(player, "the general's defiance is ignored");
            }
        }

        bool Pick_Governor_(int16_t player, int32_t option, s_subject* s) {
            int16_t leaders[MAX_LEADERS];
            int32_t n = 0;
            for (int16_t l = 0; l < MAX_LEADERS; ++l) {
                const int16_t c = Governed_Colony_(l, player);
                if (c >= 0 && !Colony_Busy_(c)) leaders[n++] = l;
            }
            if (n == 0 || (option == GOVERNOR_KEEP && State()->count >= MAX_INCIDENTS)) return false;
            s->other = leaders[Pick_(n)];
            s->target = Governed_Colony_(s->other, player);
            return true;
        }

        void Apply_Governor_(int16_t player, const s_subject& s, int32_t option) {
            const int16_t governor = s.other;
            const int16_t c = s.target;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: colony %d: the governor %d is mad, option %d", (int)MOX::stardate,
                (int)player, (int)c, (int)governor, (int)option);
            if (option == GOVERNOR_REMOVE) {
                OFFICER::Deassign_Officer(governor);
                Log_(player, "the mad governor is removed");
                return;
            }
            s_incident r = New_(KIND_MAD_GOVERNOR, player, GOVERNOR_BOOM_TURNS);
            r.target = c;
            r.other = governor;
            r.option = static_cast<int8_t>(option);
            r.value = static_cast<int16_t>(std::min<int32_t>(INT16_MAX, MOX::colony[c].n_pops * GOVERNOR_BOOM_BC));
            Record_(r);
            Log_(player, "the mad governor is kept, the colony booms");
        }

        bool Pick_Subject_(e_kind kind, int16_t player, int32_t option, s_subject* s) {
            switch (kind) {
                case KIND_PAINLESS: return Pick_Painless_(player, s);
                case KIND_NATIVE_DRUG: return Pick_Drug_(player, s);
                case KIND_TREASURY: return Pick_Treasury_(player, option, s);
                case KIND_DEPOSIT: return Pick_Deposit_(player, s);
                case KIND_REBEL_GENERAL: return Pick_General_(player, option, s);
                case KIND_MAD_GOVERNOR: return Pick_Governor_(player, option, s);
                default: return false;
            }
        }

        void Apply_Subject_(e_kind kind, int16_t player, const s_subject& s, int32_t option, const int32_t* dice_values) {
            switch (kind) {
                case KIND_PAINLESS: Apply_Painless_(player, s, option); break;
                case KIND_NATIVE_DRUG: Apply_Drug_(player, s, option); break;
                case KIND_TREASURY: Apply_Treasury_(player, s, option); break;
                case KIND_DEPOSIT: Apply_Deposit_(player, s, option); break;
                case KIND_REBEL_GENERAL: Apply_General_(player, s, option, dice_values); break;
                case KIND_MAD_GOVERNOR: Apply_Governor_(player, s, option); break;
                default: break;
            }
        }

        // A colony that can still take the event: ours, alive, without a record of its own.
        bool Colony_Free_(int16_t player, int16_t c) {
            return c >= 0 && c < MOX::NUM_COLONIES && c < MAX_COLONIES && MOX::colony[c].owner == player && MOX::colony[c].n_pops > 0 && !Colony_Busy_(c);
        }

        // A stored subject is still good: the empire lives, there is room for a record and the subject is as it was.
        bool Subject_Valid_(e_kind kind, int16_t player, const s_subject& s) {
            if (!Alive_(player) || State()->count >= MAX_INCIDENTS) return false;
            switch (kind) {
                case KIND_PAINLESS:
                case KIND_NATIVE_DRUG: return Colony_Free_(player, s.target);
                case KIND_DEPOSIT:
                    return Colony_Free_(player, s.target) && MOX::colony[s.target].planet >= 0 &&
                        MOX::planet[MOX::colony[s.target].planet].mineral_class < MINERAL_RESOURCE_ULTRA_RICH;
                case KIND_TREASURY: return s.other >= 0 && s.other != player && Alive_(s.other);
                case KIND_REBEL_GENERAL: return s.other >= 0 && s.other < MAX_LEADERS && Ship_Captain_(s.other, player);
                case KIND_MAD_GOVERNOR:
                    return Colony_Free_(player, s.target) && s.other >= 0 && s.other < MAX_LEADERS && Governed_Colony_(s.other, player) == s.target;
                default: return false;
            }
        }

        s_subject Subject_Of_(const choices::s_choice& c) {
            s_subject s;
            s.target = c.target;
            s.other = c.other;
            s.value = c.value;
            return s;
        }

        // The planet name of a colony, plain ASCII.
        void Colony_Name_(int16_t colony, char* out, size_t size) {
            out[0] = '\0';
            const int16_t planet = colony >= 0 ? MOX::colony[colony].planet : static_cast<int16_t>(-1);
            if (planet < 0 || planet >= MAX_PLANETS) {
                snprintf(out, size, "a colony");
                return;
            }
            char name[64];
            HACCESS::Do_Get_Planet_Name(planet, name, 0);
            snprintf(out, size, "%s", name);
        }

        void Set_Option_(choices::s_text* t, const char* format, ...) {
            va_list args;
            va_start(args, format);
            vsnprintf(t->option[t->count], sizeof(t->option[t->count]), format, args);
            va_end(args);
            ++t->count;
        }

        // One turn of a record; true when it is over.
        bool Run_(s_incident& r) {
            const int16_t player = r.player;
            if (r.kind == KIND_TREASURY) {
                if (MOX::stardate >= r.ends) return true;
                if (!Alive_(r.other) || !Alive_(player)) return true;
                if (BILL::Get_Their_Spy_Number(r.other, player) > 0 && static_cast<int32_t>(game_random::Random(100)) <= TREASURY_LEAK_PERCENT) {
                    opinion::Add(r.other, player, opinion::REASON_BAD_RUMOURS, TREASURY_LEAK_OPINION);
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: player %d learns of the hidden treasury through its spies",
                        (int)MOX::stardate, (int)player, (int)r.other);
                    return true;
                }
                return false;
            }
            if (r.kind == KIND_REBEL_GENERAL) {
                if (!Ship_Captain_(r.other, player)) {
                    Log_(player, "the defiant general is gone");
                    return true;
                }
                if (MOX::stardate < r.ends) return false;
                if (Pick_(2) == 0) {
                    Coup_(player, r.other);
                } else {
                    Trial_(player, r.other);
                }
                return true;
            }
            const bool colony_ok = r.target >= 0 && r.target < MOX::NUM_COLONIES && MOX::colony[r.target].owner == player && MOX::colony[r.target].n_pops > 0;
            if (!colony_ok) {
                Log_(player, "the incident ends, the colony is lost");
                return true;
            }
            const int16_t c = r.target;
            if (MOX::stardate >= r.ends) {
                if (r.kind == KIND_MAD_GOVERNOR && r.stage == 0) {
                    int16_t pops = 0;
                    int16_t buildings = 0;
                    EVENTS::Allocate_Earthquake_Hit(c, &pops, &buildings);
                    if (Governed_Colony_(r.other, player) == c) OFFICER::Deassign_Officer(r.other);
                    r.stage = 1;
                    r.morale = GOVERNOR_CRASH_MORALE;
                    r.ends = MOX::stardate + GOVERNOR_CRASH_TURNS;
                    COLCALC::Colony_Calculation(c);
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "incidents: stardate %d, player %d: colony %d collapses, %d population and %d buildings lost, the governor is removed",
                        (int)MOX::stardate, (int)player, (int)c, (int)pops, (int)buildings);
                    return false;
                }
                COLCALC::Colony_Calculation(c);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: incident %d at colony %d ends", (int)MOX::stardate, (int)player,
                    (int)r.kind, (int)c);
                return true;
            }
            switch (r.kind) {
                case KIND_NATIVE_DRUG:
                    if (r.option == DRUG_BAN) {
                        if (static_cast<int32_t>(game_random::Random(100)) <= DRUG_CONTRABAND_PERCENT) {
                            EVENTS::Pirate_Raid_Loss(player, DRUG_CONTRABAND_LOSS);
                            Log_(player, "smugglers of the banned drug rob the treasury");
                        }
                    } else {
                        Add_Bc_(player, r.value);
                    }
                    break;
                case KIND_DEPOSIT:
                    if (r.option == DEPOSIT_PLUNDER && static_cast<int32_t>(game_random::Random(100)) <= DEPOSIT_QUAKE_PERCENT) {
                        int16_t pops = 0;
                        int16_t buildings = 0;
                        EVENTS::Allocate_Earthquake_Hit(c, &pops, &buildings);
                        COLCALC::Colony_Calculation(c);
                        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: the plundered mine at colony %d quakes, %d population and %d buildings lost",
                            (int)MOX::stardate, (int)player, (int)c, (int)pops, (int)buildings);
                    }
                    break;
                case KIND_MAD_GOVERNOR:
                    if (r.stage == 0) {
                        if (Governed_Colony_(r.other, player) != c) {
                            Log_(player, "the mad governor is gone, the boom ends");
                            return true;
                        }
                        Add_Bc_(player, r.value);
                    }
                    break;
                default: break;
            }
            return false;
        }
    } // namespace

    int32_t Choose(e_kind kind, int16_t player) {
        if (!enabled || player < 0 || player >= Players_() || Human_(player)) return 0;
        int32_t option = 1;
        switch (MOX::player[player].personality) {
            case PLAYER_PERSONALITY_HONORABLE:
            case PLAYER_PERSONALITY_PACIFIST: option = 0; break;
            case PLAYER_PERSONALITY_RUTHLESS:
            case PLAYER_PERSONALITY_AGGRESSIVE: option = 2; break;
            default: break;
        }
        return kind == KIND_MAD_GOVERNOR ? std::min(option, 1) : option;
    }

    bool Start(e_kind kind, int16_t player, int32_t option, const int32_t* dice_values) {
        if (!enabled || !Alive_(player)) return false;
        const int32_t limit = kind == KIND_MAD_GOVERNOR ? 1 : 2;
        // A human with events human_choices on picks the subject now and chooses the option later (choices.h).
        const bool ask = option < 0 && choices::Ask_Human(player);
        int32_t chosen = option >= 0 ? std::min(option, limit) : (ask ? -1 : Choose(kind, player));
        s_subject subject;
        if (!Pick_Subject_(kind, player, chosen, &subject)) return false;
        if (ask) {
            choices::s_choice c;
            c.module = choices::MODULE_INCIDENTS;
            c.kind = kind;
            c.player = static_cast<int8_t>(player);
            c.target = subject.target;
            c.other = subject.other;
            c.value = subject.value;
            c.asked = MOX::stardate;
            if (choices::Add(c)) return true;
            chosen = Choose(kind, player);  // the queue is full: the first option, as without the setting
        }
        Apply_Subject_(kind, player, subject, chosen, dice_values);
        return true;
    }

    bool Choice_Text(const choices::s_choice& c, choices::s_text* t) {
        if (!enabled || c.kind < KIND_PAINLESS || c.kind > KIND_LAST) return false;
        const e_kind kind = static_cast<e_kind>(c.kind);
        const int16_t player = c.player;
        const s_subject s = Subject_Of_(c);
        if (!Subject_Valid_(kind, player, s)) return false;
        *t = choices::s_text{};
        char place[64];
        Colony_Name_(s.target, place, sizeof(place));
        switch (kind) {
            case KIND_PAINLESS: {
                snprintf(t->title, sizeof(t->title), "Disease without pain");
                snprintf(t->body, sizeof(t->body),
                    "A strange disease has spread through %s. Those who survive it feel no pain at all. Doctors can cure it, "
                    "the colony can be sealed off, or the survivors can be taken as fearless troops.",
                    place);
                if (MOX::player[player].bc - PAINLESS_CURE_COST < PAINLESS_RESERVE) {
                    Set_Option_(t, "Cure the sick (%d BC) - too dear, quarantine instead", (int)PAINLESS_CURE_COST);
                } else {
                    Set_Option_(t, "Cure the sick (%d BC)", (int)PAINLESS_CURE_COST);
                }
                Set_Option_(t, "Quarantine (morale %d for %d turns)", (int)PAINLESS_QUARANTINE_MORALE, (int)PAINLESS_TURNS);
                Set_Option_(t, "Recruit survivors (morale +%d, troops +%d, %d turns)", (int)PAINLESS_RECRUIT_MORALE, (int)PAINLESS_GROUND_BONUS,
                    (int)PAINLESS_TURNS);
                break;
            }
            case KIND_NATIVE_DRUG: {
                const int32_t pops = MOX::colony[s.target].n_pops;
                snprintf(t->title, sizeof(t->title), "Drug of the natives");
                snprintf(t->body, sizeof(t->body),
                    "The colonists of %s have taken up a drug of the natives. It calms the people, but the work suffers. "
                    "A ban would anger them and bring smugglers; a tax would fill the treasury.",
                    place);
                Set_Option_(t, "Tolerate it (morale +%d, costs %d BC a turn, %d turns)", (int)DRUG_TOLERATE_MORALE, (int)pops, (int)DRUG_TURNS);
                Set_Option_(t, "Ban it (morale %d, smugglers steal %d BC, %d turns)", (int)DRUG_BAN_MORALE, (int)DRUG_CONTRABAND_LOSS, (int)DRUG_TURNS);
                Set_Option_(t, "Tax it (morale +%d, earns %d BC a turn, %d turns)", (int)DRUG_TAX_MORALE, (int)pops, (int)DRUG_TURNS);
                break;
            }
            case KIND_TREASURY: {
                snprintf(t->title, sizeof(t->title), "A found treasury");
                snprintf(t->body, sizeof(t->body),
                    "Your crews found %d BC of stolen money on a wreck. It belongs to the %.14s, who do not know where it is. "
                    "It can be returned, returned for a commission, or kept.",
                    (int)s.value, MOX::player[s.other].race_name);
                Set_Option_(t, "Return it all (%.14s remember it kindly)", MOX::player[s.other].race_name);
                Set_Option_(t, "Return it, keep %d%% (%d BC)", (int)TREASURY_COMMISSION_PERCENT, (int)(s.value * TREASURY_COMMISSION_PERCENT / 100));
                Set_Option_(t, "Keep it all (+%d BC, they may find out)", (int)s.value);
                break;
            }
            case KIND_DEPOSIT: {
                snprintf(t->title, sizeof(t->title), "A rich deposit");
                snprintf(t->body, sizeof(t->body),
                    "Miners at %s struck a rare deposit under the colony. It can be left alone, mined with care, or plundered at once.", place);
                Set_Option_(t, "Leave it alone (morale +%d for %d turns)", (int)DEPOSIT_LEAVE_MORALE, (int)DEPOSIT_TURNS);
                Set_Option_(t, "Mine with care (planet 1 mineral class richer)");
                Set_Option_(t, "Plunder it (2 classes richer, unrest and quakes)");
                break;
            }
            case KIND_REBEL_GENERAL: {
                snprintf(t->title, sizeof(t->title), "A rebel general");
                snprintf(t->body, sizeof(t->body),
                    "Captain %.14s defies orders and his crew follows him. He can be talked round, ignored in the hope that it passes, or executed.",
                    MOX::leaders[s.other].name);
                Set_Option_(t, "Reason with him (a dice check; failure is a coup)");
                Set_Option_(t, "Ignore him (in %d turns a coup or a trial)", (int)GENERAL_RIPEN_TURNS);
                Set_Option_(t, "Execute him (other leaders lose %d loyalty)", (int)GENERAL_FEAR);
                break;
            }
            case KIND_MAD_GOVERNOR: {
                snprintf(t->title, sizeof(t->title), "A mad governor");
                snprintf(t->body, sizeof(t->body),
                    "The governor %.14s of %s doses the water of the colony with something strange. Output soars, but he is plainly mad "
                    "and the colony will pay for it.",
                    MOX::leaders[s.other].name, place);
                Set_Option_(t, "Remove him at once");
                Set_Option_(t, "Keep him (+%d BC a turn for %d turns, then a collapse)", (int)(MOX::colony[s.target].n_pops * GOVERNOR_BOOM_BC),
                    (int)GOVERNOR_BOOM_TURNS);
                break;
            }
            default: return false;
        }
        return true;
    }

    void Choice_Apply(const choices::s_choice& c, int32_t option) {
        if (!enabled || c.kind < KIND_PAINLESS || c.kind > KIND_LAST) return;
        const e_kind kind = static_cast<e_kind>(c.kind);
        const s_subject s = Subject_Of_(c);
        if (!Subject_Valid_(kind, c.player, s)) return;
        Apply_Subject_(kind, c.player, s, std::clamp<int32_t>(option, 0, kind == KIND_MAD_GOVERNOR ? 1 : 2), nullptr);
    }

    void Run_Records() {
        if (!enabled) return;
        s_state* s = State();
        for (int32_t i = 0; i < s->count;) {
            if (Run_(s->incident[i])) {
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

} // namespace incidents
