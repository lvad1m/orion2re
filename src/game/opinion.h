#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Opinion modifiers with reasons (docs/LocalLLM/mechanics.md 3.1 and 4.1; README.md 10.9, tier 1):
// a computer empire remembers why it likes or dislikes another - "attacked us without declaring
// war", "helped us in our war" - and each memory fades over time. The classic relations value is
// left as it is; where the computer decides (treaty answers, war plans, council votes, demands)
// it reads Relations(), the classic value plus the sum of its modifiers. Off by default: with the
// flag off Relations() is the classic value and no state is kept, so the game is unchanged.
namespace opinion {

    extern bool enabled;               // diplomacy opinion
    extern int32_t decay_period;       // diplomacy opinion_decay_period: turns per point a modifier fades
    extern int32_t sneak_attack;       // diplomacy opinion_sneak_attack: the victim's opinion of the attacker
    extern int32_t helped_in_war;      // diplomacy opinion_helped_in_war: for going to war with our enemy
    extern bool slow_grudges;          // diplomacy slow_grudges: bad memories fade half as fast as good ones

    // Why an empire feels as it does about another. Values are stable: they are saved.
    enum e_reason : uint8_t {
        REASON_SNEAK_ATTACK = 0,   // attacked us without declaring war
        REASON_HELPED_IN_WAR = 1,  // went to war with an enemy of ours
        REASON_EMBARGO = 2,        // put us under an embargo
        REASON_RIVAL = 3,          // declared us its rival
        REASON_DEFENDED_US = 4,    // came to our defence under a pact or a guarantee
        REASON_ABANDONED_US = 5,   // left us alone against an attacker it had promised to stop
        REASON_BROKE_BAN = 6,      // broke a ban of the galactic council
        REASON_CAUGHT_SPYING = 7,  // its spy was caught working against us
        REASON_BRIBED_OUR_LEADER = 8,
        REASON_STIRRED_UNREST = 9,
        REASON_KILLED_OUR_SCIENTIST = 10,
        REASON_FRAMED_OUR_LEADER = 11,
        REASON_SABOTAGE = 12,      // its saboteur was caught at work against us
        REASON_DID_US_A_SERVICE = 13,   // warned us of an attack, paid a favour it owed (intrigue.h)
        REASON_PLOTTED_AGAINST_US = 14, // planned an attack, paid for a war or forged a letter against us
        REASON_TREATED_OUR_SPY = 15,    // executed (-) or sent home (+) a spy of ours
        // Reasons of the A and B scope (README.md 10.9, 24.11.2026); payload version 3.
        REASON_KILLED_OUR_DIPLOMAT = 16,  // an attack on a diplomat: worse than a fleet skirmish (events 3.12)
        REASON_ATROCITY = 17,             // biological weapons, a destroyed planet, poisoned water: "the North remembers"
        REASON_BROKE_ITS_WORD = 18,       // broke a treaty with us (an oathbreaker, mechanics 3.5.6)
        REASON_BLACKMAILED_US = 19,       // used a secret of ours against us (mechanics 3.5.2)
        REASON_EXPOSED_US = 20,           // made a secret of ours public
        REASON_SMEARED_US = 21,           // its smear campaign against us was uncovered (mechanics 3.2)
        REASON_RAIDED_US = 22,            // paid raiders or pirates to strike us: worse than nothing, milder than a skirmish
        REASON_TRIED_TO_KILL_OUR_LEADER = 23,  // an assassination attempt (mechanics 3.2.2)
        REASON_BETRAYED_A_SECRET_PACT = 24,    // broke a secret treaty with us (mechanics 3.5.3)
        REASON_HARMED_A_HOSTAGE = 25,     // a hostage of ours came to harm
        REASON_FAMILY_TIES = 26,          // a dynastic marriage or a hostage kept well (+)
        REASON_OVERLORD = 27,             // our overlord: protection for tribute (+) or a harsh one (-)
        REASON_RESERVED_28 = 28,
        REASON_RESERVED_29 = 29,
        REASON_RESERVED_30 = 30,
        REASON_RESERVED_31 = 31,
        REASON_COUNT = 32,
    };

    // Graded incidents (events-catalog 3.12): an attack on a diplomat is worse than a fleet skirmish
    // (diplomacy opinion_sneak_attack, -40), a skirmish worse than a raid.
    constexpr int32_t DIPLOMAT_KILLED_OPINION = -50;
    constexpr int32_t RAIDED_OPINION = -20;
    // "The North remembers": an atrocity fades eight times slower than other memories.
    constexpr int32_t ATROCITY_FADE_STEPS = 8;

    // Largest size of one modifier; a repeated reason deepens it up to this.
    constexpr int32_t MODIFIER_LIMIT = 100;
    // Routine incidents deepen only to a grudge: MOO2 catches spies all the time (about 220 catches a
    // game of five empires), and at -15 a catch every reason "caught spying" and "decided the fate of
    // a spy of ours" sat at -100 for nearly every pair by the late game in the big bench (22.11.2026),
    // so every empire hated every other and the council could not elect anyone.
    constexpr int32_t ROUTINE_LIMIT = 30;
    // The largest size of a modifier of `reason`: ROUTINE_LIMIT for the routine spy incidents,
    // MODIFIER_LIMIT otherwise.
    int32_t Reason_Limit(e_reason reason);

    // Adds `value` to `holder`'s modifier `reason` toward `target` (a repeated reason deepens the
    // memory, up to MODIFIER_LIMIT either way). Nothing happens with the flag off, for a human
    // holder (the human player has no relations), for an eliminated or invalid player, or for
    // holder == target.
    void Add(int16_t holder, int16_t target, e_reason reason, int32_t value);

    // Sets the modifier to `value` (within MODIFIER_LIMIT), for a memory that holds while its
    // cause lasts (a declared rivalry, an embargo): its mechanic sets it every turn, and once the
    // cause ends it fades like any other. Same conditions as Add.
    void Set(int16_t holder, int16_t target, e_reason reason, int32_t value);

    // The modifier `reason` of `holder` toward `target`, 0 when there is none.
    int32_t Modifier(int16_t holder, int16_t target, e_reason reason);

    // Sum of `holder`'s modifiers toward `target` (0 with the flag off).
    int32_t Sum(int16_t holder, int16_t target);

    // What a computer empire's decisions read: the classic relations of `holder` toward `target`
    // plus Sum(), kept within -100..100. With the flag off, exactly the classic value.
    int32_t Relations(int16_t holder, int16_t target);

    // A short description of a reason for the empire view and later the diplomacy screen.
    const char* Reason_Name(e_reason reason);

    // Once a turn, with the classic end-of-turn diplomacy adjustments: every `decay_period` turns
    // each modifier moves 1 toward zero.
    void End_Of_Turn();

    // Events. A sneak attack: the victim remembers it. A war between `a` and `b`: every empire
    // already at war with `b` remembers that `a` joined against its enemy, and the same for `a`'s
    // enemies toward `b` - the enemy of my enemy.
    void On_Sneak_Attack(int16_t attacker, int16_t victim);
    void On_War_Started(int16_t a, int16_t b);

    // Forgets every modifier (a new game, a load without the section).
    void Clear();
    bool Empty();

    // Save section 'OPIN' (game/rules_sections.h): payload version 2, then MAX_PLAYERS x
    // MAX_PLAYERS x REASON_COUNT (32) int8 values; versions 1 (8 reasons per pair) and 2 (16) still load, the
    // newer reasons empty. Parse reads into a pending copy that Commit makes current once the
    // whole load has succeeded.
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace opinion
