#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "game/consts.h"
#include "game/world_view.h"

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// A directive: what one empire's advisor wants for the next turns. Format "v2" from the
// benchmark of 20.11.2026 (docs/LocalLLM/RTX_3050_ti/benchmark-2026-11-20.md, section 3.1):
// an attitude toward every empire instead of a free list of diplomatic actions, unambiguous
// military postures, and levels instead of 0..10 weights. Every field has a KEEP value
// meaning "no change"; the engine decides how a change turns into actions.
namespace advisor {

    enum e_source : uint8_t {
        SOURCE_NONE = 0,   // no directive
        SOURCE_RULES = 1,  // rule-based advisor (stage 2)
        SOURCE_LLM = 2,    // local language model (stub)
        SOURCE_DEBUG = 3,  // set by hand (debug tools, tests)
    };

    enum e_posture : uint8_t {
        POSTURE_KEEP = 0,
        POSTURE_MAKE_PEACE = 1,              // end the wars we are in
        POSTURE_DEFEND = 2,                  // protect what we have, start nothing
        POSTURE_ATTACK_CURRENT_ENEMIES = 3,  // offensive only in wars we already fight
        POSTURE_START_NEW_WAR = 4,           // open a new war (against main_rival)
        POSTURE_COUNT = 5,
    };

    enum e_attitude : uint8_t {
        ATTITUDE_KEEP = 0,
        ATTITUDE_ALLY = 1,
        ATTITUDE_FRIENDLY = 2,
        ATTITUDE_NEUTRAL = 3,
        ATTITUDE_WARY = 4,
        ATTITUDE_HOSTILE = 5,
        ATTITUDE_WAR = 6,
        ATTITUDE_COUNT = 7,
    };

    enum e_weight : uint8_t {
        WEIGHT_FLEET = 0,
        WEIGHT_DEFENSE = 1,
        WEIGHT_COLONIZATION = 2,
        WEIGHT_ECONOMY = 3,
        WEIGHT_FOOD = 4,
        WEIGHT_RESEARCH = 5,
        WEIGHT_COUNT = 6,
    };

    enum e_level : uint8_t {
        LEVEL_KEEP = 0,
        LEVEL_LOW = 1,
        LEVEL_NORMAL = 2,
        LEVEL_HIGH = 3,
        LEVEL_MAX = 4,
        LEVEL_COUNT = 5,
    };

    enum e_research_focus : uint8_t {
        RESEARCH_KEEP = 0,
        RESEARCH_BALANCED = 1,
        RESEARCH_WEAPONS = 2,
        RESEARCH_SHIELDS_ARMOR = 3,
        RESEARCH_COMPUTERS_ECM = 4,
        RESEARCH_PROPULSION = 5,
        RESEARCH_PLANETOLOGY = 6,
        RESEARCH_ECONOMY = 7,
        RESEARCH_BIOLOGY = 8,
        RESEARCH_COUNT = 9,
    };

    enum e_fleet_doctrine : uint8_t {
        DOCTRINE_KEEP = 0,
        DOCTRINE_BALANCED = 1,
        DOCTRINE_ANTI_MISSILE = 2,
        DOCTRINE_ANTI_BEAM = 3,
        DOCTRINE_ANTI_FIGHTER = 4,
        DOCTRINE_ANTI_BOARDING = 5,
        DOCTRINE_COUNT = 6,
    };

    enum e_spy_mission : uint8_t {
        SPY_KEEP = 0,
        SPY_NONE = 1,
        SPY_GATHER_INTEL = 2,
        SPY_STEAL_TECH = 3,
        SPY_SABOTAGE = 4,
        SPY_COUNTERINTEL = 5,
        SPY_COUNT = 6,
    };

    inline constexpr uint8_t OBJECTIVE_KEEP = 0xFF;  // otherwise a PLAYER_OBJECTIVE_* value 0..5
    inline constexpr uint8_t NO_PLAYER = 0xFF;

    struct s_directive {
        uint8_t source = SOURCE_NONE;     // e_source; SOURCE_NONE means "no directive"
        int32_t issued_stardate = 0;
        int32_t expires_stardate = 0;     // ignored after this stardate
        uint8_t objective = OBJECTIVE_KEEP;
        uint8_t posture = POSTURE_KEEP;
        uint8_t attitude[MAX_PLAYERS] = {};  // e_attitude, indexed by player number
        uint8_t main_rival = NO_PLAYER;
        uint8_t weights[WEIGHT_COUNT] = {};  // e_level
        uint8_t research_focus = RESEARCH_KEEP;
        uint8_t fleet_doctrine = DOCTRINE_KEEP;
        uint8_t spy_target = NO_PLAYER;
        uint8_t spy_mission = SPY_KEEP;
        uint16_t rule_id = 0;             // rules source: which rule produced it; 0 = none
    };

    void Clear_Directive(s_directive* directive);
    bool Is_Active(const s_directive* directive, int32_t stardate);

    // Rules every directive must pass before it is stored (benchmark, section 3.2). A failing
    // directive is rejected whole and the empire plays by its original AI. Checks:
    // - every enum field is in range, player indices are < MAX_PLAYERS or NO_PLAYER;
    // - main_rival and spy_target are other, living players this player has contact with;
    // - attitude WAR or HOSTILE toward a player with an alliance treaty is rejected;
    // - attitude other than KEEP toward a player without contact is rejected;
    // - POSTURE_START_NEW_WAR needs a main_rival that is not an ally;
    // - objective is OBJECTIVE_KEEP or 0..5; the human objective (100) is never set.
    // `reason` receives a short English explanation on failure.
    bool Validate_Directive(const game::s_view* world,
        uint8_t player,
        const s_directive* directive,
        std::string* reason);

    // Per-player store, indexed by player number.
    const s_directive* Get_Directive(uint8_t player);
    void Set_Directive(uint8_t player, const s_directive* directive);
    void Clear_All_Directives();

    // Stardate of the last objective change made by the advisor for `player` (0 = never).
    // Part of the store: cleared with it and saved with it.
    int32_t Last_Objective_Change(uint8_t player);
    void Set_Last_Objective_Change(uint8_t player, int32_t stardate);

    // Optional save section SAVEGAME::SECTION_TYPE_ADVISOR. The payload starts with its own
    // uint16 payload version. Read_Save parses into a temporary store and commits only on
    // success; on failure the store is unchanged and `error` says why.
    // Version 2 appends int32 Last_Objective_Change for each player; version 1 payloads are
    // still read (last changes = 0).
    inline constexpr uint16_t SAVE_PAYLOAD_VERSION = 2;
    size_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Read_Save(SERIAL::Reader_* reader, std::string* error);

} // namespace advisor
