#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Leader loyalty (docs/LocalLLM/mechanics.md 3.2.5 and 3.2.5.1; README.md 10.9, tier 2): every
// leader an empire holds has a loyalty of 0..100, 50 when it comes into the empire's service. It
// grows with service, promotions, victories and a contented empire, and falls with idleness in
// reserve, defeats, a discontented or war-weary empire, and being passed over. In tier 2 it only
// builds up and is visible; bribery and false accusations (tier 3) read it. Off by default.
namespace loyalty {

    extern bool enabled;          // loyalty leaders
    extern int32_t period;        // loyalty period: turns per step of service and mood

    constexpr int8_t UNTRACKED = -1;
    constexpr int32_t START = 50;
    constexpr int32_t LEVEL_UP = 10;
    constexpr int32_t VICTORY = 5;
    constexpr int32_t DEFEAT = -5;
    constexpr int32_t PASSED_OVER = -5;
    constexpr int32_t IDLE_TURNS = 20;
    constexpr int32_t MORALE_GOOD = 4;    // average colony morale points
    constexpr int32_t WEARY = 20;         // diplomemory war weariness

    // The loyalty of leader `leader` (0..100), UNTRACKED when no empire holds it or with the flag off.
    int32_t Of(int16_t leader);

    // Once a turn (nextturn.cpp, with the officers' steps): starts tracking leaders that came into
    // an empire's service, forgets those that left it, adds promotions, counts idle turns, and
    // every `period` turns adds service, idleness, the empire's mood and war weariness.
    void Turn();

    // A battle at `star` won by `winner` over `loser` (combfind Do_1_Combat): ship captains of
    // either side whose ship is at that star gain VICTORY or DEFEAT.
    void On_Battle(int16_t star, int16_t winner, int16_t loser);

    // `leader` was given a post (officer.cpp Assign_Leader_To_Ship / _Star): idle leaders of the
    // same empire and kind with a higher level are passed over.
    void On_Assigned(int16_t leader);

    void Clear();
    bool Empty();

    // Save section 'LOYL' (game/rules_sections.h): payload version 1, then per leader
    // (MAX_LEADERS): loyalty (int8, -1 untracked), owner (int8), last level (int8), idle turns (int16).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

    // State access for the behaviour code (loyalty_state.cpp).
    struct s_record {
        int8_t loyalty = UNTRACKED;
        int8_t owner = -1;
        int8_t last_level = 0;
        int16_t idle = 0;
    };
    s_record* Record(int16_t leader);

} // namespace loyalty
