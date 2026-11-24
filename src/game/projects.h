#pragma once

#include "game/consts.h"

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Research projects, the primitive of docs/LocalLLM/events-catalog.md sections 2 and 3.8 (README.md 10.9,
// wave 4): an empire studies an object at a star for a number of turns and, when the time is up, a dice
// check (dice.h: roll + skill against a difficulty) decides what the study yields. The skill is the
// empire's best scientist (a leader with the science skill, by level) plus a little for its research
// output. A project may have several stages (chapters): one check per stage. What a check yields is
// decided per kind inside this module. Events built on it: an anomaly found at an explored star, and
// the excavation of an ancient site (a colony at a star with ancient artifacts).
// No choices: a study needs none, so humans and computers take part alike. Off by default (events
// projects); with the flag off nothing here draws from game_random and nothing is kept.
namespace projects {

    extern bool enabled;  // events projects

    constexpr int32_t MAX_PROJECTS = 32;
    // Chances of a new project, in per mille a turn for each empire; at most one project starts a turn.
    constexpr int32_t PROJECT_CHANCE = 12;
    constexpr int32_t EXCAVATION_SHARE = 33;       // percent of new projects that are digs, when one is possible
    // Anomaly: one check after ANOMALY_TURNS.
    constexpr int32_t ANOMALY_TURNS = 5;
    constexpr int32_t ANOMALY_DIFFICULTY = 12;
    constexpr int32_t ANOMALY_RESEARCH = 100;       // research points on a success
    constexpr int32_t ANOMALY_BRILLIANT_RESEARCH = 300;  // when no technology is left to learn
    constexpr int32_t ANOMALY_LOSS_PERCENT = 10;    // of the accumulated research, on an exposed failure or a catastrophe
    // Excavation: 1..MAX_CHAPTERS chapters, a check at the end of each.
    constexpr int32_t MAX_CHAPTERS = 6;
    constexpr int32_t CHAPTER_TURNS = 4;
    constexpr int32_t DIG_DIFFICULTY = 11;
    constexpr int32_t DIG_MAX_FAILURES = 3;         // the dig is abandoned after this many failed chapters
    constexpr int32_t DIG_MAX_AGE = 150;            // a dig stalled by a guardian is abandoned after this long
    constexpr int32_t RELIC_RESEARCH = 200;         // research points when no technology is left to learn
    // The skill: the scientist's level, and one for each SKILL_RESEARCH_STEP of research output (at most 3).
    constexpr int32_t SKILL_RESEARCH_STEP = 40;
    constexpr int32_t SKILL_RESEARCH_MAX = 3;

    enum e_kind : uint8_t {
        KIND_NONE = 0,
        KIND_ANOMALY = 1,     // an anomaly at an explored star
        KIND_EXCAVATION = 2,  // chapters of a dig at an ancient site (SYSTEM_SPECIAL_ANCIENT_ARTIFACTS)
    };

    struct s_project {
        uint8_t kind = KIND_NONE;
        int8_t player = -1;
        int16_t star = -1;
        int32_t started = 0;
        uint8_t turns = 0;       // turns between checks
        uint8_t elapsed = 0;     // turns since the last check
        uint8_t chapters = 1;    // stages in all
        uint8_t stage = 0;       // stages done
        int8_t difficulty = 0;
        uint8_t failures = 0;
    };

    constexpr int32_t STUDIED_BYTES = BITMAP(MAX_STARS);

    struct s_state {
        int32_t count = 0;
        s_project project[MAX_PROJECTS];
        uint8_t studied[STUDIED_BYTES] = {};  // stars that have had a project: no second one there
    };
    s_state* State();

    // Projects of `player` that are running.
    int32_t Active(int16_t player);
    // The running project of `kind` of `player` at `star` (star -1: anywhere), or nullptr.
    const s_project* Find(e_kind kind, int16_t player, int16_t star);
    bool Studied(int16_t star);

    // The dice bonus of `player`'s scientists (see above).
    int32_t Skill(int16_t player);

    // Starts a project (the generic primitive): false when the flag is off, the table is full, the empire
    // is gone or the star is bad. A star gets only one project of the kind, ever (the studied map).
    bool Start(e_kind kind, int16_t player, int16_t star, int32_t turns, int32_t difficulty, int32_t chapters);
    // An anomaly at `star` (the empire must have explored it).
    bool Start_Anomaly(int16_t player, int16_t star);
    // A dig of `chapters` (1..MAX_CHAPTERS) at `star`: the star has ancient artifacts and `player` a colony there.
    bool Start_Excavation(int16_t player, int16_t star, int32_t chapters);

    // The check of the project at `index`: with `dice` (one value per die of dice::roll_kind) or, with
    // nullptr, rolled with game_random. Applies the outcome; finished projects are dropped.
    void Check_With_Dice(int32_t index, const int32_t* dice);

    // The cheapest technology application `player` may learn but does not know, -1 when none; its price
    // in research points (the cost of its field).
    int16_t Cheapest_Unknown_App(int16_t player);
    int32_t App_Cost(int16_t app);

    // Once a turn after world_events::Turn() (nextturn.cpp): projects advance and are checked, a new one
    // may start (the only draws from game_random, with the flag on).
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'PROJ' (game/rules_sections.h): payload version 1, then the count (uint8), per project
    // kind, player (int8), star (int16), started (int32), turns, elapsed, chapters, stage (uint8),
    // difficulty (int8), failures (uint8), then the studied-stars bitmap (BITMAP(MAX_STARS) bytes).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace projects
