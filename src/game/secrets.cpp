#include "pch.h"

#include "game/secrets.h"

#include "game/bill.h"
#include "game/dice.h"
#include "game/espionage.h"
#include "game/intrigue.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/rivalry.h"
#include "game/spy.h"
#include "game/spy_ops.h"
#include "game/spy_ranks.h"

#include <algorithm>

// The behaviour of secrets and smear campaigns (mechanics.md 3.5.2 and 3.2); the store and its save
// section are in secrets_state.cpp. game_random is drawn only with spy secrets or spy smear on.
namespace secrets {

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

        int32_t Spies_(int16_t owner, int16_t target) {
            return BILL::Get_Their_Spy_Number(owner, target);
        }

        bool Plans_(int16_t owner, int16_t victim) {
            int16_t victims[MAX_PLAYERS];
            const int32_t n = intrigue::Planned_Victims(owner, victims);
            for (int32_t i = 0; i < n; ++i) {
                if (victims[i] == victim) return true;
            }
            return false;
        }

        // The victim learns the secret: the same effects as its own spies uncovering it.
        void Tell_Victim_(const s_secret& s) {
            const int16_t owner = s.owner;
            const int16_t victim = s.victim;
            if (s.kind == KIND_PLAN) {
                opinion::Add(victim, owner, opinion::REASON_PLOTTED_AGAINST_US, intrigue::PLOTTED_OPINION);
                s_player& p = MOX::player[owner];
                if (p.sneak_attack_planet != -1 && p.sneak_attack_player == victim) {
                    p.sneak_attack_planet = -1;
                    p.sneak_attack_player = -1;
                    p.war_buildup_duration = 0;
                }
            } else if (s.kind == KIND_BRIBE && s.value >= 0 && s.value < MAX_LEADERS) {
                *spy_ops::Bribed_Slot(s.value) = -1;
                opinion::Add(victim, owner, opinion::REASON_BRIBED_OUR_LEADER, -25);
            }
        }

        const char* Kind_Name_(uint8_t kind) {
            return kind == KIND_PLAN ? "a planned attack" : kind == KIND_BRIBE ? "a bribed governor" : "a secret";
        }

        void Learn_All_() {
            for (int16_t h = 0; h < Players_(); ++h) {
                if (!Computer_(h)) continue;
                for (int16_t o = 0; o < Players_(); ++o) {
                    if (o == h || !Alive_(o)) continue;
                    const int32_t chance = LEARN_PER_SPY * std::min(5, Spies_(h, o)) + LEARN_PER_RESIDENT * spy_ranks::Residents(h, o);
                    if (chance <= 0) continue;
                    // The first secret of `o` that `h` does not know yet, if any; roll only then.
                    e_kind kind = KIND_NONE;
                    int16_t victim = -1;
                    int16_t value = -1;
                    int16_t victims[MAX_PLAYERS];
                    const int32_t n = intrigue::Planned_Victims(o, victims);
                    for (int32_t i = 0; i < n && kind == KIND_NONE; ++i) {
                        if (victims[i] == h) continue;
                        bool known = false;
                        for (int32_t k = 0; k < MAX_SECRETS; ++k) {
                            const s_secret& s = Slots()[k];
                            known = known || (s.kind == KIND_PLAN && s.owner == o && s.victim == victims[i] && s.holder == h);
                        }
                        if (!known) {
                            kind = KIND_PLAN;
                            victim = victims[i];
                        }
                    }
                    for (int16_t l = 0; l < MAX_LEADERS && kind == KIND_NONE; ++l) {
                        const s_leader_data& leader = MOX::leaders[l];
                        if (spy_ops::Bribed_By(l) != o || leader.player_index == h || leader.player_index < 0) continue;
                        bool known = false;
                        for (int32_t k = 0; k < MAX_SECRETS; ++k) {
                            const s_secret& s = Slots()[k];
                            known = known || (s.kind == KIND_BRIBE && s.value == l && s.holder == h);
                        }
                        if (!known) {
                            kind = KIND_BRIBE;
                            victim = leader.player_index;
                            value = l;
                        }
                    }
                    if (kind == KIND_NONE) continue;
                    if (static_cast<int32_t>(game_random::Random(1000)) > chance) continue;
                    if (Learn(h, kind, o, victim, value)) {
                        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d learns a secret of player %d: %s against player %d",
                            (int)MOX::stardate, (int)h, (int)o, Kind_Name_(kind), (int)victim);
                    }
                }
            }
        }

        void Use_All_() {
            for (int32_t i = 0; i < MAX_SECRETS; ++i) {
                s_secret& s = Slots()[i];
                if (s.kind == KIND_NONE) continue;
                if (!Still_True(s) || !Computer_(s.holder) || !Alive_(s.owner) || !Alive_(s.victim)) {
                    s = s_secret{};
                    continue;
                }
                switch (MOX::player[s.holder].personality) {
                    case PLAYER_PERSONALITY_HONORABLE:
                    case PLAYER_PERSONALITY_PACIFIST:
                        if (!Sell(i)) Make_Public(i);
                        break;
                    case PLAYER_PERSONALITY_RUTHLESS:
                    case PLAYER_PERSONALITY_AGGRESSIVE:
                    case PLAYER_PERSONALITY_XENOPHOBIC:
                        Blackmail(i);
                        break;
                    default:
                        Make_Public(i);
                        break;
                }
            }
        }

        void Smear_All_() {
            for (int16_t a = 0; a < Players_(); ++a) {
                if (!Computer_(a)) continue;
                for (int16_t t = 0; t < Players_(); ++t) {
                    if (t == a || !Alive_(t) || Spies_(a, t) == 0 || espionage::Points(a, t) < smear_cost) continue;
                    const bool foe = MOX::player[a].treaty[t] >= TREATY_FIRST_WAR || rivalry::Rival_Of(a) == t || opinion::Relations(a, t) <= -30;
                    if (!foe) continue;
                    Smear(a, t);
                    break;  // one campaign a period
                }
            }
        }
    } // namespace

    bool Still_True(const s_secret& s) {
        if (s.kind == KIND_NONE || MOX::stardate - s.learned >= SECRET_TURNS) return false;
        if (s.kind == KIND_PLAN) return Plans_(s.owner, s.victim);
        if (s.kind == KIND_BRIBE) return s.value >= 0 && s.value < MAX_LEADERS && spy_ops::Bribed_By(s.value) == s.owner;
        return false;
    }

    bool Sell(int32_t index) {
        if (!enabled || index < 0 || index >= MAX_SECRETS) return false;
        s_secret& s = Slots()[index];
        if (s.kind == KIND_NONE || !Computer_(s.victim) || MOX::player[s.victim].bc < SALE_PRICE) return false;
        MOX::player[s.victim].bc -= SALE_PRICE;
        MOX::player[s.holder].bc += SALE_PRICE;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d sells player %d a secret of player %d: %s",
            (int)MOX::stardate, (int)s.holder, (int)s.victim, (int)s.owner, Kind_Name_(s.kind));
        Tell_Victim_(s);
        s = s_secret{};
        return true;
    }

    bool Blackmail(int32_t index) {
        if (!enabled || index < 0 || index >= MAX_SECRETS) return false;
        s_secret& s = Slots()[index];
        if (s.kind == KIND_NONE) return false;
        s_player& owner = MOX::player[s.owner];
        const bool refuses = owner.objectives == PLAYER_OBJECTIVE_HUMAN || owner.bc < BLACKMAIL_MIN_BC ||
            opinion::Relations(s.owner, s.holder) <= BLACKMAIL_REFUSE_RELATIONS;
        if (refuses) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d refuses to pay player %d for its silence",
                (int)MOX::stardate, (int)s.owner, (int)s.holder);
            Make_Public(index);
            return false;
        }
        const int32_t price = owner.bc * BLACKMAIL_PERCENT / 100;
        owner.bc -= price;
        MOX::player[s.holder].bc += price;
        opinion::Add(s.owner, s.holder, opinion::REASON_BLACKMAILED_US, BLACKMAIL_OPINION);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d pays player %d %d BC for its silence about %s",
            (int)MOX::stardate, (int)s.owner, (int)s.holder, (int)price, Kind_Name_(s.kind));
        s = s_secret{};
        return true;
    }

    void Make_Public(int32_t index) {
        if (!enabled || index < 0 || index >= MAX_SECRETS) return;
        s_secret& s = Slots()[index];
        if (s.kind == KIND_NONE) return;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d makes public a secret of player %d: %s against player %d",
            (int)MOX::stardate, (int)s.holder, (int)s.owner, Kind_Name_(s.kind), (int)s.victim);
        Tell_Victim_(s);
        opinion::Add(s.owner, s.holder, opinion::REASON_EXPOSED_US, EXPOSED_OPINION);
        for (int16_t o = 0; o < Players_(); ++o) {
            if (o == s.owner || o == s.holder || o == s.victim || !Computer_(o) || MOX::player[o].contact[s.owner] == 0) continue;
            opinion::Add(o, s.owner, opinion::REASON_BAD_RUMOURS, RUMOUR_OPINION);
        }
        s = s_secret{};
    }

    bool Smear(int16_t attacker, int16_t target) {
        return Smear_With_Dice(attacker, target, nullptr);
    }

    bool Smear_With_Dice(int16_t attacker, int16_t target, const int32_t* dice_values) {
        if (!smear || !espionage::network || !Alive_(attacker) || !Alive_(target) || attacker == target) return false;
        const int32_t spies = Spies_(attacker, target);
        if (spies == 0 || !espionage::Spend_Points(attacker, target, smear_cost)) return false;
        int16_t offense = 0;
        int16_t defense = 0;
        spy::Compute_Spy_Bonuses(&MOX::player[attacker], &offense, &defense);
        const dice::s_modifier modifiers[3] = {{"spies", std::min<int32_t>(5, spies / 2)}, {"spy skill", offense / 10},
            {"resident", spy_ranks::Resident_Bonus(attacker, target)}};
        const int32_t difficulty = spy_ops::Difficulty(attacker, spy_ops::OP_FALSE_FLAG, target);
        const dice::s_check check = dice_values != nullptr
            ? dice::Resolve_Check(static_cast<dice::e_roll_kind>(dice::roll_kind), dice_values, modifiers, 3, difficulty)
            : dice::Roll_Check(modifiers, 3, difficulty);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "secrets: stardate %d, player %d smears player %d: %s",
            (int)MOX::stardate, (int)attacker, (int)target, dice::Describe_Check("smears", check, modifiers, 3).c_str());
        if (check.outcome >= dice::OUTCOME_SUCCESS) {
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o == attacker || o == target || !Computer_(o) || MOX::player[o].contact[target] == 0) continue;
                opinion::Add(o, target, opinion::REASON_BAD_RUMOURS, RUMOUR_OPINION);
            }
            return true;
        }
        if (check.outcome == dice::OUTCOME_EXPOSED || check.outcome == dice::OUTCOME_CATASTROPHE) {
            BILL::Set_Their_Spy_Number(attacker, target, static_cast<uint8_t>(spies - 1));
            opinion::Add(target, attacker, opinion::REASON_SMEARED_US, SMEARED_OPINION);
            espionage::On_Spy_Caught(target, attacker);
        }
        return false;
    }

    void Turn() {
        if (enabled) {
            Learn_All_();
            if (MOX::stardate % PERIOD == 9) Use_All_();
        }
        if (smear && MOX::stardate % PERIOD == 1) Smear_All_();
    }

} // namespace secrets
