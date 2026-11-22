#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Intrigues, priority A (docs/LocalLLM/mechanics.md 3.5 and 3.5.9; README.md 10.9, tier 4):
// - revealed plans: spies uncover a planned attack (the original AI's sneak attack or an advised
//   empire's new war) and the victim learns of it, or a third empire warns it - the plotter loses
//   the surprise and the warned empire owes the warner a favour;
// - proxy wars: a computer empire at war pays a third empire that dislikes its enemy to attack it;
// - forged letters: spies plant a letter between two empires bound by an alliance or a defence
//   pact; each now thinks the other plots against it, and an alliance may break;
// - favours: a ledger of debts between empires (for a warning, a defence under a pact, a spy sent
//   home); a debtor pays with its council vote or by joining its creditor's war;
// - the fate of a caught spy: a computer empire executes, expels, sends home or turns the spy.
// Every part has its own flag, off by default; with all of them off nothing here draws from
// game_random and no state is kept.
namespace intrigue {

    extern bool reveal_plans;            // spy reveal_plans
    extern int32_t reveal_chance;        // spy reveal_chance: per mille per spy per turn
    extern bool forged_letters;          // spy forged_letters
    extern int32_t forged_letter_cost;   // spy forged_letter_cost: network points
    extern bool spy_fate;                // spy spy_fate
    extern bool proxy_wars;              // diplomacy proxy_wars
    extern int32_t proxy_payment_percent;  // diplomacy proxy_payment_percent: of the payer's treasury
    extern bool favours;                 // diplomacy favours
    extern int32_t favour_vote_bonus;    // diplomacy favour_vote_bonus: council vote score

    // Programming choices (README.md 10.9: numbers are tuned on the big bench).
    constexpr int32_t REVEAL_MEMORY = 20;        // turns a revealed plan is not revealed again
    constexpr int32_t PLOTTED_OPINION = -30;     // the victim's opinion of a revealed plotter
    constexpr int32_t SERVICE_OPINION = 20;      // for a warning or a paid favour
    constexpr int32_t REFUSED_OPINION = -20;     // a favour called in and refused
    constexpr int32_t WARN_MIN_RELATIONS = 0;    // a third empire warns a victim it likes at least this much
    constexpr int32_t PROXY_MIN_PAYMENT = 100;   // BC
    constexpr int32_t PROXY_MAX_RELATIONS = -10; // a hired empire dislikes the enemy at least this much
    constexpr int32_t FAVOUR_LIMIT = 3;          // favours one empire can owe another
    constexpr int32_t FAVOUR_WAR_MAX_RELATIONS = 30;  // a debtor joins a war against an empire it likes less than this
    constexpr int32_t FORGED_OPINION = -30;      // what a forged letter does to each side's opinion
    constexpr int32_t FORGED_BREAK_RELATIONS = -20;   // an ally this disliked breaks the alliance
    constexpr int32_t FATE_EXECUTE_RELATIONS = -30;   // at or below: the spy is executed
    constexpr int32_t FATE_RETURN_RELATIONS = 20;     // at or above: the spy is sent home
    constexpr int32_t FATE_TURN_PERCENT = 34;    // otherwise the chance to turn it (with the spy network)
    constexpr int32_t EXECUTION_OPINION = -15;   // the spy's owner's opinion of the executioner
    constexpr int32_t RETURN_OPINION = 10;
    constexpr int32_t EXPEL_TURNS = 10;          // turns the expelled owner sends no new spies (computers)
    constexpr int32_t DOUBLE_AGENT_TURNS = 30;   // a turned spy works this long
    constexpr int32_t DOUBLE_AGENT_DIFFICULTY = 2;    // added to the owner's operations against its catcher
    constexpr int32_t PERIOD = 10;               // turns between the computers' proxy wars, letters and calls

    // The empire `plotter` plans to attack (the original AI's sneak attack, an advised empire's
    // new war), each victim once; returns how many were written to `victims` (at most 8).
    int32_t Planned_Victims(int16_t plotter, int16_t* victims);

    // Favours `debtor` owes `creditor` (0 with the flag off).
    int32_t Favours(int16_t debtor, int16_t creditor);
    // `debtor` now owes `creditor` one more favour (up to FAVOUR_LIMIT); nothing with the flag off.
    void Owe(int16_t debtor, int16_t creditor);

    // What a favour owed adds to `voter`'s council vote score for `candidate` (council.cpp
    // Vote_Check).
    int32_t Council_Vote_Bias(int16_t voter, int16_t candidate);
    // A member's vote was cast (council.cpp): a debt to the nominee it voted for is paid; a debt to
    // a nominee it voted against or abstained on is dishonoured. Both spend the favour.
    void On_Council_Vote(int16_t voter, int16_t voted_for, int16_t nominee0, int16_t nominee1);

    // A spy of `attacker` was caught by `victim` (espionage.cpp On_Spy_Caught): its fate is decided
    // at the end of the spy step (Turn), so the spy loops of spy.cpp never see a changed count.
    void On_Spy_Caught(int16_t victim, int16_t attacker);
    // The fate a computer `victim` gives a spy of `attacker` now (for Turn and the tests); a human
    // victim keeps the classic fate.
    enum e_fate : uint8_t {
        FATE_CLASSIC = 0,   // nothing more: the spy is lost, as in the original game
        FATE_EXECUTED = 1,
        FATE_EXPELLED = 2,
        FATE_SENT_HOME = 3,
        FATE_TURNED = 4,
    };
    e_fate Decide_Fate(int16_t victim, int16_t attacker, int32_t turn_roll);
    void Apply_Fate(int16_t victim, int16_t attacker, e_fate fate);

    // `catcher` has a turned spy of `owner` working for it (spy_ops.cpp adds to the difficulty).
    bool Has_Double_Agent(int16_t catcher, int16_t owner);
    int32_t Double_Agent_Difficulty(int16_t attacker, int16_t target);

    // A forged letter from `forger`, planted in `target` about `other` (rolls the dice; for the
    // computers and the tests). Returns true when the letter was believed.
    bool Forge_Letter(int16_t forger, int16_t target, int16_t other);
    bool Forge_Letter_With_Dice(int16_t forger, int16_t target, int16_t other, const int32_t* dice_values);

    // `payer` hires `hired` against `enemy` for `payment` BC (for the computers and the tests).
    bool Hire_Proxy(int16_t payer, int16_t hired, int16_t enemy, int32_t payment);

    // Once a turn at the end of the spy step (spy.cpp Resolve_Spies): fates of the spies caught
    // this turn, revealed plans, and every PERIOD turns the computers' proxy wars, forged letters
    // and favours called in for a war.
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'INTR' (game/rules_sections.h): payload version 1, then favours (uint8,
    // MAX_PLAYERS x MAX_PLAYERS, [debtor][creditor]), the stardate each plot was last revealed
    // (int32, [plotter][victim]) and the stardate each double agent stops working (int32,
    // [catcher][owner]).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

    // State access for the behaviour code (intrigue_state.cpp).
    struct s_state {
        uint8_t favours[8][8] = {};
        int32_t revealed[8][8] = {};
        int32_t double_agent_until[8][8] = {};
    };
    s_state* State();

    // Spies caught this turn, waiting for their fate (not saved: the spy step settles them).
    struct s_caught {
        int8_t victim = -1;
        int8_t attacker = -1;
    };
    constexpr int32_t CAUGHT_QUEUE = 64;
    s_caught* Caught_Queue(int32_t* count);
    void Clear_Caught_Queue();
    bool Any_Enabled();

} // namespace intrigue
