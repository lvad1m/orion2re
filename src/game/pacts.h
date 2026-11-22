#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Defence pacts and guarantees of independence (docs/LocalLLM/mechanics.md 3.1.4; README.md 10.9,
// tier 1): an obligation to go to war with whoever attacks a partner. Pacts are mutual, a
// guarantee protects one empire. For now only computer empires keep them (the human gets them with
// the interface step). Off by default.
namespace pacts {

    extern bool enabled;              // diplomacy pacts
    extern int32_t period;            // diplomacy pacts_period: turns between reviews
    extern int32_t opinion_min;       // diplomacy pacts_opinion: opinion partners need of each other
    extern int32_t defended_opinion;  // diplomacy pacts_defended_opinion: the victim's memory of help
    extern int32_t abandoned_opinion; // diplomacy pacts_abandoned_opinion: the victim's memory of desertion
    extern int32_t guarantees_max;    // diplomacy pacts_guarantees_max: guarantees one empire gives

    bool Has_Pact(int16_t a, int16_t b);
    bool Guarantees(int16_t guarantor, int16_t protege);
    int32_t Guarantee_Count(int16_t guarantor);

    // Signing and ending (the rules below and, later, the human's diplomacy screen); each logs
    // "pacts: ...". `threat` is the empire the treaty is aimed at (for the log), or -1.
    void Sign_Pact(int16_t a, int16_t b, int16_t threat);
    void End_Pact(int16_t a, int16_t b, const char* why);
    void Give_Guarantee(int16_t guarantor, int16_t protege, int16_t threat);
    void End_Guarantee(int16_t guarantor, int16_t protege, const char* why);

    // `aggressor` started a war on `victim` or attacked it without declaring war; called where an
    // empire decides to attack (not where a victim answers a provocation). Every computer partner
    // of the victim by pact or guarantee - alive, in contact with the aggressor, not yet at war
    // with it and not its ally - declares war on the aggressor, and the victim remembers "came to
    // our defence"; a partner too weary for a new war (diplomemory) stays out, the victim
    // remembers "left us alone", and the pact or guarantee ends. Declarations made here do not
    // call on further pacts.
    void On_Aggression(int16_t aggressor, int16_t victim);

    // Once a turn: every `period` turns ends pacts and guarantees whose terms are gone and lets
    // the computer empires sign new ones by rules (mechanics.md 3.1.4).
    void End_Of_Turn();

    void Clear();
    bool Empty();

    // Save section 'PACT' (game/rules_sections.h): payload version 1, then MAX_PLAYERS pact masks
    // and MAX_PLAYERS guarantee masks (uint8, bit k: with / of player k).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace pacts
