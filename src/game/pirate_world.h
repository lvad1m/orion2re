#pragma once

#include "game/consts.h"

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// The pirate world: seven events of piracy (docs/LocalLLM/events-catalog.md 3.6 and 3.12; README.md 10.9,
// wave 4): raiders who demand tribute, privateers, slavers, a secret base of the syndicate, a repentant
// smuggler, pirate lairs in neutral systems and the havens that the grown lairs become (defectors, a flag of
// convenience, a black market of technologies). Tribute, the smuggler and the haven's offer are one-off
// choices; the others are kept as records. A human-owned subject takes the first, safest option (no interface
// step yet) and is never driven by the privateers (only computers hire them); a computer picks by its
// personality (Choose). The shared pirate effects of the classic events and the hired pirates are reused
// (events.h: Pirates_At_Star_Turn, Pirate_Raid_Amount, Pirate_Raid_Loss).
// Off by default (events pirate_world); with the flag off nothing here draws from game_random, nothing is kept.
namespace pirate_world {

    extern bool enabled;  // events pirate_world

    constexpr int32_t MAX_RECORDS = 24;
    // A new event starts by chance, at most one a turn in the whole galaxy: a new lair first (LAIR_CHANCE per
    // mille a turn for the galaxy), then each empire (EMPIRE_CHANCE per mille, one draw each); the kind of an
    // empire's event is drawn among the six that have a subject.
    constexpr int32_t LAIR_CHANCE = 8;
    constexpr int32_t EMPIRE_CHANCE = 5;

    enum e_kind : uint8_t {
        KIND_NONE = 0,
        KIND_TRIBUTE = 1,     // one-off choice; the subject: the empire raiders demand tribute from
        KIND_PRIVATEERS = 2,  // record; player: the employer, other: the enemy, value: the escrow left
        KIND_SLAVERS = 3,     // record; player: the empire preyed on now, target: the star, value: strength
                              // left, extra: strength, option: captives taken
        KIND_SYNDICATE = 4,   // record (study or seize; arrest is one-off); player: the owner of the base
        KIND_SMUGGLER = 5,    // one-off choice
        KIND_LAIR = 6,        // record; target: the star, value: strength, extra: the cap it started with
        KIND_HAVEN = 7,       // one-off choice: a lair grown to HAVEN_STRENGTH offers its services
        KIND_LICENCE = 8,     // record: the flag of convenience; player: the empire spared the raids of the lairs
        KIND_LAST = 8,
    };

    enum e_tribute_option : uint8_t { TRIBUTE_PAY = 0, TRIBUTE_HAGGLE = 1, TRIBUTE_REFUSE = 2 };
    enum e_syndicate_option : uint8_t { SYNDICATE_ARREST = 0, SYNDICATE_STUDY = 1, SYNDICATE_SEIZE = 2 };
    enum e_smuggler_option : uint8_t { SMUGGLER_REFUSE = 0, SMUGGLER_AMNESTY = 1, SMUGGLER_USE = 2 };
    enum e_haven_option : uint8_t { HAVEN_DEFECTORS = 0, HAVEN_FLAG = 1, HAVEN_BLACK_MARKET = 2 };

    // Raiders with tribute (3.6): TRIBUTE_PERCENT of the treasury (at least TRIBUTE_MIN BC) buys peace; haggling
    // pays half and the raiders take it in TRIBUTE_HAGGLE_PERCENT of the cases, otherwise they strike all the
    // same; refusing is a raid at once. After a paid tribute a rival (a computer at war with the subject, with
    // at least RIVAL_MIN_BC) outbids it in RIVAL_PERCENT of the cases: it pays RIVAL_PRICE BC and the raid
    // comes anyway. A raid is the classic one (Pirate_Raid_Amount) and two freighters lost.
    // (The unification of the horde of the catalogue row is not built.)
    constexpr int32_t TRIBUTE_PERCENT = 15;
    constexpr int32_t TRIBUTE_MIN = 30;
    constexpr int32_t TRIBUTE_HAGGLE_PERCENT = 50;
    constexpr int32_t RIVAL_MIN_BC = 150;
    constexpr int32_t RIVAL_PERCENT = 30;
    constexpr int32_t RIVAL_PRICE = 50;
    constexpr int32_t RAID_FREIGHTERS = 2;
    // Privateers (3.6): a computer at war escrows PRIVATEER_ESCROW BC (keeping HIRE_KEEP_BC) for
    // PRIVATEER_TURNS; each turn in PRIVATEER_PERCENT of the cases the pirates sink the cheapest enemy ship that
    // stands alone at a star (no colony of the enemy there) and are paid PRIVATEER_BOUNTY from the escrow. At
    // the end (or when the escrow is spent) the unspent part comes back minus PRIVATEER_CUT percent for the
    // pirates. One contract an employer.
    constexpr int32_t PRIVATEER_ESCROW = 100;
    constexpr int32_t HIRE_KEEP_BC = 150;
    constexpr int32_t PRIVATEER_TURNS = 20;
    constexpr int32_t PRIVATEER_PERCENT = 25;
    constexpr int32_t PRIVATEER_BOUNTY = 25;
    constexpr int32_t PRIVATEER_CUT = 20;
    // Slavers (3.6): roaming raiders of SLAVER_STRENGTH come to the nearest weakly defended colony (at least two
    // people, no ship at its star). Each turn warships at their star wear them down (as the classic pirate
    // activity, Pirates_At_Star_Turn); an undefended colony loses a person in SLAVER_PERCENT of the cases, then
    // they move on. They leave after SLAVER_MAX_CAPTIVES people taken or SLAVER_TURNS. A computer that is not
    // honorable or pacifist and has SLAVER_PRICE BC and a colony with room buys the person (the person joins its
    // smallest colony); that empire is remembered badly: the victim (SLAVER_VICTIM_OPINION), everyone in
    // contact with the buyer (SLAVER_OPINION), and a deed is noted for it.
    constexpr int32_t SLAVER_STRENGTH = 8;
    constexpr int32_t SLAVER_TURNS = 15;
    constexpr int32_t SLAVER_PERCENT = 60;
    constexpr int32_t SLAVER_MAX_CAPTIVES = 3;
    constexpr int32_t SLAVER_PRICE = 60;
    constexpr int32_t SLAVER_VICTIM_OPINION = -12;
    constexpr int32_t SLAVER_OPINION = -6;
    // Secret base of the syndicate (3.6): arresting it yields SYNDICATE_LOOT BC at once; studying it pays
    // STUDY_BC a turn for SYNDICATE_TURNS (each turn the syndicate finds the mole with STUDY_LEAK_PERCENT);
    // seizing it pays SEIZE_BC a turn and every SEIZE_PERIOD turns its pirates raid the richest empire at war
    // with the owner (else in contact), half of the loss goes to the owner (a deed is noted for it).
    constexpr int32_t SYNDICATE_TURNS = 15;
    constexpr int32_t SYNDICATE_LOOT = 50;
    constexpr int32_t STUDY_BC = 8;
    constexpr int32_t STUDY_LEAK_PERCENT = 4;
    constexpr int32_t SEIZE_BC = 4;
    constexpr int32_t SEIZE_PERIOD = 5;
    constexpr int32_t SEIZE_SHARE_PERCENT = 50;
    // Repentant smuggler (3.6): refusing jails him (SMUGGLER_FINE BC); an amnesty cracks down on the crime (every
    // lair loses CRACKDOWN_LOSS strength, a lair at zero is gone) for AMNESTY_BC, but in AMNESTY_LAIR_PERCENT of
    // the cases he deceived us and a new lair appears; using him as a guide yields USE_BC and a new lair for
    // certain (a deed is noted).
    constexpr int32_t SMUGGLER_FINE = 20;
    constexpr int32_t AMNESTY_BC = 60;
    constexpr int32_t AMNESTY_LAIR_PERCENT = 25;
    constexpr int32_t CRACKDOWN_LOSS = 8;
    constexpr int32_t USE_BC = 120;
    // Lairs (3.6): at most LAIR_MAX at once, in a neutral star (no colony; a star with a pirate cache first),
    // LAIR_STRENGTH strong (LAIR_CACHE_BONUS more at a cache). Every LAIR_GROW_TURNS turns that no warship wears
    // it down it grows by LAIR_GROW up to LAIR_CAP. Every LAIR_RAID_PERIOD turns the lair raids the owner of the
    // colony nearest to it with the chance strength / LAIR_CAP (not an empire holding a flag of convenience).
    // Warships at the star wear it down by their size (Pirates_At_Star_Turn); when it is gone the empire with
    // the most ship points there lands and gets LAIR_LOOT + LAIR_LOOT_PER_STRENGTH * the strength it had.
    constexpr int32_t LAIR_MAX = 3;
    constexpr int32_t LAIR_STRENGTH = 8;
    constexpr int32_t LAIR_CACHE_BONUS = 4;
    constexpr int32_t LAIR_CAP = 30;
    constexpr int32_t LAIR_GROW = 2;
    constexpr int32_t LAIR_GROW_TURNS = 4;
    constexpr int32_t LAIR_RAID_PERIOD = 5;
    constexpr int32_t LAIR_LOOT = 60;
    constexpr int32_t LAIR_LOOT_PER_STRENGTH = 5;
    // Havens (3.12): a lair of HAVEN_STRENGTH or more is a haven; it offers an empire defectors (HAVEN_DEFECTORS_BC
    // for one technology application), a flag of convenience (HAVEN_FLAG_BC for HAVEN_FLAG_TURNS turns without
    // the raids of the lairs) or the black market (HAVEN_MARKET_BC for two applications; empires in contact hear
    // bad rumours, HAVEN_MARKET_OPINION, and a deed is noted).
    constexpr int32_t HAVEN_STRENGTH = 20;
    constexpr int32_t HAVEN_DEFECTORS_BC = 40;
    constexpr int32_t HAVEN_FLAG_BC = 60;
    constexpr int32_t HAVEN_FLAG_TURNS = 20;
    constexpr int32_t HAVEN_MARKET_BC = 150;
    constexpr int32_t HAVEN_MARKET_APPS = 2;
    constexpr int32_t HAVEN_MARKET_OPINION = -8;

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
        s_record record[MAX_RECORDS];
    };
    s_state* State();

    // The option a computer of `player` takes by its personality (honorable and pacifist the first, ruthless and
    // aggressive the third, the others the second); a human, or the flag off: 0.
    int32_t Choose(e_kind kind, int16_t player);

    // Starts the event of `kind` for `player` (-1 for a lair) and applies the option Choose gives (`option` >= 0
    // overrides it). Draws from game_random only to pick among several candidates and for the chances. False
    // when it cannot start.
    bool Start(e_kind kind, int16_t player, int32_t option = -1);

    // True while `player` holds a flag of convenience.
    bool Licensed(int16_t player);
    // True while a lair of HAVEN_STRENGTH or more exists.
    bool Has_Haven();

    // Once a turn after the overtures (nextturn.cpp): the records run and end; a new event may start.
    void Turn();
    // The first half of Turn(): the records go one turn on (no new event starts).
    void Run_Records();

    void Clear();
    bool Empty();

    // Save section 'PIRW' (game/rules_sections.h): payload version 1, then the count (uint8), per record kind
    // (uint8), player (int8), target and other (int16), started and ends (int32), option (int8), value and
    // extra (int16).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace pirate_world
