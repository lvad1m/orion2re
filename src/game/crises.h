#pragma once

#include "game/consts.h"

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Crises with a choice and event chains (docs/LocalLLM/events-catalog.md 5.1 and 3.1-3.4; README.md
// 10.9, tier 5B). A crisis opens with three options; each has a cost now, a kind of check
// (science, diplomacy, army, espionage) and may leave a mark on the empire's reputation. Three turns
// later the engine rolls the check - 2d10 + the empire's standing in that field + a leader's skill
// against the difficulty - and builds the outcome (success, partial, failure) from the classic
// primitives; some outcomes start a follow-up crisis some turns later. Computer empires choose by
// personality and objective; a human's crises wait for the interface step (none start for a human).
// Off by default; with the flag off nothing here draws from game_random and no state is kept.
namespace crises {

    extern bool enabled;        // events crises
    extern int32_t chance;      // events crisis_chance: per mille a turn for each computer empire
    extern bool situations;     // events situations: the situation scale (war weariness) and the academy contest
    extern bool crisis_front;   // events crisis_front: the threat of atrocities, the Antaran front, the council project
    extern int32_t front_strength;  // events crisis_front_strength: the Antaran front's start, 0..100; 0 = no front

    enum e_crisis : uint8_t {
        CRISIS_NONE = 0,
        CRISIS_ASTEROID = 1,         // an asteroid falls toward the capital
        CRISIS_BRAIN_PARASITES = 2,  // parasites in a colony's people
        CRISIS_REVOLUTIONARIES = 3,  // revolutionaries in a colony
        CRISIS_PARASITES_RETURN = 4, // follow-up: the parasites come back
        CRISIS_REVOLUTION = 5,       // follow-up: the revolutionaries rise
        CRISIS_COUNT = 6,
    };

    enum e_check : uint8_t {
        CHECK_SCIENCE = 0,
        CHECK_DIPLOMACY = 1,
        CHECK_ARMY = 2,
        CHECK_ESPIONAGE = 3,
        CHECK_COUNT = 4,
    };

    enum e_result : uint8_t {
        RESULT_PENDING = 0,
        RESULT_SUCCESS = 1,
        RESULT_PARTIAL = 2,
        RESULT_FAILURE = 3,
    };

    struct s_option {
        const char* name;
        int16_t cost_bc;
        uint8_t check;        // e_check
        bool cruel;           // leaves a mark: the neighbours think it likelier to attack them
    };

    constexpr int32_t OPTION_COUNT = 3;
    constexpr int32_t CHECK_DELAY = 3;        // turns from the choice to the check
    constexpr int32_t DIFFICULTY = 12;
    constexpr int32_t LEADER_BONUS = 2;
    constexpr int32_t MAX_ACTIVE = 16;
    constexpr int32_t MIN_TURN = 30;          // no crisis before this turn of the game
    constexpr int32_t PREFERRED_BONUS = 20;   // percent added to the preferred kind of check

    const char* Name(e_crisis crisis);
    const s_option* Options(e_crisis crisis);  // OPTION_COUNT options

    // The empire's modifier for a check of `kind` (its standing plus a leader's skill).
    int32_t Check_Bonus(int16_t player, e_check kind);
    // The option a computer empire takes: the best chance to succeed among those it can pay for,
    // its personality's and objective's preferred kind first by PREFERRED_BONUS points.
    int32_t Choose_Option(int16_t player, e_crisis crisis);

    // Opens `crisis` for `player` at `colony` (-1: the capital); the option is chosen at once for a
    // computer empire. False when it cannot open (flag off, a human, no colony, the table full).
    bool Open(int16_t player, e_crisis crisis, int16_t colony, int32_t delay = 0);
    // The check of an open crisis with the dice given (for the tests); the game rolls.
    e_result Resolve_With_Dice(int32_t slot, const int32_t* dice_values);

    // Once a turn after the new events (nextturn.cpp): due crises are resolved, follow-ups open,
    // and new crises may start; then the situations and the front, each under its own flag.
    void Turn();

    // ---- Wave 4, piece 7: situations and crises with a front (events-catalog.md 3.9, 3.10, 3.12) ----

    // The situation scale: a value 0..SCALE_MAX that moves every turn; every SCALE_STAGE points is a
    // stage and a stage reached from below has its effect; an "approach" is a standing policy with a
    // price paid every turn. War weariness uses it for each empire, the Antaran front for the galaxy.
    enum e_scale : uint8_t {
        SCALE_NONE = 0,
        SCALE_WAR_WEARINESS = 1,  // player's scale: it grows while the empire is at war
        SCALE_ANTARAN_FRONT = 2,  // the galaxy's scale (player -1): the Antarans' advance
        SCALE_COUNT = 3,
    };
    constexpr int32_t SCALE_MAX = 100;
    constexpr int32_t SCALE_STAGE = 25;
    constexpr int32_t MAX_SCALES = 16;
    struct s_scale {
        uint8_t kind = SCALE_NONE;
        int8_t player = -1;
        int16_t value = 0;
        int32_t started = 0;
        int8_t approach = -1;    // chosen approach, -1 before the choice
    };
    // Moves `scale` by `delta` within 0..SCALE_MAX; returns the new stage when a stage was reached from
    // below, else 0.
    int32_t Scale_Move(s_scale& scale, int32_t delta);

    // War weariness (3.9, "the scale of a situation"): +WEARY_WAR a turn in a war that presses, -WEARY_PEACE otherwise.
    // Iteration 1, decision 5 (iterations.md): "Решение автора: замедлить — рост +3 вместо +6 и только в долгой
    // войне или против сильного; сообщение о подходе — только при его смене." A war presses (the scale grows)
    // when it has lasted WEARY_LONG_TURNS turns without a break, or when an enemy at war with the empire has at
    // least WEARY_STRONGER_PERCENT per cent of the empire's own fleet strength against that enemy
    // (total_ship_strength). A shorter war against an equal or weaker enemy does not start the scale and lets
    // an existing one fall like peace. The approach is logged when it is first taken and when it changes.
    // Stage 2: the capital's output of the turn is lost; stage 3: a fine in BC; stage 4: a general strike
    // (STRIKE_TURNS turns without the capital's output; ab-review.md 3.5 softened it from the loss of a building)
    // and the scale falls to WEARY_RESET. Three approaches.
    constexpr int32_t WEARY_WAR = 3;
    constexpr int32_t WEARY_LONG_TURNS = 20;       // a "long" war
    constexpr int32_t WEARY_STRONGER_PERCENT = 120;  // an enemy "stronger": its strength against us / ours against it
    constexpr int32_t WEARY_PEACE = 4;
    constexpr int32_t WEARY_FINE = 100;
    constexpr int32_t WEARY_RESET = 60;
    constexpr int32_t APPROACH_COUNT = 3;
    struct s_approach {
        const char* name;
        int16_t cost_bc;        // paid every turn the approach stands (skipped when the empire cannot pay)
        int16_t cost_research;  // research points lost every turn
        int16_t relief;         // taken off the scale's growth every turn
    };
    const s_approach* Approaches();  // APPROACH_COUNT approaches: propaganda, martial law, carry on
    // The approach a computer takes for its scale of war weariness (a human takes 0).
    int32_t Choose_Approach(int16_t player);

    // Academy (3.9) - events-catalog.md row "��������": "������� ����������� �� ������ ��������� � ������� ��
    // ���������� ������"; the author's decision (ab-review.md 1.2): all empires compete, 3 stages of 10 turns
    // (growth of science, of industry, of BC); the winner of a stage gets a prize. The contest starts by chance
    // (ACADEMY_CHANCE per mille a turn from MIN_TURN, one at a time, at least two empires alive); no choice, so
    // humans take part. A stage's result is the growth of the empire's per-turn output of the stage's resource
    // (research_produced, industry_produced, bc_produced) from the start of the stage to its end; the winner is
    // the empire with the greatest positive growth (a tie for the top: no winner). Prizes: research points, the
    // capital's production points, BC.
    constexpr int32_t ACADEMY_CHANCE = 4;
    constexpr int32_t ACADEMY_STAGES = 3;
    constexpr int32_t ACADEMY_STAGE_TURNS = 10;
    constexpr int32_t ACADEMY_PRIZE = 150;
    enum e_academy_resource : uint8_t { ACADEMY_SCIENCE = 0, ACADEMY_INDUSTRY = 1, ACADEMY_BC = 2 };
    bool Start_Academy();                     // false: flag off, a contest runs, fewer than two empires alive
    int32_t Academy_Output(int16_t player, int32_t stage);  // the stage's resource, the empire's per-turn output

    // Iteration 1, decision 3 (iterations.md): "«Стать кризисом» — больше топлива: угрозу копят и «красная свадьба»,
    // яд на пиру, покупка у работорговцев, захват разума (не только биооружие и разрушение планет). Механику не
    // отключаем." Add_Threat is also called by reputation::On_Red_Wedding, by spy_ops for an exposed poison at a
    // feast (through reputation::On_Atrocity), by pirate_world when an empire buys a captive from the slavers and by
    // heroes when a mind control comes to light (the leader breaks free). Only a deed others can know counts.
    // Become a crisis (3.10) - row "����� ��������": "����� ����������� ������� ����������� (�������������
    // ������, ���������� ������) � ��������� ��������� ��� �����"; the author's decision (ab-review.md 1.1): a
    // threat scale fed by atrocities (reputation::On_Atrocity: biological weapons, a destroyed planet, poisoned
    // water); above the threshold every computer empire that knows the culprit declares war on it. Each atrocity
    // adds THREAT_ATROCITY (at most THREAT_MAX), the threat falls THREAT_DECAY a turn; at THREAT_WAR the galaxy
    // declares war (once for each empire, as long as the threat stays up). Any empire, a human too, can be the
    // culprit. Under events crisis_front.
    constexpr int32_t THREAT_ATROCITY = 25;
    constexpr int32_t THREAT_MAX = 100;
    constexpr int32_t THREAT_DECAY = 1;
    constexpr int32_t THREAT_WAR = 50;
    void Add_Threat(int16_t player);          // an atrocity of `player` (no-op with the flag off)
    int32_t Threat(int16_t player);

    // The strike of war weariness' stage 4 lasts STRIKE_TURNS turns: the capital's output is lost every turn.
    constexpr int32_t STRIKE_TURNS = 5;
    bool On_Strike(int16_t player);

    // The Antaran front (3.12): a galaxy scale that starts at front_strength, grows FRONT_STEP every
    // FRONT_PERIOD turns and feeds the Antarans' offensive resources. Needs the Antarans to exist.
    // Iteration 1, decision 1 (iterations.md): "Фронт Антаран стартует позже — на случайном ходу 180 ± 10 (автор:
    // «ход 190 сделай рандом, то есть 180 плюс минус 10 ходов»; беру 170–190); ресурсы фронта растут с его силой,
    // чтобы 10 и 20 были разными уровнями (часть той же идеи)." The start turn is drawn once per game (game_random,
    // only with the flag on and a front to come) in 180 - 10 .. 180 + 10 and kept in s_extra::front_start. The
    // resources grow with the strength: FRONT_START_RESOURCE per point of the strength at the start and
    // FRONT_STAGE_RESOURCE per point of front_strength per stage each time a stage is reached (strength 10: 150 a
    // stage, strength 20: 300; at the start 400 and 800).
    constexpr int32_t FRONT_STEP = 1;
    constexpr int32_t FRONT_PERIOD = 5;
    constexpr int32_t FRONT_START_TURN = 180;
    constexpr int32_t FRONT_START_SPREAD = 10;
    constexpr int32_t FRONT_START_RESOURCE = 40;
    constexpr int32_t FRONT_STAGE_RESOURCE = 15;
    constexpr int32_t RESOURCE_CAP = 30000;

    // The council's project (3.12): every COUNCIL_PERIOD turns each computer empire with COUNCIL_MIN_BC may
    // hand over trophies (BC): COUNCIL_GENEROUS for the honorable, pacifists and technologists, COUNCIL_SHARE
    // for most, nothing for the aggressive, ruthless and xenophobic. At COUNCIL_GOAL the project pushes the
    // front back by COUNCIL_PUSHBACK (and halves the Antarans' offensive resources); every contributor
    // gets research points worth COUNCIL_BONUS times what it gave. A human gives nothing (the safe choice).
    constexpr int32_t COUNCIL_PERIOD = 5;
    constexpr int32_t COUNCIL_MIN_BC = 200;
    constexpr int32_t COUNCIL_SHARE = 20;
    constexpr int32_t COUNCIL_GENEROUS = 40;
    constexpr int32_t COUNCIL_GOAL = 400;
    constexpr int32_t COUNCIL_PUSHBACK = 25;
    constexpr int32_t COUNCIL_BONUS = 2;
    // BC `player` gives at a handover (0: none), by character.
    int32_t Council_Gift(int16_t player);

    struct s_extra {
        s_scale scale[MAX_SCALES];
        uint16_t threat[MAX_PLAYERS] = {};        // the threat each empire's atrocities have built up
        uint8_t war_mask[MAX_PLAYERS] = {};       // bit o: empire o has declared war on this one for its threat
        int32_t strike_until[MAX_PLAYERS] = {};   // stardate the general strike of each empire ends, 0 = none
        uint8_t contest_stage = 0;                // the academy: 0 = none, else the running stage 1..ACADEMY_STAGES
        int32_t contest_ends = 0;                 // stardate the running stage ends
        int32_t baseline[MAX_PLAYERS] = {};       // each empire's output when the stage started
        uint8_t front_broken = 0;                 // the council has beaten the front for good
        int32_t council_progress = 0;
        uint16_t given[MAX_PLAYERS] = {};
        int32_t front_start = 0;                  // the turn (stardate - 35000) the front opens, 0 = not drawn yet
        int32_t war_since[MAX_PLAYERS] = {};      // stardate the empire's present war began, 0 = at peace
    };
    s_extra* Extra();
    const s_scale* Find_Scale(e_scale kind, int16_t player);  // nullptr when there is none
    bool Extra_Empty();

    struct s_active {
        uint8_t crisis = CRISIS_NONE;
        int8_t player = -1;
        int16_t colony = -1;
        int8_t option = -1;      // chosen option, -1 before the choice
        int32_t due = 0;         // stardate of the check (or of the opening, for a follow-up)
        uint8_t result = RESULT_PENDING;
        int8_t roll = 0;         // the dice total, for replays and the journal
    };
    s_active* Slots();          // MAX_ACTIVE slots

    void Clear();
    bool Empty();

    // Save section 'CRIS' (game/rules_sections.h): payload version 1, then MAX_ACTIVE slots of
    // crisis (uint8), player (int8), colony (int16), option (int8), due (int32), result (uint8),
    // roll (int8). Version 2 (written only when s_extra holds something; version 1 is still read) adds
    // MAX_SCALES scales (kind uint8, player int8, value int16, started int32, approach int8), the
    // academies (MAX_PLAYERS uint8), the fallen (MAX_PLAYERS int32), front_broken (uint8), the council's
    // progress (int32) and its gifts (MAX_PLAYERS uint16). Version 3 (written when s_extra holds something) drops the
    // academies and the fallen and, after the council's gifts, adds the threats (MAX_PLAYERS uint16), the war masks
    // (MAX_PLAYERS uint8), the strikes (MAX_PLAYERS int32), the contest's stage (uint8) and end (int32) and the
    // baselines (MAX_PLAYERS int32); versions 1 and 2 are still read (the old academies and fallen are dropped).
    // Version 4 (written when s_extra holds something) adds, after the baselines, the front's start turn (int32)
    // and the war starts (MAX_PLAYERS int32); versions 1 to 3 are still read.
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace crises
