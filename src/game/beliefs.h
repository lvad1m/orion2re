#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Intrigues, priority B (docs/LocalLLM/mechanics.md 3.5.7 and 3.5.10; README.md 10.5 method 5 and
// 10.9, tier 4):
// - the neighbour model: what each computer empire believes of every other - will it attack us,
//   does it keep its treaties, does it tell the truth - as log-odds: a prior from its public
//   personality plus the evidence of what it did (every opinion memory, broken treaties, lies in
//   military reports), fading slowly. Read where a computer decides whether to trust: pacts, a
//   warning of a plot, an offer of a proxy war; shown in the advisor's view;
// - disinformation: an empire with a turned spy or a network in another feeds it a false picture of
//   its fleet - stronger when it fears that empire, weaker when it plans to attack it. The advisor's
//   view of the deceived empire shows the false numbers until its spies see through the lie.
// Both off by default; with them off nothing here draws from game_random and no state is kept.
namespace beliefs {

    extern bool neighbour_model;        // diplomacy neighbour_model
    extern bool disinformation;         // spy disinformation
    extern int32_t disinformation_cost; // spy disinformation_cost: network points

    enum e_belief : uint8_t {
        BELIEF_HOSTILE = 0,   // will attack us
        BELIEF_FAITHFUL = 1,  // keeps its treaties and its word
        BELIEF_HONEST = 2,    // tells the truth
        BELIEF_COUNT = 3,
    };

    // Programming choices (README.md 10.9: tuned on the big bench).
    constexpr int32_t EVIDENCE_LIMIT = 20;      // log-odds steps either way
    constexpr int32_t DECAY_PERIOD = 10;        // turns per step the evidence fades
    constexpr int32_t TRUST_MIN_PERCENT = 35;   // below: no pact, no belief in a warning or a payer
    constexpr int32_t DISINFO_STRONGER = 150;   // percent of the true fleet shown
    constexpr int32_t DISINFO_WEAKER = 60;
    constexpr int32_t DISINFO_TURNS = 20;
    constexpr int32_t SEE_THROUGH_PER_SPY = 20; // per mille a turn for each spy of the deceived
    constexpr int32_t SEEN_THROUGH_OPINION = -15;
    constexpr int32_t PERIOD = 10;

    // The prior in log-odds steps for an empire of `personality` (s_player::personality).
    int32_t Prior(uint8_t personality, e_belief belief);
    // Evidence `observer` holds about `subject` (0 with the model off).
    int32_t Evidence(int16_t observer, int16_t subject, e_belief belief);
    // How likely `observer` thinks it, in percent: logistic(prior + evidence); 50 with the model off.
    int32_t Percent(int16_t observer, int16_t subject, e_belief belief);
    // `observer` trusts `subject` enough to act on its word (always with the model off).
    bool Trusts(int16_t observer, int16_t subject);
    bool Believes_Faithful(int16_t observer, int16_t subject);

    // Evidence: adds `steps` (log-odds) to `observer`'s belief about `subject`.
    void Observe(int16_t observer, int16_t subject, e_belief belief, int32_t steps);
    // An opinion memory was added (opinion.cpp Add): each reason is evidence.
    void On_Opinion(int16_t holder, int16_t target, uint8_t reason, int32_t value);
    // `breaker` broke a treaty with `victim` (diplomac.cpp Break_Treaties): every computer that knows
    // it thinks it less faithful, the victim most.
    void On_Treaty_Broken(int16_t breaker, int16_t victim);
    // `receiver` caught `sender` lying in a military report (advisor_reports.cpp).
    void On_Lie(int16_t receiver, int16_t sender);

    // Percent of the true fleet numbers `observer` is shown about `subject` (100 when not deceived).
    int32_t Distortion_Percent(int16_t observer, int16_t subject);
    // `deceiver` plants a false picture of its fleet in `target` (rolls the dice; for the computers
    // and the tests); `percent` is DISINFO_STRONGER or DISINFO_WEAKER.
    bool Plant_Disinformation(int16_t deceiver, int16_t target, int32_t percent);
    bool Plant_Disinformation_With_Dice(int16_t deceiver, int16_t target, int32_t percent, const int32_t* dice_values);

    // Once a turn (spy.cpp, after intrigue::Turn): deceptions may be seen through or run out; every
    // PERIOD turns the computers plant disinformation; every DECAY_PERIOD turns the evidence fades.
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'BELF' (game/rules_sections.h): payload version 1, then the evidence (int8,
    // MAX_PLAYERS x MAX_PLAYERS x BELIEF_COUNT, [observer][subject]), the distortion percent (int16,
    // [deceived][deceiver]) and when it ends (int32).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

    struct s_state {
        int8_t evidence[8][8][BELIEF_COUNT] = {};
        int16_t distortion[8][8] = {};
        int32_t distortion_until[8][8] = {};
    };
    s_state* State();

} // namespace beliefs
