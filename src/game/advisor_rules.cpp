#include "pch.h"

#include <algorithm>
#include <cstdio>

#include "game/advisor.h"
#include "game/advisor_directive.h"
#include "game/advisor_sources.h"
#include "game/advisor_view.h"
#include "game/orion2_consts.h"

namespace advisor {

    namespace {
        constexpr int32_t TREATY_WAR_FIRST_ = 4;
        constexpr int32_t TREATY_WAR_LAST_ = 6;

        constexpr int32_t GAME_START_STARDATE_ = 35000;

        // Self-play of 20.11.2026 (100 AI turns): a food deficit of 1-2 is routine for the AI,
        // and at turn 0 every MILITARIST is "at peace with no one", so both rules fired on
        // noise and erased the objectives the game rolled at setup.
        constexpr int32_t FAMINE_DEFICIT_ = 3;            // famine when food surplus <= -3
        constexpr int32_t PEACE_DIVIDEND_FIRST_TURN_ = 50;  // and at least one contact

        // Rule 401: the leader needs at least 30 % of the known power and 1.5 times the next empire.
        constexpr int32_t BALANCE_MIN_INDEX_ = 300;  // per mille
        constexpr int32_t EXPLORE_LAST_TURN_ = 100;  // rule 501 stops at this turn
        // Rule 403: a war enemy with at most this many known colonies and a third of our fighting power.
        constexpr int32_t SPARE_MAX_COLONIES_ = 2;
        constexpr int64_t SPARE_POWER_RATIO_ = 3;
        // Rule 404: a neighbour is costly to fight from 2/3 of our fighting power.
        constexpr int64_t PEACE_THREAT_NUM_ = 2;
        constexpr int64_t PEACE_THREAT_DEN_ = 3;
        // Rule 301: with this many known free planets two colony ships may be built at once.
        constexpr int32_t EXPAND_TWO_SHIPS_FREE_PLANETS_ = 3;
        constexpr int32_t ALLIANCE_TREATY_ = 2;
        constexpr int32_t PACT_TREATY_ = 1;  // non-aggression pact
        // Phase strategy (rules 406, 407): not before this turn, when the galaxy has taken shape.
        constexpr int32_t PHASE_FIRST_TURN_ = 100;
        // Rule 407: a would-be winner needs at least this many sure votes (population 100); the
        // first bench games flagged "2 of 4 known votes" at turn 100.
        constexpr int32_t PHASE_MIN_THREAT_VOTES_ = 10;

        // We know a contact's size from our spies or from colonies of theirs we have seen.
        bool Known_Size_(const s_view_contact& c) {
            return c.our_spies > 0 || c.known_colonies.value > 0;
        }

        // Council votes as the game counts them (council.cpp Get_Num_Votes): population / 10,
        // rounded up.
        int32_t Votes_(int32_t population) {
            return population > 0 ? (population + 9) / 10 : 0;
        }

        const char* Objective_Label_(int objective) {
            switch (objective) {
                case PLAYER_OBJECTIVE_DIPLOMATIC: return "DIPLOMATIC";
                case PLAYER_OBJECTIVE_MILITARIST: return "MILITARIST";
                case PLAYER_OBJECTIVE_EXPANSIONIST: return "EXPANSIONIST";
                case PLAYER_OBJECTIVE_TECHNOLOGIST: return "TECHNOLOGIST";
                case PLAYER_OBJECTIVE_INDUSTRIALIST: return "INDUSTRIALIST";
                case PLAYER_OBJECTIVE_ECOLOGIST: return "ECOLOGIST";
                default: return "?";
            }
        }

        bool Is_War_(const s_view_contact& c) {
            return c.treaty.value >= TREATY_WAR_FIRST_ && c.treaty.value <= TREATY_WAR_LAST_;
        }

        int32_t Grievance_(const s_view_contact& c) {
            return 10 * c.our_colony_losses.value + c.our_ship_losses.value;
        }

        // Lanchester's square law: fighting power grows with strength times the number of warships,
        // so many weaker ships beat fewer stronger ones of the same total strength. Linear strength
        // (the original comparison) when rules_lanchester is off. +1 keeps empty counts neutral.
        int64_t Fighting_Power_(int32_t strength, int32_t warships) {
            return rules_lanchester ? static_cast<int64_t>(strength) * (warships + 1) : strength;
        }

        bool Is_Losing_(const s_view_self& self, const s_view_contact& c) {
            const bool stronger = 2 * Fighting_Power_(c.visible_fleet_strength.value, c.visible_combat_ships.value)
                > 3 * Fighting_Power_(self.fleet_strength, self.combat_ships);
            const bool losses = c.our_ship_losses.value >= 2 * std::max(1, c.their_ship_losses.value);
            return stronger || losses;
        }

        // Rules 404 and 405: a contact is dangerous to us from 2/3 of our fighting power.
        bool Dangerous_(const s_view_self& self, const s_view_contact& c) {
            const int64_t theirs = Fighting_Power_(c.visible_fleet_strength.value, c.visible_combat_ships.value);
            return theirs > 0 && PEACE_THREAT_DEN_ * theirs >= PEACE_THREAT_NUM_ * Fighting_Power_(self.fleet_strength, self.combat_ships);
        }

        // Rule 403 with the third-party part: a beaten enemy is worth keeping as a counterweight
        // only while another contact is dangerous to us.
        bool Counterweight_Needed_(const s_empire_view* view, uint8_t beaten) {
            for (int i = 0; i < view->contact_count && i < MAX_PLAYERS; ++i) {
                const s_view_contact& c = view->contacts[i];
                if (c.player != beaten && Dangerous_(view->self, c)) return true;
            }
            return false;
        }

        // Index of the warring contact with the highest grievance among those accepted by
        // `pred` (ties: lower player number); -1 if none.
        template <typename Pred>
        int Pick_Main_Enemy_(const s_empire_view* view, int32_t count, Pred pred) {
            int best = -1;
            for (int i = 0; i < count; ++i) {
                const s_view_contact& c = view->contacts[i];
                if (!Is_War_(c) || !pred(c)) {
                    continue;
                }
                if (best < 0) {
                    best = i;
                    continue;
                }
                const s_view_contact& b = view->contacts[best];
                const int32_t g = Grievance_(c);
                const int32_t gb = Grievance_(b);
                if (g > gb || (g == gb && c.player < b.player)) {
                    best = i;
                }
            }
            return best;
        }

        void Append_Note_(std::string* text, const char* line) {
            if (!text->empty()) {
                *text += "; ";
            }
            *text += line;
        }
    } // namespace

    bool Rules_Directive(const s_empire_view* view, s_directive* out, std::string* note) {
        if (out != nullptr) {
            Clear_Directive(out);
        }
        if (note != nullptr) {
            note->clear();
        }
        if (view == nullptr || out == nullptr) {
            return false;
        }

        const s_view_self& self = view->self;
        const int32_t count = std::clamp<int32_t>(view->contact_count, 0, MAX_PLAYERS);

        bool any_war = false;
        for (int i = 0; i < count; ++i) {
            any_war = any_war || Is_War_(view->contacts[i]);
        }

        const bool pacifist = self.personality == PLAYER_PERSONALITY_PACIFIST;
        const bool honorable = self.personality == PLAYER_PERSONALITY_HONORABLE;
        const bool ruthless = self.personality == PLAYER_PERSONALITY_RUTHLESS;

        s_directive d;
        Clear_Directive(&d);
        std::string text;
        char line[256];
        uint16_t objective_rule = 0;
        uint16_t spy_rule = 0;
        int chosen = -1;

        // Objective rules: the first match wins.
        if (self.food_surplus <= -FAMINE_DEFICIT_) {
            objective_rule = 101;
            chosen = PLAYER_OBJECTIVE_ECOLOGIST;
            std::snprintf(line, sizeof(line), "rule 101 famine: food surplus %d -> ECOLOGIST", self.food_surplus);
            Append_Note_(&text, line);
        }
        if (objective_rule == 0 && any_war) {
            const int idx = Pick_Main_Enemy_(view, count, [&](const s_view_contact& c) {
                return Is_Losing_(self, c);
            });
            if (idx >= 0) {
                const s_view_contact& c = view->contacts[idx];
                objective_rule = 102;
                chosen = pacifist ? PLAYER_OBJECTIVE_DIPLOMATIC : PLAYER_OBJECTIVE_MILITARIST;
                d.main_rival = c.player;
                // Byzantine rule VI: when losing, buy peace instead of bleeding (all personalities).
                d.posture = (pacifist || Doctrine(DOCTRINE_PART_BUY_PEACE)) ? POSTURE_MAKE_PEACE : POSTURE_DEFEND;
                std::snprintf(line, sizeof(line),
                    "rule 102 losing_war: player %d visible %d vs ours %d, losses %d/%d -> %s, rival %d",
                    static_cast<int>(c.player), c.visible_fleet_strength.value, self.fleet_strength,
                    c.our_ship_losses.value, c.their_ship_losses.value, Objective_Label_(chosen),
                    static_cast<int>(c.player));
                Append_Note_(&text, line);
            }
        }
        if (objective_rule == 0 && any_war && self.objective != PLAYER_OBJECTIVE_MILITARIST
            && (self.personality == PLAYER_PERSONALITY_AGGRESSIVE || ruthless
                || self.personality == PLAYER_PERSONALITY_XENOPHOBIC
                || self.personality == PLAYER_PERSONALITY_ERRATIC)) {
            const int idx = Pick_Main_Enemy_(view, count, [](const s_view_contact&) { return true; });
            if (idx >= 0) {
                const s_view_contact& c = view->contacts[idx];
                objective_rule = 103;
                chosen = PLAYER_OBJECTIVE_MILITARIST;
                d.main_rival = c.player;
                d.posture = POSTURE_ATTACK_CURRENT_ENEMIES;
                std::snprintf(line, sizeof(line),
                    "rule 103 at_war: personality %s, main enemy %d (grievance %d) -> MILITARIST, rival %d",
                    Personality_Name(self.personality), static_cast<int>(c.player), Grievance_(c),
                    static_cast<int>(c.player));
                Append_Note_(&text, line);
            }
        }
        // Rule 104 (room_to_grow: switch to EXPANSIONIST on stagnation) was retired on 20.11.2026.
        // The objective barely moves AI colonization; rule 301 below pushes it in the production
        // planner instead (docs/LocalLLM/README.md section 10.3).
        if (objective_rule == 0 && !any_war && self.objective == PLAYER_OBJECTIVE_MILITARIST
            && count > 0 && self.stardate - GAME_START_STARDATE_ >= PEACE_DIVIDEND_FIRST_TURN_) {
            int32_t lowest = 100;
            bool calm = true;
            // Byzantine rule I (doctrine readiness): avoid war, but act as if it may start at any
            // time - keep the militarist objective while any neighbour fields more fighting power.
            int64_t strongest = 0;
            for (int i = 0; i < count; ++i) {
                const s_view_contact& c = view->contacts[i];
                lowest = std::min(lowest, c.relations.value);
                calm = calm && c.relations.value >= -25;
                strongest = std::max(strongest, Fighting_Power_(c.visible_fleet_strength.value, c.visible_combat_ships.value));
            }
            const bool deterred = Doctrine(DOCTRINE_PART_READINESS) && strongest > Fighting_Power_(self.fleet_strength, self.combat_ships);
            if (calm && deterred) {
                std::snprintf(line, sizeof(line),
                    "rule 105 peace_dividend withheld: a neighbour fields more fighting power -> keep MILITARIST");
                Append_Note_(&text, line);
            }
            if (calm && !deterred) {
                objective_rule = 105;
                chosen = (honorable || pacifist) ? PLAYER_OBJECTIVE_DIPLOMATIC
                    : ruthless ? PLAYER_OBJECTIVE_INDUSTRIALIST
                               : PLAYER_OBJECTIVE_TECHNOLOGIST;
                std::snprintf(line, sizeof(line),
                    "rule 105 peace_dividend: no wars, lowest relations %d, was MILITARIST -> %s",
                    lowest, Objective_Label_(chosen));
                Append_Note_(&text, line);
            }
        }

        // Spy rules, independent of the objective.
        if (any_war) {
            const int idx = Pick_Main_Enemy_(view, count, [](const s_view_contact& c) {
                return c.our_spies > 0;
            });
            if (idx >= 0) {
                const s_view_contact& c = view->contacts[idx];
                spy_rule = 201;
                d.spy_target = c.player;
                d.spy_mission = (honorable || pacifist) ? SPY_STEAL_TECH : SPY_SABOTAGE;
                std::snprintf(line, sizeof(line), "rule 201 spy_war: player %d, %d spies -> %s",
                    static_cast<int>(c.player), static_cast<int>(c.our_spies),
                    d.spy_mission == SPY_STEAL_TECH ? "STEAL_TECH" : "SABOTAGE");
                Append_Note_(&text, line);
            }
        } else {
            int best = -1;
            for (int i = 0; i < count; ++i) {
                const s_view_contact& c = view->contacts[i];
                if (c.our_spies == 0) {
                    continue;
                }
                if (best < 0 || c.relations.value < view->contacts[best].relations.value
                    || (c.relations.value == view->contacts[best].relations.value
                        && c.player < view->contacts[best].player)) {
                    best = i;
                }
            }
            if (best >= 0) {
                const s_view_contact& c = view->contacts[best];
                spy_rule = 202;
                d.spy_target = c.player;
                d.spy_mission = SPY_STEAL_TECH;
                std::snprintf(line, sizeof(line),
                    "rule 202 spy_rival: player %d, relations %d, %d spies -> STEAL_TECH",
                    static_cast<int>(c.player), c.relations.value, static_cast<int>(c.our_spies));
                Append_Note_(&text, line);
            }
        }

        // Rule 203 (stage 2b): no spy rule above, because we have no spies at the candidates. Name a
        // target to build spies for, so advisor::Desired_Spies asks the AI production for them: the
        // main enemy at war, otherwise the contact with the lowest relations (ties: lower player).
        if (spy_rule == 0 && count > 0) {
            int idx = -1;
            if (any_war) {
                idx = Pick_Main_Enemy_(view, count, [](const s_view_contact&) { return true; });
            } else {
                for (int i = 0; i < count; ++i) {
                    const s_view_contact& c = view->contacts[i];
                    if (idx < 0 || c.relations.value < view->contacts[idx].relations.value
                        || (c.relations.value == view->contacts[idx].relations.value
                            && c.player < view->contacts[idx].player)) {
                        idx = i;
                    }
                }
            }
            if (idx >= 0) {
                const s_view_contact& c = view->contacts[idx];
                const bool war = Is_War_(c);
                spy_rule = 203;
                d.spy_target = c.player;
                d.spy_mission = (war && !(honorable || pacifist)) ? SPY_SABOTAGE : SPY_STEAL_TECH;
                std::snprintf(line, sizeof(line), "rule 203 spy_build: player %d (%s), no spies there -> build spies, %s",
                    static_cast<int>(c.player), war ? "war" : "peace",
                    d.spy_mission == SPY_STEAL_TECH ? "STEAL_TECH" : "SABOTAGE");
                Append_Note_(&text, line);
            }
        }

        // Rule 401 (stage 2d, balance of power): a power index per empire, the mean of its colony
        // share and fleet share among us and our contacts, from what we know (spies make it exact).
        // When a contact clearly leads, everyone else gains by balancing against it: hostile to the
        // leader, friendly to the others (peace sooner, treaties easier).
        uint16_t balance_rule = 0;
        bool we_lead = false;  // the test of rule 401 applied to us
        int strongest_other = -1;  // contact index with the highest power index
        if (count > 0) {
            int64_t colonies_total = std::max(0, self.colonies);
            int64_t fleet_total = std::max(0, self.fleet_strength);
            for (int i = 0; i < count; ++i) {
                colonies_total += std::max(0, view->contacts[i].known_colonies.value);
                fleet_total += std::max(0, view->contacts[i].visible_fleet_strength.value);
            }
            const auto index = [&](int32_t colonies, int32_t fleet) -> int32_t {
                const int64_t colony_share = colonies_total > 0 ? 1000 * static_cast<int64_t>(std::max(0, colonies)) / colonies_total : 0;
                const int64_t fleet_share = fleet_total > 0 ? 1000 * static_cast<int64_t>(std::max(0, fleet)) / fleet_total : colony_share;
                return static_cast<int32_t>((colony_share + fleet_share) / 2);
            };
            int leader = -1;  // -1: we lead
            int32_t leader_index = index(self.colonies, self.fleet_strength);
            int32_t second_index = 0;
            for (int i = 0; i < count; ++i) {
                const s_view_contact& c = view->contacts[i];
                const int32_t value = index(c.known_colonies.value, c.visible_fleet_strength.value);
                if (value > leader_index || (value == leader_index && leader >= 0 && c.player < view->contacts[leader].player)) {
                    second_index = leader_index;
                    leader_index = value;
                    leader = i;
                } else if (value > second_index) {
                    second_index = value;
                }
            }
            // Without spies a contact's fleet is only what our scanners see this turn, so its power
            // looks small: an empire of one colony "led" in the first bench games. We claim the lead
            // only with spies at every contact and at least as many colonies as each (rule II).
            bool lead_known = true;
            for (int i = 0; i < count; ++i) {
                const s_view_contact& c = view->contacts[i];
                lead_known = lead_known && c.our_spies > 0 && c.known_colonies.value <= self.colonies;
            }
            we_lead = lead_known && leader < 0 && leader_index >= BALANCE_MIN_INDEX_ && 2 * leader_index >= 3 * second_index;
            int32_t strongest_index = -1;
            for (int i = 0; i < count; ++i) {
                const int32_t value = index(view->contacts[i].known_colonies.value, view->contacts[i].visible_fleet_strength.value);
                if (value > strongest_index) {
                    strongest_index = value;
                    strongest_other = i;
                }
            }
            if (leader >= 0 && leader_index >= BALANCE_MIN_INDEX_ && 2 * leader_index >= 3 * second_index) {
                const s_view_contact& top = view->contacts[leader];
                balance_rule = 401;
                if (top.treaty.value != ALLIANCE_TREATY_) {
                    d.attitude[top.player] = ATTITUDE_HOSTILE;
                }
                for (int i = 0; i < count; ++i) {
                    if (i != leader) {
                        d.attitude[view->contacts[i].player] = ATTITUDE_FRIENDLY;
                    }
                }
                if (d.main_rival == NO_PLAYER) {
                    d.main_rival = top.player;
                }
                std::snprintf(line, sizeof(line),
                    "rule 401 balance: player %d leads (index %d vs next %d per mille) -> HOSTILE to %d, FRIENDLY to the others",
                    static_cast<int>(top.player), leader_index, second_index, static_cast<int>(top.player));
                Append_Note_(&text, line);
            }
        }

        // Rule 501 (exploration push): self-play showed AI empires stop exploring after about ten
        // turns at 5 of 36 stars, because ships cannot fly beyond fuel range of their colonies.
        // While most of the galaxy is unexplored, research range - in the early game only. (A first
        // version also required few free planets in reach and fired in under 1 % of empire-turns; a
        // second fired for the whole game, because AI empires never explore half the galaxy, and
        // left them with 12 % fewer technologies by turn 300.)
        uint16_t exploration_rule = 0;
        if (2 * self.stars_explored < self.stars_total && self.stardate - GAME_START_STARDATE_ < EXPLORE_LAST_TURN_ &&
            objective_rule != 102) {
            exploration_rule = 501;
            d.research_focus = RESEARCH_PROPULSION;
            std::snprintf(line, sizeof(line),
                "rule 501 explore: %d of %d stars explored, %d free planets in reach -> research PROPULSION (range)",
                self.stars_explored, self.stars_total, self.free_planets_in_reach);
            Append_Note_(&text, line);
        }

        // Phase strategy (rules_phases; README.md 10.7). The Byzantine doctrine keeps an empire alive
        // among strong neighbours; MOO2 is won by conquest or by two thirds of the council votes
        // (population / 10; an ally always votes for its ally, council.cpp Vote_Check). From turn
        // PHASE_FIRST_TURN_, with at least two contacts:
        // - rule 407 (against a winner, any phase): a contact nominated for the council (one of the
        //   two empires with the most votes) whose own and allies' votes - not the rival nominee's -
        //   reach half of all votes we know is on its way to a council win -> HOSTILE (WARY if it
        //   is our ally), its allies courted (FRIENDLY), it becomes the main rival;
        // - rule 406 (leader, "Rome"): when we lead by power (the test rule 401 applies to others),
        //   play for victory: a fleet (fleet weight HIGH, read by the Readiness hook), peace with the
        //   strongest other empire (one front), friends and an ally among the rest for their votes,
        //   and one weak neighbour at a time conquered (WAR, START_NEW_WAR, main rival; not spared by
        //   rule 403) - unless our own and our allies' votes already reach two thirds: then keep the
        //   peace and let the council crown us.
        uint16_t phase_rule = 0;
        if (rules_phases && count >= 2 && self.stardate - GAME_START_STARDATE_ >= PHASE_FIRST_TURN_) {
            int32_t votes[MAX_PLAYERS] = {};
            const int32_t our_votes = Votes_(self.population);
            int32_t total_votes = our_votes;
            for (int i = 0; i < count; ++i) {
                // An empire of unknown size counts as large as we are.
                votes[i] = Known_Size_(view->contacts[i]) ? Votes_(view->contacts[i].population.value) : our_votes;
                total_votes += votes[i];
            }
            const auto bit = [&](int i) { return 1u << (view->contacts[i].player & 7); };
            // The council nominates the two empires with the most votes (-1: us); a nominee votes
            // for itself, so its rival nominee's votes never count as sure.
            int nominee[2] = {-2, -2};
            int32_t nominee_votes[2] = {-1, -1};
            const auto consider = [&](int i, int32_t v) {
                if (v > nominee_votes[0]) {
                    nominee[1] = nominee[0];
                    nominee_votes[1] = nominee_votes[0];
                    nominee[0] = i;
                    nominee_votes[0] = v;
                } else if (v > nominee_votes[1]) {
                    nominee[1] = i;
                    nominee_votes[1] = v;
                }
            };
            consider(-1, our_votes);
            for (int i = 0; i < count; ++i) consider(i, votes[i]);
            int threat = -1;
            int32_t threat_votes = 0;
            for (int n = 0; n < 2; ++n) {
                const int i = nominee[n];
                const int rival = nominee[1 - n];
                if (i < 0) continue;
                const s_view_contact& c = view->contacts[i];
                int32_t sure = votes[i] + (c.treaty.value == ALLIANCE_TREATY_ && rival != -1 ? our_votes : 0);
                for (int j = 0; j < count; ++j) {
                    if (j != i && j != rival && (c.allied_with & bit(j)) != 0) sure += votes[j];
                }
                if (!Known_Size_(c) || sure < PHASE_MIN_THREAT_VOTES_) continue;
                if (total_votes > 0 && 2 * sure >= total_votes && sure > threat_votes) {
                    threat = i;
                    threat_votes = sure;
                }
            }
            const int our_rival = nominee[0] == -1 ? nominee[1] : nominee[1] == -1 ? nominee[0] : -3;  // -3: we are no nominee
            if (threat >= 0) {
                const s_view_contact& x = view->contacts[threat];
                d.attitude[x.player] = x.treaty.value == ALLIANCE_TREATY_ ? ATTITUDE_WARY : ATTITUDE_HOSTILE;
                int courted = 0;
                for (int j = 0; j < count; ++j) {
                    if (j == threat || (x.allied_with & bit(j)) == 0 || Is_War_(view->contacts[j])) continue;
                    d.attitude[view->contacts[j].player] = ATTITUDE_FRIENDLY;
                    ++courted;
                }
                d.main_rival = x.player;
                phase_rule = 407;
                std::snprintf(line, sizeof(line),
                    "rule 407 council_threat: player %d has %d of %d known votes with its allies -> %s, court %d of its allies",
                    static_cast<int>(x.player), threat_votes, total_votes,
                    x.treaty.value == ALLIANCE_TREATY_ ? "WARY" : "HOSTILE", courted);
                Append_Note_(&text, line);
            } else if (we_lead) {
                phase_rule = 406;
                d.weights[WEIGHT_FLEET] = LEVEL_HIGH;
                const int64_t ours = Fighting_Power_(self.fleet_strength, self.combat_ships);
                const auto power = [](const s_view_contact& c) {
                    return Fighting_Power_(c.visible_fleet_strength.value, c.visible_combat_ships.value);
                };
                int32_t our_sure = our_votes;
                for (int i = 0; i < count; ++i) {
                    if (i != our_rival && view->contacts[i].treaty.value == ALLIANCE_TREATY_) our_sure += votes[i];
                }
                const bool secured = our_rival != -3 && 3 * our_sure >= 2 * total_votes;
                int target = -1;
                if (!secured && !any_war) {
                    for (int i = 0; i < count; ++i) {
                        const s_view_contact& c = view->contacts[i];
                        if (i == strongest_other || c.treaty.value == PACT_TREATY_ || c.treaty.value == ALLIANCE_TREATY_) continue;
                        if (c.our_spies == 0 || c.in_range == 0 || ours <= 0 || 2 * power(c) > ours) continue;
                        if (target < 0 || power(c) < power(view->contacts[target])) target = i;
                    }
                }
                if (target >= 0) {
                    d.attitude[view->contacts[target].player] = ATTITUDE_WAR;
                    d.posture = POSTURE_START_NEW_WAR;
                    d.main_rival = view->contacts[target].player;
                } else if (any_war && !secured) {
                    // Finish the weak enemies we fight (no early peace with them); stronger enemies
                    // are left to the other rules.
                    d.posture = POSTURE_ATTACK_CURRENT_ENEMIES;
                    for (int i = 0; i < count; ++i) {
                        const s_view_contact& c = view->contacts[i];
                        if (Is_War_(c) && ours > 0 && 2 * power(c) <= ours) d.attitude[c.player] = ATTITUDE_WAR;
                    }
                }
                if (strongest_other >= 0 && !Is_War_(view->contacts[strongest_other]) && strongest_other != target) {
                    d.attitude[view->contacts[strongest_other].player] = ATTITUDE_NEUTRAL;
                }
                int ally = -1;
                for (int i = 0; i < count; ++i) {
                    const s_view_contact& c = view->contacts[i];
                    if (i == target || i == strongest_other || Is_War_(c) || d.attitude[c.player] != ATTITUDE_KEEP) continue;
                    d.attitude[c.player] = ATTITUDE_FRIENDLY;
                    if (c.treaty.value != ALLIANCE_TREATY_ && (ally < 0 || votes[i] > votes[ally])) ally = i;
                }
                if (ally >= 0 && !secured) d.attitude[view->contacts[ally].player] = ATTITUDE_ALLY;
                std::snprintf(line, sizeof(line),
                    "rule 406 leader: %d of %d known votes with our allies%s; %s%d; peace with %d; ally sought %d",
                    our_sure, total_votes, secured ? " (two thirds: keep the peace)" : "",
                    target >= 0 ? "conquer player " : "no new war, target ", target >= 0 ? static_cast<int>(view->contacts[target].player) : -1,
                    strongest_other >= 0 ? static_cast<int>(view->contacts[strongest_other].player) : -1,
                    ally >= 0 && !secured ? static_cast<int>(view->contacts[ally].player) : -1);
                Append_Note_(&text, line);
            }
        }

        // Rule 402 (Byzantine rule V, enemy of my enemy): with a war on, every contact that also
        // fights one of our enemies becomes a friend (peace sooner, treaties easier), and every
        // contact allied with one of our enemies is courted (friendly), to detach it - "the best
        // allies are the enemy's allies". Not toward a leader that rule 401 made hostile.
        uint16_t doctrine_rule = 0;
        uint8_t enemies = 0;
        for (int i = 0; i < count; ++i) {
            if (Is_War_(view->contacts[i])) {
                enemies = static_cast<uint8_t>(enemies | (1u << (view->contacts[i].player & 7)));
            }
        }
        if (Doctrine(DOCTRINE_PART_ALLIANCES) && any_war) {
            int friends = 0;
            int courted = 0;
            for (int i = 0; i < count; ++i) {
                const s_view_contact& c = view->contacts[i];
                if (Is_War_(c) || d.attitude[c.player] == ATTITUDE_HOSTILE) continue;
                if ((c.at_war_with & enemies) != 0) {
                    d.attitude[c.player] = ATTITUDE_ALLY;
                    ++friends;
                } else if ((c.allied_with & enemies) != 0 && d.attitude[c.player] == ATTITUDE_KEEP) {
                    d.attitude[c.player] = ATTITUDE_FRIENDLY;
                    ++courted;
                }
            }
            if (friends > 0 || courted > 0) {
                doctrine_rule = 402;
                std::snprintf(line, sizeof(line),
                    "rule 402 enemy_of_my_enemy: %d contacts fight our enemies -> ALLY, %d allies of our enemies -> FRIENDLY",
                    friends, courted);
                Append_Note_(&text, line);
            }
        }
        if (Doctrine(DOCTRINE_PART_SPARE) && any_war && phase_rule != 406) {

            // Rule 403 (Byzantine rule VII): do not destroy a beaten enemy that does not lead the galaxy;
            // today's enemy may be tomorrow's ally, and it stays a counterweight. Make peace with it.
            for (int i = 0; i < count; ++i) {
                const s_view_contact& c = view->contacts[i];
                if (!Is_War_(c) || d.attitude[c.player] == ATTITUDE_HOSTILE) continue;
                const bool beaten = c.known_colonies.value <= SPARE_MAX_COLONIES_ &&
                    Fighting_Power_(self.fleet_strength, self.combat_ships) >=
                        SPARE_POWER_RATIO_ * Fighting_Power_(c.visible_fleet_strength.value, c.visible_combat_ships.value);
                if (!beaten) continue;
                // With the third-party part a beaten enemy is spared only while it is needed as a
                // counterweight: someone else is dangerous to us. Otherwise it is finished off.
                if (Doctrine(DOCTRINE_PART_THIRD_PARTY) && !Counterweight_Needed_(view, c.player)) continue;
                d.attitude[c.player] = ATTITUDE_FRIENDLY;
                if (doctrine_rule == 0) doctrine_rule = 403;
                std::snprintf(line, sizeof(line), "rule 403 spare: player %d is beaten (%d colonies) -> FRIENDLY, make peace",
                    static_cast<int>(c.player), c.known_colonies.value);
                Append_Note_(&text, line);
            }
        }

        // Rule 405 (third-party war, Byzantine rules III, IV, V and VII; README.md 10.6): other
        // empires' wars and weak neighbours. Only contacts no rule above judged, never a partner
        // bound to us by a pact or an alliance (we keep our word: switching sides is the Byzantine
        // answer, not betrayal).
        // - protect: Y loses a war to X (at most 2/3 of X's power), X is dangerous to us, Y is no
        //   danger to us -> Y FRIENDLY, X WARY and the main rival if we have none (balance, a buffer);
        // - jackal: Y loses a war to X, X is no danger to us, Y is weak for us, in reach, known through our
        //   spies, our rear is calm and Y has no dangerous allies -> Y HOSTILE (its fleet is busy);
        // - prey: the same Y at peace with everyone, once no free planet is left in reach ->
        //   Y HOSTILE (peaceful expansion comes first);
        // - X and Y about equal: stay out and let them bleed each other.
        // No jackal or prey while we fight a war of our own: one front at a time.
        if (Doctrine(DOCTRINE_PART_THIRD_PARTY)) {
            const int64_t ours = Fighting_Power_(self.fleet_strength, self.combat_ships);
            const auto power = [](const s_view_contact& c) {
                return Fighting_Power_(c.visible_fleet_strength.value, c.visible_combat_ships.value);
            };
            const auto dangerous = [&](const s_view_contact& c) { return Dangerous_(self, c); };
            const auto bound = [](const s_view_contact& c) {
                return c.treaty.value == PACT_TREATY_ || c.treaty.value == ALLIANCE_TREATY_;
            };
            const auto weak_for_us = [&](const s_view_contact& c) {
                return ours > 0 && 2 * power(c) <= ours && c.known_colonies.value <= self.colonies && c.our_spies > 0;
            };
            // Our rear is calm when no unbound contact is dangerous to us or unknown within reach.
            uint8_t dangerous_mask = 0;
            bool rear_calm = true;
            for (int i = 0; i < count; ++i) {
                const s_view_contact& c = view->contacts[i];
                if (dangerous(c)) dangerous_mask = static_cast<uint8_t>(dangerous_mask | (1u << (c.player & 7)));
                if (!bound(c) && (dangerous(c) || (c.our_spies == 0 && c.in_range != 0))) rear_calm = false;
            }
            int protected_count = 0;
            int jackal = 0;
            int prey = 0;
            for (int i = 0; i < count; ++i) {
                const s_view_contact& y = view->contacts[i];
                if (d.attitude[y.player] != ATTITUDE_KEEP || Is_War_(y) || bound(y)) continue;
                int attacker = -1;
                for (int j = 0; j < count; ++j) {
                    const s_view_contact& x = view->contacts[j];
                    if (j == i || (y.at_war_with & (1u << (x.player & 7))) == 0) continue;
                    if (attacker < 0 || power(x) > power(view->contacts[attacker])) attacker = j;
                }
                const bool opportunity = !any_war && rear_calm && weak_for_us(y) && y.in_range != 0 &&
                    (y.allied_with & dangerous_mask) == 0;
                if (attacker >= 0) {
                    const s_view_contact& x = view->contacts[attacker];
                    // Y must be clearly losing (at most 2/3 of X); about equal sides are left alone.
                    const bool losing = power(x) > 0 && 3 * power(y) <= 2 * power(x);
                    if (!losing) continue;
                    if (dangerous(x) && !dangerous(y)) {
                        d.attitude[y.player] = ATTITUDE_FRIENDLY;
                        if (d.attitude[x.player] == ATTITUDE_KEEP && !bound(x)) d.attitude[x.player] = ATTITUDE_WARY;
                        if (d.main_rival == NO_PLAYER) d.main_rival = x.player;
                        ++protected_count;
                    } else if (!dangerous(x) && opportunity) {
                        d.attitude[y.player] = ATTITUDE_HOSTILE;
                        ++jackal;
                    }
                } else if (opportunity && self.free_planets_in_reach == 0) {
                    d.attitude[y.player] = ATTITUDE_HOSTILE;
                    ++prey;
                }
            }
            if (protected_count > 0 || jackal > 0 || prey > 0) {
                if (doctrine_rule == 0) doctrine_rule = 405;
                std::snprintf(line, sizeof(line),
                    "rule 405 third_party: protect %d weak victims (FRIENDLY), join %d wars on weak neighbours, prey on %d (HOSTILE)",
                    protected_count, jackal, prey);
                Append_Note_(&text, line);
            }
        }

        // Rule 404 (Byzantine rule I, peace from strength): prefer peace with every neighbour that
        // would be costly to fight - at least two thirds of our fighting power - except the main
        // rival and anyone a rule above already judged. Clearly weaker neighbours get no bias: the
        // original AI's opportunism stays. Attitude NEUTRAL carries the bias (Attitude_Bias).
        if (Doctrine(DOCTRINE_PART_PEACE)) {
            const int64_t ours = Fighting_Power_(self.fleet_strength, self.combat_ships);
            int deterred = 0;
            for (int i = 0; i < count; ++i) {
                const s_view_contact& c = view->contacts[i];
                if (c.player == d.main_rival || d.attitude[c.player] != ATTITUDE_KEEP) continue;
                const int64_t theirs = Fighting_Power_(c.visible_fleet_strength.value, c.visible_combat_ships.value);
                if (theirs <= 0 || PEACE_THREAT_DEN_ * theirs < PEACE_THREAT_NUM_ * ours) continue;  // a known threat only
                d.attitude[c.player] = ATTITUDE_NEUTRAL;
                ++deterred;
            }
            if (deterred > 0) {
                if (doctrine_rule == 0) doctrine_rule = 404;
                std::snprintf(line, sizeof(line), "rule 404 peace_from_strength: %d neighbours costly to fight -> NEUTRAL (peace bias)",
                    deterred);
                Append_Note_(&text, line);
            }
        }

        // Expansion rule, independent of the objective and spy rules: a known free planet in reach
        // raises the colonization weight, which advisor::Colonization_Push turns into a colony
        // ship kept in production. Not while losing a war: production goes to defense then.
        uint16_t expansion_rule = 0;
        if ((self.free_planets_in_reach > 0 || self.free_outpost_sites_in_reach > 0) && objective_rule != 102) {
            expansion_rule = 301;
            const bool many = self.free_planets_in_reach >= EXPAND_TWO_SHIPS_FREE_PLANETS_;
            d.weights[WEIGHT_COLONIZATION] = many ? LEVEL_MAX : LEVEL_HIGH;
            std::snprintf(line, sizeof(line), "rule 301 expand: %d free planets and %d outpost sites in reach -> colonization %s",
                self.free_planets_in_reach, self.free_outpost_sites_in_reach, many ? "MAX" : "HIGH");
            Append_Note_(&text, line);
        }

        if (objective_rule == 0 && spy_rule == 0 && expansion_rule == 0 && balance_rule == 0 && exploration_rule == 0 &&
            doctrine_rule == 0 && phase_rule == 0) {
            return false;
        }

        d.source = SOURCE_RULES;
        d.issued_stardate = self.stardate;
        d.expires_stardate = self.stardate + std::max<int32_t>(1, rules_period) - 1;
        d.objective = (chosen >= 0 && chosen != self.objective) ? static_cast<uint8_t>(chosen) : OBJECTIVE_KEEP;
        d.rule_id = objective_rule != 0 ? objective_rule
            : spy_rule != 0             ? spy_rule
            : expansion_rule != 0       ? expansion_rule
            : phase_rule != 0           ? phase_rule
            : balance_rule != 0         ? balance_rule
            : doctrine_rule != 0        ? doctrine_rule
                                        : exploration_rule;
        *out = d;
        if (note != nullptr) {
            *note = text;
        }
        return true;
    }

} // namespace advisor
