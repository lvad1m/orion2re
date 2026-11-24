#include "pch.h"

#include "game/spy_ops.h"

#include "game/advisor.h"
#include "game/advisor_directive.h"
#include "game/advisor_intel.h"
#include "game/bill.h"
#include "game/bomb.h"
#include "game/colbldg.h"
#include "game/colcalc.h"
#include "game/counterspy.h"
#include "game/dice.h"
#include "game/espionage.h"
#include "game/intrigue.h"
#include "game/invasion.h"
#include "game/loyalty.h"
#include "game/officer.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/reputation.h"
#include "game/rivalry.h"
#include "game/spy.h"
#include "game/spy_ranks.h"
#include "game/stability.h"

#include <algorithm>

// The behaviour of spy operations (mechanics.md 3.2.5.3); the state and its save section are in
// spy_ops_state.cpp. game_random is drawn only by the operations' checks and the uncovering rolls.
namespace spy_ops {

    namespace {
        constexpr int8_t TREATY_FIRST_WAR = 4;
        constexpr int8_t STATUS_POOL = 0;
        constexpr int8_t STATUS_ASSIGNED = 1;
        constexpr int8_t STATUS_LIMBO = 2;
        constexpr int8_t LEADER_TYPE_COLONY = 1;
        constexpr int8_t SHIP_STATUS_FIRST_NOT_ALIVE = 5;
        constexpr int32_t EXPOSED_OPINION = -25;
        constexpr int32_t BRIBE_MIN_CHANCE = 25;  // percent: the computers bribe when the dice give at least this
        static const char* const NAMES[OP_COUNT] = {"stirs up unrest", "kills a scientist", "bribes a leader",
            "frames a leader", "collects data", "runs a false-flag operation", "assassinates a leader",
            "poisons a leader at a feast", "sabotages a shipyard", "sabotages research", "starves a colony",
            "sabotages trade", "strikes a colony", "poisons a colony's water"};

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

        int32_t Loyalty_Or_Default_(int16_t leader) {
            const int32_t value = loyalty::Of(leader);
            return value == loyalty::UNTRACKED ? 50 : value;
        }

        int16_t Scientist_(int16_t target) {
            int16_t best = -1;
            for (int16_t i = 0; i < MAX_LEADERS; ++i) {
                const s_leader_data& l = MOX::leaders[i];
                if (!Holds_(l, target)) continue;
                if ((l.special_skills & LEADER_COLONY_SKILL_SCIENCE_LEADER) == 0 && (l.general_skills & LEADER_GENERAL_SKILL_RESEARCHER) == 0) continue;
                if (best < 0 || OFFICER::Owned_Officer_Level(i) > OFFICER::Owned_Officer_Level(best)) best = i;
            }
            return best;
        }

        int16_t Bribe_Candidate_(int16_t target) {
            int16_t best = -1;
            for (int16_t i = 0; i < MAX_LEADERS; ++i) {
                const s_leader_data& l = MOX::leaders[i];
                if (!Holds_(l, target) || l.status != STATUS_ASSIGNED || *Bribed_Slot(i) >= 0) continue;
                if (best < 0 || Loyalty_Or_Default_(i) < Loyalty_Or_Default_(best)) best = i;
            }
            return best;
        }

        // The bribe's own difficulty: loyalty, level, fame or wealth make a leader harder to buy.
        int32_t Bribe_Extra_(int16_t leader) {
            const s_leader_data& l = MOX::leaders[leader];
            int32_t extra = (Loyalty_Or_Default_(leader) - 50) / 10 + OFFICER::Owned_Officer_Level(leader);
            if ((l.general_skills & (LEADER_GENERAL_SKILL_FAMOUS | LEADER_GENERAL_SKILL_MEGAWEALTH)) != 0) extra += 3;
            return extra;
        }

        int16_t Frame_Candidate_(int16_t target) {
            int16_t best = -1;
            for (int16_t i = 0; i < MAX_LEADERS; ++i) {
                const s_leader_data& l = MOX::leaders[i];
                if (!Holds_(l, target) || l.status == STATUS_LIMBO || *Immune_Slot(i) != 0) continue;
                if (best < 0 || OFFICER::Owned_Officer_Level(i) > OFFICER::Owned_Officer_Level(best)) best = i;
            }
            return best;
        }

        int16_t Unrest_Colony_(int16_t target) {
            int16_t best = -1;
            for (int16_t i = 0; i < MOX::NUM_COLONIES && i < MAX_COLONIES; ++i) {
                const s_colony& c = MOX::colony[i];
                if (c.owner != target || c.outpost_flag != 0 || c.buildings[BUILDING_CAPITOL] != 0) continue;
                if (best < 0 || c.n_pops > MOX::colony[best].n_pops) best = i;
            }
            return best;
        }

        bool Unstable_(int16_t target) {
            for (int16_t i = 0; i < MOX::NUM_COLONIES && i < MAX_COLONIES; ++i) {
                if (MOX::colony[i].owner == target && stability::Of(i) < 0) return true;
            }
            return false;
        }

        // The target's highest-level assigned leader that is not a scientist (assassination, poison).
        int16_t Assassination_Subject_(int16_t target) {
            int16_t best = -1;
            for (int16_t i = 0; i < MAX_LEADERS; ++i) {
                const s_leader_data& l = MOX::leaders[i];
                if (!Holds_(l, target) || l.status != STATUS_ASSIGNED) continue;
                if ((l.special_skills & LEADER_COLONY_SKILL_SCIENCE_LEADER) != 0 || (l.general_skills & LEADER_GENERAL_SKILL_RESEARCHER) != 0) continue;
                if (best < 0 || OFFICER::Owned_Officer_Level(i) > OFFICER::Owned_Officer_Level(best)) best = i;
            }
            return best;
        }

        // Treaty-level access to the target's table: non-aggression or alliance, or a trade treaty.
        bool Feast_Allowed_(int16_t a, int16_t t) {
            const int8_t treaty = MOX::player[a].treaty[t];
            return (treaty >= 1 && treaty < TREATY_FIRST_WAR) || MOX::player[a].trade_treaty[t] > 0;
        }

        bool Real_Colony_(const s_colony& c, int16_t owner) {
            return c.owner == owner && c.outpost_flag == 0;
        }

        // The target's ship-building colony with the most production spent.
        int16_t Shipyard_Colony_(int16_t target) {
            int16_t best = -1;
            for (int16_t i = 0; i < MOX::NUM_COLONIES && i < MAX_COLONIES; ++i) {
                const s_colony& c = MOX::colony[i];
                if (!Real_Colony_(c, target) || c.production_spent <= 0 || !COLBLDG::Colony_Production_Is_Ship(c.producing[0])) continue;
                if (best < 0 || c.production_spent > MOX::colony[best].production_spent) best = i;
            }
            return best;
        }

        // The most populous colony; `capital_ok` false skips the capitol and `min_pops` skips small ones.
        int16_t Populous_Colony_(int16_t target, bool capital_ok, int32_t min_pops) {
            int16_t best = -1;
            for (int16_t i = 0; i < MOX::NUM_COLONIES && i < MAX_COLONIES; ++i) {
                const s_colony& c = MOX::colony[i];
                if (!Real_Colony_(c, target) || (!capital_ok && c.buildings[BUILDING_CAPITOL] != 0) || c.n_pops < min_pops) continue;
                if (best < 0 || c.n_pops > MOX::colony[best].n_pops) best = i;
            }
            return best;
        }

        // The target's colony with the most industry.
        int16_t Strike_Colony_(int16_t target) {
            int16_t best = -1;
            for (int16_t i = 0; i < MOX::NUM_COLONIES && i < MAX_COLONIES; ++i) {
                const s_colony& c = MOX::colony[i];
                if (!Real_Colony_(c, target) || c.production[ECON_INDUSTRY] <= 0) continue;
                if (best < 0 || c.production[ECON_INDUSTRY] > MOX::colony[best].production[ECON_INDUSTRY]) best = i;
            }
            return best;
        }

        int32_t Freighters_Lost_(int16_t target) {
            const int32_t n = MOX::player[target].n_freighters;
            return n <= 0 ? 0 : std::min<int32_t>(n, std::max<int32_t>(2, n / 4));
        }

        opinion::e_reason Reason_(e_op op) {
            switch (op) {
                case OP_UNREST: return opinion::REASON_STIRRED_UNREST;
                case OP_KILL_SCIENTIST: return opinion::REASON_KILLED_OUR_SCIENTIST;
                case OP_BRIBERY: return opinion::REASON_BRIBED_OUR_LEADER;
                case OP_FRAME_LEADER: return opinion::REASON_FRAMED_OUR_LEADER;
                case OP_ASSASSINATION:
                case OP_POISON_FEAST: return opinion::REASON_TRIED_TO_KILL_OUR_LEADER;
                case OP_SABOTAGE_SHIPYARD:
                case OP_SABOTAGE_RESEARCH:
                case OP_SABOTAGE_FOOD:
                case OP_SABOTAGE_TRADE:
                case OP_STRIKE:
                case OP_POISON_WATER: return opinion::REASON_SABOTAGE;
                default: return opinion::REASON_CAUGHT_SPYING;
            }
        }

        // An empire other than `a` and `t`, in contact with `t`, that `a` thinks ill of: to blame.
        int16_t Scapegoat_(int16_t a, int16_t t) {
            const int16_t rival = rivalry::Rival_Of(t);
            if (rival >= 0 && rival != a && Alive_(rival)) return rival;
            for (int16_t k = 0; k < Players_(); ++k) {
                if (k == a || k == t || !Alive_(k) || MOX::player[t].contact[k] == 0) continue;
                if (opinion::Relations(a, k) < 0) return k;
            }
            return -1;
        }

        bool Is_Main_Rival_(int16_t a, int16_t t) {
            if (rivalry::Rival_Of(a) == t) return true;
            if (!advisor::enabled || ((advisor::active_players >> a) & 1) == 0) return false;
            const advisor::s_directive* d = advisor::Get_Directive(static_cast<uint8_t>(a));
            return d != nullptr && advisor::Is_Active(d, MOX::stardate) && d->main_rival == t;
        }

        // The computer's choice against `t` (OP_COUNT: none); `third` gets the scapegoat.
        e_op Choose_(int16_t a, int16_t t, int16_t* third) {
            if (espionage::Points(a, t) <= 0) return OP_COUNT;
            const auto ready = [&](e_op op) { return Enabled(op) && espionage::Points(a, t) >= Cost(op); };
            const bool at_war = MOX::player[a].treaty[t] >= TREATY_FIRST_WAR;
            const int32_t personality = MOX::player[a].personality;
            const bool ruthless = personality == PLAYER_PERSONALITY_RUTHLESS;
            const bool dark = ruthless || personality == PLAYER_PERSONALITY_XENOPHOBIC;
            if (at_war) {
                if (ready(OP_KILL_SCIENTIST) && Scientist_(t) >= 0) return OP_KILL_SCIENTIST;
                // Wave 2 (off unless their bits are set).
                if (dark && ready(OP_ASSASSINATION) && Assassination_Subject_(t) >= 0) return OP_ASSASSINATION;
                if (ruthless && ready(OP_POISON_WATER) && Populous_Colony_(t, false, POISON_WATER_MIN_POPS) >= 0) return OP_POISON_WATER;
                if (ready(OP_SABOTAGE_SHIPYARD) && Shipyard_Colony_(t) >= 0) return OP_SABOTAGE_SHIPYARD;
                if (ready(OP_SABOTAGE_RESEARCH) && MOX::player[t].research_accumulated > MOX::player[a].research_accumulated &&
                    MOX::player[t].research_accumulated >= 2) {
                    return OP_SABOTAGE_RESEARCH;
                }
                if (ready(OP_SABOTAGE_FOOD) && Populous_Colony_(t, true, 0) >= 0) return OP_SABOTAGE_FOOD;
                if (ready(OP_STRIKE) && Strike_Colony_(t) >= 0) return OP_STRIKE;
                if (ready(OP_SABOTAGE_TRADE) && Freighters_Lost_(t) > 0) return OP_SABOTAGE_TRADE;
                const bool democracy = MOX::player[t].traits[TRAIT_CURRENT_GOVERNMENT] / 2 == GOVERNMENT_DEMOCRACY / 2;
                if (ready(OP_UNREST) && (democracy || Unstable_(t)) && Unrest_Colony_(t) >= 0) return OP_UNREST;
                return ready(OP_DATA) ? OP_DATA : OP_COUNT;
            }
            if (dark && opinion::Relations(a, t) <= -50) {
                if (ready(OP_POISON_FEAST) && Feast_Allowed_(a, t) && Assassination_Subject_(t) >= 0) return OP_POISON_FEAST;
                if (ready(OP_ASSASSINATION) && Assassination_Subject_(t) >= 0) return OP_ASSASSINATION;
            }
            if (Is_Main_Rival_(a, t)) {
                const int16_t bribe = Bribe_Candidate_(t);
                if (ready(OP_BRIBERY) && bribe >= 0 &&
                    dice::Success_Chance_Percent(static_cast<dice::e_roll_kind>(dice::roll_kind), Bonus(a, t),
                        Difficulty(a, OP_BRIBERY, t) + Bribe_Extra_(bribe)) >= BRIBE_MIN_CHANCE) {
                    return OP_BRIBERY;
                }
                if (ready(OP_FRAME_LEADER) && espionage::Mode(t) != espionage::MODE_NORMAL && Frame_Candidate_(t) >= 0) {
                    return OP_FRAME_LEADER;
                }
                if (ready(OP_FALSE_FLAG)) {
                    *third = Scapegoat_(a, t);
                    if (*third >= 0) return OP_FALSE_FLAG;
                }
                if (ready(OP_SABOTAGE_FOOD) && Populous_Colony_(t, true, 0) >= 0) return OP_SABOTAGE_FOOD;
                if (ready(OP_STRIKE) && Strike_Colony_(t) >= 0) return OP_STRIKE;
                if (ready(OP_SABOTAGE_TRADE) && Freighters_Lost_(t) > 0) return OP_SABOTAGE_TRADE;
            }
            return ready(OP_DATA) ? OP_DATA : OP_COUNT;
        }

        void Uncover_Or_Hand_Over_() {
            for (int16_t i = 0; i < MAX_LEADERS; ++i) {
                int8_t* briber = Bribed_Slot(i);
                if (*briber < 0) continue;
                const s_leader_data& l = MOX::leaders[i];
                const int16_t owner = l.player_index;
                if (!Alive_(owner) || !Holds_(l, owner) || owner == *briber || !Alive_(*briber)) {
                    *briber = -1;
                    continue;
                }
                int32_t chance = UNCOVER_PER_MILLE[espionage::Mode(owner)];
                // Counter-agents at the governor's colonies watch it.
                for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                    if (MOX::colony[c].owner == owner && COLCALC::Colony_Officer(&MOX::colony[c]) == i) {
                        chance += spy_ranks::Uncover_Bonus(c);
                    }
                }
                if (static_cast<int32_t>(game_random::Random(1000)) <= chance) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "spy_ops: stardate %d, player %d uncovers its leader %d bribed by player %d",
                        (int)MOX::stardate, (int)owner, (int)i, (int)*briber);
                    opinion::Add(owner, *briber, opinion::REASON_BRIBED_OUR_LEADER, EXPOSED_OPINION);
                    *briber = -1;
                    OFFICER::Deassign_Officer(i);
                    continue;
                }
                // A bribed governor hands a colony over when the briber's warship stands at its star.
                if (l.type != LEADER_TYPE_COLONY || l.status != STATUS_ASSIGNED || l.location < 0) continue;
                const int16_t star = l.location;
                bool fleet_here = false;
                for (int16_t s = 0; s < MOX::NUM_SHIPS && !fleet_here; ++s) {
                    const s_ship_data& ship = MOX::ship[s];
                    fleet_here = ship.owner == *briber && ship.status < SHIP_STATUS_FIRST_NOT_ALIVE && ship.location == star;
                }
                if (!fleet_here) continue;
                for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                    s_colony& colony = MOX::colony[c];
                    if (colony.owner != owner || colony.outpost_flag != 0 || colony.buildings[BUILDING_CAPITOL] != 0) continue;
                    if (COLCALC::Colony_Officer(&colony) != i) continue;
                    const int16_t new_owner = *briber;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "spy_ops: stardate %d, player %d's bribed governor %d hands colony %d to player %d",
                        (int)MOX::stardate, (int)owner, (int)i, (int)c, (int)new_owner);
                    *briber = -1;
                    OFFICER::Deassign_Officer(i);
                    invasion::Change_Colony_Ownership(&colony, static_cast<uint32_t>(new_owner), nullptr);
                    break;
                }
            }
        }
    } // namespace

    int32_t Bonus(int16_t attacker, int16_t target) {
        if (!Alive_(attacker) || !Alive_(target)) return 0;
        int16_t offense = 0;
        int16_t defense = 0;
        spy::Compute_Spy_Bonuses(&MOX::player[attacker], &offense, &defense);  // the first output is the offence
        const int32_t spies = BILL::Get_Their_Spy_Number(attacker, target);
        return std::min<int32_t>(5, spies / 2) + offense / 10 + spy_ranks::Resident_Bonus(attacker, target);
    }

    int32_t Difficulty(int16_t attacker, e_op op, int16_t target) {
        if (!Alive_(attacker) || !Alive_(target)) return 0;
        int16_t offense = 0;
        int16_t defense = 0;
        spy::Compute_Spy_Bonuses(&MOX::player[target], &offense, &defense);  // the second output is the defence
        const espionage::e_mode mode = espionage::Mode(target);
        int32_t value = 10 + defense / 10 + BILL::Get_Their_Spy_Number(target, target) / 3;
        value += mode == espionage::MODE_WITCH_HUNT ? 4 : mode == espionage::MODE_HEIGHTENED ? 2 : 0;
        value += intrigue::Double_Agent_Difficulty(attacker, target);  // intrigues, off by default
        switch (op) {
            case OP_KILL_SCIENTIST: value += 2; break;
            case OP_DATA: value -= 2; break;
            case OP_FALSE_FLAG: value += 2; break;
            case OP_ASSASSINATION: value += ASSASSINATION_DIFFICULTY; break;
            case OP_SABOTAGE_SHIPYARD:
            case OP_SABOTAGE_RESEARCH:
            case OP_SABOTAGE_FOOD: value += 1; break;
            case OP_POISON_WATER: value += 2; break;
            case OP_FRAME_LEADER:  // paranoia makes framing easier
                value -= mode == espionage::MODE_WITCH_HUNT ? 4 : mode == espionage::MODE_HEIGHTENED ? 2 : 0;
                break;
            default: break;
        }
        value += counterspy::Guard_Difficulty(target, op);  // guarded leaders (off by default)
        return value;
    }

    e_result Execute(int16_t attacker, e_op op, int16_t target, int16_t third) {
        return Execute_With_Dice(attacker, op, target, third, nullptr);
    }

    e_result Execute_With_Dice(int16_t attacker, e_op op, int16_t target, int16_t third, const int32_t* dice_values) {
        if (!espionage::network || op >= OP_COUNT || !Enabled(op) || !Alive_(attacker) || !Alive_(target) || attacker == target) {
            return RESULT_NOT_POSSIBLE;
        }
        const int32_t spies = BILL::Get_Their_Spy_Number(attacker, target);
        if (spies == 0) return RESULT_NOT_POSSIBLE;
        int16_t subject = -1;
        int32_t extra = 0;
        switch (op) {
            case OP_UNREST: subject = Unrest_Colony_(target); break;
            case OP_KILL_SCIENTIST: subject = Scientist_(target); break;
            case OP_BRIBERY:
                subject = Bribe_Candidate_(target);
                if (subject >= 0) extra = Bribe_Extra_(subject);
                break;
            case OP_FRAME_LEADER: subject = Frame_Candidate_(target); break;
            case OP_DATA: subject = target; break;
            case OP_FALSE_FLAG: subject = Alive_(third) && third != attacker && third != target ? third : -1; break;
            case OP_ASSASSINATION: subject = Assassination_Subject_(target); break;
            case OP_POISON_FEAST: subject = Feast_Allowed_(attacker, target) ? Assassination_Subject_(target) : -1; break;
            case OP_SABOTAGE_SHIPYARD: subject = Shipyard_Colony_(target); break;
            case OP_SABOTAGE_RESEARCH: subject = MOX::player[target].research_accumulated >= 2 ? target : -1; break;
            case OP_SABOTAGE_FOOD: subject = Populous_Colony_(target, true, 0); break;
            case OP_SABOTAGE_TRADE: subject = Freighters_Lost_(target) > 0 ? target : -1; break;
            case OP_STRIKE: subject = Strike_Colony_(target); break;
            case OP_POISON_WATER: subject = Populous_Colony_(target, false, POISON_WATER_MIN_POPS); break;
            default: break;
        }
        if (subject < 0) return RESULT_NOT_POSSIBLE;
        if (!espionage::Spend_Points(attacker, target, Cost(op))) return RESULT_NOT_POSSIBLE;

        int16_t offense = 0;
        int16_t defense = 0;
        spy::Compute_Spy_Bonuses(&MOX::player[attacker], &offense, &defense);
        const dice::s_modifier modifiers[3] = {{"spies", std::min<int32_t>(5, spies / 2)}, {"spy skill", offense / 10},
            {"resident", spy_ranks::Resident_Bonus(attacker, target)}};
        const int32_t difficulty = Difficulty(attacker, op, target) + extra;
        const dice::s_check check = dice_values != nullptr
            ? dice::Resolve_Check(static_cast<dice::e_roll_kind>(dice::roll_kind), dice_values, modifiers, 3, difficulty)
            : dice::Roll_Check(modifiers, 3, difficulty);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "spy_ops: stardate %d, player %d %s against player %d: %s",
            (int)MOX::stardate, (int)attacker, NAMES[op], (int)target, dice::Describe_Check(NAMES[op], check, modifiers, 3).c_str());

        if (check.outcome >= dice::OUTCOME_SUCCESS) {
            // Stains and titles (off by default): harm counts toward the corsair; a scientist killed
            // without a brilliant success leaves traces - a kingslayer.
            if (op != OP_BRIBERY && op != OP_DATA) reputation::Note_Deed(attacker);
            if ((op == OP_KILL_SCIENTIST || op == OP_ASSASSINATION || op == OP_POISON_FEAST) && check.outcome == dice::OUTCOME_SUCCESS) {
                reputation::On_Leader_Killed(attacker);
            }
            counterspy::On_Attempt(target, op);  // guards (off by default)
            switch (op) {
                case OP_UNREST: {
                    uint8_t* unrest = Unrest_Slot(subject);
                    *unrest = static_cast<uint8_t>(std::min<int32_t>(UNREST_MAX, *unrest + UNREST_PER_OPERATION));
                    break;
                }
                case OP_KILL_SCIENTIST:
                case OP_ASSASSINATION:
                case OP_POISON_FEAST: OFFICER::Kill_Officer(subject); break;
                case OP_SABOTAGE_SHIPYARD:
                    MOX::colony[subject].production_spent = 0;
                    *Stall_Slot(subject) = static_cast<uint8_t>(SABOTAGE_TURNS);
                    break;
                case OP_STRIKE: *Stall_Slot(subject) = static_cast<uint8_t>(SABOTAGE_TURNS); break;
                case OP_SABOTAGE_RESEARCH: MOX::player[subject].research_accumulated -= MOX::player[subject].research_accumulated / 2; break;
                case OP_SABOTAGE_FOOD:
                    *Famine_Slot(subject) = static_cast<uint8_t>(SABOTAGE_TURNS);
                    COLCALC::Colony_Calculation(subject);
                    break;
                case OP_SABOTAGE_TRADE:
                    MOX::player[subject].n_freighters = static_cast<int16_t>(MOX::player[subject].n_freighters - Freighters_Lost_(subject));
                    break;
                case OP_POISON_WATER: {
                    s_combat_bomb_result result;
                    memset(&result, 0, sizeof(result));
                    result.attacker_idx = -1;
                    result.colony_idx = subject;
                    result.is_planetary_hit = 1;
                    result.is_bio_weapon = 1;
                    result.bio_hits_remaining = static_cast<int16_t>(POISON_WATER_BIO_HITS);
                    BOMB::Apply_Bomb_Hits(&result);
                    COLCALC::Colony_Calculation(subject);
                    break;
                }
                case OP_BRIBERY: *Bribed_Slot(subject) = static_cast<int8_t>(attacker); break;
                case OP_FRAME_LEADER: OFFICER::Deassign_Officer(subject); break;
                case OP_DATA:
                    for (int16_t s = 0; s < MOX::NUM_SHIPS; ++s) {
                        const s_ship_data& ship = MOX::ship[s];
                        if (ship.owner == target && ship.status < SHIP_STATUS_FIRST_NOT_ALIVE) {
                            advisor::Observe_Design(static_cast<uint8_t>(attacker), static_cast<uint8_t>(target), ship.d);
                        }
                    }
                    break;
                case OP_FALSE_FLAG: spy::Destroy_Random_Building(attacker, target, subject); break;
                default: break;
            }
            return RESULT_DONE;
        }
        if (check.outcome != dice::OUTCOME_EXPOSED && check.outcome != dice::OUTCOME_CATASTROPHE) return RESULT_FAILED;
        BILL::Set_Their_Spy_Number(attacker, target, static_cast<uint8_t>(spies - 1));
        // An interrogated spy of a false-flag operation names the framed empire (off by default).
        const int16_t blamed = counterspy::Blamed(target, attacker, op == OP_FALSE_FLAG ? subject : -1);
        opinion::Add(target, blamed, Reason_(op), EXPOSED_OPINION);
        counterspy::On_Attempt(target, op);
        espionage::On_Spy_Caught(target, attacker);
        if (op == OP_FRAME_LEADER) *Immune_Slot(subject) = 1;
        if (op == OP_POISON_FEAST || op == OP_POISON_WATER) reputation::On_Atrocity(attacker);
        if (op == OP_POISON_FEAST) opinion::Add(target, attacker, opinion::REASON_TRIED_TO_KILL_OUR_LEADER, EXPOSED_OPINION);
        return RESULT_EXPOSED;
    }

    int32_t Morale_Effect(const s_colony* colony) {
        if (!Enabled(OP_BRIBERY) || colony == nullptr) return 0;
        const int officer = COLCALC::Colony_Officer(const_cast<s_colony*>(colony));
        return officer >= 0 && Bribed_By(static_cast<int16_t>(officer)) >= 0 ? BRIBED_GOVERNOR_MORALE : 0;
    }

    int16_t Inside_Man_Troops(const s_colony* colony, int16_t invader, int16_t troops) {
        if (!inside_man || colony == nullptr || colony->planet < 0 || !Enabled(OP_BRIBERY)) return troops;
        const int officer = COLCALC::Colony_Officer(const_cast<s_colony*>(colony));
        if (officer < 0 || Bribed_By(static_cast<int16_t>(officer)) != invader) return troops;
        return static_cast<int16_t>(troops * INSIDE_MAN_NUMERATOR / INSIDE_MAN_DENOMINATOR);
    }

    bool Inside_Man_Spend(s_colony* colony, int16_t invader) {
        if (Inside_Man_Troops(colony, invader, 2) == 2) return false;  // not the briber's man
        const int officer = COLCALC::Colony_Officer(colony);
        *Bribed_Slot(static_cast<int16_t>(officer)) = -1;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "spy_ops: stardate %d, player %d's bribed governor %d opens the gates of colony %d to player %d",
            (int)MOX::stardate, (int)colony->owner, officer, (int)(colony - MOX::colony), (int)invader);
        OFFICER::Deassign_Officer(static_cast<int16_t>(officer));
        return true;
    }

    void Turn() {
        if (operations == 0) return;
        for (int16_t c = 0; c < MAX_COLONIES; ++c) {
            uint8_t* unrest = Unrest_Slot(c);
            if (*unrest > 0) --*unrest;
            uint8_t* famine = Famine_Slot(c);
            if (*famine > 0) --*famine;
            uint8_t* stall = Stall_Slot(c);
            if (*stall > 0) {
                --*stall;
                if (c < MOX::NUM_COLONIES) {
                    s_colony& colony = MOX::colony[c];
                    colony.production_spent = static_cast<int16_t>(std::max<int32_t>(0, colony.production_spent - colony.production[ECON_INDUSTRY]));
                }
            }
        }
        if (Enabled(OP_BRIBERY)) Uncover_Or_Hand_Over_();
        if (!espionage::network || MOX::stardate % AI_PERIOD != 0) return;
        for (int16_t a = 0; a < Players_(); ++a) {
            if (!Computer_(a)) continue;
            for (int16_t t = 0; t < Players_(); ++t) {
                if (t == a || !Alive_(t) || MOX::player[a].contact[t] == 0 || BILL::Get_Their_Spy_Number(a, t) == 0) continue;
                int16_t third = -1;
                const e_op op = Choose_(a, t, &third);
                if (op != OP_COUNT) Execute(a, op, t, third);
            }
        }
    }

} // namespace spy_ops
