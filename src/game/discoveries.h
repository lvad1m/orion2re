#pragma once

#include "game/consts.h"

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Discoveries: five events of finds (docs/LocalLLM/events-catalog.md 3.8; README.md 10.9, wave 4, built on the
// dice of the research projects, projects.h): an ancient power plant, sleeping symbionts, natives on a continent
// (each with three ways to deal with it), the vaults of the guardians (a limited set for the whole galaxy; the
// best explorers find them better) and tombs with guardians (the dig wakes a monster that must be beaten before
// the reward). The first option of a choice is the safest one: a human takes it (no screen yet), a computer
// chooses by its personality (Choose). Guardians are monsters of the game's table (SHIPS::Build_Nonplayer_Ship).
// Rewards go through the existing levers: a technology application (research points when none is left to learn),
// research points, BC, a colony building, a population point lost or a planet's mineral class (EVENTS::Enrich_Planet).
// Off by default (events discoveries); with the flag off nothing here draws from game_random, nothing is kept.
namespace discoveries {

    extern bool enabled;  // events discoveries

    constexpr int32_t MAX_RECORDS = 16;
    // A new event starts by chance: DISCOVERY_CHANCE per mille a turn for each empire (one draw each), at most one
    // new event a turn in the whole galaxy and at most MAX_PER_EMPIRE running for an empire.
    constexpr int32_t DISCOVERY_CHANCE = 8;
    constexpr int32_t MAX_PER_EMPIRE = 2;

    enum e_kind : uint8_t {
        KIND_NONE = 0,
        KIND_POWER_PLANT = 1,  // at a colony: options OPT_POWER_TEST, OPT_POWER_DISASSEMBLE, OPT_POWER_INSTALL
        KIND_SYMBIONTS = 2,    // at a colony: options OPT_SYMB_STUDY, OPT_SYMB_WAKE, OPT_SYMB_BOND
        KIND_NATIVES = 3,      // at a colony of a star with natives: options OPT_NAT_LEAVE, OPT_NAT_TEACH, OPT_NAT_WORK
        KIND_VAULT = 4,        // at an explored star; no choice
        KIND_TOMB = 5,         // at a colony at an ancient site; no choice; stage 0 digging, 1 guardians awake
        KIND_LAST = 5,
    };

    enum e_option : uint8_t {
        OPT_POWER_TEST = 0,
        OPT_POWER_DISASSEMBLE = 1,
        OPT_POWER_INSTALL = 2,
        OPT_SYMB_STUDY = 0,
        OPT_SYMB_WAKE = 1,
        OPT_SYMB_BOND = 2,
        OPT_NAT_LEAVE = 0,
        OPT_NAT_TEACH = 1,
        OPT_NAT_WORK = 2,
    };

    // Power plant (GalCiv III): all three take POWER_TURNS turns. Test on a dead world: POWER_TEST_COST BC, no risk,
    // POWER_TEST_RESEARCH research points. Disassemble: a dice check against POWER_DIFFICULTY: success a technology
    // application, brilliant also POWER_BRILLIANT_BC; an exposed failure or a catastrophe is a blast at the colony
    // (EVENTS::Allocate_Earthquake_Hit). Install on the colony: it gets an automated factory (or, when it has one,
    // a robotic factory; with both, POWER_FALLBACK_BC) and loses one population point (when it has three or more).
    constexpr int32_t POWER_TURNS = 4;
    constexpr int32_t POWER_TEST_COST = 30;
    constexpr int32_t POWER_TEST_RESEARCH = 150;
    constexpr int32_t POWER_DIFFICULTY = 12;
    constexpr int32_t POWER_BRILLIANT_BC = 100;
    constexpr int32_t POWER_FALLBACK_BC = 80;
    // Symbionts: all three take SYMB_TURNS. Study the ship (difficulty SYMB_STUDY_DIFFICULTY): success
    // SYMB_STUDY_RESEARCH, brilliant twice that. Wake (SYMB_WAKE_DIFFICULTY): success an application, brilliant also
    // SYMB_BRILLIANT_RESEARCH; an exposed failure or a catastrophe wakes a hostile guardian at the star. Bond with a
    // volunteer (SYMB_BOND_DIFFICULTY): success SYMB_BOND_RESEARCH (brilliant 1.5 times); an exposed failure or a
    // catastrophe costs the colony one population point.
    constexpr int32_t SYMB_TURNS = 5;
    constexpr int32_t SYMB_STUDY_DIFFICULTY = 8;
    constexpr int32_t SYMB_STUDY_RESEARCH = 100;
    constexpr int32_t SYMB_WAKE_DIFFICULTY = 13;
    constexpr int32_t SYMB_BRILLIANT_RESEARCH = 100;
    constexpr int32_t SYMB_BOND_DIFFICULTY = 11;
    constexpr int32_t SYMB_BOND_RESEARCH = 250;
    constexpr int16_t SYMB_GUARDIAN_TYPE = NONPLAYER_SHIP_TYPE_EEL;
    // Natives: leave them (after NATIVES_TURNS they send a gift of NATIVES_GIFT_BC), teach them (NATIVES_TEACH_COST BC
    // now; at the end the colony gets a research lab, or NATIVES_TEACH_RESEARCH points when it has one) or use them
    // as workers (NATIVES_WORK_BC now; at the end a revolt with NATIVES_REVOLT_PERCENT: the colony loses a
    // population point and NATIVES_REVOLT_BC).
    constexpr int32_t NATIVES_TURNS = 8;
    constexpr int32_t NATIVES_GIFT_BC = 25;
    constexpr int32_t NATIVES_TEACH_COST = 40;
    constexpr int32_t NATIVES_TEACH_RESEARCH = 150;
    constexpr int32_t NATIVES_WORK_BC = 80;
    constexpr int32_t NATIVES_REVOLT_PERCENT = 35;
    constexpr int32_t NATIVES_REVOLT_BC = 30;
    // Vaults: VAULTS_TOTAL in the whole galaxy ever; a study of VAULT_TURNS, a check against VAULT_DIFFICULTY with a
    // bonus VAULT_FINDER_BONUS for the empire that has explored the most stars; success an application and
    // VAULT_BC, brilliant also VAULT_BRILLIANT_RESEARCH. A failed study loses the vault.
    constexpr int32_t VAULTS_TOTAL = 5;
    constexpr int32_t VAULT_TURNS = 6;
    constexpr int32_t VAULT_DIFFICULTY = 10;
    constexpr int32_t VAULT_FINDER_BONUS = 2;
    constexpr int32_t VAULT_BC = 100;
    constexpr int32_t VAULT_BRILLIANT_RESEARCH = 150;
    // Tombs: the dig takes TOMB_DIG_TURNS and then wakes a guardian; the empire with the most ship points at the
    // star, once the guardian is gone, gets an application (research points when none is left) and TOMB_BC.
    // After TOMB_GUARD_TURNS turns with the guardian alive the tomb is given up.
    constexpr int32_t TOMB_DIG_TURNS = 6;
    constexpr int32_t TOMB_GUARD_TURNS = 80;
    constexpr int32_t TOMB_BC = 150;
    constexpr int32_t TOMB_FALLBACK_RESEARCH = 200;
    constexpr int16_t TOMB_GUARDIAN_TYPE = NONPLAYER_SHIP_TYPE_EEL;
    // Research points when no application is left to learn.
    constexpr int32_t APP_FALLBACK_RESEARCH = 150;

    struct s_record {
        uint8_t kind = KIND_NONE;
        int8_t player = -1;     // the empire that has the find (a tomb: the digger)
        int16_t star = -1;
        int16_t other = -1;     // the colony of a power plant, symbionts, natives or tomb; -1 for a vault
        int32_t started = 0;
        int32_t ends = 0;       // the stardate of the end (a tomb: of the stage)
        int8_t option = 0;      // the way chosen (choices only)
        int8_t stage = 0;       // a tomb: 0 digging, 1 guardian awake
        int16_t value = 0;
    };

    constexpr int32_t USED_BYTES = BITMAP(MAX_STARS);

    struct s_state {
        int32_t count = 0;
        s_record record[MAX_RECORDS];
        uint8_t vaults_found = 0;
        uint8_t used[USED_BYTES] = {};  // stars that have had a discovery: no second one there
    };
    s_state* State();

    bool Used(int16_t star);
    // The first running record of `kind` of `player` (-1: anyone) at `star` (-1: anywhere), or nullptr.
    const s_record* Find(e_kind kind, int16_t player, int16_t star);
    int32_t Active(int16_t player);

    // The way a computer deals with a find of `kind`, by personality (humans and kinds without a choice: 0).
    int32_t Choose(e_kind kind, int16_t player);

    // Starts a find: at `star` (-1: one of the candidates, drawing from game_random when there are several), with
    // option `option` (-1: Choose). Costs are paid now. False when the flag is off, the table is full or the
    // empire has no place for it.
    bool Start(e_kind kind, int16_t player, int16_t star = -1, int32_t option = -1);

    // The dice check of the record at `index` (kinds with a check): with `dice` (one value per die of
    // dice::roll_kind) or, with nullptr, rolled with game_random. Applies the outcome and drops the record.
    void Check_With_Dice(int32_t index, const int32_t* dice);

    // Once a turn after beasts::Turn() (nextturn.cpp): the records run and end; a new find may start.
    void Turn();
    // The first half of Turn(): the records go one turn on (no new event starts).
    void Run_Records();

    void Clear();
    bool Empty();

    // Save section 'DISC' (game/rules_sections.h): payload version 1, then the count (uint8) and the vaults found
    // (uint8), per record kind (uint8), player (int8), star and other (int16), started and ends (int32), option
    // and stage (int8), value (int16), then the used-stars bitmap (BITMAP(MAX_STARS) bytes).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace discoveries
