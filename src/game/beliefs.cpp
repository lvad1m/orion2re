#include "pch.h"

#include "game/beliefs.h"

#include "game/bill.h"
#include "game/dice.h"
#include "game/espionage.h"
#include "game/intrigue.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/spy.h"
#include "game/spy_ops.h"
#include "game/spy_ranks.h"

#include <algorithm>

// The behaviour of the neighbour model and of disinformation (mechanics.md 3.5.10); the state, the
// evidence from opinion memories and the save section are in beliefs_state.cpp. game_random is drawn
// only with spy disinformation on: the planting dice and the rolls to see through a lie.
namespace beliefs {

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

        void Fade_() {
            for (auto& row : State()->evidence) {
                for (auto& cell : row) {
                    for (int8_t& value : cell) {
                        if (value > 0) {
                            --value;
                        } else if (value < 0) {
                            ++value;
                        }
                    }
                }
            }
        }

        // The deceived empire's spies in the deceiver's empire may see through the lie.
        void See_Through_() {
            s_state* s = State();
            for (int16_t deceived = 0; deceived < Players_(); ++deceived) {
                for (int16_t deceiver = 0; deceiver < Players_(); ++deceiver) {
                    if (s->distortion[deceived][deceiver] == 0) continue;
                    if (!Alive_(deceived) || !Alive_(deceiver) || s->distortion_until[deceived][deceiver] <= MOX::stardate) {
                        s->distortion[deceived][deceiver] = 0;
                        s->distortion_until[deceived][deceiver] = 0;
                        continue;
                    }
                    const int32_t spies = std::min<int32_t>(5, BILL::Get_Their_Spy_Number(deceived, deceiver));
                    const int32_t chance = SEE_THROUGH_PER_SPY * spies + 100 * spy_ranks::Residents(deceived, deceiver);
                    if (chance <= 0 || static_cast<int32_t>(game_random::Random(1000)) > chance) continue;
                    s->distortion[deceived][deceiver] = 0;
                    s->distortion_until[deceived][deceiver] = 0;
                    opinion::Add(deceived, deceiver, opinion::REASON_PLOTTED_AGAINST_US, SEEN_THROUGH_OPINION);
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "beliefs: stardate %d, player %d sees through player %d's disinformation",
                        (int)MOX::stardate, (int)deceived, (int)deceiver);
                }
            }
        }

        void Plant_() {
            if (!espionage::network) return;
            for (int16_t deceiver = 0; deceiver < Players_(); ++deceiver) {
                if (!Computer_(deceiver)) continue;
                int16_t victims[MAX_PLAYERS];
                const int32_t planned = intrigue::Planned_Victims(deceiver, victims);
                for (int16_t target = 0; target < Players_(); ++target) {
                    if (target == deceiver || !Alive_(target) || Distortion_Percent(target, deceiver) != 100) continue;
                    const bool source = intrigue::Has_Double_Agent(deceiver, target) || BILL::Get_Their_Spy_Number(deceiver, target) > 0;
                    if (!source) continue;
                    bool plans_attack = false;
                    for (int32_t i = 0; i < planned; ++i) {
                        if (victims[i] == target) plans_attack = true;
                    }
                    // Fears it: its fleet against us is a quarter stronger than ours against it.
                    const int32_t theirs = MOX::player[target].total_ship_strength[deceiver];
                    const int32_t ours = MOX::player[deceiver].total_ship_strength[target];
                    const bool fears = theirs * 4 > ours * 5;
                    if (!plans_attack && !fears) continue;
                    if (Plant_Disinformation(deceiver, target, plans_attack ? DISINFO_WEAKER : DISINFO_STRONGER)) break;
                }
            }
        }
    } // namespace

    void On_Treaty_Broken(int16_t breaker, int16_t victim) {
        if (!neighbour_model || breaker < 0 || breaker >= Players_() || victim < 0 || victim >= Players_()) return;
        for (int16_t o = 0; o < Players_(); ++o) {
            if (o == breaker || !Computer_(o)) continue;
            if (o == victim) {
                Observe(o, breaker, BELIEF_FAITHFUL, -3);
            } else if (MOX::player[o].contact[breaker] != 0) {
                Observe(o, breaker, BELIEF_FAITHFUL, -1);
            }
        }
    }

    void On_Lie(int16_t receiver, int16_t sender) {
        Observe(receiver, sender, BELIEF_HONEST, -2);
    }

    bool Plant_Disinformation(int16_t deceiver, int16_t target, int32_t percent) {
        return Plant_Disinformation_With_Dice(deceiver, target, percent, nullptr);
    }

    bool Plant_Disinformation_With_Dice(int16_t deceiver, int16_t target, int32_t percent, const int32_t* dice_values) {
        if (!disinformation || !espionage::network || !Alive_(deceiver) || !Alive_(target) || deceiver == target) return false;
        if (percent <= 0 || percent > 1000) return false;
        s_state* s = State();
        // A turned spy carries the lie for free and cannot fail.
        if (intrigue::Has_Double_Agent(deceiver, target)) {
            s->distortion[target][deceiver] = static_cast<int16_t>(percent);
            s->distortion_until[target][deceiver] = MOX::stardate + DISINFO_TURNS;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "beliefs: stardate %d, player %d feeds player %d a false fleet (%d %%) through a turned spy",
                (int)MOX::stardate, (int)deceiver, (int)target, (int)percent);
            return true;
        }
        const int32_t spies = BILL::Get_Their_Spy_Number(deceiver, target);
        if (spies == 0 || !espionage::Spend_Points(deceiver, target, disinformation_cost)) return false;
        int16_t offense = 0;
        int16_t defense = 0;
        spy::Compute_Spy_Bonuses(&MOX::player[deceiver], &offense, &defense);
        const dice::s_modifier modifiers[3] = {{"spies", std::min<int32_t>(5, spies / 2)}, {"spy skill", offense / 10},
            {"resident", spy_ranks::Resident_Bonus(deceiver, target)}};
        const int32_t difficulty = spy_ops::Difficulty(deceiver, spy_ops::OP_DATA, target);
        const dice::s_check check = dice_values != nullptr
            ? dice::Resolve_Check(static_cast<dice::e_roll_kind>(dice::roll_kind), dice_values, modifiers, 3, difficulty)
            : dice::Roll_Check(modifiers, 3, difficulty);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "beliefs: stardate %d, player %d feeds player %d a false fleet (%d %%): %s",
            (int)MOX::stardate, (int)deceiver, (int)target, (int)percent,
            dice::Describe_Check("plants disinformation", check, modifiers, 3).c_str());
        if (check.outcome >= dice::OUTCOME_SUCCESS) {
            s->distortion[target][deceiver] = static_cast<int16_t>(percent);
            s->distortion_until[target][deceiver] = MOX::stardate + DISINFO_TURNS;
            return true;
        }
        if (check.outcome == dice::OUTCOME_EXPOSED || check.outcome == dice::OUTCOME_CATASTROPHE) {
            BILL::Set_Their_Spy_Number(deceiver, target, static_cast<uint8_t>(spies - 1));
            opinion::Add(target, deceiver, opinion::REASON_PLOTTED_AGAINST_US, SEEN_THROUGH_OPINION);
            espionage::On_Spy_Caught(target, deceiver);
        }
        return false;
    }

    void Turn() {
        if (neighbour_model && MOX::stardate % DECAY_PERIOD == 0) Fade_();
        if (!disinformation) return;
        See_Through_();
        if (MOX::stardate % PERIOD == 4) Plant_();
    }

} // namespace beliefs
