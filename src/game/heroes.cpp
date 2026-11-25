#include "pch.h"

#include "game/heroes.h"

#include "game/crises.h"
#include "game/espionage.h"
#include "game/loyalty.h"
#include "game/officer.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/spy_ops.h"

#include <algorithm>
#include <climits>

// The behaviour of the leaders' events (events-catalog.md 3.11, 3.12); the records and the save section are in
// heroes_state.cpp. game_random is drawn only with events heroes on: the chance of an event and the pick of its
// subject, the freeing of a controlled leader, and the rival heroes' death and hire.
namespace heroes {

    namespace {
        constexpr int8_t TREATY_FIRST_WAR = 4;
        constexpr int32_t TREATISE_CALM_LOYALTY = 5;

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

        void Log_(int16_t player, const char* what) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "heroes: stardate %d, player %d: %s", (int)MOX::stardate, (int)player, what);
        }

        void Log_Leader_(int16_t player, int16_t leader, const char* what) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "heroes: stardate %d, player %d, leader %d: %s", (int)MOX::stardate, (int)player, (int)leader, what);
        }

        void Add_Bc_(int16_t player, int32_t delta) {
            s_player& me = MOX::player[player];
            me.bc = static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(me.bc) + delta, 0, INT32_MAX / 2));
        }

        bool Held_(int16_t leader, int16_t player) {
            if (leader < 0 || leader >= MAX_LEADERS) return false;
            const s_leader_data& l = MOX::leaders[leader];
            return l.player_index == player && (l.status == STATUS_POOL || l.status == STATUS_ASSIGNED || l.status == STATUS_LIMBO);
        }

        bool Tied_(int16_t leader) {
            for (int32_t i = 0; i < State()->count; ++i) {
                if (State()->record[i].leader == leader) return true;
            }
            return false;
        }

        int32_t Loyalty_(int16_t leader) {
            const int32_t value = loyalty::Of(leader);
            return value == loyalty::UNTRACKED ? 50 : value;
        }

        // The loyalty record of a leader moves only when the loyalty tracking is on and holds the leader.
        void Adjust_Loyalty_(int16_t leader, int32_t delta) {
            if (loyalty::Of(leader) == loyalty::UNTRACKED) return;
            loyalty::s_record* r = loyalty::Record(leader);
            if (r != nullptr) r->loyalty = static_cast<int8_t>(std::clamp<int32_t>(r->loyalty + delta, 0, 100));
        }

        void Remove_Record_(int32_t index) {
            s_state* s = State();
            for (int32_t i = index; i + 1 < s->count; ++i) s->record[i] = s->record[i + 1];
            s->record[--s->count] = s_record{};
        }

        bool At_War_(int16_t a, int16_t b) {
            return MOX::player[a].treaty[b] >= TREATY_FIRST_WAR;
        }

        bool Contact_(int16_t a, int16_t b) {
            return MOX::player[a].contact[b] != 0 || MOX::player[b].contact[a] != 0;
        }

        bool Has_Telepath_(int16_t player) {
            for (int16_t i = 0; i < MAX_LEADERS; ++i) {
                if (Held_(i, player) && (MOX::leaders[i].general_skills & (LEADER_GENERAL_SKILL_TELEPATH | LEADER_GENERAL_SKILL_TELEPATH2)) != 0) return true;
            }
            return false;
        }

        // Mind control ----------------------------------------------------------------------------------------

        void Release_Bribe_(const s_record& r) {
            int8_t* slot = spy_ops::Bribed_Slot(r.leader);
            if (slot != nullptr && *slot == r.other) *slot = -1;
        }

        // Quirks ----------------------------------------------------------------------------------------------

        bool Pacifism_Ok_(int16_t leader) {
            return Computer_(MOX::leaders[leader].player_index) && MOX::leaders[leader].type == LEADER_TYPE_SHIP;
        }

        int32_t Quirk_Turns_(e_kind kind) {
            switch (kind) {
                case KIND_STIMULANT: return STIM_TURNS;
                case KIND_PARANOIA: return PARANOIA_TURNS;
                case KIND_PACIFISM: return PACIFISM_TURNS;
                default: return SABOTAGE_TURNS;
            }
        }

        void Give_Post_Up_(int16_t leader) {
            if (MOX::leaders[leader].status == STATUS_ASSIGNED) OFFICER::Move_Officer_To_Pool(leader);
        }

        // One turn of the record at `index`; true when it is gone.
        bool Step_(int32_t index) {
            s_record r = State()->record[index];
            if (!Alive_(r.player)) {
                Log_(r.player, "the event is dropped, the empire is gone");
                if (r.kind == KIND_MIND) Release_Bribe_(r);
                Remove_Record_(index);
                return true;
            }
            const int32_t elapsed = MOX::stardate - r.started;
            const bool over = MOX::stardate >= r.ends;
            if (r.kind == KIND_MANAGER) {  // retired kind of an old save
                Remove_Record_(index);
                return true;
            }
            if (!Held_(r.leader, r.player)) {
                Log_Leader_(r.player, r.leader, "the event ends, the leader is gone");
                if (r.kind == KIND_MIND) Release_Bribe_(r);
                Remove_Record_(index);
                return true;
            }
            switch (r.kind) {
                case KIND_MIND: {
                    if (!Alive_(r.other)) {
                        Log_Leader_(r.player, r.leader, "the alien mind lets go, its master is gone");
                        Release_Bribe_(r);
                        Remove_Record_(index);
                        return true;
                    }
                    if (spy_ops::Enabled(spy_ops::OP_BRIBERY)) {
                        int8_t* slot = spy_ops::Bribed_Slot(r.leader);
                        if (slot != nullptr && *slot < 0) *slot = r.other;
                    }
                    const int32_t tribute = std::min<int32_t>(MIND_TRIBUTE, MOX::player[r.player].bc);
                    if (tribute > 0) {
                        Add_Bc_(r.player, -tribute);
                        Add_Bc_(r.other, tribute);
                    }
                    const int32_t chance = MIND_FREE * (1 + static_cast<int32_t>(espionage::Mode(r.player)));
                    const bool freed = static_cast<int32_t>(game_random::Random(1000)) <= chance;
                    if (freed || over) {
                        Log_Leader_(r.player, r.leader, freed ? "the leader breaks free of the alien mind" : "the alien mind fades");
                        Release_Bribe_(r);
                        Remove_Record_(index);
                        return true;
                    }
                    return false;
                }
                case KIND_STIMULANT:
                    if (elapsed > 0 && elapsed % STIM_STEP == 0) {
                        OFFICER::Give_Officer_XP(r.player, r.leader, static_cast<int16_t>(STIM_XP));
                        Log_Leader_(r.player, r.leader, "the stimulant sharpens the leader's mind");
                    }
                    if (over) {
                        Adjust_Loyalty_(r.leader, STIM_CRASH);
                        Log_Leader_(r.player, r.leader, "the stimulant wears off, the leader crashes");
                        Remove_Record_(index);
                        return true;
                    }
                    return false;
                case KIND_PARANOIA:
                    if (elapsed > 0 && elapsed % PARANOIA_STEP == 0) {
                        State()->record[index].value = static_cast<int16_t>(r.value + 1);
                        Adjust_Loyalty_(r.leader, PARANOIA_LOYALTY);
                        const int32_t steps = r.value + 1;
                        const int32_t loyal = loyalty::Of(r.leader);
                        if (steps >= PARANOIA_FLEE || (loyal != loyalty::UNTRACKED && loyal <= PARANOIA_LOW)) {
                            Give_Post_Up_(r.leader);
                            Log_Leader_(r.player, r.leader, "the paranoid leader locks himself away and leaves his post");
                            Remove_Record_(index);
                            return true;
                        }
                    }
                    if (over) {
                        Adjust_Loyalty_(r.leader, TREATISE_CALM_LOYALTY);
                        Log_Leader_(r.player, r.leader, "the leader's fears pass");
                        Remove_Record_(index);
                        return true;
                    }
                    return false;
                case KIND_PACIFISM:
                    if (MOX::leaders[r.leader].status == STATUS_ASSIGNED && Pacifism_Ok_(r.leader)) {
                        Give_Post_Up_(r.leader);
                        State()->record[index].value = 1;
                        Log_Leader_(r.player, r.leader, "the pacifist captain refuses to serve and leaves his post");
                    }
                    if (over) {
                        OFFICER::Give_Officer_XP(r.player, r.leader, static_cast<int16_t>(PACIFISM_XP));
                        Log_Leader_(r.player, r.leader, "the pacifist's treatise is finished");
                        Remove_Record_(index);
                        return true;
                    }
                    return false;
                default:  // sabotage
                    if (over) {
                        Add_Bc_(r.player, -std::min<int32_t>(SABOTAGE_BC, MOX::player[r.player].bc));
                        Adjust_Loyalty_(r.leader, SABOTAGE_LOYALTY);
                        Log_Leader_(r.player, r.leader, "the leader's sabotage comes to light: the treasury is robbed");
                        Remove_Record_(index);
                        return true;
                    }
                    return false;
            }
        }

        // New events -------------------------------------------------------------------------------------------

        bool Spawn_Mind_() {
            for (int16_t c = 0; c < Players_(); ++c) {
                if (!Computer_(c) || !Has_Telepath_(c)) continue;
                if (static_cast<int32_t>(game_random::Random(1000)) > MIND_CHANCE) continue;
                int16_t cand[MAX_LEADERS];
                int32_t n = 0;
                for (int16_t v = 0; v < Players_(); ++v) {
                    if (v == c || !Alive_(v) || !Contact_(c, v)) continue;
                    if (!At_War_(c, v) && opinion::Relations(c, v) > MIND_OPINION) continue;
                    for (int16_t l = 0; l < MAX_LEADERS; ++l) {
                        const int8_t* bribe = spy_ops::Bribed_Slot(l);
                        if (Held_(l, v) && MOX::leaders[l].status == STATUS_ASSIGNED && !Tied_(l) && (bribe == nullptr || *bribe < 0)) cand[n++] = l;
                    }
                }
                if (n == 0) continue;
                const int16_t leader = cand[Pick_(n)];
                if (Start(KIND_MIND, MOX::leaders[leader].player_index, leader, c)) return true;
            }
            return false;
        }

        bool Spawn_Quirk_() {
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!Alive_(p) || Active(p) >= MAX_PER_EMPIRE) continue;
                if (static_cast<int32_t>(game_random::Random(1000)) > QUIRK_CHANCE) continue;
                int16_t cand[MAX_LEADERS];
                int32_t n = 0;
                for (int16_t l = 0; l < MAX_LEADERS; ++l) {
                    if ((Held_(l, p) && MOX::leaders[l].status != STATUS_LIMBO) && !Tied_(l)) cand[n++] = l;
                }
                if (n == 0) continue;
                const int16_t leader = cand[Pick_(n)];
                e_kind kind = Quirk_For(leader, Pick_(2));
                if (kind == KIND_PACIFISM && !Pacifism_Ok_(leader)) kind = KIND_STIMULANT;
                if (Start(kind, p, leader)) return true;
            }
            return false;
        }
    } // namespace

    bool Is_Famous(int16_t leader) {
        if (leader < 0 || leader >= MAX_LEADERS || MOX::leaders == nullptr) return false;
        const s_leader_data& l = MOX::leaders[leader];
        return l.status != STATUS_DEAD && (l.general_skills & (LEADER_GENERAL_SKILL_FAMOUS | LEADER_GENERAL_SKILL_FAMOUS2)) != 0;
    }

    int32_t Famous_Leaders(int16_t player, int16_t* out, int32_t max) {
        int32_t n = 0;
        for (int16_t i = 0; i < MAX_LEADERS; ++i) {
            if (!Is_Famous(i)) continue;
            const s_leader_data& l = MOX::leaders[i];
            const bool match = player < 0 ? (l.player_index == -1 && l.status == STATUS_FREE && i != 0x41 && i != 0x42)
                                          : (l.player_index == player && (l.status == STATUS_POOL || l.status == STATUS_ASSIGNED || l.status == STATUS_LIMBO));
            if (!match) continue;
            if (out != nullptr && n < max) out[n] = i;
            ++n;
        }
        return out != nullptr ? std::min(n, max) : n;
    }

    e_kind Quirk_For(int16_t leader, int32_t pick) {
        const int32_t l = Loyalty_(leader);
        if (l >= 60) return pick != 0 ? KIND_PACIFISM : KIND_STIMULANT;
        if (l >= 35) return pick != 0 ? KIND_STIMULANT : KIND_PARANOIA;
        return pick != 0 ? KIND_SABOTAGE : KIND_PARANOIA;
    }

    int16_t On_Crisis_Solved(int16_t player) {
        if (!enabled || !Alive_(player) || !OFFICER::Player_Can_Hire_More_Officers(player)) return -1;
        int16_t best = -1;
        int32_t best_score = 0;
        for (int16_t i = 0; i < MAX_LEADERS; ++i) {
            const s_leader_data& l = MOX::leaders[i];
            if (i == 0x41 || i == 0x42 || l.status != STATUS_FREE || l.player_index != -1) continue;
            int32_t score = 0;
            for (uint32_t bits = l.general_skills | l.special_skills; bits != 0; bits &= bits - 1) ++score;
            if (score == 0) continue;
            if (Is_Famous(i)) score += 100;
            if (score > best_score) {  // the first of the best: no dice
                best = i;
                best_score = score;
            }
        }
        if (best < 0) {
            Log_(player, "a crisis solved brilliantly, but the hire list has no leader with an ability");
            return -1;
        }
        OFFICER::Set_Officer_To_Player(best, player, 1);
        if (MOX::leaders[best].player_index != player) return -1;
        Log_Leader_(player, best, "a crisis solved brilliantly: a special leader joins the empire");
        return best;
    }

    bool Start(e_kind kind, int16_t player, int16_t leader, int16_t other) {
        s_state* st = State();
        if (!enabled || !Alive_(player) || st->count >= MAX_RECORDS || kind <= KIND_MANAGER || kind > KIND_LAST) return false;
        s_record r;
        r.kind = kind;
        r.player = static_cast<int8_t>(player);
        r.started = MOX::stardate;
        if (!Held_(leader, player) || Tied_(leader) || Active(player) >= MAX_PER_EMPIRE) return false;
        r.leader = leader;
        if (kind == KIND_MIND) {
            if (!Alive_(other) || other == player) return false;
            r.other = static_cast<int8_t>(other);
            r.ends = MOX::stardate + MIND_TURNS;
            if (spy_ops::Enabled(spy_ops::OP_BRIBERY)) {
                int8_t* slot = spy_ops::Bribed_Slot(leader);
                if (slot != nullptr && *slot < 0) *slot = static_cast<int8_t>(other);
            }
            st->record[st->count++] = r;
            Log_Leader_(player, leader, "an alien mind takes the leader over, he works for another empire");
            return true;
        }
        if (kind == KIND_PACIFISM && !Pacifism_Ok_(leader)) return false;
        r.ends = MOX::stardate + Quirk_Turns_(kind);
        st->record[st->count++] = r;
        Log_Leader_(player, leader,
            kind == KIND_STIMULANT  ? "the leader takes stimulants"
            : kind == KIND_PARANOIA ? "the leader grows paranoid"
            : kind == KIND_PACIFISM ? "the leader turns pacifist"
                                    : "the leader starts sabotage");
        return true;
    }

    void Rival_Step(bool force) {
        if (!enabled) return;
        if (!force && MOX::stardate % RIVAL_PERIOD != RIVAL_PHASE) return;
        bool hired = false;
        for (int16_t i = 0; i < MAX_LEADERS; ++i) {
            s_leader_data& l = MOX::leaders[i];
            if (i == 0x41 || i == 0x42 || l.status != STATUS_FREE || l.player_index != -1 || !Is_Famous(i)) continue;
            int16_t& grown = State()->grown[i];
            const int32_t gain = std::min<int32_t>(RIVAL_XP, RIVAL_XP_CAP - grown);
            if (gain > 0) {
                l.xp = static_cast<int16_t>(std::min<int32_t>(30000, l.xp + gain));
                grown = static_cast<int16_t>(grown + gain);
            }
            if (static_cast<int32_t>(game_random::Random(1000)) <= RIVAL_DEATH + grown / 100) {
                l.status = STATUS_DEAD;
                l.eta = 0;
                l.location = -1;
                Log_Leader_(-1, i, "a famous hero dies");
                continue;
            }
            if (hired) continue;
            int16_t best = -1;
            int32_t cost = 0;
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!Computer_(p) || !OFFICER::Player_Can_Hire_More_Officers(p)) continue;
                const int32_t c = OFFICER::Officer_Cost(p, i);
                if (MOX::player[p].bc < c + HIRE_RESERVE) continue;
                if (best < 0 || MOX::player[p].bc > MOX::player[best].bc) {
                    best = p;
                    cost = c;
                }
            }
            if (best < 0 || static_cast<int32_t>(game_random::Random(100)) > HIRE_CHANCE) continue;
            Add_Bc_(best, -cost);
            OFFICER::Set_Officer_To_Player(i, best, 1);
            if (l.player_index == best) {
                hired = true;
                Log_Leader_(best, i, "a famous hero is hired");
            } else {
                Add_Bc_(best, cost);  // no room after all: the money stays
            }
        }
    }

    void Run_Records() {
        if (!enabled) return;
        s_state* s = State();
        for (int32_t i = 0; i < s->count;) {
            if (!Step_(i)) ++i;
        }
        Rival_Step(false);
    }

    void Turn() {
        if (!enabled) return;
        Run_Records();
        if (Spawn_Mind_()) return;
        Spawn_Quirk_();
    }

} // namespace heroes
