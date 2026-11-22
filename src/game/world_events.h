#pragma once

#include <cstdint>
#include <string>

struct s_colony;

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// New events, first wave (docs/LocalLLM/events-catalog.md sections 2, 4.1 and 6; mechanics.md
// 3.2.4; README.md 10.9, tier 5A): an event table of its own without the classic one slot per event
// type, temporary modifiers of the galaxy and of empires, a bankruptcy situation with a progress
// scale, and single events built from the classic primitives - galaxy events (golden age, tourist
// boom, ore rush), empire events (an ambassador killed, a discoverer, an ancient wreck) and one rare
// event for each of the 13 stock races. Off by default; with the flag off nothing here draws from
// game_random and no state is kept.
namespace world_events {

    extern bool enabled;           // events new_events
    extern int32_t galaxy_chance;  // events galaxy_event_chance: per mille a turn
    extern int32_t empire_chance;  // events empire_event_chance: per mille a turn for each empire

    enum e_kind : uint8_t {
        KIND_NONE = 0,
        // Galaxy modifiers (player -1) and one-off galaxy events.
        KIND_GOLDEN_AGE = 1,         // each empire gets 20 % of its positive surplus as BC every turn
        KIND_TOURIST_BOOM = 2,       // each empire gets 5 BC a turn for every trade treaty
        KIND_ORE_RUSH = 3,           // one-off: three planets get two mineral classes richer
        // One-off empire events.
        KIND_AMBASSADOR_KILLED = 10, // player: the host; target: the ambassador's empire
        KIND_DISCOVERER = 11,        // the stars near a colony of the empire become known
        KIND_ANCIENT_WRECK = 12,     // a technology application another empire knows
        // Race events: player; value = e_race_event; modifiers last MODIFIER_TURNS.
        KIND_RACE_EVENT = 20,
        // Situation: bankruptcy, value = the scale 0..100.
        KIND_BANKRUPTCY = 30,
    };

    enum e_race_event : uint8_t {
        RACE_FAIR_WIND = 0,         // Alkari: +1 to the empire's ship speed (modifier)
        RACE_DUEL_OF_HONOUR = 1,    // Bulrathi: +1 morale in every colony (modifier)
        RACE_DOUBLE_AGENT = 2,      // Darlok: a spy of the contact with most spies here is turned
        RACE_PROPHECY = 3,          // Elerian: the eight stars nearest the capital become known
        RACE_GOOD_BARGAIN = 4,      // Gnolam: 25 BC per trade treaty, at least 50
        RACE_BREAKTHROUGH = 5,      // Human: every contact's relations +10
        RACE_WORKER_SWARM = 6,      // Klackon: the capital builds a quarter faster (modifier)
        RACE_NETWORK_FAILURE = 7,   // Meklar: research points so far halved, or (1 in 4) doubled
        RACE_GREAT_HUNT = 8,        // Mrrshan: ships at the capital's star gain crew experience
        RACE_INSIGHT = 9,           // Psilon: the current research field is learned
        RACE_CLUTCH = 10,           // Sakkra: a population boom in up to three colonies (modifier)
        RACE_PLATE_SHIFT = 11,      // Silicoid: the richest-to-be colony's planet one mineral class richer
        RACE_WARM_CURRENT = 12,     // Trilarian: +1 food per farmer on ocean worlds (modifier)
        RACE_EVENT_COUNT = 13,
    };

    constexpr int32_t MAX_ENTRIES = 32;
    constexpr int32_t MODIFIER_TURNS = 10;
    constexpr int32_t GALAXY_MODIFIER_TURNS = 15;
    constexpr int32_t GOLDEN_AGE_PERCENT = 20;
    constexpr int32_t TOURIST_BC_PER_TREATY = 5;
    constexpr int32_t AMBASSADOR_RELATIONS = -30;
    constexpr int32_t AMBASSADOR_WAR_RELATIONS = -50;   // the ambassador's empire this hostile declares war
    constexpr int32_t BANKRUPTCY_STEP = 10;             // a turn in debt
    constexpr int32_t BANKRUPTCY_RECOVERY = 5;          // a turn out of debt
    constexpr int32_t BANKRUPTCY_EMERGENCY = 75;        // a building of the capital is sold
    constexpr int32_t GREAT_HUNT_EXPERIENCE = 50;

    struct s_entry {
        uint8_t kind = KIND_NONE;
        int8_t player = -1;         // the empire it concerns, -1 for the galaxy
        int16_t target = -1;        // a colony, a star or a player, -1 when none
        int32_t started = 0;
        int32_t ends = 0;           // the stardate it ends; 0 for a situation (until resolved)
        int16_t value = 0;
    };

    // Active entries (for the tests and the advisor's view).
    const s_entry* Entries(int32_t* count);
    // An active modifier or situation of `kind` for `player` (-1: the galaxy); for race events
    // `race_event` picks the event, otherwise it is ignored.
    const s_entry* Find(e_kind kind, int16_t player, int32_t race_event = -1);
    // Adds an entry; false when the table is full.
    bool Add(const s_entry& entry);

    // Modifiers the engine reads (0 / false with the flag off or nothing active).
    int32_t Speed_Bonus(int16_t player);           // initship.cpp Calc_Player_FTL_Speed
    int32_t Morale_Bonus(const s_colony* colony);  // colcalc_main.cpp Colony_Morale (morale points)
    int32_t Food2_Bonus(const s_colony* colony);   // colcalc.cpp Colony_Food2_Per_Farmer (half food)
    bool Population_Boom(int16_t colony);          // colcalc.cpp, as the classic population boom

    // Once a turn after the classic events (nextturn.cpp): modifiers pay or run out, the bankruptcy
    // scale moves, and new events may start.
    void Turn();
    // Starts an event now (for Turn and the tests).
    bool Start_Galaxy_Event(e_kind kind);
    bool Start_Empire_Event(e_kind kind, int16_t player);
    bool Start_Race_Event(int16_t player, e_race_event event);

    void Clear();
    bool Empty();

    // Save section 'EVTS' (game/rules_sections.h): payload version 1, then MAX_ENTRIES entries of
    // kind (uint8), player (int8), target (int16), started and ends (int32), value (int16).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

    s_entry* Entry_Slots();  // MAX_ENTRIES slots (world_events_state.cpp)

} // namespace world_events
