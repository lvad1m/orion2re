#include "pch.h"

#include "game/incidents.h"

#include "game/bill.h"
#include "game/colcalc.h"
#include "game/dice.h"
#include "game/events.h"
#include "game/hostages.h"
#include "game/loyalty.h"
#include "game/officer.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/ships.h"

#include <algorithm>
#include <climits>

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

        bool Start_Painless_(int16_t player, int32_t option) {
            int16_t colonies[MAX_COLONIES];
            const int32_t n = Colonies_(player, false, colonies);
            if (n == 0 || State()->count >= MAX_INCIDENTS) return false;
            const int16_t c = colonies[Pick_(n)];
            if (option == PAINLESS_CURE && MOX::player[player].bc - PAINLESS_CURE_COST < PAINLESS_RESERVE) option = PAINLESS_QUARANTINE;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: colony %d: a disease without pain, option %d", (int)MOX::stardate,
                (int)player, (int)c, (int)option);
            if (option == PAINLESS_CURE) {
                Add_Bc_(player, -PAINLESS_CURE_COST);
                Log_(player, "the disease is cured");
                return true;
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
            return true;
        }

        bool Start_Drug_(int16_t player, int32_t option) {
            int16_t colonies[MAX_COLONIES];
            const int32_t n = Colonies_(player, false, colonies);
            if (n == 0 || State()->count >= MAX_INCIDENTS) return false;
            const int16_t c = colonies[Pick_(n)];
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
            return true;
        }

        bool Start_Treasury_(int16_t player, int32_t option) {
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
            const int16_t owner = owners[Pick_(n)];
            const int32_t amount = TREASURY_MIN + Pick_(TREASURY_MAX - TREASURY_MIN + 1);
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
            return true;
        }

        bool Start_Deposit_(int16_t player, int32_t option) {
            int16_t colonies[MAX_COLONIES];
            const int32_t n = Colonies_(player, true, colonies);
            if (n == 0 || State()->count >= MAX_INCIDENTS) return false;
            const int16_t c = colonies[Pick_(n)];
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: colony %d: a rare deposit under the colony, option %d",
                (int)MOX::stardate, (int)player, (int)c, (int)option);
            if (option == DEPOSIT_CAREFUL) {
                EVENTS::Enrich_Planet(MOX::colony[c].planet, 1);
                Log_(player, "the deposit is mined with care, the planet is a class richer");
                return true;
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
            return true;
        }

        bool Start_General_(int16_t player, int32_t option, const int32_t* dice_values) {
            int16_t leaders[MAX_LEADERS];
            int32_t n = 0;
            for (int16_t l = 0; l < MAX_LEADERS; ++l) {
                if (Ship_Captain_(l, player)) leaders[n++] = l;
            }
            if (n == 0 || (option == GENERAL_IGNORE && State()->count >= MAX_INCIDENTS)) return false;
            const int16_t general = leaders[Pick_(n)];
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
            return true;
        }

        bool Start_Governor_(int16_t player, int32_t option) {
            int16_t leaders[MAX_LEADERS];
            int32_t n = 0;
            for (int16_t l = 0; l < MAX_LEADERS; ++l) {
                const int16_t c = Governed_Colony_(l, player);
                if (c >= 0 && !Colony_Busy_(c)) leaders[n++] = l;
            }
            if (n == 0 || (option == GOVERNOR_KEEP && State()->count >= MAX_INCIDENTS)) return false;
            const int16_t governor = leaders[Pick_(n)];
            const int16_t c = Governed_Colony_(governor, player);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "incidents: stardate %d, player %d: colony %d: the governor %d is mad, option %d", (int)MOX::stardate,
                (int)player, (int)c, (int)governor, (int)option);
            if (option == GOVERNOR_REMOVE) {
                OFFICER::Deassign_Officer(governor);
                Log_(player, "the mad governor is removed");
                return true;
            }
            s_incident r = New_(KIND_MAD_GOVERNOR, player, GOVERNOR_BOOM_TURNS);
            r.target = c;
            r.other = governor;
            r.option = static_cast<int8_t>(option);
            r.value = static_cast<int16_t>(std::min<int32_t>(INT16_MAX, MOX::colony[c].n_pops * GOVERNOR_BOOM_BC));
            Record_(r);
            Log_(player, "the mad governor is kept, the colony booms");
            return true;
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
        const int32_t chosen = option >= 0 ? std::min(option, limit) : Choose(kind, player);
        switch (kind) {
            case KIND_PAINLESS: return Start_Painless_(player, chosen);
            case KIND_NATIVE_DRUG: return Start_Drug_(player, chosen);
            case KIND_TREASURY: return Start_Treasury_(player, chosen);
            case KIND_DEPOSIT: return Start_Deposit_(player, chosen);
            case KIND_REBEL_GENERAL: return Start_General_(player, chosen, dice_values);
            case KIND_MAD_GOVERNOR: return Start_Governor_(player, chosen);
            default: return false;
        }
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
