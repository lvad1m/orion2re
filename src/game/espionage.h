#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// The core of the spy network (docs/LocalLLM/mechanics.md 3.2.5.2; README.md 10.9, tier 3A):
// network points that spies build up in a foreign empire (spent by the operations of tier 3B), a
// free spy now and then for spy masters and neural scanners, counterintelligence modes, and a caught
// spy as a diplomatic incident. Off by default.
namespace espionage {

    extern bool network;                    // spy network
    extern int32_t points_per_spy;          // spy network_points_per_spy
    extern int32_t network_cap;             // spy network_cap
    extern int32_t free_spy_turns;          // spy free_spy_turns
    extern bool counterintelligence;        // spy counterintelligence
    extern int32_t witch_hunt_accusation;   // spy witch_hunt_accusation: per mille per leader per turn
    extern int32_t caught_opinion;          // spy caught_opinion

    enum e_mode : uint8_t {
        MODE_NORMAL = 0,
        MODE_HEIGHTENED = 1,
        MODE_WITCH_HUNT = 2,
        MODE_COUNT = 3,
    };

    // Effects of the modes (mechanics.md 3.2.5.2): defence against spies, morale points, share of
    // income paid each turn.
    constexpr int32_t MODE_DEFENSE[MODE_COUNT] = {0, 10, 25};
    constexpr int32_t MODE_MORALE[MODE_COUNT] = {0, -1, -3};
    constexpr int32_t MODE_COST_PERCENT[MODE_COUNT] = {0, 2, 4};
    constexpr int32_t RECENT_TURNS = 20;    // spies caught this long ago still count
    constexpr int32_t MODE_PERIOD = 10;     // turns between the computers' choices of mode

    // Network points of `owner`'s spies in `target` (0 with the flag off).
    int32_t Points(int16_t owner, int16_t target);
    // Spends `amount` points if there are enough; false (nothing spent) otherwise.
    bool Spend_Points(int16_t owner, int16_t target, int32_t amount);

    // `player`'s counterintelligence mode (MODE_NORMAL with the flag off); Set_Mode is for the rules
    // and, later, the human's spy screen.
    e_mode Mode(int16_t player);
    void Set_Mode(int16_t player, e_mode mode);

    // What the mode adds to `player`'s defence against spies (spy.cpp, after Compute_Spy_Bonuses).
    int32_t Defense_Bonus(int16_t player);
    // What the mode adds to the morale of `player`'s colonies (Colony_Morale).
    int32_t Morale_Effect(int16_t player);

    // A spy of `attacker` was caught in `victim`'s empire (spy.cpp Resolve_Player_Spies): the
    // victim remembers it, and the catch counts toward its choice of mode.
    void On_Spy_Caught(int16_t victim, int16_t attacker);

    // Once a turn, at the end of the spy step (spy.cpp Resolve_Spies): network points, the free
    // spy, the mode's cost, false accusations under a witch hunt (game_random draws only then), and
    // every MODE_PERIOD turns the computers' choice of mode.
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'SPYN' (game/rules_sections.h): payload version 1, then points (int16,
    // MAX_PLAYERS x MAX_PLAYERS), modes (uint8 x MAX_PLAYERS), spies caught recently (uint8 x
    // MAX_PLAYERS) and the turn of the last catch (int32 x MAX_PLAYERS).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

    // State access for the behaviour code (espionage_state.cpp).
    struct s_state {
        int16_t points[8][8] = {};
        uint8_t mode[8] = {};
        uint8_t caught[8] = {};
        int32_t last_caught[8] = {};
    };
    s_state* State();

} // namespace espionage
