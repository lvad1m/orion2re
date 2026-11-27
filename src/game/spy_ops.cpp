#include "pch.h"

#include "game/spy_ops.h"

#include "game/advisor.h"
#include "game/advisor_directive.h"
#include "game/advisor_intel.h"
#include "game/bill.h"
#include "game/bomb.h"
#include "game/colbldg.h"
#include "game/colcalc.h"
#include "game/hostages.h"
#include "game/counterspy.h"
#include "game/dice.h"
#include "game/ericnet.h"
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
            return n <= 0 ? 0 : std::min<int32_t>(n, std::max<int32_t>(TRADE_LOSS_MIN, n * TRADE_LOSS_PERCENT / 100));
        }

        // Iteration 3: a killed leader leaves a mark ("убийство лидера оставляет заметный след (без адмирала
        // флот слабеет, без губернатора колония в смуте)"). Called before the kill, for assassination and
        // poison at a feast. A governor: unrest and mourning in the colonies he governed. A ship officer:
        // the crew experience of his ship and the ships of his group (same owner and position) is cut,
        // the level recomputed (the combat formulas are untouched). No dice.
        void Leave_Mark_(int16_t leader) {
            const s_leader_data& l = MOX::leaders[leader];
            if (l.status != STATUS_ASSIGNED || l.location < 0) return;
            if (l.type == LEADER_TYPE_COLONY) {
                for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                    s_colony& colony = MOX::colony[c];
                    if (colony.owner != l.player_index || colony.outpost_flag != 0 || COLCALC::Colony_Officer(&colony) != leader) continue;
                    uint8_t* unrest = Unrest_Slot(c);
                    *unrest = static_cast<uint8_t>(std::min<int32_t>(UNREST_MAX, *unrest + GOVERNOR_UNREST));
                    *Mourn_Slot(c) = static_cast<uint8_t>(MOURN_TURNS);
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "spy_ops: stardate %d, colony %d of player %d mourns its governor %d",
                        (int)MOX::stardate, (int)c, (int)colony.owner, (int)leader);
                }
                return;
            }
            if (l.location >= MOX::NUM_SHIPS || l.location >= MAX_SHIPS) return;
            const s_ship_data lead = MOX::ship[l.location];
            for (int16_t s = 0; s < MOX::NUM_SHIPS && s < MAX_SHIPS; ++s) {
                s_ship_data& ship = MOX::ship[s];
                const bool grouped = s == l.location ||
                    (ship.location == lead.location && ship.x == lead.x && ship.y == lead.y);
                if (ship.owner != lead.owner || ship.status >= SHIP_STATUS_FIRST_NOT_ALIVE || !grouped || ship.crew_experience == 0) continue;
                const int32_t lost = ship.crew_experience * SHIP_XP_LOSS_PERCENT / 100;
                ship.crew_experience = static_cast<uint16_t>(ship.crew_experience - lost);
                ERICNET::Calc_Ship_Level(s);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "spy_ops: stardate %d, ship %d of player %d loses %d crew experience with officer %d",
                    (int)MOX::stardate, (int)s, (int)ship.owner, (int)lost, (int)leader);
            }
        }

        // Iteration 3: the computers rotate among the sabotage kinds that can bite now (a stalled colony or
        // a running famine is not hit again), by the turn, the attacker and the target: no dice, so the
        // chooser spreads over all kinds instead of always taking the first. OP_COUNT: none.
        // `mask`: the kinds allowed (bit per e_op); `need_points` false asks only whether a kind could bite
        // (the iteration 4 chooser saves points for it).
        e_op Sabotage_Pick_(int16_t a, int16_t t, int32_t mask = -1, bool need_points = true) {
            const auto ready = [&](e_op op) {
                return Enabled(op) && ((mask >> op) & 1) != 0 && (!need_points || espionage::Points(a, t) >= Cost(op));
            };
            e_op list[5];
            int32_t n = 0;
            const int16_t yard = Shipyard_Colony_(t);
            if (ready(OP_SABOTAGE_SHIPYARD) && yard >= 0 && !Stalled(yard)) list[n++] = OP_SABOTAGE_SHIPYARD;
            if (ready(OP_SABOTAGE_RESEARCH) && MOX::player[t].research_accumulated > MOX::player[a].research_accumulated &&
                MOX::player[t].research_accumulated >= 2) {
                list[n++] = OP_SABOTAGE_RESEARCH;
            }
            const int16_t farm = Populous_Colony_(t, true, 0);
            if (ready(OP_SABOTAGE_FOOD) && farm >= 0 && *Famine_Slot(farm) == 0) list[n++] = OP_SABOTAGE_FOOD;
            const int16_t factory = Strike_Colony_(t);
            if (ready(OP_STRIKE) && factory >= 0 && !Stalled(factory)) list[n++] = OP_STRIKE;
            if (ready(OP_SABOTAGE_TRADE) && Freighters_Lost_(t) > 0) list[n++] = OP_SABOTAGE_TRADE;
            if (n == 0) return OP_COUNT;
            return list[(MOX::stardate / AI_PERIOD + a + t) % n];
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

        // ---- Iteration 4, piece A: "все против всех" (docs/LocalLLM/iterations.md, the author's decision of
        // 25.11.2026). No dice, no new game_random draws; used only when a bit of 64-8192 is set.
        // Strength measure: the original AI's fleet strength (s_player::total_ship_strength, as vassals.cpp
        // and crises.cpp read it). "Stronger than us": t's strength against us exceeds ours against t.
        // "The strongest empire": the largest sum of an empire's fleet strengths against all others.
        int32_t Fleet_(int16_t of, int16_t against) {
            return MOX::player[of].total_ship_strength[against];
        }

        int64_t Total_Strength_(int16_t p) {
            int64_t sum = 0;
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o != p && Alive_(o)) sum += std::max<int32_t>(0, Fleet_(p, o));
            }
            return sum;
        }

        bool Stronger_Than_(int16_t t, int16_t a) {
            return Fleet_(t, a) > Fleet_(a, t);
        }

        bool Strongest_(int16_t t) {
            const int64_t mine = Total_Strength_(t);
            if (mine <= 0) return false;
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o == t || !Alive_(o)) continue;
                const int64_t theirs = Total_Strength_(o);
                if (theirs > mine || (theirs == mine && o < t)) return false;
            }
            return true;
        }

        // Both at war (treaty >= TREATY_FIRST_WAR) with the same third empire.
        bool Common_War_(int16_t a, int16_t t) {
            for (int16_t k = 0; k < Players_(); ++k) {
                if (k == a || k == t || !Alive_(k)) continue;
                if (MOX::player[a].treaty[k] >= TREATY_FIRST_WAR && MOX::player[t].treaty[k] >= TREATY_FIRST_WAR) return true;
            }
            return false;
        }

        // Not at war and not a foe (relations above FOE_RELATIONS): the "friend" of the decision.
        bool Friend_Ish_(int16_t a, int16_t t) {
            return MOX::player[a].treaty[t] < TREATY_FIRST_WAR && opinion::Relations(a, t) > FOE_RELATIONS;
        }

        // "Not more than once in 30 turns against one friend", with no state: the computers act every
        // AI_PERIOD turns, and a given (attacker, friend) pair gets its turn every third period.
        bool Friend_Turn_(int16_t a, int16_t t) {
            return (MOX::stardate / AI_PERIOD + a + 2 * t) % (FRIEND_PERIOD_TURNS / AI_PERIOD) == 0;
        }

        // False flag, "разделяй и властвуй": a pair `t` and k (k blamed) that get on (relations at least
        // GOOD_TERMS both ways, no war between them, in contact), at least one of them stronger than `a`.
        int16_t False_Flag_Third_(int16_t a, int16_t t) {
            for (int16_t k = 0; k < Players_(); ++k) {
                if (k == a || k == t || !Alive_(k) || MOX::player[t].contact[k] == 0) continue;
                if (MOX::player[t].treaty[k] >= TREATY_FIRST_WAR || MOX::player[k].treaty[t] >= TREATY_FIRST_WAR) continue;
                if (opinion::Relations(t, k) < GOOD_TERMS || opinion::Relations(k, t) < GOOD_TERMS) continue;
                if (Stronger_Than_(t, a) || Stronger_Than_(k, a) || Strongest_(t) || Strongest_(k)) return k;
            }
            return -1;
        }

        // Whether an operation of the attacker against `t` now is a betrayal of a friend (a computer,
        // new bits set, not at war, not a foe): its exposure costs twice the opinion.
        bool Betrayal_(int16_t a, int16_t t) {
            return (operations & NEW_OPERATION_BITS) != 0 && Computer_(a) && Friend_Ish_(a, t);
        }

        // The author's decision (iterations.md, "Решение автора (25.11.2026): «все против всех»"), quoted:
        // "три вида цели: враг на войне; недруг в мире (отношение -50 и ниже); «друг» — любой другой, но только
        // сильнее нас или сильнейшая империя; не тот, с кем мы вместе воюем против общего врага; не чаще раза в
        // 30 ходов против одного «друга»; раскрытая операция против друга — предательство, удар по отношениям
        // вдвое сильнее; против врага, недруга и сильного «друга» очки копятся на операцию, а не тратятся на
        // сбор данных; false flag — «разделяй и властвуй»: подставить одну империю перед другой, когда они
        // ладят между собой и хотя бы одна сильнее нас". By character: ruthless - war: all incl. poisoned
        // water, foe: all, strong friend: sabotage, false flag; xenophobe - the same without poisoned water at
        // war; aggressive and unpredictable - war: sabotage and assassination, foe and friend: sabotage,
        // false flag; honourable - sabotage at open war only, no false flag; peaceable - bloodless sabotage
        // (shipyard, research, trade) at war only, no false flag.
        //
        // The table of the decision, by personality (rows) and target kind (columns), as a rotating list
        // of what is possible now; the rotation (by turn and pair, no dice) spreads the points over the kinds
        // and a chosen kind that is not yet affordable is waited for (points are saved, no data collection).
        // Returns true when it decided (`out`: the operation, OP_COUNT = save the points); false hands over
        // to the old chooser (personalities and targets the table has no entry for).
        bool Choose_New_(int16_t a, int16_t t, int16_t* third, e_op* out) {
            const int32_t personality = MOX::player[a].personality;
            const bool ruthless = personality == PLAYER_PERSONALITY_RUTHLESS;
            const bool xenophobe = personality == PLAYER_PERSONALITY_XENOPHOBIC;
            const bool aggressive = personality == PLAYER_PERSONALITY_AGGRESSIVE || personality == PLAYER_PERSONALITY_ERRATIC;
            const bool honorable = personality == PLAYER_PERSONALITY_HONORABLE;
            const bool pacifist = personality == PLAYER_PERSONALITY_PACIFIST;
            if (!(ruthless || xenophobe || aggressive || honorable || pacifist)) return false;
            const bool gentle = honorable || pacifist;
            const bool at_war = MOX::player[a].treaty[t] >= TREATY_FIRST_WAR;
            if (gentle && !at_war) return false;  // honourable and peaceable computers: open war only
            const bool foe = !at_war && opinion::Relations(a, t) <= FOE_RELATIONS;
            bool strong_friend = false;
            if (!at_war && !foe) {
                if (Common_War_(a, t)) return false;  // we fight a common enemy: no betrayal
                strong_friend = Stronger_Than_(t, a) || Strongest_(t);
                if (!Friend_Turn_(a, t)) {
                    // Not the friend's turn: a strong friend is not collected data from, the points wait.
                    if (!strong_friend || Sabotage_Pick_(a, t, -1, false) == OP_COUNT) return false;
                    *out = OP_COUNT;
                    return true;
                }
            }
            const int32_t sabotage_mask = pacifist ? BLOODLESS_SABOTAGE_MASK : -1;
            constexpr e_op SABOTAGE = OP_SABOTAGE_SHIPYARD;  // stands for "some sabotage kind"
            e_op list[6];
            int32_t n = 0;
            const auto add = [&](e_op op, bool possible) { if (possible && Enabled(op)) list[n++] = op; };
            const auto add_sabotage = [&]() { if (Sabotage_Pick_(a, t, sabotage_mask, false) != OP_COUNT) list[n++] = SABOTAGE; };
            const bool kills = ruthless || xenophobe;  // "all": assassination, poison at a feast, a scientist
            if (at_war) {
                add(OP_ASSASSINATION, !gentle && Assassination_Subject_(t) >= 0);
                add(OP_POISON_WATER, ruthless && Populous_Colony_(t, false, POISON_WATER_MIN_POPS) >= 0);
                // A scientist is killed at war by the same ones who assassinate (not by the gentle ones, whose
                // war is bloodless or open): the cheapest of the kinds, so it comes after the dearer ones in the rotation.
                add(OP_KILL_SCIENTIST, !gentle && Scientist_(t) >= 0);
                add_sabotage();
            } else if (foe) {
                add(OP_POISON_FEAST, kills && Feast_Allowed_(a, t) && Assassination_Subject_(t) >= 0);
                add(OP_ASSASSINATION, kills && Assassination_Subject_(t) >= 0);
                add(OP_POISON_WATER, ruthless && Populous_Colony_(t, false, POISON_WATER_MIN_POPS) >= 0);
                add(OP_KILL_SCIENTIST, kills && Scientist_(t) >= 0);
                add_sabotage();
            } else {
                if (strong_friend) add_sabotage();
            }
            if (!gentle) {
                const int16_t k = Enabled(OP_FALSE_FLAG) ? False_Flag_Third_(a, t) : -1;
                if (k >= 0) {
                    list[n++] = OP_FALSE_FLAG;
                    *third = k;
                }
            }
            if (n == 0) {
                if (!gentle) return false;
                *out = Enabled(OP_DATA) && espionage::Points(a, t) >= Cost(OP_DATA) ? OP_DATA : OP_COUNT;
                return true;
            }
            e_op op = list[(MOX::stardate / AI_PERIOD + a + t) % n];
            if (op == SABOTAGE) op = Sabotage_Pick_(a, t, sabotage_mask, true);
            if (op == OP_COUNT || espionage::Points(a, t) < Cost(op)) op = OP_COUNT;  // not yet affordable: save
            *out = op;
            return true;
        }

        // The computer's choice against `t` (OP_COUNT: none); `third` gets the scapegoat.
        e_op Choose_(int16_t a, int16_t t, int16_t* third) {
            if (espionage::Points(a, t) <= 0) return OP_COUNT;
            if ((operations & NEW_OPERATION_BITS) != 0) {
                e_op chosen = OP_COUNT;
                if (Choose_New_(a, t, third, &chosen)) return chosen;
            }
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
                const e_op sabotage = Sabotage_Pick_(a, t);
                if (sabotage != OP_COUNT) return sabotage;
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
                const e_op sabotage = Sabotage_Pick_(a, t);
                if (sabotage != OP_COUNT) return sabotage;
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
                    hostages::Arrest(i, true);  // an uncovered bribe: the traitor is jailed (ab-review 3.1)
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
            counterspy::On_Harm(target, op);     // the victim sees the harm and suspects a mole (off by default)
            espionage::On_Harm(target, op);      // ... and may call a witch hunt (off by default)
            switch (op) {
                case OP_UNREST: {
                    uint8_t* unrest = Unrest_Slot(subject);
                    *unrest = static_cast<uint8_t>(std::min<int32_t>(UNREST_MAX, *unrest + UNREST_PER_OPERATION));
                    break;
                }
                case OP_ASSASSINATION:
                case OP_POISON_FEAST: Leave_Mark_(subject); OFFICER::Kill_Officer(subject); break;
                case OP_KILL_SCIENTIST: OFFICER::Kill_Officer(subject); break;
                case OP_SABOTAGE_SHIPYARD:
                    MOX::colony[subject].production_spent = 0;
                    *Stall_Slot(subject) = static_cast<uint8_t>(SABOTAGE_TURNS);
                    break;
                case OP_STRIKE: *Stall_Slot(subject) = static_cast<uint8_t>(SABOTAGE_TURNS); break;
                case OP_SABOTAGE_RESEARCH: MOX::player[subject].research_accumulated -= MOX::player[subject].research_accumulated * RESEARCH_LOSS_PERCENT / 100; break;
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
                case OP_FRAME_LEADER: hostages::Arrest(subject, false); break;  // the framed honest leader is jailed (ab-review 3.1)
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
        // Iteration 4: an exposed operation of a computer against a "friend" is a betrayal: twice the blow
        // (the same reason, a doubled amount; the reason's own limit still applies, no save format change).
        const bool betrayal = op != OP_DATA && Betrayal_(attacker, target);
        opinion::Add(target, blamed, Reason_(op), betrayal ? EXPOSED_OPINION * BETRAYAL_FACTOR : EXPOSED_OPINION);
        counterspy::On_Attempt(target, op);
        espionage::On_Spy_Caught(target, attacker);
        if (op == OP_FRAME_LEADER) *Immune_Slot(subject) = 1;
        if (op == OP_POISON_FEAST || op == OP_POISON_WATER) reputation::On_Atrocity(attacker);
        if (op == OP_POISON_FEAST) {
            opinion::Add(target, attacker, opinion::REASON_TRIED_TO_KILL_OUR_LEADER, betrayal ? EXPOSED_OPINION * BETRAYAL_FACTOR : EXPOSED_OPINION);
        }
        return RESULT_EXPOSED;
    }

    int32_t Morale_Effect(const s_colony* colony) {
        if (colony == nullptr) return 0;
        int32_t effect = 0;
        if (Enabled(OP_BRIBERY)) {
            const int officer = COLCALC::Colony_Officer(const_cast<s_colony*>(colony));
            if (officer >= 0 && Bribed_By(static_cast<int16_t>(officer)) >= 0) effect += BRIBED_GOVERNOR_MORALE;
        }
        if (Enabled(OP_ASSASSINATION) || Enabled(OP_POISON_FEAST)) {
            const uint8_t* mourn = Mourn_Slot(static_cast<int16_t>(colony - MOX::colony));
            if (mourn != nullptr && *mourn > 0) effect += MOURN_MORALE;
        }
        return effect;
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

    e_op Choose_Operation(int16_t attacker, int16_t target) {
        int16_t third = -1;
        return Choose_(attacker, target, &third);
    }

    void Turn() {
        if (operations == 0) return;
        for (int16_t c = 0; c < MAX_COLONIES; ++c) {
            uint8_t* unrest = Unrest_Slot(c);
            if (*unrest > 0) --*unrest;
            uint8_t* famine = Famine_Slot(c);
            if (*famine > 0) --*famine;
            uint8_t* mourn = Mourn_Slot(c);
            if (*mourn > 0) --*mourn;
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
