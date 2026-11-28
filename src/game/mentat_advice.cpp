#include "pch.h"

#include "game/mentat.h"
#include "game/texts.h"

#include "game/advisor_directive.h"
#include "game/advisor_sources.h"
#include "game/advisor_view.h"
#include "game/aipower.h"
#include "game/choices.h"
#include "game/legacy_world.h"
#include "game/opinion.h"
#include "game/spy_ranks.h"

// The mentat's advice (docs/LocalLLM/mentat.md, M2). Read-only, like the rest of the mentat:
// - the human's own view is built with AIPOWER::read_only_strength set (Build_Empire_View measures ships with
//   AIPOWER::Ship_Strength_Vs_Ships, which in strategic combat refreshes each ship's design: a write);
// - advisor::Rules_Directive works only on that view and the advisor's flags (no stored directive, no draw);
// - the view's other readers are getters of the modules' tables (checked one by one while writing M2: rivalry, pacts,
//   embargo, espionage::Points, reputation::Titles, vassals, hostages, covenants, beliefs::Percent and Distortion_Percent,
//   opinion::Modifier, spy_ranks::Residents, stability::Of, loyalty::Of, the intel memory's Seen_Ships, Military_Report
//   (empty for a human), news::Recent_For);
// - none of the evaluators of advisor_apply.h is called: they read the directive stored for an advised computer empire
//   and are meant for the AI's decisions, not for a human's view.
// The advisor's rule flags decide only which rules of the plain set run (doctrine and phase parts are off by default);
// `advisor enabled` is not consulted: the human gets the same advice with the advisor off.
namespace mentat {

    namespace {
        constexpr int32_t TREATY_NONE = 0;
        constexpr int32_t TREATY_PACT = 1;
        constexpr int32_t TREATY_ALLIANCE = 2;
        constexpr int32_t TREATY_PEACE = 3;
        constexpr int32_t TREATY_WAR_FIRST = 4;
        constexpr int32_t TREATY_WAR_LAST = 6;

        // What the strategy keys are called: mentat.advice.<name>, mentat.advice_why.<name>.
        const char* const NAMES[ADVICE_COUNT] = {
            "defend", "unrest", "peace", "choices", "famine", "bankrupt", "spy_send", "spy_steal", "spy_sabotage",
            "counter_agent", "resident", "pact", "trade", "research_treaty", "alliance", "opinion", "war", "focus", "colonize",
        };
        // The urgency of each kind: threats first, then decisions and the economy, spies, diplomacy, growth.
        const uint8_t URGENCY[ADVICE_COUNT] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4};

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        std::string Num_(int64_t value) {
            return std::to_string(value);
        }

        std::string Name_(int32_t p) {
            if (p < 0 || p >= Players_()) return "?";
            const s_player& player = MOX::player[p];
            return std::string(player.race_name, strnlen(player.race_name, sizeof(player.race_name)));
        }

        std::string Colony_Star_Name_(int32_t colony) {
            if (colony < 0 || colony >= MOX::NUM_COLONIES) return "?";
            const int32_t planet = MOX::colony[colony].planet;
            if (planet < 0 || planet >= MAX_PLANETS) return "?";
            const int32_t star = MOX::planet[planet].star_index;
            if (star < 0 || star >= MAX_STARS) return "?";
            const s_star_data& data = MOX::star[star];
            return std::string(data.name, strnlen(data.name, sizeof(data.name)));
        }

        // Restores the strength measure's switch when the advice is done, however it ends.
        struct s_read_only_scope {
            bool saved = AIPOWER::read_only_strength;
            s_read_only_scope() { AIPOWER::read_only_strength = true; }
            ~s_read_only_scope() { AIPOWER::read_only_strength = saved; }
        };

        bool At_War_(const advisor::s_view_contact& c) {
            return c.treaty.value >= TREATY_WAR_FIRST && c.treaty.value <= TREATY_WAR_LAST;
        }

        const advisor::s_view_contact* Find_Contact_(const advisor::s_empire_view& view, int32_t player) {
            for (int32_t i = 0; i < view.contact_count && i < MAX_PLAYERS; ++i) {
                if (view.contacts[i].player == player) return &view.contacts[i];
            }
            return nullptr;
        }

        std::string Screen_(const char* screen) {
            const std::string key = std::string("mentat.screen.") + screen;
            return texts::Get(key.c_str());
        }

        struct s_builder {
            std::vector<s_advice>* list;

            void Add(e_advice kind, const char* screen, texts::t_args args, texts::t_args why_args, const char* why_name = nullptr,
                bool estimate = false) {
                s_advice advice;
                advice.kind = kind;
                advice.urgency = URGENCY[kind];
                advice.estimate = estimate;
                args.emplace_back("screen", Screen_(screen));
                advice.text.key = std::string("mentat.advice.") + NAMES[kind];
                advice.text.args = std::move(args);
                advice.why.key = std::string("mentat.advice_why.") + (why_name != nullptr ? why_name : NAMES[kind]);
                advice.why.args = std::move(why_args);
                list->push_back(std::move(advice));
            }
        };

        // The first populated colony of `player` where `product` can be built; -1 when none.
        int32_t Colony_For_(int16_t player, int16_t product) {
            for (int32_t i = 0; i < MOX::NUM_COLONIES; ++i) {
                const s_colony& colony = MOX::colony[i];
                if (colony.owner != player || colony.outpost_flag != 0) continue;
                if (spy_ranks::Can_Build(&colony, product)) return i;
            }
            return -1;
        }

        void Alert_Advice_(s_builder* b, int16_t player, uint8_t detail_level, const advisor::s_empire_view& view) {
            for (const s_alert& alert : Find_Alerts(player, detail_level)) {
                switch (alert.kind) {
                    case ALERT_FLEET:
                        b->Add(ADVICE_DEFEND, "fleet", alert.text.args, alert.why.args);
                        break;
                    case ALERT_UNREST:
                        b->Add(ADVICE_UNREST, "colony", alert.text.args, alert.why.args);
                        break;
                    case ALERT_BANKRUPTCY:
                        b->Add(ADVICE_BANKRUPT, "colony", alert.why.args, alert.why.args);
                        break;
                    case ALERT_SPY: {
                        const int32_t colony = Colony_For_(player, spy_ranks::PRODUCT_COUNTER_AGENT);
                        if (colony >= 0) {
                            b->Add(ADVICE_COUNTER_AGENT, "colony", {{"star", Colony_Star_Name_(colony)}}, alert.text.args);
                        }
                        break;
                    }
                    default: break;
                }
            }
            (void)view;
        }

        // A war going badly (the advisor's directive wants peace, and the human's own numbers agree) -> peace.
        void Peace_Advice_(s_builder* b, const advisor::s_empire_view& view, const advisor::s_directive& d, bool has_directive) {
            if (!has_directive || (d.posture != advisor::POSTURE_MAKE_PEACE && d.rule_id != 102)) return;
            const advisor::s_view_contact* rival = d.main_rival != advisor::NO_PLAYER ? Find_Contact_(view, d.main_rival) : nullptr;
            if (rival == nullptr || !At_War_(*rival)) {
                rival = nullptr;
                for (int32_t i = 0; i < view.contact_count && i < MAX_PLAYERS && rival == nullptr; ++i) {
                    if (At_War_(view.contacts[i])) rival = &view.contacts[i];
                }
            }
            if (rival == nullptr) return;
            const advisor::s_view_self& self = view.self;
            const bool stronger = 2 * static_cast<int64_t>(rival->visible_fleet_strength.value) > 3 * static_cast<int64_t>(self.fleet_strength);
            const bool losses = rival->our_ship_losses.value >= 2 * std::max(1, rival->their_ship_losses.value);
            const int32_t settled = std::max<int32_t>(1, self.colonies - self.outposts);
            const bool unrest = self.unstable_colonies > 0 && 3 * self.unstable_colonies >= settled;
            const std::string name = Name_(rival->player);
            if (stronger || losses) {
                b->Add(ADVICE_PEACE, "diplomacy", {{"name", name}},
                    {{"name", name}, {"theirs", Num_(rival->visible_fleet_strength.value)}, {"mine", Num_(self.fleet_strength)},
                        {"lost", Num_(rival->our_ship_losses.value)}, {"killed", Num_(rival->their_ship_losses.value)}},
                    "peace_losing");
            } else if (unrest) {
                b->Add(ADVICE_PEACE, "diplomacy", {{"name", name}},
                    {{"unstable", Num_(self.unstable_colonies)}, {"colonies", Num_(settled)}}, "peace_unrest");
            }
        }

        void Spy_Advice_(s_builder* b, int16_t player, const advisor::s_empire_view& view, const advisor::s_directive& d, bool has_directive) {
            if (!has_directive || d.spy_target == advisor::NO_PLAYER) return;
            const advisor::s_view_contact* target = Find_Contact_(view, d.spy_target);
            if (target == nullptr) return;
            const std::string name = Name_(target->player);
            const bool war = At_War_(*target);
            if (target->our_spies == 0) {
                b->Add(ADVICE_SPY_SEND, "spies", {{"name", name}}, {{"name", name}}, war ? "spy_send_war" : "spy_send");
                return;
            }
            if (d.spy_mission == advisor::SPY_STEAL_TECH) {
                b->Add(ADVICE_SPY_STEAL, "spies", {{"name", name}, {"count", Num_(target->our_spies)}},
                    {{"name", name}, {"relations", Num_(target->relations.value)}}, war ? "spy_steal_war" : "spy_steal");
            } else if (d.spy_mission == advisor::SPY_SABOTAGE) {
                b->Add(ADVICE_SPY_SABOTAGE, "spies", {{"name", name}, {"count", Num_(target->our_spies)}}, {{"name", name}});
            }
            if (spy_ranks::Residents(player, target->player) == 0) {
                const int32_t colony = Colony_For_(player, spy_ranks::PRODUCT_RESIDENT);
                if (colony >= 0) {
                    b->Add(ADVICE_RESIDENT, "colony", {{"name", name}, {"star", Colony_Star_Name_(colony)}},
                        {{"name", name}, {"count", Num_(target->our_spies)}});
                }
            }
        }

        // The classic diplomacy screen's chances: one proposal per neighbour, the biggest first.
        void Treaty_Advice_(s_builder* b, const advisor::s_empire_view& view, const advisor::s_directive& d, bool has_directive) {
            for (int32_t i = 0; i < view.contact_count && i < MAX_PLAYERS; ++i) {
                const advisor::s_view_contact& c = view.contacts[i];
                if (At_War_(c)) continue;
                if (has_directive) {
                    const uint8_t attitude = d.attitude[c.player];
                    if (attitude == advisor::ATTITUDE_WARY || attitude == advisor::ATTITUDE_HOSTILE || attitude == advisor::ATTITUDE_WAR) continue;
                }
                const int32_t relations = c.relations.value;
                const int32_t treaty = c.treaty.value;
                const std::string name = Name_(c.player);
                const texts::t_args args = {{"name", name}};
                const texts::t_args why = {{"name", name}, {"relations", Num_(relations)}};
                if ((treaty == TREATY_PACT || treaty == TREATY_PEACE) && relations >= ALLIANCE_RELATIONS) {
                    b->Add(ADVICE_ALLIANCE, "diplomacy", args, why);
                } else if (treaty != TREATY_NONE && c.research_treaty.value == 0 && relations >= RESEARCH_RELATIONS) {
                    b->Add(ADVICE_RESEARCH_TREATY, "diplomacy", args, why);
                } else if (treaty != TREATY_NONE && c.trade_treaty.value == 0 && relations >= TRADE_RELATIONS) {
                    b->Add(ADVICE_TRADE, "diplomacy", args, why);
                } else if (treaty == TREATY_NONE && relations >= PACT_RELATIONS) {
                    b->Add(ADVICE_PACT, "diplomacy", args, why);
                }
            }
        }

        // Advice on the neighbours' hidden state, detailed only: the empire that remembers the most against you (the view
        // keeps its opinion of you in opinion::Modifier(it, you, reason)), and a war the advisor's reckoning of the council would start.
        void Estimate_Advice_(s_builder* b, int16_t player, const advisor::s_empire_view& view, const advisor::s_directive& d, bool has_directive) {
            int32_t worst_value = 0;
            int32_t worst_reason = -1;
            const advisor::s_view_contact* worst = nullptr;
            for (int32_t i = 0; i < view.contact_count && i < MAX_PLAYERS; ++i) {
                const advisor::s_view_contact& c = view.contacts[i];
                for (int32_t r = 0; r < opinion::REASON_RESERVED_30; ++r) {
                    const int32_t value = opinion::Modifier(c.player, player, static_cast<opinion::e_reason>(r));
                    if (value <= -OPINION_MIN && value < worst_value) {
                        worst_value = value;
                        worst_reason = r;
                        worst = &c;
                    }
                }
            }
            if (worst != nullptr) {
                const std::string reason = texts::Get(("mentat.reason." + Num_(worst_reason)).c_str());
                b->Add(ADVICE_OPINION, "diplomacy", {{"name", Name_(worst->player)}, {"reason", reason}},
                    {{"name", Name_(worst->player)}, {"reason", reason}, {"value", Num_(worst_value)}}, nullptr, true);
            }
            if (has_directive && d.posture == advisor::POSTURE_START_NEW_WAR && d.main_rival != advisor::NO_PLAYER) {
                const advisor::s_view_contact* rival = Find_Contact_(view, d.main_rival);
                if (rival != nullptr && !At_War_(*rival)) {
                    b->Add(ADVICE_WAR, "diplomacy", {{"name", Name_(rival->player)}},
                        {{"name", Name_(rival->player)}, {"theirs", Num_(rival->visible_fleet_strength.value)},
                            {"mine", Num_(view.self.fleet_strength)}}, nullptr, true);
                }
            }
        }

        void Economy_Advice_(s_builder* b, int16_t player, const advisor::s_empire_view& view, const advisor::s_directive& d, bool has_directive) {
            const advisor::s_view_self& self = view.self;
            if (self.food_surplus <= -FAMINE_DEFICIT) {
                b->Add(ADVICE_FAMINE, "colony", {{"deficit", Num_(-self.food_surplus)}}, {{"deficit", Num_(-self.food_surplus)}});
            }
            const int32_t waiting = choices::Count(player);
            if (waiting > 0) {
                b->Add(ADVICE_CHOICES, "choices", {{"count", Num_(waiting)}}, {{"count", Num_(waiting)}});
            }
            if (!has_directive) return;
            if (d.research_focus == advisor::RESEARCH_PROPULSION) {
                b->Add(ADVICE_FOCUS, "science", {}, {{"explored", Num_(self.stars_explored)}, {"total", Num_(self.stars_total)}});
            }
            if (d.weights[advisor::WEIGHT_COLONIZATION] >= advisor::LEVEL_HIGH &&
                (self.free_planets_in_reach > 0 || self.free_outpost_sites_in_reach > 0)) {
                b->Add(ADVICE_COLONIZE, "colony",
                    {{"planets", Num_(self.free_planets_in_reach)}, {"sites", Num_(self.free_outpost_sites_in_reach)}},
                    {{"planets", Num_(self.free_planets_in_reach)}, {"sites", Num_(self.free_outpost_sites_in_reach)}});
            }
        }
    } // namespace

    void Build_Advice(int16_t player, uint8_t detail_level, std::vector<s_advice>* out) {
        if (out == nullptr) return;
        out->clear();
        if (player < 0 || player >= Players_() || MOX::player == nullptr) return;
        const s_read_only_scope read_only;
        const game::s_view world = legacy_world::World();
        auto view = std::make_unique<advisor::s_empire_view>();
        if (!advisor::Build_Empire_View(&world, static_cast<uint8_t>(player), view.get())) return;
        advisor::s_directive directive;
        std::string note;
        const bool has_directive = advisor::Rules_Directive(view.get(), &directive, &note);

        s_builder builder{out};
        Alert_Advice_(&builder, player, detail_level, *view);
        Peace_Advice_(&builder, *view, directive, has_directive);
        Spy_Advice_(&builder, player, *view, directive, has_directive);
        Treaty_Advice_(&builder, *view, directive, has_directive);
        Economy_Advice_(&builder, player, *view, directive, has_directive);
        if (detail_level >= DETAIL_DETAILED) Estimate_Advice_(&builder, player, *view, directive, has_directive);

        std::stable_sort(out->begin(), out->end(), [](const s_advice& a, const s_advice& b) { return a.urgency < b.urgency; });
        const size_t limit = detail_level >= DETAIL_DETAILED ? out->size()
            : static_cast<size_t>(detail_level == DETAIL_MEDIUM ? ADVICE_MEDIUM : ADVICE_BRIEF);
        if (out->size() > limit) out->resize(limit);
    }

} // namespace mentat
