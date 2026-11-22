#include "pch.h"

#include "game/crises.h"

#include "game/beliefs.h"
#include "game/bill.h"
#include "game/colcalc.h"
#include "game/dice.h"
#include "game/events.h"
#include "game/invasion.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/stability.h"
#include "game/techdata.h"

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

        void Choose_(s_active& s) {
            s.option = static_cast<int8_t>(Choose_Option(s.player, static_cast<e_crisis>(s.crisis)));
            const s_option& option = Options(static_cast<e_crisis>(s.crisis))[s.option];
            MOX::player[s.player].bc -= option.cost_bc;
            s.due = MOX::stardate + CHECK_DELAY;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "crises: stardate %d, player %d: %s at colony %d - it chooses to %s",
                (int)MOX::stardate, (int)s.player, Name(static_cast<e_crisis>(s.crisis)), (int)s.colony, option.name);
            if (option.cruel) Mark_Reputation_(s.player);
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
        if (!enabled || !Computer_(player) || crisis == CRISIS_NONE || crisis >= CRISIS_COUNT) return false;
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
        if (!Apply_Outcome_(s)) s = s_active{};
        return result;
    }

    void Turn() {
        if (!enabled) return;
        for (int32_t i = 0; i < MAX_ACTIVE; ++i) {
            s_active& s = Slots()[i];
            if (s.crisis == CRISIS_NONE) continue;
            if (!Alive_(s.player)) {
                s = s_active{};
                continue;
            }
            if (s.due > MOX::stardate) continue;
            if (s.option < 0) {
                Choose_(s);  // a follow-up opens
            } else {
                Resolve_With_Dice(i, nullptr);
            }
        }
        const int32_t turn = MOX::stardate - 35000;
        if (chance <= 0 || turn < MIN_TURN) return;
        for (int16_t p = 0; p < Players_(); ++p) {
            if (!Computer_(p) || static_cast<int32_t>(game_random::Random(1000)) > chance) continue;
            const e_crisis crisis = static_cast<e_crisis>(1 + Pick_(3));
            const int16_t colony = crisis == CRISIS_ASTEROID ? Capital_(p) : Random_Colony_(p, crisis == CRISIS_REVOLUTIONARIES);
            if (colony >= 0) Open(p, crisis, colony);
        }
    }

} // namespace crises
