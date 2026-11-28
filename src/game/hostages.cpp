#include "pch.h"

#include "game/hostages.h"

#include "game/bill.h"
#include "game/choices.h"
#include "game/colcalc.h"
#include "game/officer.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/reputation.h"
#include "game/spy_ops.h"
#include "game/texts.h"

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

        // The record of a prisoner that `index`'s holder could be exchanged for: one the owner holds of the holder's.
        int32_t Exchange_Partner_(int32_t index) {
            const s_state* s = State();
            const s_held a = s->held[index];
            for (int32_t j = 0; j < s->count; ++j) {
                const s_held& b = s->held[j];
                if (j != index && b.kind == KIND_PRISONER && b.owner == a.holder && b.holder == a.owner) return j;
            }
            return -1;
        }

        // One for one: both leaders go home, the higher index first (the records move up).
        void Exchange_Leaders_(int32_t i, int32_t j) {
            const int32_t lo = std::min(i, j);
            const int32_t hi = std::max(i, j);
            const int16_t hi_owner = State()->held[hi].owner;
            const int16_t lo_owner = State()->held[lo].owner;
            Release(hi, hi_owner, "exchanged");
            Release(lo, lo_owner, "exchanged");
        }

        // The owner buys its prisoner back (the caller checked the sum).
        void Pay_Ransom_(int32_t index) {
            const s_held h = State()->held[index];
            const int32_t ransom = Ransom_Of(h.leader);
            MOX::player[h.owner].bc -= ransom;
            MOX::player[h.holder].bc += ransom;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, player %d pays %d BC to player %d for leader %d",
                (int)MOX::stardate, (int)h.owner, (int)ransom, (int)h.holder, (int)h.leader);
            Release(index, h.owner, "ransomed");
        }

        int32_t Spy_Cell_(int16_t jailer, int16_t owner) {
            const s_state* s = State();
            for (int32_t i = 0; i < s->spy_count; ++i) {
                if (s->spies[i].jailer == jailer && s->spies[i].owner == owner) return i;
            }
            return -1;
        }

        void Exchange_Spies_(int32_t i, int32_t j) {
            Free_Spy_(std::max(i, j), "exchanged", true);  // the higher cell first: the cells move up
            Free_Spy_(std::min(i, j), "exchanged", true);
        }

        void Pay_Spy_Ransom_(int32_t cell) {
            const s_spies c = State()->spies[cell];
            MOX::player[c.owner].bc -= SPY_RANSOM;
            MOX::player[c.jailer].bc += SPY_RANSOM;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, player %d pays %d BC to player %d for a spy",
                (int)MOX::stardate, (int)c.owner, (int)SPY_RANSOM, (int)c.jailer);
            Free_Spy_(cell, "ransomed", true);
        }

        // Queues a window of the human (one of a kind for the same subject at a time; a full queue: nothing).
        void Queue_Choice_(uint8_t kind, int16_t player, int16_t target, int16_t other, int32_t value) {
            const choices::s_state* s = choices::State();
            for (int32_t i = 0; i < s->count; ++i) {
                const choices::s_choice& c = s->choice[i];
                if (c.module == choices::MODULE_HOSTAGES && c.kind == kind && c.player == player && c.target == target && c.other == other) return;
            }
            choices::s_choice c;
            c.module = choices::MODULE_HOSTAGES;
            c.kind = kind;
            c.player = static_cast<int8_t>(player);
            c.target = target;
            c.other = other;
            c.value = value;
            c.asked = MOX::stardate;
            choices::Add(c);
        }

        // A human owner of prisoner `index`: asked when the ransom is affordable or an exchange is possible.
        void Ask_Leader_Owner_(int32_t index) {
            const s_held h = State()->held[index];
            const int32_t ransom = Ransom_Of(h.leader);
            const bool exchange = Computer_(h.holder) && Exchange_Partner_(index) >= 0;
            if (MOX::player[h.owner].bc < ransom && !exchange) return;
            Queue_Choice_(CHOICE_LEADER_OWNER, h.owner, h.leader, h.holder, ransom);
        }

        void Ask_Leader_Holder_(int32_t index) {
            const s_held h = State()->held[index];
            Queue_Choice_(CHOICE_LEADER_HOLDER, h.holder, h.leader, h.owner, Ransom_Of(h.leader));
        }

        void Ask_Spy_Owner_(const s_spies& c) {
            const bool exchange = Computer_(c.jailer) && Spy_Cell_(c.owner, c.jailer) >= 0;
            if (MOX::player[c.owner].bc < SPY_RANSOM && !exchange) return;
            Queue_Choice_(CHOICE_SPY_OWNER, c.owner, -1, c.jailer, SPY_RANSOM);
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
                if (MOX::stardate - c.since >= SPY_PRISON_TURNS && MOX::stardate % PERIOD == PHASE && choices::Ask_Human(c.jailer)) {
                    Queue_Choice_(CHOICE_SPY_HOLDER, c.jailer, -1, c.owner, c.count);
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
                    Exchange_Spies_(i, j);
                    i = -1;  // start over: the cells moved
                    break;
                }
            }
            for (int32_t i = 0; i < s->spy_count;) {
                const s_spies c = s->spies[i];
                if (!Alive_(c.jailer)) {
                    ++i;
                    continue;
                }
                if (!Computer_(c.owner)) {
                    if (choices::Ask_Human(c.owner)) Ask_Spy_Owner_(c);
                    ++i;
                    continue;
                }
                if (MOX::player[c.owner].bc - SPY_RANSOM < RANSOM_RESERVE) {
                    ++i;
                    continue;
                }
                const int32_t before = s->spy_count;
                Pay_Spy_Ransom_(i);
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
                    Exchange_Leaders_(i, j);  // the higher index first: the records move up
                    --i;  // the record at i is a new one now
                    break;
                }
            }
            // Ransom: a computer owner that keeps a reserve buys its leader back.
            for (int32_t i = 0; i < s->count;) {
                const s_held h = s->held[i];
                if (h.kind != KIND_PRISONER || !Alive_(h.holder)) {
                    ++i;
                    continue;
                }
                if (!Computer_(h.owner)) {
                    if (choices::Ask_Human(h.owner)) Ask_Leader_Owner_(i);
                    ++i;
                    continue;
                }
                if (MOX::player[h.owner].bc - Ransom_Of(h.leader) < RANSOM_RESERVE) {
                    ++i;
                    continue;
                }
                Pay_Ransom_(i);
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

    bool Choice_Text(const choices::s_choice& c, choices::s_text* t) {
        const int16_t player = c.player;
        const int16_t other = c.other;
        if (!Alive_(player) || !Alive_(other) || player == other) return false;
        const auto Short = [](const char* name) { return std::string(name, strnlen(name, 14)); };
        const auto N = [](int32_t v) { return std::to_string(v); };
        const std::string race = Short(MOX::player[other].race_name);
        const auto Option = [&](const char* key, std::initializer_list<std::pair<const char*, std::string>> args) {
            texts::Format(t->option[t->count], sizeof(t->option[t->count]), (std::string("choices.hostages.") + key).c_str(), args);
            ++t->count;
        };
        const auto Head = [&](const char* title, const char* body, std::initializer_list<std::pair<const char*, std::string>> args) {
            *t = choices::s_text{};
            texts::Format(t->title, sizeof(t->title), (std::string("choices.hostages.") + title).c_str(), {});
            texts::Format(t->body, sizeof(t->body), (std::string("choices.hostages.") + body).c_str(), args);
        };
        if (c.kind == CHOICE_LEADER_OWNER || c.kind == CHOICE_LEADER_HOLDER) {
            if (!prisoners) return false;
            const bool owner_side = c.kind == CHOICE_LEADER_OWNER;
            const int32_t index = Find(c.target);
            if (index < 0) return false;
            const s_held h = State()->held[index];
            if (h.kind != KIND_PRISONER || h.owner != (owner_side ? player : other) || h.holder != (owner_side ? other : player)) return false;
            const int32_t price = Ransom_Of(h.leader);
            const int32_t partner = Exchange_Partner_(index);
            const bool exchange = partner >= 0 && Computer_(other);
            const std::string theirs = exchange ? Short(MOX::leaders[State()->held[partner].leader].name) : std::string();
            const std::string name = Short(MOX::leaders[h.leader].name);
            if (owner_side) {
                Head("owner_leader.title", "owner_leader.body",
                    {{"leader", name}, {"race", race}, {"turns", N(MOX::stardate - h.since)}, {"price", N(price)},
                     {"escape", N(ESCAPE_PERCENT)}, {"limit", N(PRISONER_TURNS)}});
                Option(MOX::player[player].bc >= price ? "owner_leader.option1" : "owner_leader.option1_poor", {{"price", N(price)}});
                if (exchange) Option("owner_leader.option2", {{"theirs", theirs}});
                Option("owner_leader.option3", {});
            } else {
                Head("holder_leader.title", "holder_leader.body",
                    {{"leader", name}, {"race", race}, {"turns", N(MOX::stardate - h.since)}, {"price", N(price)}});
                Option("holder_leader.option1", {{"opinion", N(EXECUTED_PRISONER_OPINION)}});
                if (exchange) {
                    Option("holder_leader.option2_exchange", {{"theirs", theirs}, {"race", race}});
                } else {
                    Option("holder_leader.option2_release", {{"race", race}});
                }
                Option("holder_leader.option3", {});
            }
            return true;
        }
        if (c.kind == CHOICE_SPY_OWNER || c.kind == CHOICE_SPY_HOLDER) {
            if (!spy_prison) return false;
            const bool owner_side = c.kind == CHOICE_SPY_OWNER;
            const int32_t cell = owner_side ? Spy_Cell_(other, player) : Spy_Cell_(player, other);
            if (cell < 0) return false;
            const s_spies cs = State()->spies[cell];
            const int32_t reverse = owner_side ? Spy_Cell_(player, other) : Spy_Cell_(other, player);
            const bool exchange = reverse >= 0 && Computer_(other);
            if (owner_side) {
                Head("owner_spy.title", "owner_spy.body",
                    {{"race", race}, {"count", N(cs.count)}, {"turns", N(MOX::stardate - cs.since)}, {"price", N(SPY_RANSOM)}});
                Option(MOX::player[player].bc >= SPY_RANSOM ? "owner_spy.option1" : "owner_spy.option1_poor", {{"price", N(SPY_RANSOM)}});
                if (exchange) Option("owner_spy.option2", {{"theirs", N(State()->spies[reverse].count)}});
                Option("owner_spy.option3", {});
            } else {
                Head("holder_spy.title", "holder_spy.body",
                    {{"race", race}, {"count", N(cs.count)}, {"turns", N(MOX::stardate - cs.since)}, {"price", N(SPY_RANSOM)}});
                Option("holder_spy.option1", {{"opinion", N(EXECUTED_SPY_OPINION)}});
                if (exchange) {
                    Option("holder_spy.option2_exchange", {{"theirs", N(State()->spies[reverse].count)}, {"race", race}});
                } else {
                    Option("holder_spy.option2_release", {{"race", race}});
                }
                Option("holder_spy.option3", {});
            }
            return true;
        }
        return false;
    }

    void Choice_Apply(const choices::s_choice& c, int32_t option) {
        const int16_t player = c.player;
        const int16_t other = c.other;
        if (!Alive_(player) || !Alive_(other) || player == other) return;
        if (c.kind == CHOICE_LEADER_OWNER || c.kind == CHOICE_LEADER_HOLDER) {
            if (!prisoners) return;
            const bool owner_side = c.kind == CHOICE_LEADER_OWNER;
            const int32_t index = Find(c.target);
            if (index < 0) return;
            const s_held h = State()->held[index];
            if (h.kind != KIND_PRISONER || h.owner != (owner_side ? player : other) || h.holder != (owner_side ? other : player)) return;
            const int32_t partner = Exchange_Partner_(index);
            const bool exchange = partner >= 0 && Computer_(other);
            if (owner_side) {
                if (option == 0) {
                    if (MOX::player[player].bc >= Ransom_Of(h.leader)) Pay_Ransom_(index);
                } else if (option == 1 && exchange) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, players %d and %d exchange leaders %d and %d",
                        (int)MOX::stardate, (int)player, (int)other, (int)h.leader, (int)State()->held[partner].leader);
                    Exchange_Leaders_(index, partner);
                }
            } else if (option == 0) {
                opinion::Add(h.owner, h.holder, opinion::REASON_HARMED_A_HOSTAGE, EXECUTED_PRISONER_OPINION);
                reputation::On_Leader_Killed(h.holder);  // stains and titles (off by default)
                Execute(index, "the human holder executes the prisoner");
            } else if (option == 1) {
                if (exchange) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, players %d and %d exchange leaders %d and %d",
                        (int)MOX::stardate, (int)player, (int)other, (int)h.leader, (int)State()->held[partner].leader);
                    Exchange_Leaders_(index, partner);
                } else {
                    Release(index, h.owner, "the human holder releases the prisoner");
                }
            }
            return;
        }
        if (c.kind == CHOICE_SPY_OWNER || c.kind == CHOICE_SPY_HOLDER) {
            if (!spy_prison) return;
            const bool owner_side = c.kind == CHOICE_SPY_OWNER;
            const int32_t cell = owner_side ? Spy_Cell_(other, player) : Spy_Cell_(player, other);
            if (cell < 0) return;
            const int32_t reverse = owner_side ? Spy_Cell_(player, other) : Spy_Cell_(other, player);
            const bool exchange = reverse >= 0 && Computer_(other);
            if (owner_side) {
                if (option == 0) {
                    if (MOX::player[player].bc >= SPY_RANSOM) Pay_Spy_Ransom_(cell);
                } else if (option == 1 && exchange) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, players %d and %d exchange a spy each",
                        (int)MOX::stardate, (int)player, (int)other);
                    Exchange_Spies_(cell, reverse);
                }
            } else if (option == 0) {
                opinion::Add(other, player, opinion::REASON_TREATED_OUR_SPY, EXECUTED_SPY_OPINION);
                Free_Spy_(cell, "executed by the human jailer", false);
            } else if (option == 1) {
                if (exchange) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hostages: stardate %d, players %d and %d exchange a spy each",
                        (int)MOX::stardate, (int)player, (int)other);
                    Exchange_Spies_(cell, reverse);
                } else {
                    Free_Spy_(cell, "released by the human jailer", true);
                }
            }
        }
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
            if (MOX::stardate - h.since >= PRISONER_TURNS && MOX::stardate % PERIOD == PHASE && choices::Ask_Human(h.holder)) {
                Ask_Leader_Holder_(i);
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
