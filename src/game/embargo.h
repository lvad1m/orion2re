#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Embargo and council resolutions (docs/LocalLLM/mechanics.md 2 and 2.7.1; README.md 10.9, tier 1).
// Stages 1-2 (diplomacy embargo): an empire puts another under an embargo and others join; the
// participants' trade and research treaties with the target end and none are signed while it
// lasts; the target holds "put us under an embargo" against each participant (opinion.h); while
// more than half of the target's former trade partners hold it, the target loses part of its
// income every turn. Stage 3 (diplomacy council_resolutions): the council votes on an embargo
// resolution, binding on everyone but the target. Off by default.
namespace embargo {

    extern bool enabled;               // diplomacy embargo
    extern bool council_resolutions;   // diplomacy council_resolutions
    extern int32_t period;             // diplomacy embargo_period: turns between the computer's reviews
    extern int32_t opinion_value;      // diplomacy embargo_opinion: the target's opinion of each participant
    extern int32_t penalty_percent;    // diplomacy embargo_penalty_percent: income the target loses
    extern int32_t min_turns;          // diplomacy embargo_min_turns: before an embargo may be lifted
    extern int32_t council_turns;      // diplomacy embargo_council_turns: how long a council embargo lasts
    extern int32_t opinion_min;        // diplomacy embargo_opinion_min: opinion at or below which one declares
    extern int32_t join_opinion;       // diplomacy embargo_join_opinion: opinion at or below which one joins

    // One embargo per target (state in embargo_state.cpp).
    struct s_embargo {
        int8_t initiator = -1;        // who declared it (the proposer of a council resolution)
        uint8_t participants = 0;     // bit k: player k holds it; 0: no embargo
        uint8_t former_partners = 0;  // bit k: player k had a trade treaty with the target at the start
        uint8_t by_council = 0;       // 1: a council resolution
        int32_t start = 0;            // the turn it began
    };

    // The record of the embargo on `target` (participants 0 when there is none), or nullptr for
    // an invalid player. Behaviour code changes it through this.
    s_embargo* Record(int16_t target);

    bool Active(int16_t target);
    bool Participates(int16_t member, int16_t target);
    // More than half of the target's former trade partners hold the embargo.
    bool Coalition_Holds(int16_t target);
    // No trade or research treaty between `a` and `b` while one holds an embargo on the other.
    bool Blocks_Treaty(int16_t a, int16_t b);

    // Behaviour (embargo.cpp). Each logs "embargo: ...".
    void Declare(int16_t initiator, int16_t target, bool by_council);
    void Join(int16_t member, int16_t target);
    void Leave(int16_t member, int16_t target, const char* why);
    void Lift(int16_t target, const char* why);

    // Would computer empire `member` join an embargo on `target`? Its opinion of the target is at
    // or below join_opinion, it is not the target's ally, an advised empire is not friendly to it,
    // and joining is cheap: no trade treaty with the target, or an opinion of -50 or worse.
    bool Would_Join(int16_t member, int16_t target);

    // The target computer empire `player` would put under an embargo now (-1: none): one it thinks
    // ill of (at or below opinion_min) with a bad name - treaties broken with us, an attack
    // without declaration, or three wars or more - not already under an embargo.
    int16_t Would_Declare(int16_t player);

    // Once a turn, with the other diplomacy steps: keeps the targets' opinions, charges the
    // coalition penalty, ends what has run out, and every `period` turns lets the computer
    // empires declare, join and lift embargoes.
    void End_Of_Turn();

    // At a council meeting without humans, after the vote on the leader: the computer members
    // propose embargoes; the one with the most support is voted on (each member's council votes;
    // "for": those that would join and the proposer's allies; passes with more than half of all
    // votes) and binds everyone but the target. Council_Proposal finds that proposal (false when
    // there is none) and Council_Enact makes a passed one binding; resolutions.h weighs it against
    // the other resolutions, Council_Resolution votes on it alone.
    bool Council_Proposal(int16_t* proposer, int16_t* target, int32_t* support);
    void Council_Enact(int16_t proposer, int16_t target);
    void Council_Resolution();

    void Clear();
    bool Empty();

    // Save section 'EMBG' (game/rules_sections.h): payload version 1, then per target: initiator
    // (int8), participants, former partners, by-council (uint8 each), start (int32).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace embargo
