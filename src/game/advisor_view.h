#pragma once

#include <cstdint>
#include <string>

#include "game/consts.h"
#include "game/world_view.h"

// An empire's view of the galaxy, gathered for a directive source and the turn journal, and
// formatted as a compact English summary for a language model.
//
// The view holds beliefs, not truth (docs/LocalLLM/mechanics.md 3.5.7): every fact about
// another empire carries its source, the stardate it was learned and a confidence. Stage 1
// fills facts only from honest sources (own records, diplomacy, own sensors); spy reports,
// rumours and disinformation come later through the same fields.
//
// Own data is complete. Other empires appear only after contact, their colonies only at stars
// this empire has visited, and their fleets only as this empire detects them; with our spies in
// an empire its colonies, fleet and technologies come from the spy report (FACT_SPY)
// (SHIPSTAK::Is_Ship_Detected_By, the rule of Remove_Non_Detected_Ships for any observer).
namespace advisor {

    inline constexpr int32_t VIEW_NAME_LENGTH = 21;

    enum e_fact_source : uint8_t {
        FACT_UNKNOWN = 0,
        FACT_OWN_RECORDS = 1,  // our own bookkeeping (losses, visited stars)
        FACT_DIPLOMACY = 2,    // what the diplomacy screen shows (relations, treaties)
        FACT_SENSORS = 3,      // seen this turn by our scanners
        FACT_BATTLE = 4,       // seen in combat (later)
        FACT_SPY = 5,          // spy report (later)
        FACT_RUMOR = 6,        // hearsay, possibly planted (later)
    };

    enum e_confidence : uint8_t {
        CONFIDENCE_NONE = 0,
        CONFIDENCE_LOW = 1,
        CONFIDENCE_MEDIUM = 2,
        CONFIDENCE_HIGH = 3,
    };

    struct s_fact {
        int32_t value = 0;
        uint8_t source = FACT_UNKNOWN;     // e_fact_source
        uint8_t confidence = CONFIDENCE_NONE;
        int32_t stardate = 0;              // when it was learned
    };

    struct s_view_self {
        uint8_t player = 0;
        char race_name[VIEW_NAME_LENGTH] = {};
        char leader_name[VIEW_NAME_LENGTH] = {};
        uint8_t objective = 0;
        uint8_t personality = 0;
        int32_t stardate = 0;
        int32_t treasury_bc = 0;
        int32_t income_bc = 0;            // per turn, may be negative
        int32_t industry = 0;
        int32_t research = 0;
        int32_t food_surplus = 0;         // may be negative (famine)
        int32_t population = 0;
        int32_t colonies = 0;
        int32_t ships = 0;
        int32_t fleet_strength = 0;
        int32_t combat_ships = 0;         // ships with a combat strength above zero
        int32_t command_points = 0;
        int32_t command_points_used = 0;
        int32_t stars_explored = 0;
        int32_t stars_total = 0;
        // Uncolonized regular planets at stars we have visited and within our extended ship
        // range: the targets the AI colonization code could send a colony ship to.
        int32_t free_planets_in_reach = 0;
        // Uncolonized asteroids and gas giants at visited stars within extended range: outpost sites.
        int32_t free_outpost_sites_in_reach = 0;
        int32_t outposts = 0;             // our colonies without population (included in `colonies`)
        int32_t techs_known = 0;          // researched technology applications
    };

    struct s_view_contact {
        uint8_t player = 0;
        char race_name[VIEW_NAME_LENGTH] = {};
        char leader_name[VIEW_NAME_LENGTH] = {};
        uint8_t personality = 0;          // shown on the diplomacy screen
        s_fact relations;                 // -100..100
        s_fact treaty;                    // s_player::treaty value
        s_fact trade_treaty;              // 0/1
        s_fact research_treaty;           // 0/1
        s_fact known_colonies;            // at stars we have visited
        s_fact visible_ships;             // detected this turn
        s_fact visible_fleet_strength;    // same measure as s_view_self::fleet_strength
        s_fact visible_combat_ships;      // detected ships with a combat strength above zero
        s_fact techs_known;               // researched applications; only from our spies there
        s_fact our_ship_losses;           // ships we lost to them
        s_fact their_ship_losses;         // ships they lost to us
        s_fact our_colony_losses;         // colonies we lost to them
        uint8_t our_spies = 0;            // our spies in their empire
        uint8_t at_war_with = 0;          // bit k: this empire is at war with player k (public)
    };

    struct s_empire_view {
        s_view_self self;
        int32_t contact_count = 0;
        s_view_contact contacts[MAX_PLAYERS];
    };

    // Fills `out` with what `player` knows. Returns false, leaving `out` cleared, when the
    // world is missing or the player is out of range or eliminated. Reads game state only.
    bool Build_Empire_View(const game::s_view* world, uint8_t player, s_empire_view* out);

    // Replaces `out` with a short English summary (about 20-40 lines) in the format the
    // benchmark used: facts tagged "[source, stardate, confidence]".
    void Format_Empire_Summary(const s_empire_view* view, std::string* out);

    const char* Objective_Name(uint8_t objective);
    const char* Personality_Name(uint8_t personality);
    const char* Treaty_Name(int32_t treaty);
    const char* Fact_Source_Name(uint8_t source);
    const char* Confidence_Name(uint8_t confidence);

} // namespace advisor
