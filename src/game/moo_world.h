#pragma once

#include "game/consts.h"

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Events of the MOO2 world (docs/LocalLLM/events-catalog.md 4.2; README.md 10.9, wave 5): the pirate king, the
// epidemic in a fleet and the spy scandal. The module's record table is general (kind, players, a star, a value,
// timers) so that the later events of the world can join it. A human-owned subject takes the first, safest option
// (no interface step yet); a computer picks by its personality (Choose). The shared pirate effects are reused
// (events.h: Pirates_At_Star_Turn, Pirate_Raid_Amount, Pirate_Raid_Loss).
// Off by default (events moo_world); with the flag off nothing here draws from game_random, nothing is kept.
namespace moo_world {

    extern bool enabled;  // events moo_world

    constexpr int32_t MAX_RECORDS = 16;

    enum e_kind : uint8_t {
        KIND_NONE = 0,
        KIND_PIRATE_KING = 1,     // record; target: the star, value: strength left, extra: strength it rose with
        KIND_FLEET_EPIDEMIC = 2,  // record; player: the empire whose fleet fell ill, target: the star it is grounded at
        KIND_LAST = 2,
    };

    // Pirate king: while at least two pirate presences (a lair of the pirate world, pirates hired in the world
    // events) have existed at once for UNITE_TURNS turns running, they unite: a king rises at the strongest one
    // (the lair's star), the strength is the sum, and the presences are gone. Every KING_PERIOD turns he raids every
    // empire with a colony within KING_REACH2 (squared map units: 6 parsecs of 45) of his star: the empire is
    // asked for tribute and answers by its personality (Choose): pays KING_TRIBUTE_PERCENT of the treasury (at
    // least KING_TRIBUTE_MIN) and is spared, haggles (half, accepted in KING_HAGGLE_PERCENT of the cases, else
    // raided) or refuses (raided at once). An empire too poor for the minimum is raided. A raid is the classic one
    // (Pirate_Raid_Amount) and KING_FREIGHTERS freighters lost. Warships at his star wear him down by their size
    // (Pirates_At_Star_Turn); when he is gone the empire with the most ship points there lands and gets
    // KING_LOOT + KING_LOOT_PER_STRENGTH * the strength he rose with. He disbands after KING_TURNS.
    constexpr int32_t UNITE_TURNS = 12;
    constexpr int32_t KING_PERIOD = 5;
    constexpr int32_t KING_REACH2 = 270 * 270;
    constexpr int32_t KING_TRIBUTE_PERCENT = 20;
    constexpr int32_t KING_TRIBUTE_MIN = 40;
    constexpr int32_t KING_HAGGLE_PERCENT = 50;
    constexpr int32_t KING_FREIGHTERS = 2;
    constexpr int32_t KING_LOOT = 100;
    constexpr int32_t KING_LOOT_PER_STRENGTH = 4;
    constexpr int32_t KING_TURNS = 60;

    // Epidemic in a fleet: a new one starts by chance, EPIDEMIC_CHANCE per mille a turn for each empire with a
    // fleet (at least FLEET_MIN_SHIPS ships in orbit at one star; the star with the most of its ships): no ship
    // can leave that star for EPIDEMIC_MIN_TURNS + 0..EPIDEMIC_SPAN_TURNS turns (2 to 5).
    constexpr int32_t EPIDEMIC_CHANCE = 3;
    constexpr int32_t FLEET_MIN_SHIPS = 3;
    constexpr int32_t EPIDEMIC_MIN_TURNS = 2;
    constexpr int32_t EPIDEMIC_SPAN_TURNS = 3;

    // Spy scandal: each ordered pair (owner, target) in which the owner keeps spies is exposed by chance,
    // SCANDAL_PER_SPY per mille a turn for each spy (a big network is easier to expose). All its spies there are
    // lost, the target remembers it (SCANDAL_OPINION, REASON_CAUGHT_SPYING) and every other empire in contact with
    // the owner hears bad rumours (SCANDAL_RUMOUR, REASON_BAD_RUMOURS).
    constexpr int32_t SCANDAL_PER_SPY = 2;
    constexpr int32_t SCANDAL_OPINION = -25;
    constexpr int32_t SCANDAL_RUMOUR = -8;

    struct s_record {
        uint8_t kind = KIND_NONE;
        int8_t player = -1;
        int16_t target = -1;
        int16_t other = -1;
        int32_t started = 0;
        int32_t ends = 0;
        int8_t option = 0;
        int16_t value = 0;
        int16_t extra = 0;
    };

    struct s_state {
        int32_t count = 0;
        // Turns running with at least two pirate presences and no king.
        int16_t unite_turns = 0;
        s_record record[MAX_RECORDS];
    };
    s_state* State();

    // The option a computer of `player` takes when the king asks for tribute: 0 pay (honorable, pacifist and
    // every human), 2 refuse (ruthless, aggressive), 1 haggle (the others). The flag off: 0.
    int32_t Choose(int16_t player);

    // Starts the epidemic in the fleet of `player` at `star` for `turns` turns; false when it cannot start (the
    // flag off, the star already grounded, the table full).
    bool Start_Epidemic(int16_t player, int16_t star, int32_t turns);
    // Exposes the network of `owner` in `target`; false with the flag off or without spies there.
    bool Start_Scandal(int16_t owner, int16_t target);
    // Raises a pirate king at `star` with `strength`; false when the flag is off, one rules already or the table
    // is full.
    bool Raise_King(int16_t star, int16_t strength);

    // True while a flagged star holds an epidemic (shipmove.cpp: no ship leaves it). False with the flag off.
    bool Star_Grounded(int16_t star);
    // True while a pirate king lives and `player` has a colony within his reach.
    bool King_Near(int16_t player);

    // Once a turn after the other events (nextturn.cpp): the records run and end, the pirates unite, a new
    // event may start (at most one a turn).
    void Turn();
    // The first half of Turn(): the records go one turn on (no new event starts, nobody unites).
    void Run_Records();

    void Clear();
    bool Empty();

    // Save section 'MOOW' (game/rules_sections.h): payload version 1, then the count (uint8), the unite turns
    // (int16), per record kind (uint8), player (int8), target and other (int16), started and ends (int32),
    // option (int8), value and extra (int16).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace moo_world
