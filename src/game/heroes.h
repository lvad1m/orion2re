#pragma once

#include "game/consts.h"

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Leaders and characters (docs/LocalLLM/events-catalog.md 3.11 and 3.12; README.md 10.9, wave 4, piece 8): a crisis
// manager who helps an empire in a crisis, an alien mind that takes a leader over for a while, the quirks of leaders
// (stimulants, paranoia, pacifism, sabotage; they follow the leader's loyalty, loyalty.h) and rival heroes (the
// famous leaders nobody has hired grow, die and are hired by the richest computer). Nothing is written into the
// leader record except what the classic officer code writes itself (experience, a post given up, the owner on a
// hire): the quirks and the rest live in this module's state. No event here has a choice; a human is a subject of
// them like anybody (a pacifist leader only refuses a computer's post). Off by default (events heroes); with the
// flag off nothing here draws from game_random, nothing is kept and every hook returns the classic value.
namespace heroes {

    extern bool enabled;  // events heroes

    constexpr int32_t MAX_RECORDS = 24;
    constexpr int32_t MAX_PER_EMPIRE = 3;

    enum e_kind : uint8_t {
        KIND_NONE = 0,
        KIND_MANAGER = 1,      // retired: only so old saves load (the record is dropped)
        KIND_MIND = 2,         // an alien mind controls `leader` of `player`; `other` is the empire that profits
        KIND_STIMULANT = 3,    // a leader on stimulants
        KIND_PARANOIA = 4,
        KIND_PACIFISM = 5,
        KIND_SABOTAGE = 6,
        KIND_LAST = 6,
    };

    // The statuses of the leader records (orion2.h s_leader_data).
    constexpr int8_t STATUS_DEAD = -2;
    constexpr int8_t STATUS_FREE = -1;      // on the list of the leaders nobody holds
    constexpr int8_t STATUS_POOL = 0;
    constexpr int8_t STATUS_ASSIGNED = 1;
    constexpr int8_t STATUS_LIMBO = 2;

    // --- Crisis manager (3.11) - events-catalog.md row "Антикризисный управляющий": "удачно решённый кризис даёт
    // особого лидера"; the author's decision (ab-review.md 1.3): a brilliant or triumphant solution of a crisis
    // (crises::Resolve_With_Dice, dice.h OUTCOME_BRILLIANT / OUTCOME_TRIUMPH) gives the empire a leader with a
    // special ability from the list of leaders for hire (status -1), hired free of charge: the one with the most
    // abilities, a famous one first (no dice). KIND_MANAGER stays in the enum only so that old saves load; a record
    // of it is dropped at once.

    // --- Mind control (3.11): a computer empire that holds a telepath and is at war with another empire (or
    // thinks MIND_OPINION or worse of it) takes over one of its assigned leaders, MIND_CHANCE per mille a turn. For
    // up to MIND_TURNS turns MIND_TRIBUTE BC a turn go from the victim to the empire that profits; each turn the
    // victim may free the leader: MIND_FREE per mille plus MIND_FREE per counterintelligence mode step. With spy
    // bribery on, the leader also stands bribed by the captor (spy_ops.h): the governor's morale and the inside man
    // work as for a bought one; if the spies uncover it first, the control ends.
    constexpr int32_t MIND_CHANCE = 5;
    constexpr int32_t MIND_TURNS = 30;
    constexpr int32_t MIND_TRIBUTE = 6;
    constexpr int32_t MIND_FREE = 25;
    constexpr int32_t MIND_OPINION = -25;

    // --- Quirks (3.12): QUIRK_CHANCE per mille a turn for each empire (a draw each), one new quirk a turn in the
    // galaxy, at most MAX_PER_EMPIRE records for an empire. The kind follows the leader's loyalty (50 when it is not
    // tracked): 60 and more stimulant or pacifism, 35 to 59 paranoia or stimulant, less paranoia or sabotage.
    //  stimulant: STIM_TURNS turns, STIM_XP experience every STIM_STEP turns, then a crash of STIM_CRASH loyalty.
    //  paranoia: PARANOIA_TURNS turns, PARANOIA_LOYALTY every PARANOIA_STEP turns; after PARANOIA_FLEE steps (or when
    //            the loyalty is at PARANOIA_LOW) the leader locks himself away: he leaves his post for the pool.
    //  pacifism: PACIFISM_TURNS turns in which a computer's ship captain keeps leaving his post for the pool; at the
    //            end he has written a treatise, PACIFISM_XP experience.
    //  sabotage: SABOTAGE_TURNS turns, then the empire loses SABOTAGE_BC and the leader's loyalty SABOTAGE_LOYALTY.
    constexpr int32_t QUIRK_CHANCE = 6;
    constexpr int32_t STIM_TURNS = 10;
    constexpr int32_t STIM_STEP = 5;
    constexpr int32_t STIM_XP = 15;
    constexpr int32_t STIM_CRASH = -10;
    constexpr int32_t PARANOIA_TURNS = 20;
    constexpr int32_t PARANOIA_STEP = 5;
    constexpr int32_t PARANOIA_LOYALTY = -3;
    constexpr int32_t PARANOIA_FLEE = 3;
    constexpr int32_t PARANOIA_LOW = 25;
    constexpr int32_t PACIFISM_TURNS = 15;
    constexpr int32_t PACIFISM_XP = 30;
    constexpr int32_t SABOTAGE_TURNS = 6;
    constexpr int32_t SABOTAGE_BC = 60;
    constexpr int32_t SABOTAGE_LOYALTY = -10;

    // --- Rival heroes (3.12): every RIVAL_PERIOD turns (stardate % RIVAL_PERIOD == RIVAL_PHASE) each famous leader
    // that nobody holds gains RIVAL_XP experience (up to RIVAL_XP_CAP), may die (RIVAL_DEATH per mille plus one per
    // hundred experience gained), and the richest computer that can afford the leader (its cost plus HIRE_RESERVE
    // BC) and has room may hire it: HIRE_CHANCE percent a step, one hire a step in the galaxy.
    constexpr int32_t RIVAL_PERIOD = 10;
    constexpr int32_t RIVAL_PHASE = 3;
    constexpr int32_t RIVAL_XP = 40;
    constexpr int32_t RIVAL_XP_CAP = 800;
    constexpr int32_t RIVAL_DEATH = 20;
    constexpr int32_t HIRE_RESERVE = 150;
    constexpr int32_t HIRE_CHANCE = 35;

    struct s_record {
        uint8_t kind = KIND_NONE;
        int8_t player = -1;     // the empire the event touches (the leader's owner)
        int16_t leader = -1;    // KIND_MANAGER: -1
        int8_t other = -1;      // KIND_MIND: the empire that profits
        int32_t started = 0;
        int32_t ends = 0;
        int16_t value = 0;      // steps done (paranoia), 1 once a pacifist has left his post
    };

    struct s_state {
        int32_t count = 0;
        s_record record[MAX_RECORDS];
        int16_t grown[MAX_LEADERS] = {};  // experience a free famous leader has gained while nobody held him
    };
    s_state* State();

    // The first record of `kind` for `player` (-1: anyone) and `leader` (-1: any), or nullptr; nullptr with the flag off.
    const s_record* Find(e_kind kind, int16_t player, int16_t leader);
    int32_t Active(int16_t player);

    // Hook of crises::Resolve_With_Dice: `player` has solved a crisis brilliantly or triumphantly; hires it the best
    // leader of the hire list for nothing. Returns the leader's index, -1 when none was hired (flag off, no leader
    // with an ability on the list, no room for another officer).
    int16_t On_Crisis_Solved(int16_t player);
    // Whether `leader` is controlled by an alien mind.
    bool Controlled(int16_t leader);

    // The famous leaders (the Famous skill, both levels) that are alive: `player` >= 0 lists those the empire holds
    // (in the pool, at a post or in limbo), -1 those that nobody holds and that may be hired. Fills `out` (at most
    // `max`), returns how many there are. A pure query, valid with the flag off; for the later "famous exile" event.
    bool Is_Famous(int16_t leader);
    int32_t Famous_Leaders(int16_t player, int16_t* out, int32_t max);

    // Starts an event by hand (the chance and the pick are the caller's): the mind control of `leader` by `other` (the empire that profits), or a quirk of `leader`.
    // Costs are paid now. False when the flag is off, the table is full or the subject does not qualify.
    bool Start(e_kind kind, int16_t player, int16_t leader = -1, int16_t other = -1);

    // The kind of quirk a computer's or human's leader gets by its loyalty; `pick` is 0 or 1 (a draw).
    e_kind Quirk_For(int16_t leader, int32_t pick);

    // Once a turn right after world_events::Turn() (nextturn.cpp): the records run and end, the rival heroes grow,
    // die and are hired, and a new event may start.
    void Turn();
    // The first part of Turn(): the records go one turn on and the rival heroes step (no new event starts).
    void Run_Records();
    // The rival heroes' step alone (it acts on the phase only unless `force`; for the tests).
    void Rival_Step(bool force);

    void Clear();
    bool Empty();

    // Save section 'HERO' (game/rules_sections.h): payload version 1, then the count (uint8), per record kind (uint8),
    // player (int8), leader (int16), other (int8), started and ends (int32), value (int16), then MAX_LEADERS int16 of
    // grown experience.
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace heroes
