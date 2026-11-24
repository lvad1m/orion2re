#pragma once

#include "game/consts.h"

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Beasts: four events of monsters and shortcuts (docs/LocalLLM/events-catalog.md 3.7 and 3.12; README.md 10.9,
// wave 4): self-replicating harvesters that strip colonies and split in two, an ambush of a predator in an
// asteroid belt, a stolen flagship that turns into a monster, and an unstable wormhole with guardians that is
// more dangerous with each use. None has a choice for the player (the catalogue column says "no"); how bold a
// computer is with the wormhole depends on its personality. Monsters are ships of the game's table
// (SHIPS::Build_Nonplayer_Ship, as the ghost ship of world_events); the harvesters are records only.
// Off by default (events beasts); with the flag off nothing here draws from game_random, nothing is kept.
namespace beasts {

    extern bool enabled;  // events beasts

    constexpr int32_t MAX_RECORDS = 16;
    // A new event starts by chance, at most one a turn in the whole galaxy: harvesters (HARVESTER_CHANCE per mille
    // a turn for the galaxy), an ambush (AMBUSH_CHANCE), then each empire (EMPIRE_CHANCE per mille, one draw
    // each) gets a stolen flagship or a wormhole (picked by one more draw when both are possible).
    constexpr int32_t HARVESTER_CHANCE = 3;
    constexpr int32_t AMBUSH_CHANCE = 4;
    constexpr int32_t EMPIRE_CHANCE = 5;

    enum e_kind : uint8_t {
        KIND_NONE = 0,
        KIND_HARVESTERS = 1,  // record; target: the star, value: strength, started: the last split or start
        KIND_AMBUSH = 2,      // record; target: the star of the belt; option 1: a ship is swallowed (player: its
                              // owner, other: the ship, value: turns left to save it)
        KIND_HIJACK = 3,      // record; player: the owner, target: the star of the monster, value: the cost of the
                              // stolen ship
        KIND_WORMHOLE = 4,    // record; player: the empire that found it, target: the entrance, other: the exit,
                              // value: uses so far
        KIND_LAST = 4,
    };

    // Harvesters (3.7): machines of HARVESTER_STRENGTH at a colony star. Every HARVEST_PERIOD turns each colony
    // there loses HARVEST_BC (taken from its owner) and the machines grow by one up to HARVESTER_CAP; with no
    // colony at the star they go to the nearest colony star. Warships at the star wear them down by their size
    // points (no draw); when they are gone the empire with the most ship points there salvages SALVAGE_BC. After
    // SPLIT_TURNS turns left alone they split in two (the new half goes to the nearest colony star without
    // harvesters), up to MAX_HARVESTERS at once. (The catalogue's stronger counter-strike is not built.)
    constexpr int32_t HARVESTER_STRENGTH = 6;
    constexpr int32_t HARVESTER_CAP = 14;
    constexpr int32_t MAX_HARVESTERS = 3;
    constexpr int32_t HARVEST_PERIOD = 3;
    constexpr int32_t HARVEST_BC = 25;
    constexpr int32_t SPLIT_TURNS = 20;
    constexpr int32_t SALVAGE_BC = 80;
    // Ambush (3.7): a predator lives AMBUSH_TURNS in a belt (a star with an asteroid planet). Each turn with
    // AMBUSH_PERCENT it swallows the first ship of an empire that stands at the star; the owner has
    // AMBUSH_SAVE_TURNS turns: if the ship leaves, or another ship of the owner is at the star, it is freed (a
    // rescuer drives the predator off); else it is lost. At most MAX_AMBUSHES at once.
    constexpr int32_t AMBUSH_TURNS = 40;
    constexpr int32_t AMBUSH_PERCENT = 40;
    constexpr int32_t AMBUSH_SAVE_TURNS = 3;
    constexpr int32_t MAX_AMBUSHES = 2;
    // Stolen flagship (3.7): the most expensive idle ship of a computer empire (at least HIJACK_MIN_COST) is
    // stolen: it is scrapped and a monster of its own (a crystal) sits at its star. The empire that destroys it
    // (the one with the most ship points at the star) gets HIJACK_REWARD BC and half the ship's cost; the
    // owner, if it is another empire, remembers the service (opinion HIJACK_OPINION), if it is the owner itself
    // it gets the whole cost back. When HIJACK_TURNS pass without it the thieves unleash a hydra at the star.
    // (The monster does not roam: ships of the table cannot be moved from here.)
    constexpr int32_t HIJACK_TURNS = 30;
    constexpr int32_t HIJACK_MIN_COST = 40;
    constexpr int32_t HIJACK_REWARD = 60;
    constexpr int32_t HIJACK_OPINION = 10;
    // Wormhole (3.12): found by a computer empire at one of its colony stars (entrance), leading to a star it has
    // not visited (exit); it lives WORM_TURNS and collapses after WORM_MAX_USES uses. Each turn a computer with
    // a ship at the entrance sends the most expensive one through with a chance by personality (honorable and
    // pacifist WORM_PERCENT_SAFE, ruthless and aggressive WORM_PERCENT_BOLD, others WORM_PERCENT). The guardians
    // sink the ship with WORM_DANGER + WORM_DANGER_STEP per earlier use percent; else the empire gets
    // WORM_REWARD + WORM_REWARD_STEP per earlier use BC and learns of the exit and of the nearest unvisited star.
    // (The ship does not really travel: it waits at the entrance.) A human is not offered a wormhole.
    constexpr int32_t WORM_TURNS = 40;
    constexpr int32_t WORM_MAX_USES = 4;
    constexpr int32_t WORM_PERCENT = 8;
    constexpr int32_t WORM_PERCENT_SAFE = 4;
    constexpr int32_t WORM_PERCENT_BOLD = 15;
    constexpr int32_t WORM_DANGER = 20;
    constexpr int32_t WORM_DANGER_STEP = 15;
    constexpr int32_t WORM_REWARD = 60;
    constexpr int32_t WORM_REWARD_STEP = 30;

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

    // Starts the event of `kind` (`player` is the subject of a stolen flagship or a wormhole, -1 for the two
    // galactic ones). Draws from game_random only to pick among several candidates. False when it cannot start
    // (flag off, no candidate, table full, a human subject of a wormhole).
    bool Start(e_kind kind, int16_t player);

    // The first record of `kind` at `star` (-1: any), or nullptr.
    const s_record* At(e_kind kind, int16_t star);

    // Once a turn after the pirate world (nextturn.cpp): the records run and end; a new event may start.
    void Turn();
    // The first half of Turn(): the records go one turn on (no new event starts).
    void Run_Records();

    void Clear();
    bool Empty();

    // Save section 'BEST' (game/rules_sections.h): payload version 1, then the count (uint8), per record kind
    // (uint8), player (int8), target and other (int16), started and ends (int32), option (int8), value and
    // extra (int16).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace beasts
