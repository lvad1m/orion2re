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

        bool Is_Losing_(const s_view_self& self, const s_view_contact& c) {
            const bool stronger = 2 * static_cast<int64_t>(c.visible_fleet_strength.value)
                > 3 * static_cast<int64_t>(self.fleet_strength);
            const bool losses = c.our_ship_losses.value >= 2 * std::max(1, c.their_ship_losses.value);
            return stronger || losses;
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
                d.posture = pacifist ? POSTURE_MAKE_PEACE : POSTURE_DEFEND;
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
            for (int i = 0; i < count; ++i) {
                const int32_t r = view->contacts[i].relations.value;
                lowest = std::min(lowest, r);
                calm = calm && r >= -25;
            }
            if (calm) {
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

        // Expansion rule, independent of the objective and spy rules: a known free planet in reach
        // raises the colonization weight, which advisor::Colonization_Push turns into a colony
        // ship kept in production. Not while losing a war: production goes to defense then.
        uint16_t expansion_rule = 0;
        if (self.free_planets_in_reach > 0 && objective_rule != 102) {
            expansion_rule = 301;
            d.weights[WEIGHT_COLONIZATION] = LEVEL_HIGH;
            std::snprintf(line, sizeof(line), "rule 301 expand: %d free planets in reach -> colonization HIGH",
                self.free_planets_in_reach);
            Append_Note_(&text, line);
        }

        if (objective_rule == 0 && spy_rule == 0 && expansion_rule == 0) {
            return false;
        }

        d.source = SOURCE_RULES;
        d.issued_stardate = self.stardate;
        d.expires_stardate = self.stardate + std::max<int32_t>(1, rules_period) - 1;
        d.objective = (chosen >= 0 && chosen != self.objective) ? static_cast<uint8_t>(chosen) : OBJECTIVE_KEEP;
        d.rule_id = objective_rule != 0 ? objective_rule : spy_rule != 0 ? spy_rule : expansion_rule;
        *out = d;
        if (note != nullptr) {
            *note = text;
        }
        return true;
    }

} // namespace advisor
