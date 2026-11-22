#pragma once

#include <cstdint>
#include <string>

struct s_colony;

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Counter-agents and spy ranks (docs/LocalLLM/mechanics.md 3.2.7, 3.2.8 and 3.2.5.4; README.md 10.9,
// tier 3C): three new colony products - a counter-agent guards its colony, a saboteur makes one
// attempt at the enemy and is gone, a resident settles at a rival and lifts our operations there.
// Two flags: spy counter_agents and spy ranks, both off by default.
namespace spy_ranks {

    extern bool counter_agents;      // spy counter_agents
    extern bool ranks;               // spy ranks
    extern int32_t counter_agent_cost;  // spy counter_agent_cost (production points)
    extern int32_t saboteur_cost;       // spy saboteur_cost
    extern int32_t resident_cost;       // spy resident_cost

    // The new colony products (beyond the original COLONY_PRODUCTION_ID values).
    constexpr int16_t PRODUCT_COUNTER_AGENT = -18;
    constexpr int16_t PRODUCT_SABOTEUR = -19;
    constexpr int16_t PRODUCT_RESIDENT = -20;
    bool Is_Product(int16_t product);

    constexpr int32_t MAX_PER_COLONY = 3;
    constexpr int32_t FOIL_PERCENT = 15;          // per counter-agent
    constexpr int32_t FOIL_MAX_PERCENT = 45;
    constexpr int32_t CATCH_FOILED_PERCENT = 50;
    constexpr int32_t UNCOVER_BRIBE_PER_MILLE = 50;  // per counter-agent at a bribed governor's colony
    constexpr int32_t SABOTEUR_PERCENT = 20;
    constexpr int32_t SABOTEUR_ATTEMPTS = 3;      // per target and turn
    constexpr int32_t SABOTEUR_OPINION = -30;
    constexpr int32_t RESIDENT_BONUS = 3;
    constexpr int32_t AI_PERIOD = 10;

    // The products: whether the colony may build it, its cost, its name and description (English
    // until the interface step), and what happens when it is done (false: it could not be placed,
    // half the cost comes back as with a spy).
    bool Can_Build(const s_colony* colony, int16_t product);
    int32_t Cost(int16_t product);
    const char* Name(int16_t product);
    const char* Description(int16_t product);
    bool On_Built(int16_t colony, int16_t product);

    int32_t Counter_Agents(int16_t colony);
    int32_t Residents(int16_t owner, int16_t target);
    // Half a defender against theft for each counter-agent in the capitol (spy.cpp).
    int32_t Capitol_Defenders(int16_t player);
    // BC a turn: one for two counter-agents, two per resident.
    int32_t Upkeep(int16_t player);
    // A resident at the target: +3 to operations there (spy_ops.cpp).
    int32_t Resident_Bonus(int16_t owner, int16_t target);
    // Counter-agents at the colony of a bribed governor raise the uncovering (spy_ops.cpp).
    int32_t Uncover_Bonus(int16_t colony);

    // A sabotage by `actor` picked colony `colony` (spy.cpp Destroy_Random_Building): the
    // counter-agents there may foil it and then catch the spy. True: foiled.
    bool Foil_Sabotage(int16_t actor, int16_t colony);
    // A building at `colony` was destroyed by sabotage (for the computers' placement).
    void Note_Sabotage(int16_t colony);

    // Once a turn (spy.cpp, after the operations): saboteurs make their attempts, and every
    // AI_PERIOD turns the computers buy counter-agents, saboteurs and residents with BC.
    void Turn();

    void Clear();
    bool Empty();

    // Save section 'SPYR' (game/rules_sections.h): payload version 1, then counter-agents per colony
    // (uint8 x MAX_COLONIES), saboteurs waiting per player (uint8 x MAX_PLAYERS), residents per pair
    // (uint8 x MAX_PLAYERS x MAX_PLAYERS) and the last sabotaged colony per player (int16 x MAX_PLAYERS).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

    struct s_state {
        uint8_t counter_agents[MAX_COLONIES] = {};
        uint8_t saboteurs[MAX_PLAYERS] = {};
        uint8_t residents[MAX_PLAYERS][MAX_PLAYERS] = {};
        int16_t last_sabotaged[MAX_PLAYERS];
        s_state() {
            for (int16_t& c : last_sabotaged) c = -1;
        }
    };
    s_state* State();

} // namespace spy_ranks
