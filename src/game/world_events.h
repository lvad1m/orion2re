#pragma once

#include "game/choices.h"

#include <cstdint>
#include <string>

struct s_colony;

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// New events, first wave (docs/LocalLLM/events-catalog.md sections 2, 4.1 and 6; mechanics.md
// 3.2.4; README.md 10.9, tier 5A): an event table of its own without the classic one slot per event
// type, temporary modifiers of the galaxy and of empires, situations with a progress scale
// (bankruptcy, a spreading epidemic, pirates' bounties and protection), and single events built from
// the classic primitives - galaxy events (golden age, tourist boom, ore rush, a ghost ship, a
// territorial beast), empire events (an ambassador killed, a discoverer, an ancient wreck, a colony
// mutiny, a warm place for a leader, an outbreak) and one rare event for each of the 13 stock races.
// Off by default; with the flag off nothing here draws from game_random and no state is kept.
// A computer under a bounty pays the pirates for protection; a human is asked in the choice window when events
// human_choices is on (choices.h: pay or refuse), otherwise it is offered nothing and pays nothing, as before.
namespace world_events {

    extern bool enabled;           // events new_events
    extern int32_t galaxy_chance;  // events galaxy_event_chance: per mille a turn
    extern int32_t empire_chance;  // events empire_event_chance: per mille a turn for each empire
    extern bool hired_pirates;     // spy hired_pirates (mechanics.md 3.2.1); works without new_events

    enum e_kind : uint8_t {
        KIND_NONE = 0,
        // Galaxy modifiers (player -1) and one-off galaxy events.
        KIND_GOLDEN_AGE = 1,         // each empire gets 20 % of its positive surplus as BC every turn
        KIND_TOURIST_BOOM = 2,       // each empire gets 5 BC a turn for every trade treaty
        KIND_ORE_RUSH = 3,           // one-off: three planets get two mineral classes richer
        KIND_GHOST_SHIP = 4,         // target: the star it haunts; value: returns left; started: when it
                                     // was last destroyed (0 while it is about)
        KIND_TERRITORIAL_BEAST = 5,  // one-off: a monster settles at an empty star and stays there
        // Wave 3 modifiers (events-catalog.md 3.1).
        KIND_STAR_FLARE = 6,         // target: the star; no building there, no ship leaves it
        KIND_SYSTEM_EMP = 7,         // target: the star; the colonies there produce nothing, ships stand
        KIND_GALACTIC_SLOWDOWN = 8,  // galaxy: every empire's ship speed -25 %
        KIND_SENSOR_FAILURE = 9,     // galaxy: every empire's scanner range halved
        // One-off empire events.
        KIND_AMBASSADOR_KILLED = 10, // player: the host; target: the ambassador's empire
        KIND_DISCOVERER = 11,        // retired (wave 3: now a rule in Turn, see Discoverers_); the number stays
                                     // reserved and readable in saves, no event of it starts
        KIND_ANCIENT_WRECK = 12,     // 1..10 weapon or shield applications the empire does not know
        KIND_MUTINY = 13,            // target: the colony; the colony revolts (engine rebellion); morale and
                                     // stability fall for MODIFIER_TURNS
        KIND_WARM_PLACE = 14,        // target: the leader; experience now, no new post for WARM_PLACE_TURNS
        KIND_EPIDEMIC = 15,          // target: the colony; value: cure points left (its research cures it)
        KIND_COMET_ORE = 16,         // one-off: a comet hits a colony and leaves a richer deposit
        // Race events: player; value = e_race_event; modifiers last MODIFIER_TURNS.
        KIND_RACE_EVENT = 20,
        // Situation: bankruptcy, value = the scale 0..100.
        KIND_BANKRUPTCY = 30,
        // Pirates (events-catalog.md 3.6): a price on an empire's head, protection money.
        KIND_BOUNTY = 31,            // player: the empire with a price on its head; value: BC offered
        KIND_PROTECTION = 32,        // player: the empire paying the pirates; value: the fee a turn
        // Hired pirates (mechanics.md 3.2.1): player: the empire harassed; target: the star; value:
        // strength left; extra: the strength paid for; hirer: who paid.
        KIND_HIRED_PIRATES = 33,
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
    // Wave 3 modifiers: durations are MIN + a draw 0..SPAN.
    constexpr int32_t FLARE_MIN_TURNS = 5;
    constexpr int32_t FLARE_SPAN_TURNS = 5;
    constexpr int32_t EMP_MIN_TURNS = 2;
    constexpr int32_t EMP_SPAN_TURNS = 3;
    constexpr int32_t SLOWDOWN_MIN_TURNS = 10;
    constexpr int32_t SLOWDOWN_SPAN_TURNS = 10;
    constexpr int32_t SENSOR_MIN_TURNS = 5;
    constexpr int32_t SENSOR_SPAN_TURNS = 5;
    constexpr int32_t SLOWDOWN_NUMERATOR = 3;           // ship speed * 3 / 4
    constexpr int32_t SLOWDOWN_DENOMINATOR = 4;
    constexpr int32_t TOURIST_BC_PER_TREATY = 5;
    constexpr int32_t AMBASSADOR_RELATIONS = -30;
    constexpr int32_t AMBASSADOR_WAR_RELATIONS = -50;   // the ambassador's empire this hostile declares war
    constexpr int32_t BANKRUPTCY_STEP = 10;             // a turn in debt
    constexpr int32_t BANKRUPTCY_RECOVERY = 5;          // a turn out of debt
    constexpr int32_t BANKRUPTCY_EMERGENCY = 75;        // a building of the capital is sold
    constexpr int32_t GREAT_HUNT_EXPERIENCE = 50;
    constexpr int32_t GHOST_RETURNS = 3;                // a ghost ship comes back this often
    constexpr int32_t GHOST_RETURN_TURNS = 10;
    constexpr int32_t DISCOVERER_BC = 10;               // per planet of the first-explored star
    constexpr int32_t DISCOVERER_SPECIAL_BC = 25;       // per planet when the star or the planet has a special
    constexpr int32_t WRECK_MAX_APPS = 10;              // 1..10 applications
    constexpr int32_t MUTINY_MORALE = -2;
    constexpr int32_t MUTINY_UNREST = 20;               // stability points (stability.h)
    constexpr int32_t WARM_PLACE_TURNS = 20;
    constexpr int32_t WARM_PLACE_EXPERIENCE = 50;
    constexpr int32_t EPIDEMIC_SPREAD_PERCENT = 10;     // a turn, to the owner's nearest healthy colony
    constexpr int32_t EPIDEMIC_TURNS = 30;
    constexpr int32_t EPIDEMIC_CURE_BASE = 50;          // cure points, plus 10 a population unit
    constexpr int32_t BOUNTY_PAYER_MIN_BC = 100;
    constexpr int32_t BOUNTY_PERCENT = 10;              // of the payer's treasury, at least BOUNTY_MIN
    constexpr int32_t BOUNTY_MIN = 20;
    constexpr int32_t BOUNTY_RAID_MIN = 100;            // pirates strike for a price this high
    constexpr int32_t PROTECTION_FEE = 10;              // BC a turn
    constexpr int32_t PROTECTION_TURNS = 10;
    // Hired pirates (spy hired_pirates).
    constexpr int32_t HIRE_PERIOD = 10;                 // computers hire when stardate % 10 == 4
    constexpr int32_t HIRE_MIN_BC = 150;                // a computer keeps this much after paying
    constexpr int32_t HIRE_RELATIONS = -30;             // or at war
    constexpr int32_t RAID_PRICE = 50;
    constexpr int32_t RAID_MIN_BC = 100;                // as the classic raid: a poorer target is not worth it
    constexpr int32_t ARM_PERCENT = 20;                 // of the hirer's treasury, ARM_PRICE..ARM_PRICE_MAX
    constexpr int32_t ARM_PRICE = 100;
    constexpr int32_t ARM_PRICE_MAX = 300;
    constexpr int32_t HIRED_PIRATE_TURNS = 30;

    struct s_entry {
        uint8_t kind = KIND_NONE;
        int8_t player = -1;         // the empire it concerns, -1 for the galaxy
        int16_t target = -1;        // a colony, a star or a player, -1 when none
        int32_t started = 0;
        int32_t ends = 0;           // the stardate it ends; 0 for a situation (until resolved)
        int16_t value = 0;
        int8_t hirer = -1;          // hired pirates: who paid (payload v2)
        int16_t extra = 0;          // hired pirates: the strength paid for (payload v2)
    };

    // Active entries (for the tests and the advisor's view).
    const s_entry* Entries(int32_t* count);
    // An active modifier or situation of `kind` for `player` (-1: the galaxy); for race events
    // `race_event` picks the event, otherwise it is ignored.
    const s_entry* Find(e_kind kind, int16_t player, int32_t race_event = -1);
    // Adds an entry; false when the table is full.
    bool Add(const s_entry& entry);

    // The window of a human (choices.h; module choices::MODULE_WORLD_EVENTS, kind CHOICE_PROTECTION, value the
    // fee): the pirates' offer of protection after a raid for a price on the empire's head. The text is false when
    // the empire is gone or already pays for protection; the application (option 0 pays) re-checks, needs a
    // treasury that covers one fee and then starts the protection as a computer's.
    enum e_choice : uint8_t { CHOICE_PROTECTION = 1 };
    bool Choice_Text(const choices::s_choice& choice, choices::s_text* text);
    void Choice_Apply(const choices::s_choice& choice, int32_t option);

    // Modifiers the engine reads (0 / false with the flag off or nothing active).
    int32_t Speed_Bonus(int16_t player);           // initship.cpp Calc_Player_FTL_Speed
    int32_t Morale_Bonus(const s_colony* colony);  // colcalc_main.cpp Colony_Morale (morale points)
    int32_t Food2_Bonus(const s_colony* colony);   // colcalc.cpp Colony_Food2_Per_Farmer (half food)
    bool Population_Boom(int16_t colony);          // colcalc.cpp, as the classic population boom
    bool Epidemic(int16_t colony);                 // colcalc.cpp, as the classic plague
    int32_t Unrest(int16_t colony);                // stability.cpp: a mutiny (stability points)
    bool Leader_Locked(int16_t leader);            // officer.cpp: a warm place keeps the leader at its post
    // Wave 3 (all neutral with the flag off): shipmove.cpp - no ship may leave a flared or EMP'd star.
    bool Star_Grounded(int16_t star);
    bool Star_Dark(int16_t star);                  // colcalc_main.cpp: an EMP - industry, research and taxes zero
    int32_t Scaled_Speed(int32_t speed);           // initship.cpp Calc_Player_FTL_Speed: the galactic slowdown
    int32_t Scanner_Range(int32_t range);          // shipstak.cpp Best_Detection_At: the sensor failure

    // Hired pirates (spy hired_pirates; mechanics.md 3.2.1). `hirer` pays RAID_PRICE for a classic
    // pirate raid on `target`, or ARM_PRICE..ARM_PRICE_MAX to arm pirates at the star of one of
    // `target`'s colonies (the classic pirate activity, stronger for the money, until warships
    // drive them off or HIRED_PIRATE_TURNS pass). Needs spies in `target` or a known pirate cache. A
    // dice check (as a spy's data gathering) decides: success - the pirates strike; failure - they
    // keep the money; exposed - the target learns who paid (raided us, -20) and a spy is caught.
    enum e_hire : uint8_t { HIRE_RAID = 0, HIRE_ARM = 1 };
    enum e_hire_result : uint8_t { HIRE_NOT_POSSIBLE = 0, HIRE_FAILED = 1, HIRE_EXPOSED = 2, HIRE_DONE = 3 };
    e_hire_result Hire_Pirates(int16_t hirer, int16_t target, e_hire how);
    e_hire_result Hire_Pirates_With_Dice(int16_t hirer, int16_t target, e_hire how, const int32_t* dice_values);
    // True while hired pirates sit at a star where `player` has a colony (EVENTS::Event_Check_Pirate_Activity).
    bool Pirates_Harass(int16_t player);

    // Once a turn after the classic events (nextturn.cpp): modifiers pay or run out, the bankruptcy
    // scale moves, and new events may start.
    void Turn();
    // Starts an event now (for Turn and the tests).
    bool Start_Galaxy_Event(e_kind kind);
    bool Start_Empire_Event(e_kind kind, int16_t player);
    bool Start_Race_Event(int16_t player, e_race_event event);
    // An infected colony passes the epidemic to its owner's nearest healthy colony (Turn rolls
    // EPIDEMIC_SPREAD_PERCENT first); false when there is none.
    bool Spread_Epidemic(int16_t from);

    void Clear();
    bool Empty();

    // Discoverers (wave 3): the stars somebody has already explored. Not valid (false) until the first
    // Turn with new_events on, which takes every star visited by anyone as explored (so an old save
    // pays nothing retroactively); saved in payload v3.
    bool Explored_Valid();
    bool Star_Explored(int16_t star);
    void Mark_Explored(int16_t star);
    void Mark_Explored_Valid();

    // Save section 'EVTS' (game/rules_sections.h): payload version 1, then MAX_ENTRIES entries of
    // kind (uint8), player (int8), target (int16), started and ends (int32), value (int16); v2 adds
    // hirer (int8) and extra (int16) to each entry; v3 adds after the entries the explored flag (uint8)
    // and the explored-stars bitmap (BITMAP(MAX_STARS) bytes).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

    s_entry* Entry_Slots();  // MAX_ENTRIES slots (world_events_state.cpp)

} // namespace world_events
