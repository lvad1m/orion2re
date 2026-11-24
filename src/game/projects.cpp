#include "pch.h"

#include "game/projects.h"

#include "game/colcalc.h"
#include "game/dice.h"
#include "game/officer.h"
#include "game/random.h"
#include "game/ships.h"
#include "game/techdata.h"

#include <algorithm>
#include <climits>

// The behaviour of research projects and their events (events-catalog.md 2 and 3.8); the running projects
// and the save section are in projects_state.cpp. game_random is drawn only with events projects on: the
// dice of a check, and the chance of a new project each turn.
namespace projects {

    namespace {
        constexpr int8_t LEADER_POOL = 0;
        constexpr int8_t LEADER_ASSIGNED = 1;
        constexpr int8_t SHIP_STATUS_GONE = 5;
        constexpr int16_t GUARDIAN_TYPE = NONPLAYER_SHIP_TYPE_AMOEBA;  // a small monster, not the Orion guardian

        int16_t Players_() {
            return static_cast<int16_t>(std::min<int32_t>(MOX::NUM_PLAYERS, MAX_PLAYERS));
        }

        bool Alive_(int16_t player) {
            return player >= 0 && player < Players_() && MOX::player[player].eliminated == 0;
        }

        int32_t Pick_(int32_t n) {
            return n <= 1 ? 0 : static_cast<int32_t>(game_random::Random(n)) - 1;
        }

        void Log_(int16_t player, const char* what, int16_t star) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "projects: stardate %d, player %d: %s at star %d", (int)MOX::stardate, (int)player,
                what, (int)star);
        }

        void Remove_Record_(int32_t index) {
            s_state* s = State();
            for (int32_t i = index; i + 1 < s->count; ++i) s->project[i] = s->project[i + 1];
            s->project[--s->count] = s_project{};
        }

        bool Monster_At_(int16_t star) {
            for (int16_t i = 0; i < MOX::NUM_SHIPS; ++i) {
                const s_ship_data& ship = MOX::ship[i];
                if (ship.owner >= MAX_PLAYERS && ship.status < SHIP_STATUS_GONE && ship.location == star) return true;
            }
            return false;
        }

        int32_t Scientist_Level_(int16_t player) {
            int32_t best = 0;
            for (int16_t i = 0; i < MAX_LEADERS; ++i) {
                const s_leader_data& l = MOX::leaders[i];
                if (l.player_index != player || (l.status != LEADER_POOL && l.status != LEADER_ASSIGNED)) continue;
                if ((l.special_skills & LEADER_COLONY_SKILL_SCIENCE_LEADER) == 0 && (l.general_skills & LEADER_GENERAL_SKILL_RESEARCHER) == 0) continue;
                best = std::max<int32_t>(best, OFFICER::Owned_Officer_Level(i));
            }
            return best;
        }

        int32_t Research_Bonus_(int16_t player) {
            return std::clamp<int32_t>(MOX::player[player].research_produced / SKILL_RESEARCH_STEP, 0, SKILL_RESEARCH_MAX);
        }

        void Add_Research_(s_player& me, int32_t points) {
            me.research_accumulated = static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(me.research_accumulated) + points, 0, INT32_MAX / 2));
        }

        // A technology application if one is left, else research points. Returns the application or -1.
        int16_t Reward_(int16_t player, int32_t fallback_points) {
            s_player& me = MOX::player[player];
            const int16_t app = Cheapest_Unknown_App(player);
            if (app >= 0) {
                COLCALC::Player_Gets_Tech_App(&me, app);
            } else {
                Add_Research_(me, fallback_points);
            }
            return app;
        }

        void Anomaly_Outcome_(const s_project& p, dice::e_outcome outcome) {
            s_player& me = MOX::player[p.player];
            if (outcome >= dice::OUTCOME_BRILLIANT) {
                const int16_t app = Reward_(p.player, ANOMALY_BRILLIANT_RESEARCH);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "projects: stardate %d, player %d: the anomaly at star %d yields %s %d",
                    (int)MOX::stardate, (int)p.player, (int)p.star, app >= 0 ? "application" : "research points", app >= 0 ? (int)app : ANOMALY_BRILLIANT_RESEARCH);
            } else if (outcome == dice::OUTCOME_SUCCESS) {
                Add_Research_(me, ANOMALY_RESEARCH);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "projects: stardate %d, player %d: the anomaly at star %d yields %d research points",
                    (int)MOX::stardate, (int)p.player, (int)p.star, ANOMALY_RESEARCH);
            } else if (outcome <= dice::OUTCOME_EXPOSED) {
                const int32_t loss = me.research_accumulated * ANOMALY_LOSS_PERCENT / 100;
                Add_Research_(me, -loss);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "projects: stardate %d, player %d: the anomaly at star %d ruins an experiment, %d research points lost",
                    (int)MOX::stardate, (int)p.player, (int)p.star, (int)loss);
            } else {
                Log_(p.player, "the study of the anomaly finds nothing", p.star);
            }
        }

        // Updates the dig after a check; true when the project is over.
        bool Dig_Outcome_(s_project& p, dice::e_outcome outcome) {
            if (outcome >= dice::OUTCOME_BRILLIANT) {
                p.stage = static_cast<uint8_t>(std::min<int32_t>(p.chapters, p.stage + 2));
            } else if (outcome == dice::OUTCOME_SUCCESS) {
                ++p.stage;
            } else {
                ++p.failures;
                if (outcome <= dice::OUTCOME_EXPOSED && !Monster_At_(p.star) &&
                    SHIPS::Build_Nonplayer_Ship(GUARDIAN_TYPE, p.star, 0) >= 0) {
                    Log_(p.player, "a guardian wakes in the dig", p.star);
                }
            }
            p.elapsed = 0;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "projects: stardate %d, player %d: the dig at star %d: chapter %d of %d done, %d failed",
                (int)MOX::stardate, (int)p.player, (int)p.star, (int)p.stage, (int)p.chapters, (int)p.failures);
            if (p.stage >= p.chapters) {
                // The relic: a technology application; research points when nothing is left to learn.
                const int16_t app = Reward_(p.player, RELIC_RESEARCH);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "projects: stardate %d, player %d: the dig at star %d ends with a relic: %s %d",
                    (int)MOX::stardate, (int)p.player, (int)p.star, app >= 0 ? "application" : "research points", app >= 0 ? (int)app : RELIC_RESEARCH);
                return true;
            }
            if (p.failures >= DIG_MAX_FAILURES) {
                Log_(p.player, "the dig is abandoned after too many failures", p.star);
                return true;
            }
            return false;
        }

        // A star the empire has explored, has had no project and holds no running one.
        int32_t Anomaly_Stars_(int16_t player, int16_t* stars) {
            int32_t n = 0;
            const uint8_t bit = static_cast<uint8_t>(1u << player);
            for (int16_t s = 0; s < MOX::NUM_STARS && s < MAX_STARS; ++s) {
                if ((MOX::star[s].visited & bit) != 0 && !Studied(s)) stars[n++] = s;
            }
            return n;
        }

        // Stars with ancient artifacts where the empire has a colony and nobody has dug.
        int32_t Dig_Stars_(int16_t player, int16_t* stars) {
            int32_t n = 0;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                const s_colony& colony = MOX::colony[c];
                if (colony.owner != player || colony.planet < 0) continue;
                const int16_t star = MOX::planet[colony.planet].star_index;
                if (star < 0 || star >= MAX_STARS || MOX::star[star].system_special != SYSTEM_SPECIAL_ANCIENT_ARTIFACTS || Studied(star)) continue;
                bool seen = false;
                for (int32_t i = 0; i < n && !seen; ++i) seen = stars[i] == star;
                if (!seen) stars[n++] = star;
            }
            return n;
        }

        void Spawn_() {
            for (int16_t p = 0; p < Players_(); ++p) {
                if (!Alive_(p) || static_cast<int32_t>(game_random::Random(1000)) > PROJECT_CHANCE) continue;
                if (Active(p) >= 3) continue;
                int16_t digs[MAX_COLONIES];
                int16_t anomalies[MAX_STARS];
                const int32_t dig_count = Dig_Stars_(p, digs);
                const int32_t anomaly_count = Anomaly_Stars_(p, anomalies);
                if (dig_count > 0 && (anomaly_count == 0 || Pick_(100) < EXCAVATION_SHARE)) {
                    Start_Excavation(p, digs[Pick_(dig_count)], 1 + Pick_(MAX_CHAPTERS));
                } else if (anomaly_count > 0) {
                    Start_Anomaly(p, anomalies[Pick_(anomaly_count)]);
                } else {
                    continue;
                }
                break;  // at most one new project a turn
            }
        }
    } // namespace

    int32_t Skill(int16_t player) {
        if (player < 0 || player >= MAX_PLAYERS) return 0;
        return Scientist_Level_(player) + Research_Bonus_(player);
    }

    bool Start(e_kind kind, int16_t player, int16_t star, int32_t turns, int32_t difficulty, int32_t chapters) {
        if (!enabled || (kind != KIND_ANOMALY && kind != KIND_EXCAVATION) || !Alive_(player)) return false;
        if (star < 0 || star >= MAX_STARS || star >= MOX::NUM_STARS || Studied(star)) return false;
        if (turns < 1 || turns > 255 || chapters < 1 || chapters > MAX_CHAPTERS || difficulty < -100 || difficulty > 100) return false;
        s_state* s = State();
        if (s->count >= MAX_PROJECTS) return false;
        s_project& p = s->project[s->count++];
        p = s_project{};
        p.kind = kind;
        p.player = static_cast<int8_t>(player);
        p.star = star;
        p.started = MOX::stardate;
        p.turns = static_cast<uint8_t>(turns);
        p.chapters = static_cast<uint8_t>(chapters);
        p.difficulty = static_cast<int8_t>(difficulty);
        s->studied[star >> 3] = static_cast<uint8_t>(s->studied[star >> 3] | (1u << (star & 7)));
        return true;
    }

    bool Start_Anomaly(int16_t player, int16_t star) {
        if (!enabled || !Alive_(player) || star < 0 || star >= MAX_STARS) return false;
        if (((MOX::star[star].visited >> player) & 1) == 0) return false;
        if (!Start(KIND_ANOMALY, player, star, ANOMALY_TURNS, ANOMALY_DIFFICULTY, 1)) return false;
        Log_(player, "an anomaly is found, the study begins", star);
        return true;
    }

    bool Start_Excavation(int16_t player, int16_t star, int32_t chapters) {
        if (!enabled || !Alive_(player) || star < 0 || star >= MAX_STARS) return false;
        if (MOX::star[star].system_special != SYSTEM_SPECIAL_ANCIENT_ARTIFACTS) return false;
        bool colony_here = false;
        for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES && !colony_here; ++c) {
            const s_colony& colony = MOX::colony[c];
            colony_here = colony.owner == player && colony.planet >= 0 && MOX::planet[colony.planet].star_index == star;
        }
        if (!colony_here || !Start(KIND_EXCAVATION, player, star, CHAPTER_TURNS, DIG_DIFFICULTY, chapters)) return false;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "projects: stardate %d, player %d: a dig of %d chapters begins at star %d", (int)MOX::stardate,
            (int)player, (int)chapters, (int)star);
        return true;
    }

    void Check_With_Dice(int32_t index, const int32_t* dice_values) {
        s_state* s = State();
        if (index < 0 || index >= s->count) return;
        s_project& p = s->project[index];
        const dice::s_modifier modifiers[2] = {{"scientist level", Scientist_Level_(p.player)}, {"research output", Research_Bonus_(p.player)}};
        const dice::e_roll_kind kind = static_cast<dice::e_roll_kind>(dice::roll_kind);
        const dice::s_check check = dice_values != nullptr ? dice::Resolve_Check(kind, dice_values, modifiers, 2, p.difficulty)
                                                           : dice::Roll_Check(modifiers, 2, p.difficulty);
        const std::string action = p.kind == KIND_ANOMALY ? "study the anomaly" : "dig chapter " + std::to_string(p.stage + 1);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "projects: stardate %d, player %d: star %d, %s", (int)MOX::stardate, (int)p.player, (int)p.star,
            dice::Describe_Check(action.c_str(), check, modifiers, 2).c_str());
        bool over = true;
        if (p.kind == KIND_ANOMALY) {
            Anomaly_Outcome_(p, check.outcome);
        } else {
            over = Dig_Outcome_(p, check.outcome);
        }
        if (over) Remove_Record_(index);
    }

    int32_t App_Cost(int16_t app) {
        if (app <= TECH_APP_NO_TECH || app >= TECH_APP_COUNT) return INT32_MAX;
        const int16_t field = TECHDATA::technology_applications[app].tech_field_id;
        if (field < 0 || field >= TECH_FIELD_COUNT) return INT32_MAX;
        return static_cast<int32_t>(TECHDATA::technology_fields[field].cost);
    }

    int16_t Cheapest_Unknown_App(int16_t player) {
        if (player < 0 || player >= MAX_PLAYERS) return -1;
        s_player* me = &MOX::player[player];
        int16_t best = -1;
        for (int16_t app = TECH_APP_NO_TECH + 1; app < TECH_APP_COUNT; ++app) {
            if (me->tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED || !COLCALC::Player_May_Get_Tech_App(me, app)) continue;
            if (best < 0 || App_Cost(app) < App_Cost(best)) best = app;
        }
        return best;
    }

    void Turn() {
        if (!enabled) return;
        s_state* s = State();
        for (int32_t i = 0; i < s->count;) {
            s_project& p = s->project[i];
            if (!Alive_(p.player)) {
                Log_(p.player, "the project is dropped, the empire is gone", p.star);
                Remove_Record_(i);
                continue;
            }
            if (p.kind == KIND_EXCAVATION && MOX::stardate - p.started >= DIG_MAX_AGE) {
                Log_(p.player, "the dig is abandoned, it took too long", p.star);
                Remove_Record_(i);
                continue;
            }
            if (p.kind == KIND_EXCAVATION && Monster_At_(p.star)) {  // the work waits until the guardian is gone
                ++i;
                continue;
            }
            ++p.elapsed;
            if (p.elapsed >= p.turns) {
                const int32_t before = s->count;
                Check_With_Dice(i, nullptr);
                if (s->count < before) continue;  // the record at i is a new one
            }
            ++i;
        }
        Spawn_();
    }

} // namespace projects
