#include "pch.h"

#include "game/espionage.h"

#include "game/advisor.h"
#include "game/advisor_directive.h"
#include "game/bill.h"
#include "game/counterspy.h"
#include "game/hostages.h"
#include "game/intrigue.h"
#include "game/loyalty.h"
#include "game/officer.h"
#include "game/opinion.h"
#include "game/random.h"

#include <algorithm>

// The behaviour of the spy network core (mechanics.md 3.2.5.2); the state and its save section are
// in espionage_state.cpp. game_random is drawn only under a witch hunt.
namespace espionage {

    namespace {
        constexpr int8_t STATUS_POOL = 0;
        constexpr int8_t STATUS_ASSIGNED = 1;
        constexpr int8_t STATUS_LIMBO = 2;

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        bool Computer_(int16_t player) {
            return Alive_(player) && MOX::player[player].objectives != PLAYER_OBJECTIVE_HUMAN;
        }

        bool Holds_(const s_leader_data& leader, int16_t player) {
            return leader.player_index == player &&
                (leader.status == STATUS_POOL || leader.status == STATUS_ASSIGNED || leader.status == STATUS_LIMBO);
        }

        // A spy master in the empire's service, or the neural scanner: a free spy now and then.
        bool Draws_Free_Spies_(int16_t player) {
            if (MOX::player[player].tech_applications[TECH_APP_NEURAL_SCANNER] == TECH_RESEARCH_STATUS_RESEARCHED) return true;
            for (int16_t i = 0; i < MAX_LEADERS; ++i) {
                const s_leader_data& leader = MOX::leaders[i];
                if (Holds_(leader, player) &&
                    (leader.general_skills & (LEADER_GENERAL_SKILL_SPY_MASTER | LEADER_GENERAL_SKILL_SPY_MASTER2)) != 0) {
                    return true;
                }
            }
            return false;
        }

        // Colonies owned by `target`, outposts included: the same definition as the advisor's
        // "OUR STATE: N colonies" (advisor_view.cpp, Fill_Self: owner == player over all colonies).
        int32_t Colonies_Of_(int16_t target) {
            int32_t n = 0;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                if (MOX::colony[c].owner == static_cast<int8_t>(target)) ++n;
            }
            return n;
        }

        // "Сеть копит в цели +1 очко за шпиона в ход за каждые 8 колоний цели, но не больше +2 (до 7 колоний -
        // как сейчас, 8-15 - вдвое быстрее, 16 и больше - втрое); для всех, включая человека" (iterations.md,
        // решение автора 27.11.2026, «слабые против сильного»). The step is network_size_step; 0 = off.
        int32_t Size_Bonus_(int16_t target) {
            if (network_size_step < 1) return 0;
            return std::min<int32_t>(SIZE_BONUS_MAX, Colonies_Of_(target) / network_size_step);
        }

        void Network_Turn_() {
            s_state* s = State();
            for (int16_t owner = 0; owner < Players_(); ++owner) {
                for (int16_t target = 0; target < Players_(); ++target) {
                    if (owner == target) continue;
                    int16_t& points = s->points[owner][target];
                    const int32_t spies = Alive_(owner) && Alive_(target) ? BILL::Get_Their_Spy_Number(owner, target) : 0;
                    if (spies > 0) {
                        const int32_t gain = spies * (points_per_spy + Size_Bonus_(target));
                        points = static_cast<int16_t>(std::min<int32_t>(network_cap, points + gain));
                    } else if (points > 0) {
                        --points;
                    }
                }
            }
            if (free_spy_turns < 1 || MOX::stardate % free_spy_turns != 0) return;
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!Alive_(p) || !Draws_Free_Spies_(p)) continue;
                if (BILL::Add_Their_Spy(p, p) != 0) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "espionage: stardate %d, player %d gains a spy", (int)MOX::stardate, (int)p);
                }
            }
        }

        void Counterintelligence_Turn_() {
            s_state* s = State();
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!Alive_(p)) continue;
                if (s->caught[p] != 0 && MOX::stardate - s->last_caught[p] > RECENT_TURNS) s->caught[p] = 0;
                const e_mode mode = Mode(p);
                s_player& player = MOX::player[p];
                const int32_t cost = std::max<int32_t>(0, player.surplus_bc) * MODE_COST_PERCENT[mode] / 100;
                player.bc -= std::min<int32_t>(player.bc, cost);
                if (mode != MODE_WITCH_HUNT) continue;
                // Решение автора (iterations.md, итерация 2, п. 2): «Охота на ведьм ограничена так же, как охота на крота: не больше одного ареста за 20 ходов на империю».
                if (s->last_arrest[p] != 0 && MOX::stardate - s->last_arrest[p] < WITCH_COOLDOWN) continue;  // no draws while it rests
                for (int16_t i = 0; i < MAX_LEADERS; ++i) {
                    const s_leader_data& leader = MOX::leaders[i];
                    if (!Holds_(leader, p) || leader.status == STATUS_LIMBO) continue;
                    const int32_t trust = loyalty::Of(i);
                    const int32_t chance = trust == loyalty::UNTRACKED ? witch_hunt_accusation
                                                                       : witch_hunt_accusation * (100 - trust) / 50;
                    if (static_cast<int32_t>(game_random::Random(1000)) > chance) continue;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "espionage: stardate %d, player %d jails its leader %d in a witch hunt",
                        (int)MOX::stardate, (int)p, (int)i);
                    hostages::Arrest(i, false);  // a false accusation: an honest leader is jailed (mechanics.md 3.2.3, ab-review 3.1)
                    // Iteration 1, decision 2 (iterations.md): "каждый посаженный невиновный сильнее снижает лояльность
                    // остальных лидеров" - the witch hunt jails innocents too, so the same cost applies.
                    counterspy::On_Innocent_Jailed(p, i);
                    s->last_arrest[p] = MOX::stardate;
                    break;  // one arrest per cooldown
                }
            }
            if (MOX::stardate % MODE_PERIOD != 0) return;
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!Computer_(p)) continue;
                // Решение автора (iterations.md, итерация 4, «варианты 0 и 1»): "Компьютер переходит в повышенную
                // бдительность после трёх поимок за последние 20 ходов ... Охота на ведьм - только если за последние
                // 20 ходов против империи удалась операция, которую жертва видит как дело чужих шпионов, или
                // поймано шесть шпионов и больше." (the old caught / last_caught fields are no longer read here)
                int32_t catches = 0;
                for (int32_t t : s->catch_ring[p]) {
                    if (t != 0 && t > MOX::stardate - CATCH_WINDOW) ++catches;
                }
                const bool harmed = s->last_harm[p] != 0 && MOX::stardate - s->last_harm[p] <= HARM_TURNS;
                e_mode wanted = harmed || catches >= WITCH_CATCHES ? MODE_WITCH_HUNT
                    : catches >= HEIGHTENED_CATCHES ? MODE_HEIGHTENED : MODE_NORMAL;
                if (advisor::enabled && ((advisor::active_players >> p) & 1) != 0) {
                    const advisor::s_directive* d = advisor::Get_Directive(static_cast<uint8_t>(p));
                    if (d != nullptr && advisor::Is_Active(d, MOX::stardate) && d->spy_mission == advisor::SPY_COUNTERINTEL &&
                        wanted < MODE_HEIGHTENED) {
                        wanted = MODE_HEIGHTENED;
                    }
                }
                Set_Mode(p, wanted);
            }
        }
    } // namespace

    void On_Spy_Caught(int16_t victim, int16_t attacker) {
        intrigue::On_Spy_Caught(victim, attacker);  // the spy's fate (off by default)
        // A spy whose fate nobody decided otherwise sits in the catcher's prison (off by default; ab-review 2.2).
        if (hostages::spy_prison && intrigue::Decide_Fate(victim, attacker, INT32_MAX) == intrigue::FATE_CLASSIC) {
            hostages::Imprison_Spy(victim, attacker);
        }
        counterspy::On_Spy_Caught(victim, attacker);  // suspicion, interrogation (off by default)
        if (!counterintelligence || !Alive_(victim) || attacker < 0 || attacker >= MAX_PLAYERS || victim == attacker) return;
        s_state* s = State();
        if (s->caught[victim] != 0 && MOX::stardate - s->last_caught[victim] > RECENT_TURNS) s->caught[victim] = 0;
        if (s->caught[victim] < 255) ++s->caught[victim];
        s->last_caught[victim] = MOX::stardate;
        for (int32_t k = WITCH_CATCHES - 1; k > 0; --k) s->catch_ring[victim][k] = s->catch_ring[victim][k - 1];
        s->catch_ring[victim][0] = MOX::stardate;  // newest first; the oldest of the last WITCH_CATCHES falls out
        opinion::Add(victim, attacker, opinion::REASON_CAUGHT_SPYING, caught_opinion);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "espionage: stardate %d, player %d catches a spy of player %d",
            (int)MOX::stardate, (int)victim, (int)attacker);
    }

    // The option the author chose (27.11.2026, «варианты 0 и 1»): "Охота на ведьм включается, только если за
    // 20 ходов против империи прошла настоящая операция (саботаж, убийство лидера или учёного, яд, подкуп) ...
    // Пойманный сборщик данных — не повод сажать своих": every successful operation but data collection.
    void On_Harm(int16_t victim, spy_ops::e_op op) {
        if (!counterintelligence || !Alive_(victim) || op == spy_ops::OP_DATA) return;
        State()->last_harm[victim] = MOX::stardate;
    }

    void Turn() {
        if (network) Network_Turn_();
        if (counterintelligence) Counterintelligence_Turn_();
    }

} // namespace espionage
