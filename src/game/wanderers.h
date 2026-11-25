#pragma once

#include "game/consts.h"

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Wanderers, the primitives "moving threat" and "NPC ship" of docs/LocalLLM/events-catalog.md section 2
// (README.md 10.9, wave 4): an object that is at a star, moves to the next star of its route every few
// turns and acts where it is. It is not a ship of the game's ship table, only a record here: a position,
// a route of up to MAX_ROUTE stars and the stardate of its next move. What it does on arrival and what
// it does to the star while it is there is decided per kind inside this module. Events built on it: the
// space storm (events-catalog 3.1) and the wandering trader (3.3).
// Wave 6, ab-review 2.7. The row (events-catalog 3.1): "буря движется к системе, вредит колониям и блокирует
// прыжки, оставляет след на несколько ходов; постройки-громоотводы". The author's decision: "громоотвод -
// существующая постройка колонии (например, планетарный щит) снижает урон бури". Built: a colony that has a
// planetary shield (radiation, barrier or flux: Has_Lightning_Rod) is hit by the storm with STORM_ROD_HIT_PERCENT
// instead of STORM_HIT_PERCENT (no new building, no extra draw).
// Off by default (events wanderers); with the flag off nothing here draws from game_random, nothing is
// kept and Star_Grounded is false. A human-owned subject takes the first, safest option: a human empire
// never buys from the trader (no interface step yet).
namespace wanderers {

    extern bool enabled;  // events wanderers

    constexpr int32_t MAX_WANDERERS = 16;
    constexpr int32_t MAX_ROUTE = 8;
    // A new wanderer starts by chance, in per mille a turn for the galaxy (one draw; at most one starts a turn).
    constexpr int32_t STORM_CHANCE = 6;
    constexpr int32_t TRADER_CHANCE = 4;
    constexpr int32_t MAX_STORMS = 2;               // at once
    constexpr int32_t MAX_TRADERS = 1;
    // Storm: moves every STORM_MOVE_TURNS along a route of nearest-neighbour hops toward a colony; on
    // arrival each colony there is hit (an earthquake's hit: population or buildings) with STORM_HIT_PERCENT;
    // no ship leaves the star where it is, nor the star it left STORM_TRAIL_TURNS ago; it dissolves at the
    // end of its route.
    constexpr int32_t STORM_MOVE_TURNS = 2;
    constexpr int32_t STORM_HIT_PERCENT = 40;
    constexpr int32_t STORM_TRAIL_TURNS = 3;
    constexpr int32_t STORM_NEIGHBOURS = 4;         // a hop goes to one of the nearest stars
    constexpr int32_t STORM_ROD_HIT_PERCENT = 10;   // the hit chance of a colony with a lightning rod
    // Trader: visits up to TRADER_STOPS colony stars, one stop every TRADER_MOVE_TURNS; every computer empire
    // with a colony there may buy one technology application it does not know (the cheapest by its field's
    // cost) for the cost / 2 BC, between TRADER_MIN_PRICE and TRADER_MAX_PRICE, keeping TRADER_RESERVE BC.
    constexpr int32_t TRADER_MOVE_TURNS = 4;
    constexpr int32_t TRADER_STOPS = 5;
    constexpr int32_t TRADER_RESERVE = 150;
    constexpr int32_t TRADER_MIN_PRICE = 40;
    constexpr int32_t TRADER_MAX_PRICE = 400;

    enum e_kind : uint8_t {
        KIND_NONE = 0,
        KIND_STORM = 1,   // a moving threat
        KIND_TRADER = 2,  // a neutral NPC ship; value: BC it has earned
    };

    struct s_wanderer {
        uint8_t kind = KIND_NONE;
        int16_t star = -1;           // where it is now: route[step]
        int8_t step = 0;
        int8_t length = 0;           // stars on the route
        int16_t route[MAX_ROUTE];
        int32_t started = 0;
        int32_t next_move = 0;       // the stardate it moves on (or, at the last star, dissolves)
        int16_t trail_star = -1;     // storm: the star it left last, grounded until trail_until
        int32_t trail_until = 0;
        int32_t value = 0;

        s_wanderer() {
            for (int16_t& r : route) r = -1;
        }
    };

    struct s_state {
        int32_t count = 0;
        s_wanderer wanderer[MAX_WANDERERS];
    };
    s_state* State();

    // The first wanderer of `kind` at `star`, or nullptr.
    const s_wanderer* At(e_kind kind, int16_t star);
    // True while a storm is at `star` or left it less than STORM_TRAIL_TURNS ago (false with the flag off):
    // shipmove.cpp - no ship leaves such a star, as at a flared one.
    bool Star_Grounded(int16_t star);
    // Whether the colony has a lightning rod: a planetary shield (radiation, barrier or flux shield).
    bool Has_Lightning_Rod(int16_t colony);

    // The primitive: puts a wanderer of `kind` on `route` (`length` stars, 2..MAX_ROUTE), a move every
    // `move_turns`; it arrives at the first star at once (the arrival action runs). Returns its index, -1
    // when the flag is off, the table is full or the route is bad.
    int32_t Add(e_kind kind, const int16_t* route, int32_t length, int32_t move_turns);
    // A route of nearest-neighbour hops from `from` toward `to` (at most MAX_ROUTE stars, `from` first; it
    // ends at `to` when the hops reach it); returns the length.
    int32_t Greedy_Route(int16_t from, int16_t to, int16_t* route);

    // A storm from a random star toward a random colony, a trader over colony stars (they draw from
    // game_random). False when none can start.
    bool Start_Storm();
    bool Start_Trader();

    // Once a turn after projects::Turn() (nextturn.cpp): wanderers move, act and dissolve; a new one may
    // start (the draws from game_random, with the flag on).
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'WAND' (game/rules_sections.h): payload version 1, then the count (uint8), per wanderer
    // kind (uint8), star (int16), step and length (int8), MAX_ROUTE route stars (int16), started and
    // next_move (int32), trail_star (int16), trail_until and value (int32).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace wanderers
