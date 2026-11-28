#pragma once

#include "game/choices.h"
#include "game/consts.h"

#include <cstdint>
#include <string>

struct s_colony;

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Incidents: six events of biology, economy and internal politics (docs/LocalLLM/events-catalog.md 3.2, 3.3
// and 3.4; README.md 10.9, wave 4): a disease without pain, a drug of the natives, a found treasury, a rich
// deposit under a colony, a rebel general and a mad governor. Each is an event of an empire with a choice:
// a human chooses in the choice window when events human_choices is on (otherwise the first, safest option),
// a computer picks by its personality (Choose). One-off effects are applied at once; the lasting ones (morale, unrest, a flow of
// BC, a decision still to come) are kept as records here.
// Off by default (events incidents); with the flag off nothing here draws from game_random, nothing is
// kept and every hook returns the classic value.
namespace incidents {

    extern bool enabled;  // events incidents

    constexpr int32_t MAX_INCIDENTS = 24;
    // A new incident starts by chance, in per mille a turn for each empire (one draw each; at most one starts a
    // turn in the whole galaxy). The kind is drawn among the six.
    constexpr int32_t EMPIRE_CHANCE = 8;

    enum e_kind : uint8_t {
        KIND_NONE = 0,
        KIND_PAINLESS = 1,       // disease without pain; target: the colony
        KIND_NATIVE_DRUG = 2,    // drug of the natives; target: the colony
        KIND_TREASURY = 3,       // found treasury kept hidden; player: the finder, other: the owner, value: BC
        KIND_DEPOSIT = 4,        // a rich deposit under a colony; target: the colony
        KIND_REBEL_GENERAL = 5,  // a general that ignores orders; other: the leader
        KIND_MAD_GOVERNOR = 6,   // target: the colony, other: the governor
        KIND_LAST = 6,
    };

    // Options by kind (the first is the safest; a human without events human_choices takes it).
    enum e_painless_option : uint8_t { PAINLESS_CURE = 0, PAINLESS_QUARANTINE = 1, PAINLESS_RECRUIT = 2 };
    enum e_drug_option : uint8_t { DRUG_TOLERATE = 0, DRUG_BAN = 1, DRUG_TAX = 2 };
    enum e_treasury_option : uint8_t { TREASURY_RETURN = 0, TREASURY_COMMISSION = 1, TREASURY_KEEP = 2 };
    enum e_deposit_option : uint8_t { DEPOSIT_LEAVE = 0, DEPOSIT_CAREFUL = 1, DEPOSIT_PLUNDER = 2 };
    enum e_general_option : uint8_t { GENERAL_REASON = 0, GENERAL_IGNORE = 1, GENERAL_EXECUTE = 2 };
    enum e_governor_option : uint8_t { GOVERNOR_REMOVE = 0, GOVERNOR_KEEP = 1 };

    // Disease without pain (3.2): the survivors feel no pain. Cure costs PAINLESS_CURE_COST BC (an empire that
    // cannot pay and keep PAINLESS_RESERVE quarantines); quarantine weighs on the colony's morale
    // (PAINLESS_QUARANTINE_MORALE) for PAINLESS_TURNS; recruiting the survivors as soldiers (the classic plague's
    // loss of people for PAINLESS_LEVY_TURNS, as a levy) lifts the morale (PAINLESS_RECRUIT_MORALE) for
    // PAINLESS_TURNS, and the empire's ground troops fight with a bonus of PAINLESS_GROUND_BONUS strength
    // points (Ground_Bonus, invasion.cpp Compute_Ground_Combat_Info) for the same PAINLESS_TURNS, attacking
    // or defending.
    // Design row (events-catalog.md 3.2): "Болезнь без боли | выжившие не чувствуют боли: лечить, карантин или
    // набрать в десант | выбор, ± население, бонус наземным войскам".
    constexpr int32_t PAINLESS_CURE_COST = 60;
    constexpr int32_t PAINLESS_RESERVE = 50;
    constexpr int32_t PAINLESS_TURNS = 10;
    constexpr int32_t PAINLESS_LEVY_TURNS = 2;
    constexpr int32_t PAINLESS_QUARANTINE_MORALE = -2;   // morale points (5 % each)
    constexpr int32_t PAINLESS_RECRUIT_MORALE = 1;
    constexpr int32_t PAINLESS_GROUND_BONUS = 10;  // strength points (the roll is 1..100; a rifle gives about 5)
    // Drug of the natives (3.2): tolerated, it calms the people (DRUG_TOLERATE_MORALE) but costs the treasury
    // a BC per colony population a turn (the lost output); a ban weighs on the morale (DRUG_BAN_MORALE) and
    // smugglers rob the empire (DRUG_CONTRABAND_PERCENT a turn, DRUG_CONTRABAND_LOSS BC); a tax gives the
    // empire a BC per population a turn and a milder calm. All last DRUG_TURNS.
    constexpr int32_t DRUG_TURNS = 20;
    constexpr int32_t DRUG_TOLERATE_MORALE = 2;
    constexpr int32_t DRUG_TAX_MORALE = 1;
    constexpr int32_t DRUG_BAN_MORALE = -1;
    constexpr int32_t DRUG_CONTRABAND_PERCENT = 10;
    constexpr int32_t DRUG_CONTRABAND_LOSS = 20;
    // Found treasury (3.3): stolen money on a wreck, TREASURY_MIN..TREASURY_MAX BC, of another empire in
    // contact. Returned in full (the owner's opinion +TREASURY_RETURN_OPINION), or with a commission of
    // TREASURY_COMMISSION_PERCENT (+TREASURY_COMMISSION_OPINION), or kept (the finder gets all; the owner
    // finds out through its spies, each turn TREASURY_LEAK_PERCENT, for TREASURY_SECRET_TURNS, and remembers
    // it, TREASURY_LEAK_OPINION).
    constexpr int32_t TREASURY_MIN = 60;
    constexpr int32_t TREASURY_MAX = 240;
    constexpr int32_t TREASURY_RETURN_OPINION = 15;
    constexpr int32_t TREASURY_COMMISSION_PERCENT = 20;
    constexpr int32_t TREASURY_COMMISSION_OPINION = 7;
    constexpr int32_t TREASURY_SECRET_TURNS = 60;
    constexpr int32_t TREASURY_LEAK_PERCENT = 6;
    constexpr int32_t TREASURY_LEAK_OPINION = -20;
    // Rich deposit under a colony (3.3): left alone it is a pride (DEPOSIT_LEAVE_MORALE for DEPOSIT_TURNS);
    // careful mining makes the planet one mineral class richer (Enrich_Planet); plunder, two classes, but
    // the people revolt (DEPOSIT_PLUNDER_MORALE, DEPOSIT_PLUNDER_UNREST stability points) and each turn
    // DEPOSIT_QUAKE_PERCENT an earthquake hits the colony, for DEPOSIT_PLUNDER_TURNS.
    constexpr int32_t DEPOSIT_TURNS = 15;
    constexpr int32_t DEPOSIT_LEAVE_MORALE = 1;
    constexpr int32_t DEPOSIT_PLUNDER_TURNS = 15;
    constexpr int32_t DEPOSIT_PLUNDER_MORALE = -2;
    constexpr int32_t DEPOSIT_PLUNDER_UNREST = 10;
    constexpr int32_t DEPOSIT_QUAKE_PERCENT = 5;
    // Rebel general (3.4): a ship captain that ignores orders. Reasoning is a dice check against
    // GENERAL_DIFFICULTY (+ the general's loyalty over 50 in tens): a success restores GENERAL_LOYALTY_GAIN
    // loyalty, a failure is a coup. Ignored, the matter ripens for GENERAL_RIPEN_TURNS, then half the time it
    // is a coup and half a trial (the general is dismissed, loyalty -GENERAL_JAILED). A coup kills the
    // general and his ship goes over to the enemy of the empire (ab-review 3.3: the empire it is at war with
    // that it thinks worst of; the ship changes owner as in the surrender of an empire, its officer gone); at
    // peace with everybody the ship is lost.
    // Design row (events-catalog.md 3.4): "Мятежный генерал | генерал не подчиняется: урезонить, игнорировать,
    // казнить; ветки — переворот или суд | выбор, лидер, мораль"; decision ab-review 3.3: "корабль уходит к
    // врагу империи".
    // An execution kills the general at once and frightens the other leaders (loyalty -GENERAL_FEAR).
    constexpr int32_t GENERAL_DIFFICULTY = 12;
    constexpr int32_t GENERAL_RIPEN_TURNS = 10;
    constexpr int32_t GENERAL_LOYALTY_GAIN = 20;
    constexpr int32_t GENERAL_JAILED = 20;
    constexpr int32_t GENERAL_FEAR = 10;
    // Mad governor (3.4): the colony's governor doses the water. Kept, the colony booms for GOVERNOR_BOOM_TURNS
    // (GOVERNOR_BOOM_BC a population a turn), then it collapses: an earthquake's hit (people or buildings),
    // morale GOVERNOR_CRASH_MORALE for GOVERNOR_CRASH_TURNS, and the governor is removed. Removed at once, it
    // is dismissed with nothing else.
    constexpr int32_t GOVERNOR_BOOM_TURNS = 10;
    constexpr int32_t GOVERNOR_BOOM_BC = 2;
    constexpr int32_t GOVERNOR_CRASH_TURNS = 10;
    constexpr int32_t GOVERNOR_CRASH_MORALE = -2;

    struct s_incident {
        uint8_t kind = KIND_NONE;
        int8_t player = -1;
        int16_t target = -1;      // a colony
        int16_t other = -1;       // a leader or the other empire
        int32_t started = 0;
        int32_t ends = 0;         // the stardate the record ends or ripens
        int8_t option = 0;
        uint8_t stage = 0;        // governor: 0 the boom, 1 the crash
        int8_t morale = 0;        // what Morale_Bonus gives the target colony
        int8_t unrest = 0;        // what Unrest gives it
        int16_t value = 0;        // BC a turn, or BC for the treasury; levy: the plague turns
    };

    struct s_state {
        int32_t count = 0;
        s_incident incident[MAX_INCIDENTS];
    };
    s_state* State();

    // Hooks (return the classic value, 0 or false, with the flag off).
    int32_t Morale_Bonus(const s_colony* colony);  // colcalc_main.cpp Colony_Morale (morale points)
    int32_t Unrest(int16_t colony);                // stability.cpp (stability points)
    bool Plague(int16_t colony);                   // colcalc.cpp: a levy of the survivors, as the classic plague
    // invasion.cpp Compute_Ground_Combat_Info: strength points added to every ground unit of `player` while a
    // recruited-survivors record of the painless disease runs (PAINLESS_GROUND_BONUS); 0 with the flag off.
    int32_t Ground_Bonus(int16_t player);

    // The choice window (choices.h): the text of a pending choice of this module (false when the subject is
    // gone or there is no room for a record) and the application of the option picked, with the subject
    // re-checked; the rebel general's dice are drawn now.
    bool Choice_Text(const choices::s_choice& choice, choices::s_text* text);
    void Choice_Apply(const choices::s_choice& choice, int32_t option);

    // The option a computer of `player` takes for `kind` by its personality; a human, or the flag off: 0.
    int32_t Choose(e_kind kind, int16_t player);

    // Starts the event of `kind` for `player` on a subject it picks (draws from game_random when there is a
    // choice of subjects) and applies the option Choose gives. A human with events human_choices on gets a
    // pending choice instead (choices.h; true, nothing applied yet). `option` >= 0 overrides the choice;
    // `dice_values` (3 values) replaces the dice of the reasoning check. False when it cannot start.
    bool Start(e_kind kind, int16_t player, int32_t option = -1, const int32_t* dice_values = nullptr);

    // Once a turn after wanderers::Turn() (nextturn.cpp): the records run and end; a new incident may start.
    void Turn();
    // The first half of Turn(): the running records go one turn on and end (no new incident starts).
    void Run_Records();

    void Clear();
    bool Empty();

    // Save section 'INCD' (game/rules_sections.h): payload version 1, then the count (uint8), per record kind
    // (uint8), player (int8), target and other (int16), started and ends (int32), option (int8), stage (uint8),
    // morale and unrest (int8), value (int16).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace incidents
