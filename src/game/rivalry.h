#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Rivalry (docs/LocalLLM/mechanics.md 3.1.3; README.md 10.9, tier 1): an empire formally declares
// another its rival. While the rivalry holds, the rival's opinion of the declarer carries "declared
// us its rival" (opinion.h; needs diplomacy opinion), and the declarer counts its fleet against the
// rival at `strength_percent` when it weighs war (Fleet_Strength_Comparison). Computer empires
// declare and drop rivals by rules every `period` turns. Off by default.
namespace rivalry {

    extern bool enabled;             // diplomacy rivalry
    extern int32_t period;           // diplomacy rivalry_period: turns between reviews
    extern int32_t opinion_value;    // diplomacy rivalry_opinion: the rival's opinion of the declarer
    extern int32_t strength_percent; // diplomacy rivalry_strength_percent: our fleet against the rival
    extern int32_t hatred;           // diplomacy rivalry_hatred: opinion at or below which a rival is declared

    constexpr int16_t NO_RIVAL = -1;

    // `player`'s declared rival, NO_RIVAL when none (or with the flag off).
    int16_t Rival_Of(int16_t player);

    // The turn `player` declared its rival (0 when none).
    int32_t Rival_Since(int16_t player);

    // Declares or drops a rivalry (the rules below and, later, the human's diplomacy screen).
    // Declaring replaces an earlier rival. Logs "rivalry: ...".
    void Declare(int16_t player, int16_t rival);
    void Drop(int16_t player);

    // Fleet_Strength_Comparison multiplies `player`'s strength against `target` by this, in
    // percent: strength_percent against its declared rival, 100 otherwise.
    int32_t Strength_Percent(int16_t player, int16_t target);

    // Once a turn, after the opinion step: keeps each rival's "declared us its rival" opinion at
    // full value, and every `period` turns reviews the computer empires' rivalries:
    // - a rivalry ends when the rival is gone, an alliance or peace was made with it, our opinion
    //   of it rose above zero, or (for an empire with an active advisor directive) the directive's
    //   main rival is another empire;
    // - an empire without a rival declares one: with an active advisor directive, its main rival
    //   when the attitude toward it is hostile or war; otherwise the contact in range with the
    //   worst opinion, if that is at or below `hatred`.
    void End_Of_Turn();

    void Clear();
    bool Empty();

    // Save section 'RIVL' (game/rules_sections.h): payload version 1, then for each of
    // MAX_PLAYERS players its rival (int8) and the turn it was declared (int32).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace rivalry
