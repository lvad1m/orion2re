#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Stains and titles (docs/LocalLLM/mechanics.md 3.5.6 and events-catalog.md 3.12, priority A;
// README.md 10.9, the A and B scope). Every empire sees an empire's deeds as a reputation:
// - stains: an oathbreaker (broke an alliance, or two treaties within 50 turns), a kingslayer (a
//   leader of another empire killed by its spies), "the North remembers" (an atrocity - biological
//   weapons, a destroyed planet, poisoned water - which every empire remembers and hardly forgets);
// - titles for good conduct: keeps its word (no broken treaty in 100 turns, two treaties held 30
//   turns), a peacemaker (two peaces made in 100 turns);
// - titles by style, the one leader of the galaxy in each: a trade prince (most trade treaties), a
//   warlord (by far the strongest fleet), a corsair (most raids and sabotage in 50 turns).
// Rivals react: a title adds to (or takes from) the relations every computer empire reads
// (opinion::Relations), the more for characters it touches; the advisor's view shows them.
// Off by default; with the flag off nothing is kept and Relations is unchanged. Nothing here draws
// from game_random.
namespace reputation {

    extern bool enabled;   // diplomacy titles

    enum e_title : uint8_t {
        TITLE_OATHBREAKER = 0,
        TITLE_KINGSLAYER = 1,
        TITLE_ATROCITIES = 2,      // "the North remembers"
        TITLE_KEEPS_WORD = 3,
        TITLE_PEACEMAKER = 4,
        TITLE_TRADE_PRINCE = 5,
        TITLE_WARLORD = 6,
        TITLE_CORSAIR = 7,
        TITLE_COUNT = 8,
    };

    // Programming choices (README.md 10.9: tuned on the big bench).
    constexpr int32_t OATHBREAKER_TURNS = 100;
    constexpr int32_t BREACH_WINDOW = 50;          // two broken treaties within it make an oathbreaker
    constexpr int32_t KINGSLAYER_TURNS = 100;
    constexpr int32_t ATROCITY_TURNS = 300;
    constexpr int32_t ATROCITY_OPINION = -40;      // every computer empire's memory (opinion::REASON_ATROCITY)
    constexpr int32_t BROKE_WORD_OPINION = -30;    // the victim's memory (opinion::REASON_BROKE_ITS_WORD)
    constexpr int32_t GOOD_CONDUCT_WINDOW = 100;
    constexpr int32_t KEPT_TREATY_TURNS = 30;
    constexpr int32_t DEEDS_WINDOW = 50;
    constexpr int32_t CORSAIR_MIN_DEEDS = 3;
    constexpr int32_t TRADE_PRINCE_MIN_TREATIES = 3;
    constexpr int32_t WARLORD_LEAD_PERCENT = 150;  // the fleet against the second strongest
    constexpr int32_t STYLE_PERIOD = 25;           // turns between the style titles' reviews
    constexpr int32_t PERIOD = 10;                 // turns between the good-conduct reviews
    constexpr int32_t BIAS_LIMIT = 50;

    const char* Title_Name(e_title title);
    // `player` holds `title` now (false with the flag off).
    bool Has(int16_t player, e_title title);
    // Bit `title` set for each title `player` holds (0 with the flag off).
    uint32_t Titles(int16_t player);
    // What `target`'s titles add to `holder`'s relations with it (opinion::Relations), by `holder`'s
    // character; within +-BIAS_LIMIT; 0 with the flag off or for a human holder.
    int32_t Relations_Bias(int16_t holder, int16_t target);

    // Deeds.
    // `breaker` broke a treaty `treaty` (s_player::treaty value, 1 NAP .. 3 peace) with `victim`
    // (diplomac.cpp Break_Treaties).
    void On_Treaty_Broken(int16_t breaker, int16_t victim, int32_t treaty);
    // `player` used biological weapons, destroyed a planet or poisoned water.
    void On_Atrocity(int16_t player);
    // `player`'s spies killed a leader of another empire and it became known.
    void On_Leader_Killed(int16_t player);
    // A raid or a sabotage by `player` (a saboteur's strike, a spy operation that harmed, a war paid
    // for, a price on a head): counts toward the corsair.
    void Note_Deed(int16_t player);

    // Once a turn (diplomac.cpp End_Of_Turn_Diplomacy_Adjustments): peaces made are noted, stains
    // run out, every PERIOD turns good conduct and every STYLE_PERIOD turns the style titles are
    // reviewed.
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'REPU' (game/rules_sections.h): payload version 1, then per player the stardate
    // each title runs out (int32 x TITLE_COUNT), the last two broken treaties and the last two peaces
    // (int32 x 2 each), the deeds of the current window (int16), the empires it was at war with last
    // turn (uint8 bits); then the window's start (int32).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

    struct s_state {
        int32_t until[8][TITLE_COUNT] = {};
        int32_t breaches[8][2] = {};
        int32_t peaces[8][2] = {};
        int16_t deeds[8] = {};
        uint8_t at_war[8] = {};
        int32_t deeds_window = 0;
    };
    s_state* State();

} // namespace reputation
