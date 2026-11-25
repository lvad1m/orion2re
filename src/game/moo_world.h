#pragma once

#include "game/consts.h"

#include <cstdint>
#include <string>

struct s_colony;

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Events of the MOO2 world (docs/LocalLLM/events-catalog.md 4.2; README.md 10.9, wave 5): the pirate king, the
// epidemic in a fleet, the spy scandal, the famous exile, the scientist who defects, the council's demand, the
// Antaran scout and the refugees. The module's record table is general (kind, players, a star, a value,
// timers) so that the later events of the world can join it. A human-owned subject takes the first, safest option
// (no interface step yet); a computer picks by its personality (Choose). The shared pirate effects are reused
// (events.h: Pirates_At_Star_Turn, Pirate_Raid_Amount, Pirate_Raid_Loss).
// Off by default (events moo_world); with the flag off nothing here draws from game_random, nothing is kept.
namespace moo_world {

    extern bool enabled;  // events moo_world

    constexpr int32_t MAX_RECORDS = 16;

    enum e_kind : uint8_t {
        KIND_NONE = 0,
        KIND_PIRATE_KING = 1,     // record; target: the star, value: strength left, extra: strength it rose with
        KIND_FLEET_EPIDEMIC = 2,  // record; player: the empire whose fleet fell ill, target: the star it is grounded at
        KIND_ANTARAN_SCOUT = 3,   // record; player: the empire whose colony it was seen near, target: that colony's star
        KIND_REFUGEES = 4,        // record; player: the old owner, option: the aggressor, target: the colony's star,
                                  // value: the race of the refugees (a pop race id), ends: when they arrive
        KIND_LAST = 4,
    };

    // Pirate king: while at least two pirate presences (a lair of the pirate world, pirates hired in the world
    // events) have existed at once for UNITE_TURNS turns running, they unite: a king rises at the strongest one
    // (the lair's star), the strength is the sum, and the presences are gone. Every KING_PERIOD turns he raids every
    // empire with a colony within KING_REACH2 (squared map units: 6 parsecs of 45) of his star: the empire is
    // asked for tribute and answers by its personality (Choose): pays KING_TRIBUTE_PERCENT of the treasury (at
    // least KING_TRIBUTE_MIN) and is spared, haggles (half, accepted in KING_HAGGLE_PERCENT of the cases, else
    // raided) or refuses (raided at once). An empire too poor for the minimum is raided. A raid is the classic one
    // (Pirate_Raid_Amount) and KING_FREIGHTERS freighters lost. Warships at his star wear him down by their size
    // (Pirates_At_Star_Turn); when he is gone the empire with the most ship points there lands and gets
    // KING_LOOT + KING_LOOT_PER_STRENGTH * the strength he rose with. He disbands after KING_TURNS.
    constexpr int32_t UNITE_TURNS = 12;
    constexpr int32_t KING_PERIOD = 5;
    constexpr int32_t KING_REACH2 = 270 * 270;
    constexpr int32_t KING_TRIBUTE_PERCENT = 20;
    constexpr int32_t KING_TRIBUTE_MIN = 40;
    constexpr int32_t KING_HAGGLE_PERCENT = 50;
    constexpr int32_t KING_FREIGHTERS = 2;
    constexpr int32_t KING_LOOT = 100;
    constexpr int32_t KING_LOOT_PER_STRENGTH = 4;
    constexpr int32_t KING_TURNS = 60;

    // Epidemic in a fleet: a new one starts by chance, EPIDEMIC_CHANCE per mille a turn for each empire with a
    // fleet (at least FLEET_MIN_SHIPS ships in orbit at one star; the star with the most of its ships): no ship
    // can leave that star for EPIDEMIC_MIN_TURNS + 0..EPIDEMIC_SPAN_TURNS turns (2 to 5).
    constexpr int32_t EPIDEMIC_CHANCE = 3;
    constexpr int32_t FLEET_MIN_SHIPS = 3;
    constexpr int32_t EPIDEMIC_MIN_TURNS = 2;
    constexpr int32_t EPIDEMIC_SPAN_TURNS = 3;

    // Spy scandal: each ordered pair (owner, target) in which the owner keeps spies is exposed by chance,
    // SCANDAL_PER_SPY per mille a turn for each spy (a big network is easier to expose). All its spies there are
    // lost, the target remembers it (SCANDAL_OPINION, REASON_CAUGHT_SPYING) and every other empire in contact with
    // the owner hears bad rumours (SCANDAL_RUMOUR, REASON_BAD_RUMOURS).
    constexpr int32_t SCANDAL_PER_SPY = 2;
    constexpr int32_t SCANDAL_OPINION = -25;
    constexpr int32_t SCANDAL_RUMOUR = -8;

    // Famous exile: when an empire is eliminated, its best surviving leader (the highest level; in the pool, at a post
    // or in limbo) goes on the hire list and the computer that thinks best of the dead empire (opinion::Relations,
    // ties to the lowest number) hires it at once if it has room and can pay the classic cost. No draws.
    // Scientist defection: each turn, DEFECT_CHANCE per mille for every computer empire with an unstable colony
    // (stability <= DEFECT_STABILITY, or colony morale <= DEFECT_MORALE when stability is off) and a leader with a
    // science skill: the best such leader goes over to the neighbour (in contact, room for a leader) that the old
    // owner thinks best of, and brings the cheapest technology application the neighbour lacks. The old owner
    // remembers it (REASON_PLOTTED_AGAINST_US, DEFECT_OPINION) only if it keeps spies in the neighbour.
    constexpr int32_t DEFECT_CHANCE = 4;
    constexpr int32_t DEFECT_STABILITY = -3;
    constexpr int32_t DEFECT_MORALE = -5;
    constexpr int32_t DEFECT_OPINION = -15;

    // Design row (events-catalog.md 4.2): "Совет требует | совет требует прекратить применение биологического
    // оружия; отказ — −отношения со всеми | совет, biological_weapons_flag[]"; ab-review 3.4: "флаг биооружия
    // движок не ставит -> доделать: отмечать биооружие там, где его уже ловит reputation::On_Atrocity"
    // (On_Bio_Weapon below sets the flag).
    // The council's demand: an empire that has used biological weapons (a biological_weapons_flag of its set, or
    // the atrocity title of the reputation module) is told to stop, once for every time it starts using them
    // again. A computer agrees by its personality (a ruthless, aggressive or xenophobic one refuses); a human
    // agrees. A refusal costs it COUNCIL_REFUSE_OPINION (REASON_BAD_RUMOURS) with every computer that knows it,
    // an agreement earns COUNCIL_AGREE_OPINION (REASON_DID_US_A_SERVICE); using them again after agreeing costs
    // COUNCIL_BROKE_OPINION (REASON_BROKE_ITS_WORD) with every computer that knows it.
    constexpr int32_t COUNCIL_REFUSE_OPINION = -10;
    constexpr int32_t COUNCIL_AGREE_OPINION = 4;
    constexpr int32_t COUNCIL_BROKE_OPINION = -20;

    // Antaran scout: once the Antarans' attack pressure (MOX::antarans.attack_pressure) has reached SCOUT_PRESSURE
    // and the last scout was SCOUT_COOLDOWN turns ago, a scout is seen near a colony of a random empire (stays in
    // the records SCOUT_TURNS turns). The computer empires with a colony within SCOUT_REACH2 of that star that are
    // not at war with each other may sign a defence pact (pacts.h; the pair thinks well of each other, relations
    // >= 0 both ways); with the pacts off, each leans to the other by SCOUT_OPINION (REASON_DID_US_A_SERVICE).
    constexpr int32_t SCOUT_PRESSURE = 3;
    constexpr int32_t SCOUT_COOLDOWN = 40;
    constexpr int32_t SCOUT_TURNS = 10;
    constexpr int32_t SCOUT_REACH2 = KING_REACH2;
    constexpr int32_t SCOUT_OPINION = 5;

    // Design row (events-catalog.md 4.2): "Беженцы | после разгрома колонии беженцы прибывают к соседу; принять
    // (+население чужой расы, −отношения с агрессором) или отказать | выбор, население, отношения"; ab-review
    // 3.6: "только после захвата колонии, не после уничтожения -> доделать: хук в месте уничтожения планеты".
    // Refugees: after an invasion takes a colony (or a stellar converter destroys it) the refugees of its race reach, REFUGEE_DELAY turns later, the
    // nearest colony of a third empire with room for a population unit. A computer accepts them (honorable,
    // pacifist, erratic, dishonored) or refuses (xenophobic, ruthless, aggressive); a human accepts. Accepted: a
    // unit of the race joins the colony, the aggressor remembers it (REFUGEE_AGGRESSOR_OPINION, REASON_BAD_RUMOURS).
    // Refused: the old owner remembers it (REFUGEE_REFUSED_OPINION, REASON_ABANDONED_US).
    constexpr int32_t REFUGEE_DELAY = 3;
    constexpr int32_t REFUGEE_AGGRESSOR_OPINION = -10;
    constexpr int32_t REFUGEE_REFUSED_OPINION = -8;

    struct s_record {
        uint8_t kind = KIND_NONE;
        int8_t player = -1;
        int16_t target = -1;
        int16_t other = -1;
        int32_t started = 0;
        int32_t ends = 0;
        int8_t option = 0;
        int16_t value = 0;
        int16_t extra = 0;
    };

    struct s_state {
        int32_t count = 0;
        // Turns running with at least two pirate presences and no king.
        int16_t unite_turns = 0;
        // Bit p: the famous exile of empire p was dealt with; p was guilty of biological weapons last turn; p agreed
        // to the council's demand and has not been guilty since. Never set with the flag off.
        uint16_t exile_mask = 0;
        uint16_t guilty_mask = 0;
        uint16_t agreed_mask = 0;
        // The stardate of the last Antaran scout (0: none yet).
        int32_t last_scout = 0;
        s_record record[MAX_RECORDS];
    };
    s_state* State();

    // The option a computer of `player` takes when the king asks for tribute: 0 pay (honorable, pacifist and
    // every human), 2 refuse (ruthless, aggressive), 1 haggle (the others). The flag off: 0.
    int32_t Choose(int16_t player);

    // Starts the epidemic in the fleet of `player` at `star` for `turns` turns; false when it cannot start (the
    // flag off, the star already grounded, the table full).
    bool Start_Epidemic(int16_t player, int16_t star, int32_t turns);
    // Exposes the network of `owner` in `target`; false with the flag off or without spies there.
    bool Start_Scandal(int16_t owner, int16_t target);
    // Raises a pirate king at `star` with `strength`; false when the flag is off, one rules already or the table
    // is full.
    bool Raise_King(int16_t star, int16_t strength);

    // True while a flagged star holds an epidemic (shipmove.cpp: no ship leaves it). False with the flag off.
    bool Star_Grounded(int16_t star);
    // True while a pirate king lives and `player` has a colony within his reach.
    bool King_Near(int16_t player);

    // True when the table holds a scout of the Antarans near a colony of `player`.
    bool Scout_Near(int16_t player);

    // Deals with the leaders of the eliminated empire `dead` (the famous exile); false when the flag is off, the
    // empire is not eliminated, was dealt with already or had no leader.
    bool Start_Exile(int16_t dead);
    // The leader with a science skill of `owner` defects to a neighbour with a technology (no draw: the chance is
    // the caller's); false when no neighbour, leader or technology qualifies.
    bool Start_Defection(int16_t owner);
    // Raises the Antaran scout near a colony of `player` (at `star`) and lets its neighbours answer; false with the
    // flag off or the table full.
    bool Start_Scout(int16_t player, int16_t star);
    // The council's demand to `player` (it has used biological weapons): the answer by personality is recorded.
    // True with the flag on and a live empire (a repeat use after an agreement is remembered as a broken word).
    bool Council_Demand(int16_t player);
    // Whether a computer of `player` accepts refugees (0) or refuses (1); a human accepts; the flag off: 0.
    int32_t Refugee_Choice(int16_t player);
    // Hook (invasion.cpp Change_Colony_Ownership, before the pops change hands): refugees of the old owner's race
    // are on their way. Does nothing with the flag off.
    void On_Colony_Taken(const s_colony* colony, int16_t old_owner, int16_t new_owner);
    // Hook (combfind.cpp, next to reputation::On_Atrocity, before the planet is destroyed): the colony of its
    // owner is wiped out by `attacker`; its refugees flee as from a taken colony. Nothing with the flag off.
    void On_Colony_Destroyed(const s_colony* colony, int16_t attacker);
    // Hook (combat_lifecycle.cpp Apply_Bio_Weapon_Penalty, next to reputation::On_Atrocity): `attacker` used
    // biological weapons at `star`; the engine's flag biological_weapons_flag[victim] is set (the victim: the
    // owner of a colony at the star, else the attacker's own slot) so that the council's demand sees it.
    // Nothing with the flag off.
    void On_Bio_Weapon(int16_t attacker, int16_t star);

    // Once a turn after the other events (nextturn.cpp): the records run and end, the pirates unite, a new
    // event may start (at most one a turn).
    void Turn();
    // The first half of Turn(): the records go one turn on (no new event starts, nobody unites).
    void Run_Records();

    void Clear();
    bool Empty();

    // Save section 'MOOW' (game/rules_sections.h): payload version 2 (version 1 is still read: no masks, no scout
    // stardate), then the count (uint8), the unite turns (int16), the exile, guilty and agreed masks (uint16 each),
    // the stardate of the last scout (int32), per record kind (uint8), player (int8), target and other (int16), started and ends (int32),
    // option (int8), value and extra (int16).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace moo_world
