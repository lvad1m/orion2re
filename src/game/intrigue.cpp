#include "pch.h"

#include "game/intrigue.h"

#include "game/advisor.h"
#include "game/advisor_directive.h"
#include "game/beliefs.h"
#include "game/bill.h"
#include "game/counterspy.h"
#include "game/dice.h"
#include "game/diplomac.h"
#include "game/diplomacy_memory.h"
#include "game/espionage.h"
#include "game/opinion.h"
#include "game/pacts.h"
#include "game/random.h"
#include "game/reputation.h"
#include "game/rivalry.h"
#include "game/spy.h"
#include "game/spy_ops.h"
#include "game/spy_ranks.h"

#include <algorithm>
#include <climits>
#include <initializer_list>

// The behaviour of the intrigues (mechanics.md 3.5.9); the state and its save section are in
// intrigue_state.cpp. game_random is drawn only by the parts that are on: the reveal rolls, the
// turn of a caught spy, the forged letter's dice and the wars it leads to (Declare_War).
namespace intrigue {

    namespace {
        constexpr int8_t TREATY_ALLIANCE = 2;
        constexpr int8_t TREATY_FIRST_WAR = 4;
        constexpr int32_t HOME_SPY_LIMIT = 250;

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
            return MOX::player[a].contact[b] != 0;
        }

        // An alliance, a defence pact or a guarantee either way.
        bool Bound_(int16_t a, int16_t b) {
            return MOX::player[a].treaty[b] == TREATY_ALLIANCE || pacts::Has_Pact(a, b) || pacts::Guarantees(a, b) ||
                pacts::Guarantees(b, a);
        }

        bool Advised_(int16_t player) {
            return advisor::enabled && ((advisor::active_players >> player) & 1) != 0;
        }

        const advisor::s_directive* Active_Directive_(int16_t player) {
            if (!Advised_(player)) return nullptr;
            const advisor::s_directive* directive = advisor::Get_Directive(static_cast<uint8_t>(player));
            return directive != nullptr && advisor::Is_Active(directive, MOX::stardate) ? directive : nullptr;
        }

        // An advised empire told to be an ally or a friend of `other` starts no war on it.
        bool Advisor_Forbids_War_(int16_t player, int16_t other) {
            const advisor::s_directive* directive = Active_Directive_(player);
            if (directive == nullptr) return false;
            const uint8_t attitude = directive->attitude[other];
            return attitude == advisor::ATTITUDE_ALLY || attitude == advisor::ATTITUDE_FRIENDLY;
        }

        int32_t Spies_(int16_t owner, int16_t target) {
            return BILL::Get_Their_Spy_Number(owner, target);
        }

        void Add_Home_Spies_(int16_t owner, int32_t count) {
            if (count <= 0) return;
            const int32_t home = Spies_(owner, owner);
            BILL::Set_Their_Spy_Number(owner, owner, static_cast<uint8_t>(std::min(HOME_SPY_LIMIT, home + count)));
        }

        // `victim` learns of `plotter`'s plan, from its own spies (warner -1) or from `warner`.
        void Expose_(int16_t plotter, int16_t victim, int16_t warner) {
            counterspy::On_Secret_Leaked(plotter);  // the plotter suspects a mole, sets a canary trap (off by default)
            State()->revealed[plotter][victim] = MOX::stardate;
            opinion::Add(victim, plotter, opinion::REASON_PLOTTED_AGAINST_US, PLOTTED_OPINION);
            s_player& p = MOX::player[plotter];
            const bool surprise_lost = p.sneak_attack_planet != -1 && p.sneak_attack_player == victim;
            if (surprise_lost) {
                p.sneak_attack_planet = -1;
                p.sneak_attack_player = -1;
                p.war_buildup_duration = 0;
            }
            if (warner >= 0) {
                opinion::Add(victim, warner, opinion::REASON_DID_US_A_SERVICE, SERVICE_OPINION);
                Owe(victim, warner);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "intrigue: stardate %d, player %d warns player %d that player %d plans to attack it%s",
                    (int)MOX::stardate, (int)warner, (int)victim, (int)plotter, surprise_lost ? "; the surprise is lost" : "");
            } else {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "intrigue: stardate %d, player %d's spies uncover player %d's plan to attack it%s",
                    (int)MOX::stardate, (int)victim, (int)plotter, surprise_lost ? "; the surprise is lost" : "");
            }
        }

        void Reveal_Plans_() {
            for (int16_t plotter = 0; plotter < Players_(); ++plotter) {
                if (!Alive_(plotter)) continue;
                int16_t victims[MAX_PLAYERS];
                const int32_t count = Planned_Victims(plotter, victims);
                for (int32_t i = 0; i < count; ++i) {
                    const int16_t victim = victims[i];
                    const int32_t last = State()->revealed[plotter][victim];
                    if (last != 0 && MOX::stardate - last < REVEAL_MEMORY) continue;
                    for (int16_t s = 0; s < Players_(); ++s) {
                        if (s == plotter || !Computer_(s)) continue;
                        const int32_t sources = std::min(5, Spies_(s, plotter)) + (Has_Double_Agent(s, plotter) ? 1 : 0);
                        if (sources == 0) continue;
                        if (s != victim) {
                            // A third empire keeps an ally's secret and warns only an empire it likes,
                            // unless it is at war with the plotter or its rival; a warner the victim
                            // thinks a liar is not believed (the neighbour model).
                            if (Bound_(s, plotter) || !Contact_(s, victim) || At_War_(s, victim)) continue;
                            if (Computer_(victim) && !beliefs::Trusts(victim, s)) continue;
                            if (opinion::Relations(s, victim) < WARN_MIN_RELATIONS && !At_War_(s, plotter) &&
                                rivalry::Rival_Of(s) != plotter) {
                                continue;
                            }
                        }
                        const int32_t chance = reveal_chance * sources + 100 * spy_ranks::Residents(s, plotter);
                        if (static_cast<int32_t>(game_random::Random(1000)) > chance) continue;
                        Expose_(plotter, victim, s == victim ? -1 : s);
                        break;
                    }
                }
            }
        }

        void Proxy_Wars_() {
            for (int16_t payer = 0; payer < Players_(); ++payer) {
                if (!Computer_(payer) || MOX::player[payer].bc < PROXY_MIN_PAYMENT) continue;
                const advisor::s_directive* directive = Active_Directive_(payer);
                if (directive != nullptr && directive->posture == advisor::POSTURE_MAKE_PEACE) continue;
                for (int16_t enemy = 0; enemy < Players_(); ++enemy) {
                    if (enemy == payer || !Alive_(enemy) || !At_War_(payer, enemy)) continue;
                    int16_t best = -1;
                    int32_t best_relations = PROXY_MAX_RELATIONS + 1;
                    for (int16_t t = 0; t < Players_(); ++t) {
                        if (t == payer || t == enemy || !Computer_(t)) continue;
                        if (!Contact_(t, payer) || !Contact_(t, enemy)) continue;
                        if (At_War_(t, enemy) || At_War_(t, payer) || Bound_(t, enemy)) continue;
                        if (diplomemory::Too_Weary_For_New_War(t) || Advisor_Forbids_War_(t, enemy)) continue;
                        if (opinion::Relations(t, payer) < 0 || !beliefs::Trusts(t, payer)) continue;
                        const int32_t relations = opinion::Relations(t, enemy);
                        if (relations <= PROXY_MAX_RELATIONS && relations < best_relations) {
                            best = t;
                            best_relations = relations;
                        }
                    }
                    if (best < 0) continue;
                    const int32_t payment = std::max(PROXY_MIN_PAYMENT, MOX::player[payer].bc * proxy_payment_percent / 100);
                    if (Hire_Proxy(payer, best, enemy, payment)) break;  // one deal a period
                }
            }
        }

        void Forged_Letters_() {
            if (!espionage::network) return;
            for (int16_t forger = 0; forger < Players_(); ++forger) {
                if (!Computer_(forger)) continue;
                bool done = false;
                for (int16_t target = 0; target < Players_() && !done; ++target) {
                    if (target == forger || !Alive_(target) || Spies_(forger, target) == 0) continue;
                    if (espionage::Points(forger, target) < forged_letter_cost) continue;
                    for (int16_t other = 0; other < Players_() && !done; ++other) {
                        if (other == forger || other == target || !Alive_(other) || !Bound_(target, other)) continue;
                        const int16_t rival = rivalry::Rival_Of(forger);
                        const bool wants_them_apart = At_War_(forger, target) || At_War_(forger, other) || rival == target ||
                            rival == other || opinion::Relations(forger, target) < 0 || opinion::Relations(forger, other) < 0;
                        if (!wants_them_apart) continue;
                        Forge_Letter(forger, target, other);
                        done = true;
                    }
                }
            }
        }

        void Call_Favours_() {
            for (int16_t creditor = 0; creditor < Players_(); ++creditor) {
                if (!Computer_(creditor)) continue;
                for (int16_t enemy = 0; enemy < Players_(); ++enemy) {
                    if (enemy == creditor || !Alive_(enemy) || !At_War_(creditor, enemy)) continue;
                    bool called = false;
                    for (int16_t debtor = 0; debtor < Players_(); ++debtor) {
                        if (debtor == creditor || debtor == enemy || !Computer_(debtor)) continue;
                        uint8_t& owed = State()->favours[debtor][creditor];
                        if (owed == 0) continue;
                        if (At_War_(debtor, enemy) || !Contact_(debtor, enemy) || Bound_(debtor, enemy) || At_War_(debtor, creditor)) continue;
                        called = true;
                        --owed;
                        const bool joins = opinion::Relations(debtor, enemy) < FAVOUR_WAR_MAX_RELATIONS &&
                            !diplomemory::Too_Weary_For_New_War(debtor) && !Advisor_Forbids_War_(debtor, enemy);
                        if (joins) {
                            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "intrigue: stardate %d, player %d pays its debt to player %d: war on player %d",
                                (int)MOX::stardate, (int)debtor, (int)creditor, (int)enemy);
                            diplomac::Declare_War(debtor, enemy);
                            opinion::Add(creditor, debtor, opinion::REASON_DID_US_A_SERVICE, SERVICE_OPINION);
                        } else {
                            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "intrigue: stardate %d, player %d refuses player %d's call for war on player %d",
                                (int)MOX::stardate, (int)debtor, (int)creditor, (int)enemy);
                            opinion::Add(creditor, debtor, opinion::REASON_ABANDONED_US, REFUSED_OPINION);
                        }
                    }
                    if (called) break;  // one call a period
                }
            }
        }

        void Settle_Caught_Spies_() {
            int32_t count = 0;
            const s_caught* queue = Caught_Queue(&count);
            for (int32_t i = 0; i < count; ++i) {
                const int16_t victim = queue[i].victim;
                const int16_t attacker = queue[i].attacker;
                if (!Alive_(victim) || !Alive_(attacker)) continue;
                e_fate fate = Decide_Fate(victim, attacker, INT32_MAX);
                if (fate == FATE_EXPELLED) fate = Decide_Fate(victim, attacker, static_cast<int32_t>(game_random::Random(100)));
                Apply_Fate(victim, attacker, fate);
            }
            Clear_Caught_Queue();
        }
    } // namespace

    int32_t Planned_Victims(int16_t plotter, int16_t* victims) {
        int32_t count = 0;
        if (!Alive_(plotter) || victims == nullptr) return 0;
        const auto add = [&](int32_t v) {
            if (v < 0 || v >= Players_() || v == plotter || !Alive_(static_cast<int16_t>(v)) || At_War_(plotter, static_cast<int16_t>(v))) return;
            for (int32_t i = 0; i < count; ++i) {
                if (victims[i] == v) return;
            }
            victims[count++] = static_cast<int16_t>(v);
        };
        const s_player& p = MOX::player[plotter];
        if (p.sneak_attack_planet != -1) add(p.sneak_attack_player);
        const advisor::s_directive* directive = Active_Directive_(plotter);
        if (directive != nullptr) {
            if (directive->posture == advisor::POSTURE_START_NEW_WAR && directive->main_rival != advisor::NO_PLAYER) {
                add(directive->main_rival);
            }
            for (int16_t v = 0; v < Players_(); ++v) {
                if (directive->attitude[v] == advisor::ATTITUDE_WAR) add(v);
            }
        }
        return count;
    }

    void Owe(int16_t debtor, int16_t creditor) {
        if (!favours || !Alive_(debtor) || !Alive_(creditor) || debtor == creditor) return;
        uint8_t& owed = State()->favours[debtor][creditor];
        if (owed >= FAVOUR_LIMIT) return;
        ++owed;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "intrigue: stardate %d, player %d owes player %d a favour (%d)",
            (int)MOX::stardate, (int)debtor, (int)creditor, (int)owed);
    }

    int32_t Council_Vote_Bias(int16_t voter, int16_t candidate) {
        return Favours(voter, candidate) > 0 ? favour_vote_bonus : 0;
    }

    void On_Council_Vote(int16_t voter, int16_t voted_for, int16_t nominee0, int16_t nominee1) {
        if (!favours || voter < 0 || voter >= MAX_PLAYERS) return;
        for (const int16_t nominee : {nominee0, nominee1}) {
            if (nominee < 0 || nominee >= MAX_PLAYERS || nominee == voter) continue;
            uint8_t& owed = State()->favours[voter][nominee];
            if (owed == 0) continue;
            --owed;
            if (voted_for == nominee) {
                opinion::Add(nominee, voter, opinion::REASON_DID_US_A_SERVICE, SERVICE_OPINION);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "intrigue: stardate %d, player %d pays its debt to player %d with its council vote",
                    (int)MOX::stardate, (int)voter, (int)nominee);
            } else {
                opinion::Add(nominee, voter, opinion::REASON_ABANDONED_US, REFUSED_OPINION);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "intrigue: stardate %d, player %d owes player %d a favour and denies it its council vote",
                    (int)MOX::stardate, (int)voter, (int)nominee);
            }
        }
    }

    e_fate Decide_Fate(int16_t victim, int16_t attacker, int32_t turn_roll) {
        if (!spy_fate || !Computer_(victim) || !Alive_(attacker) || victim == attacker) return FATE_CLASSIC;
        const int32_t relations = opinion::Relations(victim, attacker);
        if (At_War_(victim, attacker) || relations <= FATE_EXECUTE_RELATIONS) return FATE_EXECUTED;
        if (relations >= FATE_RETURN_RELATIONS) return FATE_SENT_HOME;
        if (MOX::player[victim].tech_applications[TECH_APP_SPY_NETWORK] == TECH_RESEARCH_STATUS_RESEARCHED &&
            !Has_Double_Agent(victim, attacker) && turn_roll <= FATE_TURN_PERCENT) {
            return FATE_TURNED;
        }
        return FATE_EXPELLED;
    }

    void Apply_Fate(int16_t victim, int16_t attacker, e_fate fate) {
        if (!Alive_(victim) || !Alive_(attacker) || victim == attacker) return;
        const char* what = nullptr;
        switch (fate) {
            case FATE_EXECUTED:
                opinion::Add(attacker, victim, opinion::REASON_TREATED_OUR_SPY, EXECUTION_OPINION);
                what = "executes a spy of";
                break;
            case FATE_EXPELLED: {
                const int32_t spies = Spies_(attacker, victim);
                BILL::Set_Their_Spy_Number(attacker, victim, 0);
                Add_Home_Spies_(attacker, spies);
                if (Computer_(attacker)) {
                    int8_t& pause = MOX::player[attacker].stop_spying_duration[victim];
                    pause = static_cast<int8_t>(std::max<int32_t>(pause, EXPEL_TURNS));
                }
                what = "expels every spy of";
                break;
            }
            case FATE_SENT_HOME:
                Add_Home_Spies_(attacker, 1);
                opinion::Add(attacker, victim, opinion::REASON_TREATED_OUR_SPY, RETURN_OPINION);
                Owe(attacker, victim);
                what = "sends home a spy of";
                break;
            case FATE_TURNED:
                State()->double_agent_until[victim][attacker] = MOX::stardate + DOUBLE_AGENT_TURNS;
                what = "turns a spy of";
                break;
            default: return;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "intrigue: stardate %d, player %d %s player %d",
            (int)MOX::stardate, (int)victim, what, (int)attacker);
    }

    bool Forge_Letter(int16_t forger, int16_t target, int16_t other) {
        return Forge_Letter_With_Dice(forger, target, other, nullptr);
    }

    bool Forge_Letter_With_Dice(int16_t forger, int16_t target, int16_t other, const int32_t* dice_values) {
        if (!forged_letters || !espionage::network || !Alive_(forger) || !Alive_(target) || !Alive_(other)) return false;
        if (forger == target || forger == other || target == other) return false;
        const int32_t spies = Spies_(forger, target);
        if (spies == 0) return false;
        if (!espionage::Spend_Points(forger, target, forged_letter_cost)) return false;
        int16_t offense = 0;
        int16_t defense = 0;
        spy::Compute_Spy_Bonuses(&MOX::player[forger], &offense, &defense);
        const dice::s_modifier modifiers[3] = {{"spies", std::min<int32_t>(5, spies / 2)}, {"spy skill", offense / 10},
            {"resident", spy_ranks::Resident_Bonus(forger, target)}};
        const int32_t difficulty = spy_ops::Difficulty(forger, spy_ops::OP_FALSE_FLAG, target);
        const dice::s_check check = dice_values != nullptr
            ? dice::Resolve_Check(static_cast<dice::e_roll_kind>(dice::roll_kind), dice_values, modifiers, 3, difficulty)
            : dice::Roll_Check(modifiers, 3, difficulty);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "intrigue: stardate %d, player %d forges a letter to player %d about player %d: %s",
            (int)MOX::stardate, (int)forger, (int)target, (int)other,
            dice::Describe_Check("forges a letter", check, modifiers, 3).c_str());
        if (check.outcome >= dice::OUTCOME_SUCCESS) {
            opinion::Add(target, other, opinion::REASON_PLOTTED_AGAINST_US, FORGED_OPINION);
            opinion::Add(other, target, opinion::REASON_PLOTTED_AGAINST_US, FORGED_OPINION);
            // Only a computer acts on the letter; a human decides for itself.
            if (Computer_(target) && opinion::Relations(target, other) <= FORGED_BREAK_RELATIONS) {
                if (MOX::player[target].treaty[other] == TREATY_ALLIANCE) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "intrigue: stardate %d, player %d breaks its alliance with player %d over a forged letter",
                        (int)MOX::stardate, (int)target, (int)other);
                    diplomac::Break_Treaties(target, other);
                }
                pacts::End_Pact(target, other, "a forged letter");
            }
            return true;
        }
        if (check.outcome == dice::OUTCOME_EXPOSED || check.outcome == dice::OUTCOME_CATASTROPHE) {
            BILL::Set_Their_Spy_Number(forger, target, static_cast<uint8_t>(spies - 1));
            opinion::Add(target, forger, opinion::REASON_PLOTTED_AGAINST_US, PLOTTED_OPINION);
            opinion::Add(other, forger, opinion::REASON_PLOTTED_AGAINST_US, PLOTTED_OPINION);
            espionage::On_Spy_Caught(target, forger);
        }
        return false;
    }

    bool Hire_Proxy(int16_t payer, int16_t hired, int16_t enemy, int32_t payment) {
        if (!proxy_wars || !Alive_(payer) || !Alive_(hired) || !Alive_(enemy)) return false;
        if (payer == hired || payer == enemy || hired == enemy || payment <= 0) return false;
        if (MOX::player[payer].bc < payment || At_War_(hired, enemy)) return false;
        MOX::player[payer].bc -= payment;
        MOX::player[hired].bc += payment;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "intrigue: stardate %d, player %d pays %d BC to player %d to attack player %d",
            (int)MOX::stardate, (int)payer, (int)payment, (int)hired, (int)enemy);
        diplomac::Declare_War(hired, enemy);
        reputation::Note_Deed(payer);  // stains and titles (off by default)
        if (Spies_(enemy, payer) > 0 || Spies_(enemy, hired) > 0) {
            opinion::Add(enemy, payer, opinion::REASON_PLOTTED_AGAINST_US, PLOTTED_OPINION);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "intrigue: stardate %d, player %d learns that player %d paid for the war",
                (int)MOX::stardate, (int)enemy, (int)payer);
        }
        return true;
    }

    void Turn() {
        if (!Any_Enabled()) {
            Clear_Caught_Queue();
            return;
        }
        if (spy_fate) {
            Settle_Caught_Spies_();
        } else {
            Clear_Caught_Queue();
        }
        if (reveal_plans) Reveal_Plans_();
        const int32_t phase = MOX::stardate % PERIOD;
        if (proxy_wars && phase == 3) Proxy_Wars_();
        if (forged_letters && phase == 6) Forged_Letters_();
        if (favours && phase == 8) Call_Favours_();
    }

} // namespace intrigue
