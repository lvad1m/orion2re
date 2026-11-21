#pragma once

#include <cstdint>

#include "game/world_view.h"

// Military reports (docs/LocalLLM/mechanics.md 3.1.1; phase part 32, PHASE_PART_REPORTS). An empire
// at war tells the advised computer empires it is courting how much fighting power it has near
// their colonies, so a small neighbour of a would-be winner can tell whether help is at hand
// (rule 408). The sender may exaggerate - by its leader's character, without random numbers - the
// receiver believes it as far as it trusts the sender (s_player::trust_worthiness, which the
// diplomacy memory keeps), and a lie its scanners catch costs the liar that trust.
// Reports are recomputed every turn and never saved; caught lies live on in the trust.
namespace advisor {

    struct s_military_report {
        bool sent = false;
        int64_t claimed = 0;      // fighting power (strength x (warships + 1)) the sender claims near us
        int64_t detected = 0;     // the fighting power of those ships our scanners see
        int32_t credibility = 0;  // percent of the claim we believe
    };

    // Ships within this many map units (6 parsecs; 45 units each) of one of the receiver's
    // colonies count as near it.
    inline constexpr int32_t REPORT_RADIUS = 6 * 45;
    // A claim above this percent of what the receiver's scanners see near it is a caught lie.
    inline constexpr int32_t REPORT_LIE_PERCENT = 140;
    // Trust the receiver withdraws from a caught liar, once every REPORT_CHECK_PERIOD turns.
    inline constexpr int32_t REPORT_LIE_TRUST = 5;
    inline constexpr int32_t REPORT_CHECK_PERIOD = 5;

    // How much a leader of this character inflates its reports: honorable and pacifist 100,
    // aggressive and xenophobic 150, ruthless and erratic 200 (percent).
    int32_t Report_Exaggeration_Percent(uint8_t personality);

    // How much of a report the receiver believes for its trust in the sender: 50 + trust,
    // between 10 and 100 (percent).
    int32_t Report_Credibility_Percent(int32_t trust);

    // The report `sender` gives `receiver` this turn. Sent only with the reports part on, to an
    // advised computer empire, by a living computer empire in contact with it, not at war with it
    // and at war with someone else.
    s_military_report Military_Report(const game::s_view* world, uint8_t receiver, uint8_t sender);

    // Once a turn from Run_Turn, before the views: every REPORT_CHECK_PERIOD turns each advised
    // empire compares the reports it gets with what its scanners see and lowers its trust in a
    // caught liar by REPORT_LIE_TRUST (diplomemory::Lower_Trust).
    void Check_Military_Reports(const game::s_view* world);

} // namespace advisor
