#include "pch.h"

#include "game/crises.h"

#include "game/beliefs.h"
#include "game/bill.h"
#include "game/colcalc.h"
#include "game/diplomac.h"
#include "game/dice.h"
#include "game/events.h"
#include "game/haccess.h"
#include "game/heroes.h"
#include "game/invasion.h"
#include "game/news.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/stability.h"
#include "game/techdata.h"
#include "game/texts.h"

#include <algorithm>

// The crisis template (events-catalog.md 5.1); the open crises and the save section are in
// crises_state.cpp. game_random is drawn only with events crises on: the start rolls, the choice
// of colony, the checks and the hits of a failure.
namespace crises {

    namespace {
        constexpr int8_t TREATY_FIRST_WAR = 4;
        constexpr int8_t STATUS_ASSIGNED = 1;

        // Options: name, cost in BC now, the kind of check, cruel (a mark on the reputation).
        const s_option ASTEROID[OPTION_COUNT] = {
            {"build an interceptor", 100, CHECK_SCIENCE, false},
            {"blow it up", 50, CHECK_ARMY, false},
            {"evacuate with the neighbours' help", 0, CHECK_DIPLOMACY, false},
        };
        const s_option PARASITES[OPTION_COUNT] = {
            {"communicate with them", 0, CHECK_DIPLOMACY, false},
            {"find a cure", 50, CHECK_SCIENCE, false},
            {"make them a weapon", 0, CHECK_ESPIONAGE, true},
        };
        const s_option REVOLUTIONARIES[OPTION_COUNT] = {
            {"negotiate", 30, CHECK_DIPLOMACY, false},
            {"put them on trial", 0, CHECK_ESPIONAGE, false},
            {"execute them", 0, CHECK_ARMY, true},
        };

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

        int16_t Capital_(int16_t player) {
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                if (MOX::colony[c].owner == player && MOX::colony[c].buildings[BUILDING_CAPITOL] != 0) return c;
            }
            return -1;
        }

        int16_t Random_Colony_(int16_t player, bool not_capital) {
            int16_t colonies[MAX_COLONIES];
            int32_t n = 0;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                const s_colony& colony = MOX::colony[c];
                if (colony.owner != player || colony.n_pops == 0) continue;
                if (not_capital && colony.buildings[BUILDING_CAPITOL] != 0) continue;
                colonies[n++] = c;
            }
            return n == 0 ? -1 : colonies[Pick_(n)];
        }

        bool Has_Leader_Skill_(int16_t player, uint32_t general, uint32_t colony_skill) {
            for (int16_t l = 0; l < MAX_LEADERS; ++l) {
                const s_leader_data& leader = MOX::leaders[l];
                if (leader.player_index != player || leader.status != STATUS_ASSIGNED) continue;
                if ((leader.general_skills & general) != 0 || (leader.special_skills & colony_skill) != 0) return true;
            }
            return false;
        }

        // The kind of check an empire's character leans to (events-catalog.md 5.1, "AI on rules").
        int32_t Preferred_(int16_t player) {
            const s_player& p = MOX::player[player];
            switch (p.personality) {
                case PLAYER_PERSONALITY_AGGRESSIVE:
                case PLAYER_PERSONALITY_RUTHLESS: return CHECK_ARMY;
                case PLAYER_PERSONALITY_HONORABLE:
                case PLAYER_PERSONALITY_PACIFIST: return CHECK_DIPLOMACY;
                case PLAYER_PERSONALITY_XENOPHOBIC: return CHECK_ESPIONAGE;
                default: break;
            }
            return p.objectives == PLAYER_OBJECTIVE_TECHNOLOGIST ? CHECK_SCIENCE : -1;
        }

        void Log_(int16_t player, const char* format_tail, const char* what) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "crises: stardate %d, player %d: %s%s", (int)MOX::stardate, (int)player,
                format_tail, what);
        }

        void Lose_Dearest_Building_(int16_t colony) {
            int16_t lost = -1;
            for (int16_t b = 0; b < BUILDING_COUNT; ++b) {
                if (b == BUILDING_CAPITOL || MOX::colony[colony].buildings[b] == 0) continue;
                if (lost < 0 || TECHDATA::buildings[b].cost > TECHDATA::buildings[lost].cost) lost = b;
            }
            if (lost < 0) return;
            MOX::colony[colony].buildings[lost] = 0;
            COLCALC::Colony_Calculation(colony);
        }

        // Schedules `follow_up` in slot `s` (reused) `delay` turns from now.
        void Follow_Up_(s_active& s, e_crisis follow_up, int32_t delay) {
            s.crisis = follow_up;
            s.option = -1;
            s.result = RESULT_PENDING;
            s.roll = 0;
            s.due = MOX::stardate + delay;
            Log_(s.player, "it is not over: ", Name(follow_up));
            news::Add(news::KIND_CRISIS_START, s.player, -1, follow_up);  // the news ring (off by default)
        }

        // The outcome, built from the classic primitives. Returns true when the slot was reused for a
        // follow-up crisis.
        bool Apply_Outcome_(s_active& s) {
            const int16_t player = s.player;
            s_player& p = MOX::player[player];
            const int16_t colony = s.colony;
            const bool colony_ok = colony >= 0 && colony < MOX::NUM_COLONIES && MOX::colony[colony].owner == player;
            switch (s.crisis) {
                case CRISIS_ASTEROID:
                    if (s.result == RESULT_PARTIAL && colony_ok) Lose_Dearest_Building_(colony);
                    if (s.result == RESULT_FAILURE && colony_ok) EVENTS::Allocate_Comet_Hit(colony);
                    return false;
                case CRISIS_BRAIN_PARASITES:
                    if (s.result == RESULT_SUCCESS) {
                        if (s.option == 0) p.research_accumulated += 100;
                        if (s.option == 2) BILL::Set_Their_Spy_Number(player, player,
                            static_cast<uint8_t>(std::min<int32_t>(250, BILL::Get_Their_Spy_Number(player, player) + 2)));
                        return false;
                    }
                    if (s.result == RESULT_FAILURE) p.research_accumulated /= 2;
                    Follow_Up_(s, CRISIS_PARASITES_RETURN, s.result == RESULT_FAILURE ? 5 : 10);
                    return true;
                case CRISIS_PARASITES_RETURN:
                    if (s.result == RESULT_PARTIAL) p.research_accumulated /= 2;
                    if (s.result == RESULT_FAILURE && colony_ok) EVENTS::Allocate_Comet_Hit(colony);
                    return false;
                case CRISIS_REVOLUTIONARIES:
                    if (s.result == RESULT_PARTIAL) p.bc = std::max<int32_t>(0, p.bc - 50);
                    if (s.result == RESULT_FAILURE) {
                        Follow_Up_(s, CRISIS_REVOLUTION, 8);
                        return true;
                    }
                    return false;
                case CRISIS_REVOLUTION:
                    if (!colony_ok) return false;
                    if (s.result == RESULT_PARTIAL) MOX::colony[colony].production_spent = 0;
                    if (s.result == RESULT_FAILURE) {
                        const int16_t to = MOX::colony[colony].buildings[BUILDING_CAPITOL] == 0 ? stability::Leaning_Neighbour(colony) : -1;
                        if (to >= 0) {
                            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "crises: stardate %d, colony %d of player %d breaks away to player %d",
                                (int)MOX::stardate, (int)colony, (int)player, (int)to);
                            invasion::Change_Colony_Ownership(&MOX::colony[colony], static_cast<uint32_t>(to), nullptr);
                        } else {
                            EVENTS::Allocate_Comet_Hit(colony);
                        }
                    }
                    return false;
                default: return false;
            }
        }

        // A cruel choice leaves a mark: every computer that knows the empire thinks it likelier to
        // attack (the neighbour model) and remembers it as a plot when it is at war with it.
        void Mark_Reputation_(int16_t player) {
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o == player || !Computer_(o) || MOX::player[o].contact[player] == 0) continue;
                beliefs::Observe(o, player, beliefs::BELIEF_HOSTILE, 1);
                if (MOX::player[o].treaty[player] >= TREATY_FIRST_WAR) {
                    opinion::Add(o, player, opinion::REASON_PLOTTED_AGAINST_US, -10);
                }
            }
        }

        // A human with events human_choices on is asked instead of being skipped (choices.h).
        bool Human_Asks_(int16_t player) {
            return Alive_(player) && choices::Ask_Human(player);
        }

        choices::s_choice Choice_(e_choice kind, int16_t player, int16_t target, int16_t other) {
            choices::s_choice c;
            c.module = choices::MODULE_CRISES;
            c.kind = kind;
            c.player = static_cast<int8_t>(player);
            c.target = target;
            c.other = other;
            c.asked = MOX::stardate;
            return c;
        }

        bool Pending_(int16_t player, e_choice kind) {
            const choices::s_state* state = choices::State();
            for (int32_t i = 0; i < state->count; ++i) {
                const choices::s_choice& c = state->choice[i];
                if (c.module == choices::MODULE_CRISES && c.kind == kind && c.player == player) return true;
            }
            return false;
        }

        // Queues the choice of the option of the crisis in `s` (a human); the record waits with option -1 and
        // `roll` = 1 marks that it has been asked.
        bool Queue_Crisis_(s_active& s) {
            if (!choices::Add(Choice_(CHOICE_CRISIS, s.player, s.colony, s.crisis))) return false;
            s.roll = 1;
            return true;
        }

        // The crisis of `player` that waits for a human's option.
        s_active* Waiting_(int16_t player, int32_t crisis) {
            for (int32_t i = 0; i < MAX_ACTIVE; ++i) {
                s_active& s = Slots()[i];
                if (s.crisis == crisis && s.player == player && s.option < 0) return &s;
            }
            return nullptr;
        }

        // Takes `option` for the open crisis in `s`: pays for it, sets the date of the check, marks a cruel choice.
        void Take_Option_(s_active& s, int32_t chosen) {
            s.option = static_cast<int8_t>(chosen);
            const s_option& option = Options(static_cast<e_crisis>(s.crisis))[s.option];
            MOX::player[s.player].bc -= option.cost_bc;
            s.due = MOX::stardate + CHECK_DELAY;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "crises: stardate %d, player %d: %s at colony %d - it chooses to %s",
                (int)MOX::stardate, (int)s.player, Name(static_cast<e_crisis>(s.crisis)), (int)s.colony, option.name);
            if (option.cruel) Mark_Reputation_(s.player);
        }

        void Choose_(s_active& s) {
            Take_Option_(s, Choose_Option(s.player, static_cast<e_crisis>(s.crisis)));
        }
    } // namespace

    const char* Name(e_crisis crisis) {
        switch (crisis) {
            case CRISIS_ASTEROID: return "an asteroid falls toward the capital";
            case CRISIS_BRAIN_PARASITES: return "brain parasites in a colony";
            case CRISIS_REVOLUTIONARIES: return "revolutionaries in a colony";
            case CRISIS_PARASITES_RETURN: return "the brain parasites return";
            case CRISIS_REVOLUTION: return "the revolutionaries rise";
            default: return "no crisis";
        }
    }

    const s_option* Options(e_crisis crisis) {
        switch (crisis) {
            case CRISIS_ASTEROID: return ASTEROID;
            case CRISIS_BRAIN_PARASITES:
            case CRISIS_PARASITES_RETURN: return PARASITES;
            default: return REVOLUTIONARIES;
        }
    }

    int32_t Check_Bonus(int16_t player, e_check kind) {
        if (!Alive_(player)) return 0;
        const s_player& p = MOX::player[player];
        int32_t standing = 0;
        bool leader = false;
        switch (kind) {
            case CHECK_SCIENCE:
                standing = p.research_produced / 50;
                leader = Has_Leader_Skill_(player, LEADER_GENERAL_SKILL_RESEARCHER, LEADER_COLONY_SKILL_SCIENCE_LEADER);
                break;
            case CHECK_DIPLOMACY: {
                int32_t sum = 0;
                int32_t n = 0;
                for (int16_t o = 0; o < Players_(); ++o) {
                    if (o == player || !Alive_(o) || p.contact[o] == 0) continue;
                    sum += opinion::Relations(o, player);
                    ++n;
                }
                standing = n == 0 ? 0 : sum / n / 20;
                leader = Has_Leader_Skill_(player, LEADER_GENERAL_SKILL_DIPLOMAT, 0);
                break;
            }
            case CHECK_ARMY: {
                int32_t strongest = 0;
                for (int16_t o = 0; o < Players_(); ++o) strongest = std::max<int32_t>(strongest, p.total_ship_strength[o]);
                standing = strongest / 200;
                leader = Has_Leader_Skill_(player, LEADER_GENERAL_SKILL_COMMANDO, LEADER_COLONY_SKILL_TACTICS);
                break;
            }
            case CHECK_ESPIONAGE:
                standing = BILL::Get_Their_Spy_Number(player, player) / 2;
                leader = Has_Leader_Skill_(player, LEADER_GENERAL_SKILL_OPERATIONS | LEADER_GENERAL_SKILL_SPY_MASTER, 0);
                break;
            default: break;
        }
        return std::clamp<int32_t>(standing, -5, 5) + (leader ? LEADER_BONUS : 0);
    }

    int32_t Choose_Option(int16_t player, e_crisis crisis) {
        const s_option* options = Options(crisis);
        const int32_t preferred = Alive_(player) ? Preferred_(player) : -1;
        int32_t best = -1;
        int32_t best_score = -1000;
        for (int32_t i = 0; i < OPTION_COUNT; ++i) {
            const bool affordable = options[i].cost_bc == 0 || (Alive_(player) && MOX::player[player].bc >= options[i].cost_bc);
            if (!affordable) continue;
            int32_t score = dice::Success_Chance_Percent(static_cast<dice::e_roll_kind>(dice::roll_kind),
                Check_Bonus(player, static_cast<e_check>(options[i].check)), DIFFICULTY);
            if (options[i].check == preferred) score += PREFERRED_BONUS;
            if (score > best_score) {
                best = i;
                best_score = score;
            }
        }
        return best < 0 ? OPTION_COUNT - 1 : best;
    }

    bool Open(int16_t player, e_crisis crisis, int16_t colony, int32_t delay) {
        if (!enabled || !(Computer_(player) || Human_Asks_(player)) || crisis == CRISIS_NONE || crisis >= CRISIS_COUNT) return false;
        if (colony < 0) colony = Capital_(player);
        if (colony < 0) return false;
        s_active* free_slot = nullptr;
        for (int32_t i = 0; i < MAX_ACTIVE; ++i) {
            s_active& s = Slots()[i];
            if (s.crisis != CRISIS_NONE && s.player == player) return false;  // one crisis at a time
            if (s.crisis == CRISIS_NONE && free_slot == nullptr) free_slot = &s;
        }
        if (free_slot == nullptr) return false;
        free_slot->crisis = crisis;
        free_slot->player = static_cast<int8_t>(player);
        free_slot->colony = colony;
        free_slot->option = -1;
        free_slot->result = RESULT_PENDING;
        free_slot->roll = 0;
        free_slot->due = MOX::stardate + delay;
        if (!Computer_(player)) {
            // A human: the option waits for the window (a follow-up asks when it is due).
            if (delay == 0 && !Queue_Crisis_(*free_slot)) {
                *free_slot = s_active{};
                return false;
            }
            news::Add(news::KIND_CRISIS_START, player, -1, crisis);
            return true;
        }
        news::Add(news::KIND_CRISIS_START, player, -1, crisis);  // the news ring (off by default)
        if (delay == 0) Choose_(*free_slot);
        return true;
    }

    e_result Resolve_With_Dice(int32_t slot, const int32_t* dice_values) {
        if (!enabled || slot < 0 || slot >= MAX_ACTIVE) return RESULT_PENDING;
        s_active& s = Slots()[slot];
        if (s.crisis == CRISIS_NONE || s.option < 0 || !Alive_(s.player)) return RESULT_PENDING;
        const s_option& option = Options(static_cast<e_crisis>(s.crisis))[s.option];
        const dice::s_modifier modifiers[1] = {{"standing and leader", Check_Bonus(s.player, static_cast<e_check>(option.check))}};
        const dice::s_check check = dice_values != nullptr
            ? dice::Resolve_Check(static_cast<dice::e_roll_kind>(dice::roll_kind), dice_values, modifiers, 1, DIFFICULTY)
            : dice::Roll_Check(modifiers, 1, DIFFICULTY);
        s.roll = static_cast<int8_t>(std::clamp<int32_t>(check.total, -128, 127));
        s.result = check.outcome >= dice::OUTCOME_SUCCESS ? RESULT_SUCCESS
            : check.outcome == dice::OUTCOME_FAILURE ? RESULT_PARTIAL : RESULT_FAILURE;
        static const char* const RESULTS[4] = {"pending", "success", "partial success", "failure"};
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "crises: stardate %d, player %d: %s - %s: %s",
            (int)MOX::stardate, (int)s.player, Name(static_cast<e_crisis>(s.crisis)), RESULTS[s.result],
            dice::Describe_Check(option.name, check, modifiers, 1).c_str());
        const e_result result = static_cast<e_result>(s.result);
        news::Add(news::KIND_CRISIS_END, s.player, -1, s.crisis * 10 + s.result);  // the news ring (off by default)
        // A brilliant or triumphant solution gives the empire a special leader (events-catalog.md 3.11; heroes.cpp).
        if (check.outcome >= dice::OUTCOME_BRILLIANT) heroes::On_Crisis_Solved(s.player);
        if (!Apply_Outcome_(s)) s = s_active{};
        return result;
    }

    namespace {
        void Crisis_Turn_();
        void Situations_Turn_();
        void Front_Turn_();
    } // namespace

    // The three parts run in this order; each is under its own flag, so with the new flags off the old
    // crises draw exactly what they drew before.
    void Turn() {
        if (enabled) Crisis_Turn_();
        if (situations) Situations_Turn_();
        if (crisis_front || become_crisis) Front_Turn_();
    }

    namespace {
    void Crisis_Turn_() {
        for (int32_t i = 0; i < MAX_ACTIVE; ++i) {
            s_active& s = Slots()[i];
            if (s.crisis == CRISIS_NONE) continue;
            if (!Alive_(s.player)) {
                s = s_active{};
                continue;
            }
            if (s.due > MOX::stardate) continue;
            if (s.option < 0) {
                if (Computer_(s.player)) {
                    Choose_(s);  // a follow-up opens
                } else if (!Human_Asks_(s.player)) {
                    s = s_active{};  // the setting went off
                } else if (s.roll == 0) {
                    if (!Queue_Crisis_(s)) s = s_active{};  // a human's follow-up opens: ask
                } else if (!Pending_(s.player, CHOICE_CRISIS)) {
                    s = s_active{};  // the choice was dropped (its colony is gone)
                }
            } else {
                Resolve_With_Dice(i, nullptr);
            }
        }
        const int32_t turn = MOX::stardate - 35000;
        if (chance <= 0 || turn < MIN_TURN) return;
        for (int16_t p = 0; p < Players_(); ++p) {
            if (!(Computer_(p) || Human_Asks_(p)) || static_cast<int32_t>(game_random::Random(1000)) > chance) continue;
            const e_crisis crisis = static_cast<e_crisis>(1 + Pick_(3));
            const int16_t colony = crisis == CRISIS_ASTEROID ? Capital_(p) : Random_Colony_(p, crisis == CRISIS_REVOLUTIONARIES);
            if (colony >= 0) Open(p, crisis, colony);
        }
    }

    // ---- Wave 4, piece 7: situations and crises with a front ----

    constexpr s_approach APPROACHES[APPROACH_COUNT] = {
        {"propaganda", 5, 0, 3},
        {"martial law", 0, 3, 2},
        {"carry on", 0, 0, 0},
    };

    s_scale* Mutable_Scale_(e_scale kind, int16_t player) {
        return const_cast<s_scale*>(Find_Scale(kind, player));
    }

    s_scale* New_Scale_(e_scale kind, int16_t player) {
        for (s_scale& slot : Extra()->scale) {
            if (slot.kind != SCALE_NONE) continue;
            slot = s_scale{};
            slot.kind = kind;
            slot.player = static_cast<int8_t>(player);
            slot.started = MOX::stardate;
            return &slot;
        }
        return nullptr;
    }

    void Log_Line_(const char* what, int16_t player, int32_t value) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "crises: stardate %d, player %d: %s (%d)", (int)MOX::stardate, (int)player, what,
            (int)value);
    }

    int16_t Pops_(int16_t player) {
        int32_t pops = 0;
        for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
            if (MOX::colony[c].owner == player) pops += MOX::colony[c].n_pops;
        }
        return static_cast<int16_t>(std::min<int32_t>(pops, 30000));
    }

    // A human (events human_choices on) picks the approach in the window; until then it stands at 0.
    void Ask_Approach_(int16_t player) {
        if (Pending_(player, CHOICE_APPROACH)) return;
        choices::Add(Choice_(CHOICE_APPROACH, player, -1, -1));
    }

    // Decision 5 (iterations.md): "сообщение о подходе — только при его смене" - logged only when the approach changes.
    void Pick_Approach_(s_scale& scale) {
        const int8_t before = scale.approach;
        scale.approach = static_cast<int8_t>(Computer_(scale.player) ? Choose_Approach(scale.player) : 0);
        if (scale.approach == before) return;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "crises: stardate %d, player %d: war weariness - it takes the approach: %s",
            (int)MOX::stardate, (int)scale.player, APPROACHES[scale.approach].name);
    }

    // Decision 5: the war presses ("только в долгой войне или против сильного"; header of crises.h).
    bool War_Presses_(int16_t p, int32_t war_since) {
        if (war_since != 0 && MOX::stardate - war_since >= WEARY_LONG_TURNS) return true;
        const s_player& player = MOX::player[p];
        for (int16_t o = 0; o < Players_(); ++o) {
            if (o == p || !Alive_(o) || player.contact[o] == 0 || player.treaty[o] < TREATY_FIRST_WAR) continue;
            const int64_t theirs = MOX::player[o].total_ship_strength[p];
            const int64_t ours = player.total_ship_strength[o];
            if (theirs > 0 && theirs * 100 >= ours * WEARY_STRONGER_PERCENT) return true;
        }
        return false;
    }

    // Решение автора 28.11.2026 (iterations.md, «усталость от войны по правлению и по тому, кто напал»): the growth is
    // WEARY_WAR times the government's multiplier (events weariness_by_government) times the aggressor's multiplier
    // (events weariness_by_aggressor); democracy and federation also fade slower. See the constants in crises.h.
    bool Remembers_War_(int16_t p) {
        if (!weariness_by_government) return false;
        const int8_t government = MOX::player[p].traits[TRAIT_CURRENT_GOVERNMENT];
        return government == GOVERNMENT_DEMOCRACY || government == GOVERNMENT_FEDERATION;
    }

    int32_t Government_Percent_(int16_t p) {
        if (!weariness_by_government) return 100;
        const int8_t government = MOX::player[p].traits[TRAIT_CURRENT_GOVERNMENT];
        return government >= 0 && government < GOVERNMENT_COUNT ? WEARY_GOVERNMENT_PERCENT[government] : 100;
    }

    // The wars that press on the empire: in a long war every war, else the wars with a stronger enemy (War_Presses_).
    int32_t Aggressor_Percent_(int16_t p, bool long_war) {
        if (!weariness_by_aggressor) return 100;
        const s_player& player = MOX::player[p];
        const s_extra* extra = Extra();
        int32_t counted = 0;
        int32_t on_us = 0;
        for (int16_t o = 0; o < Players_(); ++o) {
            if (o == p || !Alive_(o) || player.contact[o] == 0 || player.treaty[o] < TREATY_FIRST_WAR) continue;
            const int64_t theirs = MOX::player[o].total_ship_strength[p];
            const int64_t ours = player.total_ship_strength[o];
            if (!long_war && !(theirs > 0 && theirs * 100 >= ours * WEARY_STRONGER_PERCENT)) continue;
            ++counted;
            if (((extra->declared[p] >> o) & 1) != 0) return WEARY_AGGRESSOR_PERCENT;
            if (((extra->declared[o] >> p) & 1) != 0) ++on_us;
        }
        return counted > 0 && on_us == counted ? WEARY_DEFENDER_PERCENT : 100;
    }

    // The growth in 1/10000 of a point: WEARY_WAR * government per cent * aggressor per cent.
    int32_t Growth_Basis_(int16_t p, int32_t war_since, int32_t* government, int32_t* aggressor) {
        const bool long_war = war_since != 0 && MOX::stardate - war_since >= WEARY_LONG_TURNS;
        *government = Government_Percent_(p);
        *aggressor = Aggressor_Percent_(p, long_war);
        return WEARY_WAR * *government * *aggressor;
    }

    // The scale of war weariness of every empire (events-catalog.md 3.9).
    // Решение автора (iterations.md, итерация 2, п. 3): «Усталость от войны: шкала гаснет постепенно, а не падает в ноль, когда война не давит (нет «мигания»); порог долгой войны — 15 ходов вместо 20, чтобы стадии изредка наступали».
    // A war that does not press lets the scale fade by WEARY_FADE a turn; it ends (and is logged) only at 0 while
    // the war does not press, and starts (logged) only when it is created.
    void War_Weariness_() {
        for (int16_t p = 0; p < Players_(); ++p) {
            s_extra* extra = Extra();
            if (extra->strike_until[p] != 0) {
                const int16_t strike_capital = Capital_(p);
                if (!Alive_(p) || MOX::stardate >= extra->strike_until[p]) {
                    extra->strike_until[p] = 0;
                    if (Alive_(p)) Log_Line_("war weariness: the general strike is over", p, 0);
                } else if (strike_capital >= 0) {
                    MOX::colony[strike_capital].production_spent = 0;
                }
            }
            if (!Alive_(p)) {
                extra->war_since[p] = 0;
                continue;
            }
            s_player& player = MOX::player[p];
            bool at_war = false;
            for (int16_t o = 0; o < Players_(); ++o) {
                if (o != p && Alive_(o) && player.contact[o] != 0 && player.treaty[o] >= TREATY_FIRST_WAR) at_war = true;
            }
            for (int16_t o = 0; o < Players_(); ++o) {  // the declarer's record ends with the war
                if (o == p || !(Alive_(o) && player.contact[o] != 0 && player.treaty[o] >= TREATY_FIRST_WAR)) {
                    extra->declared[p] = static_cast<uint8_t>(extra->declared[p] & ~(1u << o));
                }
            }
            if (!at_war) extra->war_since[p] = 0;
            else if (extra->war_since[p] == 0) extra->war_since[p] = MOX::stardate;
            const bool presses = at_war && War_Presses_(p, extra->war_since[p]);
            s_scale* scale = Mutable_Scale_(SCALE_WAR_WEARINESS, p);
            if (scale == nullptr) {
                if (!presses) continue;
                scale = New_Scale_(SCALE_WAR_WEARINESS, p);
                if (scale == nullptr) continue;
                Log_Line_("war weariness: the scale starts", p, scale->value);
                Pick_Approach_(*scale);
                if (Human_Asks_(p)) Ask_Approach_(p);
            }
            int32_t delta = -(Remembers_War_(p) ? WEARY_FADE_LONG_MEMORY : WEARY_FADE);
            if (presses) {
                const s_approach& approach = APPROACHES[std::clamp<int32_t>(scale->approach, 0, APPROACH_COUNT - 1)];
                int32_t government = 100;
                int32_t aggressor = 100;
                const int32_t basis = Growth_Basis_(p, extra->war_since[p], &government, &aggressor);
                delta = WEARY_WAR;
                if (government != 100 || aggressor != 100) {  // a whole number a turn, the fraction carried (no game_random)
                    const int32_t total = basis + extra->weary_rest[p];
                    delta = total / 10000;
                    extra->weary_rest[p] = static_cast<uint16_t>(total % 10000);
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "crises: stardate %d, player %d: war weariness +%d (government %d%%, aggressor %d%%)", (int)MOX::stardate,
                        (int)p, (int)delta, (int)government, (int)aggressor);
                }
                if (player.bc >= approach.cost_bc) {
                    player.bc -= approach.cost_bc;
                    player.research_accumulated = std::max<int32_t>(0, player.research_accumulated - approach.cost_research);
                    delta -= approach.relief;
                }
            }
            const int32_t stage = Scale_Move(*scale, delta);
            if (scale->value == 0 && !presses) {
                *scale = s_scale{};
                extra->weary_rest[p] = 0;
                Log_Line_("war weariness: the scale ends", p, 0);
                continue;
            }
            if (stage == 0) continue;
            const int16_t capital = Capital_(p);
            if (stage == 2 && capital >= 0) {
                MOX::colony[capital].production_spent = 0;
                Log_Line_("war weariness: strikes - the capital's output of the turn is lost", p, scale->value);
            } else if (stage == 3) {
                player.bc = std::max<int32_t>(0, player.bc - WEARY_FINE);
                Log_Line_("war weariness: unrest - the war fund is fined", p, scale->value);
            } else if (stage >= 4) {
                // Softened (ab-review.md 3.5): a strike of STRIKE_TURNS turns instead of the loss of a building.
                Extra()->strike_until[p] = MOX::stardate + STRIKE_TURNS;
                scale->value = WEARY_RESET;
                Log_Line_("war weariness: a general strike begins - the capital's output is lost for some turns", p, STRIKE_TURNS);
            }
            if (Computer_(p)) Pick_Approach_(*scale);
            else if (Human_Asks_(p)) Ask_Approach_(p);
        }
    }

    // ---- The academy contest (events-catalog.md 3.9, row "Академия"; header of crises.h) ----

    const char* const STAGE_NAMES[ACADEMY_STAGES] = {"science", "industry", "BC"};

    void Record_Baselines_(int32_t stage) {
        for (int16_t p = 0; p < Players_(); ++p) Extra()->baseline[p] = Alive_(p) ? Academy_Output(p, stage) : 0;
    }

    void Give_Prize_(int16_t p, int32_t stage) {
        s_player& player = MOX::player[p];
        if (stage == ACADEMY_SCIENCE) {
            player.research_accumulated += ACADEMY_PRIZE;
        } else if (stage == ACADEMY_INDUSTRY) {
            const int16_t capital = Capital_(p);
            if (capital >= 0) {
                MOX::colony[capital].production_spent = static_cast<int16_t>(std::min<int32_t>(30000, MOX::colony[capital].production_spent + ACADEMY_PRIZE));
            }
        } else {
            player.bc += ACADEMY_PRIZE;
        }
    }

    void Academy_Turn_() {
        s_extra* extra = Extra();
        if (extra->contest_stage == 0) {
            if (MOX::stardate - 35000 < MIN_TURN) return;
            if (static_cast<int32_t>(game_random::Random(1000)) > ACADEMY_CHANCE) return;
            Start_Academy();
            return;
        }
        if (MOX::stardate < extra->contest_ends) return;
        const int32_t stage = extra->contest_stage - 1;
        int16_t winner = -1;
        int32_t best = 0;
        bool tie = false;
        for (int16_t p = 0; p < Players_(); ++p) {
            if (!Alive_(p)) continue;
            const int32_t growth = Academy_Output(p, stage) - extra->baseline[p];
            if (growth > best) {
                best = growth;
                winner = p;
                tie = false;
            } else if (growth == best && winner >= 0) {
                tie = true;
            }
        }
        if (winner >= 0 && !tie) {
            Give_Prize_(winner, stage);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "crises: stardate %d, player %d: wins the academy's stage %d (%s grew by %d) and gets %d",
                (int)MOX::stardate, (int)winner, (int)extra->contest_stage, STAGE_NAMES[stage], (int)best, (int)ACADEMY_PRIZE);
        } else {
            Log_Line_("the academy's stage has no winner", -1, extra->contest_stage);
        }
        if (extra->contest_stage >= ACADEMY_STAGES) {
            extra->contest_stage = 0;
            extra->contest_ends = 0;
            for (int32_t& base : extra->baseline) base = 0;
            Log_Line_("the academy contest is over", -1, 0);
            return;
        }
        ++extra->contest_stage;
        extra->contest_ends = MOX::stardate + ACADEMY_STAGE_TURNS;
        Record_Baselines_(extra->contest_stage - 1);
    }

    void Situations_Turn_() {
        War_Weariness_();
        Academy_Turn_();
    }

    // Become a crisis (events-catalog.md 3.10; header of crises.h): the threat decays, and above the threshold
    // every computer empire that knows the culprit declares war on it, once.
    void Threat_() {
        s_extra* extra = Extra();
        for (int16_t p = 0; p < Players_(); ++p) {
            if (extra->threat[p] == 0) {
                extra->war_mask[p] = 0;
                continue;
            }
            if (!Alive_(p)) {
                extra->threat[p] = 0;
                extra->war_mask[p] = 0;
                continue;
            }
            if (extra->threat[p] >= THREAT_WAR) {
                for (int16_t o = 0; o < Players_(); ++o) {
                    if (o == p || !Computer_(o) || (extra->war_mask[p] & (1u << o)) != 0) continue;
                    if (MOX::player[o].contact[p] == 0 && MOX::player[p].contact[o] == 0) continue;
                    extra->war_mask[p] = static_cast<uint8_t>(extra->war_mask[p] | (1u << o));
                    if (MOX::player[o].treaty[p] >= TREATY_FIRST_WAR) continue;  // already at war
                    diplomac::Declare_War(o, p);
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "crises: stardate %d, player %d: declares war on player %d, whose atrocities threaten everyone (threat %d)",
                        (int)MOX::stardate, (int)o, (int)p, (int)extra->threat[p]);
                }
            }
            extra->threat[p] = static_cast<uint16_t>(std::max<int32_t>(0, extra->threat[p] - THREAT_DECAY));
            if (extra->threat[p] < THREAT_WAR) extra->war_mask[p] = 0;
        }
    }

    // `p` hands `gift` BC of trophies to the council's project.
    void Give_To_Council_(int16_t p, int32_t gift) {
        s_extra* extra = Extra();
        MOX::player[p].bc -= gift;
        extra->given[p] = static_cast<uint16_t>(std::min<int32_t>(65535, extra->given[p] + gift));
        extra->council_progress += gift;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "crises: stardate %d, player %d: hands %d BC of trophies to the council's project (%d of %d)",
            (int)MOX::stardate, (int)p, (int)gift, (int)extra->council_progress, (int)COUNCIL_GOAL);
    }

    void Antaran_Front_() {
        if (front_strength <= 0 || MOX::settings.xenons_exist == 0 || MOX::antarans.field6_0x3c != 0) return;
        s_extra* extra = Extra();
        if (extra->front_broken != 0) return;
        // Decision 1 (iterations.md): "на случайном ходу 180 ± 10"; drawn once, kept in the save.
        if (extra->front_start == 0) {
            extra->front_start = FRONT_START_TURN - FRONT_START_SPREAD
                + static_cast<int32_t>(game_random::Random(2 * FRONT_START_SPREAD + 1));
            Log_Line_("the Antaran front's start turn is drawn", -1, extra->front_start);
        }
        if (MOX::stardate - 35000 < extra->front_start) return;
        const int32_t strength = std::clamp<int32_t>(front_strength, 1, SCALE_MAX);
        const auto add_resources = [](int32_t amount) {
            MOX::antarans.resource_offensive = static_cast<int16_t>(std::min<int32_t>(RESOURCE_CAP, MOX::antarans.resource_offensive + amount));
        };
        s_scale* front = Mutable_Scale_(SCALE_ANTARAN_FRONT, -1);
        if (front == nullptr) {
            front = New_Scale_(SCALE_ANTARAN_FRONT, -1);
            if (front == nullptr) return;
            front->value = static_cast<int16_t>(std::clamp<int32_t>(front_strength, 1, SCALE_MAX));
            add_resources(front->value * FRONT_START_RESOURCE);  // grows with the strength
            Log_Line_("the Antaran front opens", -1, front->value);
            return;
        }
        if (MOX::stardate % FRONT_PERIOD == 0) {
            const int32_t stage = Scale_Move(*front, FRONT_STEP);
            if (stage > 0) {
                add_resources(stage * FRONT_STAGE_RESOURCE * strength);
                Log_Line_("the Antaran front advances: a new stage", -1, front->value);
            }
        }
        // The council's project (3.12): trophies against the crisis.
        if (MOX::stardate % COUNCIL_PERIOD != 1) return;
        for (int16_t p = 0; p < Players_(); ++p) {
            const int32_t gift = Council_Gift(p);
            if (gift == 0) {
                // A human is asked (the gift is given when the window answers).
                if (Human_Asks_(p) && MOX::player[p].bc >= COUNCIL_MIN_BC && !Pending_(p, CHOICE_COUNCIL)) {
                    choices::Add(Choice_(CHOICE_COUNCIL, p, -1, -1));
                }
                continue;
            }
            Give_To_Council_(p, gift);
        }
        if (extra->council_progress < COUNCIL_GOAL) return;
        for (int16_t p = 0; p < Players_(); ++p) {
            if (extra->given[p] == 0 || !Alive_(p)) continue;
            MOX::player[p].research_accumulated += extra->given[p] * COUNCIL_BONUS;
            Log_Line_("the council's project is done: research for the contributor", p, extra->given[p] * COUNCIL_BONUS);
        }
        MOX::antarans.resource_offensive = static_cast<int16_t>(MOX::antarans.resource_offensive / 2);
        front->value = static_cast<int16_t>(std::max<int32_t>(0, front->value - COUNCIL_PUSHBACK));
        extra->council_progress = 0;
        for (uint16_t& given : extra->given) given = 0;
        if (front->value == 0) {
            *front = s_scale{};
            extra->front_broken = 1;
            Log_Line_("the council has beaten the Antaran front", -1, 0);
        } else {
            Log_Line_("the council pushes the Antaran front back", -1, front->value);
        }
    }

    void Front_Turn_() {
        if (become_crisis) Threat_();
        if (crisis_front) Antaran_Front_();
    }
    } // namespace

    int32_t Scale_Move(s_scale& scale, int32_t delta) {
        const int32_t before = scale.value / SCALE_STAGE;
        scale.value = static_cast<int16_t>(std::clamp<int32_t>(scale.value + delta, 0, SCALE_MAX));
        const int32_t after = scale.value / SCALE_STAGE;
        return after > before ? after : 0;
    }

    const s_approach* Approaches() {
        return APPROACHES;
    }

    int32_t Weariness_Growth(int16_t player) {
        if (!situations || !Alive_(player)) return WEARY_WAR;
        int32_t government = 100;
        int32_t aggressor = 100;
        const int32_t basis = Growth_Basis_(player, Extra()->war_since[player], &government, &aggressor);
        return (basis + 5000) / 10000;
    }

    void On_War_Declared(int16_t declarer, int16_t target) {
        if (!situations || !weariness_by_aggressor || declarer < 0 || target < 0 || declarer >= MAX_PLAYERS || target >= MAX_PLAYERS
            || declarer == target) {
            return;
        }
        Extra()->declared[declarer] = static_cast<uint8_t>(Extra()->declared[declarer] | (1u << target));
    }

    int32_t Choose_Approach(int16_t player) {
        if (!Alive_(player)) return 0;
        switch (MOX::player[player].personality) {
            case PLAYER_PERSONALITY_AGGRESSIVE:
            case PLAYER_PERSONALITY_RUTHLESS: return 1;  // martial law
            default: break;
        }
        return MOX::player[player].bc >= 100 ? 0 : 1;  // propaganda when it can pay for it
    }

    bool Start_Academy() {
        if (!situations || Extra()->contest_stage != 0) return false;
        int32_t alive = 0;
        for (int16_t p = 0; p < Players_(); ++p) alive += Alive_(p) ? 1 : 0;
        if (alive < 2) return false;
        Extra()->contest_stage = 1;
        Extra()->contest_ends = MOX::stardate + ACADEMY_STAGE_TURNS;
        Record_Baselines_(0);
        Log_Line_("the academy contest begins: three stages, science first", -1, ACADEMY_STAGE_TURNS);
        return true;
    }

    int32_t Academy_Output(int16_t player, int32_t stage) {
        if (!Alive_(player)) return 0;
        const s_player& p = MOX::player[player];
        return stage == ACADEMY_SCIENCE ? p.research_produced : stage == ACADEMY_INDUSTRY ? p.industry_produced : p.bc_produced;
    }

    void Add_Threat(int16_t player) {
        if (!become_crisis || !Alive_(player)) return;
        uint16_t& threat = Extra()->threat[player];
        threat = static_cast<uint16_t>(std::min<int32_t>(THREAT_MAX, threat + THREAT_ATROCITY));
        Log_Line_("an atrocity raises the empire's threat to the galaxy", player, threat);
    }

    int32_t Threat(int16_t player) {
        return player >= 0 && player < MAX_PLAYERS ? Extra()->threat[player] : 0;
    }

    bool On_Strike(int16_t player) {
        return player >= 0 && player < MAX_PLAYERS && Extra()->strike_until[player] != 0;
    }

    int32_t Council_Gift(int16_t player) {
        if (!Computer_(player) || MOX::player[player].bc < COUNCIL_MIN_BC) return 0;
        switch (MOX::player[player].personality) {
            case PLAYER_PERSONALITY_AGGRESSIVE:
            case PLAYER_PERSONALITY_RUTHLESS:
            case PLAYER_PERSONALITY_XENOPHOBIC: return 0;
            case PLAYER_PERSONALITY_HONORABLE:
            case PLAYER_PERSONALITY_PACIFIST: return COUNCIL_GENEROUS;
            default: break;
        }
        return MOX::player[player].objectives == PLAYER_OBJECTIVE_TECHNOLOGIST ? COUNCIL_GENEROUS : COUNCIL_SHARE;
    }

    // ---- The window of a human (choices.h) ----

    bool Choice_Text(const choices::s_choice& c, choices::s_text* t) {
        const int16_t player = c.player;
        if (!Alive_(player)) return false;
        const s_player& me = MOX::player[player];
        auto N = [](int32_t v) { return std::to_string(v); };
        *t = choices::s_text{};
        const auto Add_Option = [&](const std::string& key, std::initializer_list<std::pair<const char*, std::string>> args) {
            texts::Format(t->option[t->count], sizeof(t->option[t->count]), key.c_str(), args);
            ++t->count;
        };
        switch (c.kind) {
            case CHOICE_CRISIS: {
                if (!enabled || Waiting_(player, c.other) == nullptr) return false;
                if (c.target < 0 || c.target >= MOX::NUM_COLONIES || c.target >= MAX_COLONIES || MOX::colony[c.target].owner != player) return false;
                const e_crisis crisis = static_cast<e_crisis>(c.other);
                static const char* const PARTS[CRISIS_COUNT] = {"", "asteroid", "parasites", "revolutionaries", "parasites_return", "revolution"};
                static const char* const OPTION_PARTS[CRISIS_COUNT] = {"", "asteroid", "parasites", "revolutionaries", "parasites", "revolutionaries"};
                static const char* const FIELDS[CHECK_COUNT] = {"science", "diplomacy", "army", "espionage"};
                if (crisis <= CRISIS_NONE || crisis >= CRISIS_COUNT) return false;
                const std::string prefix = std::string("choices.crises.") + PARTS[crisis] + ".";
                const std::string option_prefix = std::string("choices.crises.") + OPTION_PARTS[crisis] + ".";
                char place[64] = {};
                HACCESS::Do_Get_Planet_Name(MOX::colony[c.target].planet, place, 0);
                texts::Format(t->title, sizeof(t->title), (prefix + "title").c_str(), {});
                texts::Format(t->body, sizeof(t->body), (prefix + "body").c_str(), {{"place", std::string(place)}});
                const s_option* options = Options(crisis);
                for (int32_t i = 0; i < OPTION_COUNT; ++i) {
                    const s_option& o = options[i];
                    char field[48] = {};
                    texts::Format(field, sizeof(field), (std::string("choices.crises.field.") + FIELDS[o.check]).c_str(), {});
                    const int32_t chance = dice::Success_Chance_Percent(static_cast<dice::e_roll_kind>(dice::roll_kind),
                        Check_Bonus(player, static_cast<e_check>(o.check)), DIFFICULTY);
                    const bool poor = o.cost_bc > 0 && me.bc < o.cost_bc;
                    Add_Option(option_prefix + "option" + std::to_string(i + 1) + (poor ? "_poor" : ""),
                        {{"cost", N(o.cost_bc)}, {"field", std::string(field)}, {"chance", N(chance)}});
                }
                return true;
            }
            case CHOICE_APPROACH: {
                const s_scale* scale = Find_Scale(SCALE_WAR_WEARINESS, player);
                if (!situations || scale == nullptr) return false;
                texts::Format(t->title, sizeof(t->title), "choices.crises.approach.title", {});
                texts::Format(t->body, sizeof(t->body), "choices.crises.approach.body",
                    {{"value", N(scale->value)}, {"max", N(SCALE_MAX)}, {"fine", N(WEARY_FINE)}, {"turns", N(STRIKE_TURNS)}, {"growth", N(Weariness_Growth(player))}});
                const s_approach* approaches = Approaches();
                const int32_t growth = Weariness_Growth(player);
                Add_Option("choices.crises.approach.option1", {{"bc", N(approaches[0].cost_bc)}, {"net", N(growth - approaches[0].relief)}});
                Add_Option("choices.crises.approach.option2", {{"rp", N(approaches[1].cost_research)}, {"net", N(growth - approaches[1].relief)}});
                Add_Option("choices.crises.approach.option3", {{"net", N(growth - approaches[2].relief)}});
                return true;
            }
            case CHOICE_COUNCIL: {
                if (!crisis_front || Find_Scale(SCALE_ANTARAN_FRONT, -1) == nullptr || Extra()->front_broken != 0) return false;
                texts::Format(t->title, sizeof(t->title), "choices.crises.council.title", {});
                texts::Format(t->body, sizeof(t->body), "choices.crises.council.body",
                    {{"progress", N(Extra()->council_progress)}, {"goal", N(COUNCIL_GOAL)}, {"given", N(Extra()->given[player])},
                        {"bonus", N(COUNCIL_BONUS)}, {"push", N(COUNCIL_PUSHBACK)}});
                Add_Option("choices.crises.council.option1", {});
                Add_Option(me.bc >= COUNCIL_SHARE ? "choices.crises.council.option2" : "choices.crises.council.option2_poor",
                    {{"gift", N(COUNCIL_SHARE)}});
                Add_Option(me.bc >= COUNCIL_GENEROUS ? "choices.crises.council.option3" : "choices.crises.council.option3_poor",
                    {{"gift", N(COUNCIL_GENEROUS)}});
                return true;
            }
            default: return false;
        }
    }

    void Choice_Apply(const choices::s_choice& c, int32_t option) {
        const int16_t player = c.player;
        if (!Alive_(player)) return;
        option = std::clamp<int32_t>(option, 0, 2);
        switch (c.kind) {
            case CHOICE_CRISIS: {
                s_active* s = enabled ? Waiting_(player, c.other) : nullptr;
                if (s == nullptr || c.target < 0 || c.target >= MOX::NUM_COLONIES || MOX::colony[c.target].owner != player) return;
                const s_option& o = Options(static_cast<e_crisis>(s->crisis))[option];
                // An option the empire cannot pay for becomes the last, free one (a computer's fallback).
                if (o.cost_bc > 0 && MOX::player[player].bc < o.cost_bc) option = OPTION_COUNT - 1;
                Take_Option_(*s, option);
                break;
            }
            case CHOICE_APPROACH: {
                s_scale* scale = situations ? Mutable_Scale_(SCALE_WAR_WEARINESS, player) : nullptr;
                if (scale == nullptr || scale->approach == option) return;
                scale->approach = static_cast<int8_t>(option);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "crises: stardate %d, player %d: war weariness - it takes the approach: %s",
                    (int)MOX::stardate, (int)player, APPROACHES[option].name);
                break;
            }
            case CHOICE_COUNCIL: {
                if (!crisis_front || Find_Scale(SCALE_ANTARAN_FRONT, -1) == nullptr || Extra()->front_broken != 0) return;
                const int32_t gift = option == 1 ? COUNCIL_SHARE : option == 2 ? COUNCIL_GENEROUS : 0;
                if (gift > 0 && MOX::player[player].bc >= gift) Give_To_Council_(player, gift);
                break;
            }
            default: break;
        }
    }

} // namespace crises
