#include "pch.h"

#include "game/hostages.h"

#include "game/bill.h"
#include "game/colcalc.h"
#include "game/officer.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/spy_ops.h"

#include <algorithm>

// The behaviour of hostages and captured leaders (mechanics.md 3.5.3, events-catalog 3.12); the held
// leaders and the save section are in hostages_state.cpp. game_random is drawn only with diplomacy
// prisoners on and a prisoner held (the escape roll), once a turn per prisoner; with diplomacy spy_prison on
// and spies in prison, once a turn per (jailer, owner) pair; and once for a returned hostage with the
// bribery operation on (the mole roll).
//
// Wave 6 design rows (docs/LocalLLM/):
// - mechanics.md 3.5.3, "Заложники и воспитанники": "лидер в залог договора; при нарушении — решается его
//   судьба; вернувшийся может быть верен бывшему хозяину". ab-review 2.3: "воспитанник, вернувшийся домой,
//   с шансом становится «кротом» хозяина (подкупленный лидер spy_ops)" -> Mole_With_Roll.
// - events-catalog.md 3.12, "Пленные лидеры и выкуп": "пойманные шпионы и лидеры сидят в чужой тюрьме:
//   выкуп, обмен, побег". ab-review 2.2: "пойманный шпион сидит, если судьба шпиона не решила иначе:
//   выкуп, обмен, побег" -> Imprison_Spy and the spy cells.
// - ab-review 3.1: "сделать настоящую тюрьму (как у заложников: лидер держится вне службы N ходов, потом
//   возвращается или казнён)" for the arrests of mechanics.md 3.2.3 (подстава, раскрытый подкуп) and 3.5.5
//   (охота на крота, суд над генералом) -> Arrest / Jail, KIND_JAIL and KIND_TRAITOR.
namespace hostages {

    namespace {
        constexpr int8_t LEADER_DEAD = -2;
        constexpr int8_t LEADER_POOL = 0;
        constexpr int8_t LEADER_ASSIGNED = 1;

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        bool Computer_(int16_t player) {
            return Alive_(player) && MOX::player[player].objectives != PLAYER_OBJECTIVE_HUMAN;
        }

        int32_t Strength_(int16_t of, int16_t against) {
            return MOX::player[of].total_ship_strength[against];
        }

        bool Kind_On_(uint8_t kind) {
            if (kind >= KIND_JAIL) return true;  // a jail record exists only because an arrest site's own flag was on
            return kind == KIND_HOSTAGE ? hostage_swaps : prisoners;
        }

        const char* Kind_Name_(uint8_t kind) {
            switch (kind) {
                case KIND_HOSTAGE: return "hostage";
                case KIND_PRISONER: return "prisoner";
                case KIND_TRAITOR: return "jailed traitor";
                default: return "jailed leader";
            }
        }

        // A hostage goes home; it may now serve its former holder (the roll is drawn only with bribery on).
        void Hostage_Returns_(int32_t index, int16_t owner, const char* how) {
            const s_held h = State()->held[index];
            Release(index, owner, how);
            if (h.kind == KIND_HOSTAGE && spy_ops::Enabled(spy_ops::OP_BRIBERY)) {
                Mole_With_Roll(h.leader, h.holder, static_cast<int32_t>(game_random::Random(100)));
            }
        }

        void Remove_Spy_Cell_(int32_t cell) {
            s_state* s = State();
            for (int32_t i = cell; i + 1 < s->spy_count; ++i) s->spies[i] = s->spies[i + 1];
            s->spies[--s->spy_count] = s_spies{};
        }

        // One spy of the cell leaves the prison: for its home, or (executed) for good.
        void Free_Spy_(int32_t cell, const char* how, bool goes_home) {
            s_state* s = State();
            const s_spies c = s->spies[cell];
            if (--s->spies[cell].count == 0) {
                Remove_Spy_Cell_(cell);
            } else {
                s->spies[cell].since = MOX::stardate;
            }
            if (goes_home) {
                const int32_t home = BILL::Get_Their_Spy_Number(c.owner, c.owner);
                BILL::Set_Their_Spy_Number(c.owner, c.owner, static_cast<int16_t>(std::min(HOME_SPY_LIMIT, home + 1)));
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, a spy of player %d held by player %d: %s",
                (int)MOX::stardate, (int)c.owner, (int)c.jailer, how);
        }

        void Spy_Prison_Turn_() {
            s_state* s = State();
            for (int32_t i = 0; i < s->spy_count;) {
                const s_spies c = s->spies[i];
                if (!Alive_(c.jailer) || !Alive_(c.owner)) {
                    Remove_Spy_Cell_(i);  // a gone empire's prison empties
                    continue;
                }
                if (Spy_Escape_With_Roll(i, static_cast<int32_t>(game_random::Random(100)))) continue;
                if (MOX::stardate - c.since >= SPY_PRISON_TURNS && Computer_(c.jailer)) {
                    if (MOX::player[c.jailer].personality == PLAYER_PERSONALITY_RUTHLESS) {
                        opinion::Add(c.owner, c.jailer, opinion::REASON_TREATED_OUR_SPY, EXECUTED_SPY_OPINION);
                        Free_Spy_(i, "executed, held too long", false);
                    } else {
                        Free_Spy_(i, "released, held too long", true);
                    }
                    continue;
                }
                ++i;
            }
        }

        // On the phase: one spy for one spy between two computers that hold each other's spies; then a computer
        // owner that keeps the reserve pays SPY_RANSOM for one spy a pair.
        void Spy_Exchange_And_Ransom_() {
            s_state* s = State();
            for (int32_t i = 0; i < s->spy_count; ++i) {
                const s_spies a = s->spies[i];
                if (!Computer_(a.owner) || !Computer_(a.jailer)) continue;
                for (int32_t j = i + 1; j < s->spy_count; ++j) {
                    const s_spies b = s->spies[j];
                    if (b.owner != a.jailer || b.jailer != a.owner) continue;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, players %d and %d exchange a spy each",
                        (int)MOX::stardate, (int)a.owner, (int)a.jailer);
                    Free_Spy_(j, "exchanged", true);  // the higher index first: the cells move up
                    Free_Spy_(i, "exchanged", true);
                    i = -1;  // start over: the cells moved
                    break;
                }
            }
            for (int32_t i = 0; i < s->spy_count;) {
                const s_spies c = s->spies[i];
                if (!Computer_(c.owner) || !Alive_(c.jailer) || MOX::player[c.owner].bc - SPY_RANSOM < RANSOM_RESERVE) {
                    ++i;
                    continue;
                }
                MOX::player[c.owner].bc -= SPY_RANSOM;
                MOX::player[c.jailer].bc += SPY_RANSOM;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, player %d pays %d BC to player %d for a spy",
                    (int)MOX::stardate, (int)c.owner, (int)SPY_RANSOM, (int)c.jailer);
                const int32_t before = s->spy_count;
                Free_Spy_(i, "ransomed", true);
                if (s->spy_count == before) ++i;  // the cell still holds spies: one a period
            }
        }

        // The holder's mind about a hostage whose giver broke a treaty: 0 execute, 1 send home, 2 keep as a ward.
        int Fate_(int16_t holder) {
            switch (MOX::player[holder].personality) {
                case PLAYER_PERSONALITY_RUTHLESS:
                case PLAYER_PERSONALITY_AGGRESSIVE:
                    return 0;
                case PLAYER_PERSONALITY_HONORABLE:
                case PLAYER_PERSONALITY_PACIFIST:
                    return 1;
                default:
                    return 2;
            }
        }

        void Remove_Record_(int32_t index) {
            s_state* s = State();
            for (int32_t i = index; i + 1 < s->count; ++i) s->held[i] = s->held[i + 1];
            s->held[--s->count] = s_held{};
        }

        void Exchange_And_Ransom_() {
            s_state* s = State();
            // Exchanges: one for one between two computers that hold each other's leaders.
            for (int32_t i = 0; i < s->count; ++i) {
                const s_held a = s->held[i];
                if (a.kind != KIND_PRISONER || !Computer_(a.owner) || !Computer_(a.holder)) continue;
                for (int32_t j = i + 1; j < s->count; ++j) {
                    const s_held b = s->held[j];
                    if (b.kind != KIND_PRISONER || b.owner != a.holder || b.holder != a.owner) continue;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, players %d and %d exchange leaders %d and %d",
                        (int)MOX::stardate, (int)a.owner, (int)a.holder, (int)a.leader, (int)b.leader);
                    Release(j, b.owner, "exchanged");  // the higher index first: the records move up
                    Release(i, a.owner, "exchanged");
                    --i;  // the record at i is a new one now
                    break;
                }
            }
            // Ransom: a computer owner that keeps a reserve buys its leader back.
            for (int32_t i = 0; i < s->count;) {
                const s_held h = s->held[i];
                const int32_t ransom = Ransom_Of(h.leader);
                if (h.kind != KIND_PRISONER || !Computer_(h.owner) || !Alive_(h.holder) ||
                    MOX::player[h.owner].bc - ransom < RANSOM_RESERVE) {
                    ++i;
                    continue;
                }
                MOX::player[h.owner].bc -= ransom;
                MOX::player[h.holder].bc += ransom;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, player %d pays %d BC to player %d for leader %d",
                    (int)MOX::stardate, (int)h.owner, (int)ransom, (int)h.holder, (int)h.leader);
                Release(i, h.owner, "ransomed");
            }
        }
    } // namespace

    bool Hold(int16_t leader, int16_t holder, uint8_t kind) {
        if (!Kind_On_(kind) || leader < 0 || leader >= MAX_LEADERS || !Alive_(holder)) return false;
        s_state* s = State();
        if (s->count >= MAX_HELD || Find(leader) >= 0) return false;
        s_leader_data& l = MOX::leaders[leader];
        const int16_t owner = l.player_index;
        if (owner < 0 || owner >= MAX_PLAYERS || (owner == holder) != (kind >= KIND_JAIL)) return false;
        if (l.status != LEADER_POOL && l.status != LEADER_ASSIGNED) return false;
        OFFICER::Kill_Officer(leader);  // out of service: post cleared, colonies recalculated, status -2
        s->held[s->count++] = {static_cast<int8_t>(leader), static_cast<int8_t>(owner), static_cast<int8_t>(holder), kind, MOX::stardate};
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, player %d holds leader %d of player %d as a %s",
            (int)MOX::stardate, (int)holder, (int)leader, (int)owner, Kind_Name_(kind));
        return true;
    }

    bool Jail(int16_t leader, bool traitor) {
        if (leader < 0 || leader >= MAX_LEADERS) return false;
        const int16_t owner = MOX::leaders[leader].player_index;
        return Hold(leader, owner, traitor ? KIND_TRAITOR : KIND_JAIL);
    }

    void Arrest(int16_t leader, bool traitor) {
        if (!Jail(leader, traitor)) OFFICER::Deassign_Officer(leader);
    }

    bool Mole_With_Roll(int16_t leader, int16_t holder, int32_t roll) {
        if (!spy_ops::Enabled(spy_ops::OP_BRIBERY) || roll >= MOLE_PERCENT || leader < 0 || leader >= MAX_LEADERS) return false;
        const s_leader_data& l = MOX::leaders[leader];
        if (!Alive_(holder) || l.player_index == holder || l.player_index < 0 || (l.status != LEADER_POOL && l.status != LEADER_ASSIGNED)) return false;
        *spy_ops::Bribed_Slot(leader) = static_cast<int8_t>(holder);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, leader %d of player %d comes home a mole of player %d",
            (int)MOX::stardate, (int)leader, (int)l.player_index, (int)holder);
        return true;
    }

    bool Imprison_Spy(int16_t jailer, int16_t owner) {
        if (!spy_prison || !Alive_(jailer) || !Alive_(owner) || jailer == owner) return false;
        s_state* s = State();
        for (int32_t i = 0; i < s->spy_count; ++i) {
            if (s->spies[i].jailer != jailer || s->spies[i].owner != owner) continue;
            if (s->spies[i].count >= MAX_SPIES_PER_CELL) return false;
            ++s->spies[i].count;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, player %d imprisons a spy of player %d (%d held)",
                (int)MOX::stardate, (int)jailer, (int)owner, (int)s->spies[i].count);
            return true;
        }
        if (s->spy_count >= MAX_SPY_CELLS) return false;
        s->spies[s->spy_count++] = {static_cast<int8_t>(jailer), static_cast<int8_t>(owner), 1, MOX::stardate};
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, player %d imprisons a spy of player %d (1 held)",
            (int)MOX::stardate, (int)jailer, (int)owner);
        return true;
    }

    bool Spy_Escape_With_Roll(int32_t cell, int32_t roll) {
        if (!spy_prison) return false;
        const s_state* s = State();
        if (cell < 0 || cell >= s->spy_count || roll >= SPY_ESCAPE_PERCENT) return false;
        Free_Spy_(cell, "escaped", true);
        return true;
    }

    void Release(int32_t index, int16_t to, const char* how) {
        s_state* s = State();
        if (index < 0 || index >= s->count) return;
        const s_held h = s->held[index];
        Remove_Record_(index);
        s_leader_data& l = MOX::leaders[h.leader];
        l.status = LEADER_POOL;
        l.location = -1;
        l.eta = 0;
        l.player_index = static_cast<int8_t>(to);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, leader %d of player %d (held by player %d) goes to player %d: %s",
            (int)MOX::stardate, (int)h.leader, (int)h.owner, (int)h.holder, (int)to, how != nullptr ? how : "");
    }

    void Execute(int32_t index, const char* why) {
        s_state* s = State();
        if (index < 0 || index >= s->count) return;
        const s_held h = s->held[index];
        Remove_Record_(index);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, player %d executes leader %d of player %d: %s",
            (int)MOX::stardate, (int)h.holder, (int)h.leader, (int)h.owner, why != nullptr ? why : "");
    }

    int16_t Pick_Hostage(int16_t giver) {
        if (giver < 0 || giver >= MAX_PLAYERS) return -1;
        int16_t capital_governor = -1;
        const int16_t home = MOX::player[giver].home_planet_id;
        if (home >= 0 && home < MAX_PLANETS) {
            const int16_t star = MOX::planet[home].star_index;
            if (star >= 0 && star < MAX_STARS) capital_governor = MOX::star[star].officer_index[giver];
        }
        int16_t best = -1;
        for (int16_t i = 0; i < MAX_LEADERS; ++i) {
            const s_leader_data& l = MOX::leaders[i];
            if (l.player_index != giver || (l.status != LEADER_POOL && l.status != LEADER_ASSIGNED) || i == capital_governor || Find(i) >= 0) continue;
            if (best < 0 || l.level < MOX::leaders[best].level) best = i;
        }
        return best;
    }

    void On_Alliance(int16_t a, int16_t b) {
        if (!hostage_swaps || a == b || !Computer_(a) || !Computer_(b)) return;
        const int16_t giver = Strength_(a, b) <= Strength_(b, a) ? a : b;
        const int16_t holder = giver == a ? b : a;
        const s_state* s = State();
        for (int32_t i = 0; i < s->count; ++i) {
            if (s->held[i].kind == KIND_HOSTAGE && s->held[i].owner == giver && s->held[i].holder == holder) return;  // one is already held
        }
        const int16_t leader = Pick_Hostage(giver);
        if (leader >= 0) Hold(leader, holder, KIND_HOSTAGE);
    }

    void On_Treaty_Broken(int16_t breaker, int16_t victim) {
        if (!hostage_swaps || !Computer_(victim)) return;
        s_state* s = State();
        for (int32_t i = 0; i < s->count;) {
            const s_held h = s->held[i];
            if (h.kind != KIND_HOSTAGE || h.owner != breaker || h.holder != victim) {
                ++i;
                continue;
            }
            switch (Fate_(victim)) {
                case 0:
                    opinion::Add(breaker, victim, opinion::REASON_HARMED_A_HOSTAGE, EXECUTED_HOSTAGE_OPINION);
                    Execute(i, "the giver broke a treaty");
                    break;
                case 1:
                    Hostage_Returns_(i, breaker, "the holder sends the hostage home");
                    break;
                default:
                    Release(i, victim, "the holder keeps the hostage as a ward");
                    break;
            }
        }
    }

    bool On_Colony_Invaded(const s_colony* colony, int16_t new_owner) {
        if (!prisoners || colony == nullptr) return false;
        const int16_t owner = colony->owner;
        if (owner == new_owner || owner < 0 || owner >= MAX_PLAYERS || !Alive_(new_owner)) return false;
        const int16_t star = MOX::planet[colony->planet].star_index;
        // The governor is lost only with the owner's last colony at the star (as BOMB::Handle_Leader_At_Lost_Colony).
        for (int32_t slot = 0; slot < 5; ++slot) {
            const int16_t p = MOX::star[star].planet_index[slot];
            if (p == -1) continue;
            const int16_t c = MOX::planet[p].colony_index;
            if (c != -1 && &MOX::colony[c] != colony && MOX::colony[c].owner == owner) return false;
        }
        const int leader = COLCALC::Colony_Officer(const_cast<s_colony*>(colony));
        if (leader < 0) return false;
        return Hold(static_cast<int16_t>(leader), new_owner, KIND_PRISONER);
    }

    int32_t Ransom_Of(int16_t leader) {
        if (leader < 0 || leader >= MAX_LEADERS) return RANSOM_BASE;
        return RANSOM_BASE + RANSOM_PER_LEVEL * static_cast<int32_t>(MOX::leaders[leader].level);
    }

    bool Escape_With_Roll(int32_t index, int32_t roll) {
        if (!prisoners) return false;
        const s_state* s = State();
        if (index < 0 || index >= s->count || s->held[index].kind != KIND_PRISONER || roll >= ESCAPE_PERCENT) return false;
        Release(index, s->held[index].owner, "escaped");
        return true;
    }

    void Turn() {
        s_state* s = State();
        for (int32_t i = 0; i < s->count;) {
            const s_held h = s->held[i];
            if (!Kind_On_(h.kind)) {
                ++i;
                continue;
            }
            if (!Alive_(h.owner)) {
                if (Alive_(h.holder)) {
                    Release(i, h.holder, "its owner is gone");
                } else {
                    Execute(i, "both empires are gone");
                }
                continue;
            }
            if (!Alive_(h.holder)) {
                Release(i, h.owner, "its holder is gone");
                continue;
            }
            if (h.kind >= KIND_JAIL) {
                if (MOX::stardate - h.since < JAIL_TURNS) {
                    ++i;
                } else if (h.kind == KIND_TRAITOR && Computer_(h.owner) && MOX::player[h.owner].personality == PLAYER_PERSONALITY_RUTHLESS) {
                    Execute(i, "a ruthless empire executes its traitor");
                } else {
                    Release(i, h.owner, "the jail term is over");
                }
                continue;
            }
            if (h.kind == KIND_HOSTAGE) {
                if (MOX::stardate - h.since >= HOSTAGE_TURNS) {
                    opinion::Add(h.owner, h.holder, opinion::REASON_FAMILY_TIES, RETURNED_OPINION);
                    Hostage_Returns_(i, h.owner, "the term is over");
                    continue;
                }
                ++i;
                continue;
            }
            if (Escape_With_Roll(i, static_cast<int32_t>(game_random::Random(100)))) continue;
            if (MOX::stardate - h.since >= PRISONER_TURNS && Computer_(h.holder)) {
                if (MOX::player[h.holder].personality == PLAYER_PERSONALITY_RUTHLESS) {
                    opinion::Add(h.owner, h.holder, opinion::REASON_HARMED_A_HOSTAGE, EXECUTED_PRISONER_OPINION);
                    Execute(i, "held too long");
                } else {
                    Release(i, h.owner, "held too long, released");
                }
                continue;
            }
            ++i;
        }
        if (prisoners && MOX::stardate % PERIOD == PHASE) Exchange_And_Ransom_();
        if (spy_prison) {
            Spy_Prison_Turn_();
            if (MOX::stardate % PERIOD == PHASE) Spy_Exchange_And_Ransom_();
        }
    }

} // namespace hostages
