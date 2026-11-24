#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Optional save sections of the extended rules (README.md 10.9): one table entry per mechanic
// instead of hand-written code in savegame.cpp for each. A section is written only while its
// mechanic holds state, so saves without the extended rules stay byte-identical; an older build
// skips the unknown optional section, and a section with another version is skipped too.
// Loading parses every section into the mechanic's pending copy and commits all of them only
// after the whole save has loaded; a failed load discards them.
namespace SAVEGAME {

    struct s_rules_section {
        uint32_t type;      // four characters, first character in the lowest byte
        uint16_t version;   // section format version
        const char* name;   // for error messages
        bool (*has_state)();
        uint32_t (*size)();
        void (*write)(SERIAL::Writer_* writer);
        bool (*parse)(SERIAL::Reader_* reader, std::string* error);
        void (*commit)();
        void (*discard)();
        void (*clear)();    // at the start of every load: a save without the section leaves nothing
    };

    // 'OPIN': opinion modifiers (game/opinion.h).
    inline constexpr uint32_t SECTION_TYPE_OPINION = 0x4E49504F;
    // 'RIVL': declared rivals (game/rivalry.h).
    inline constexpr uint32_t SECTION_TYPE_RIVALRY = 0x4C564952;
    // 'PACT': defence pacts and guarantees of independence (game/pacts.h).
    inline constexpr uint32_t SECTION_TYPE_PACTS = 0x54434150;
    // 'EMBG': embargoes (game/embargo.h).
    inline constexpr uint32_t SECTION_TYPE_EMBARGO = 0x47424D45;
    // 'RESO': council resolutions - bans and the Antaran defence fund (game/resolutions.h).
    inline constexpr uint32_t SECTION_TYPE_RESOLUTIONS = 0x4F534552;
    // 'STAB': colony stability (game/stability.h).
    inline constexpr uint32_t SECTION_TYPE_STABILITY = 0x42415453;
    // 'LOYL': leader loyalty (game/loyalty.h).
    inline constexpr uint32_t SECTION_TYPE_LOYALTY = 0x4C594F4C;
    // 'SPYN': the spy network core - network points, counterintelligence (game/espionage.h).
    inline constexpr uint32_t SECTION_TYPE_ESPIONAGE = 0x4E595053;
    // 'SPYO': spy operations - unrest, bribes, immunity (game/spy_ops.h).
    inline constexpr uint32_t SECTION_TYPE_SPY_OPS = 0x4F595053;
    // 'SPYR': counter-agents, saboteurs and residents (game/spy_ranks.h).
    inline constexpr uint32_t SECTION_TYPE_SPY_RANKS = 0x52595053;
    // 'INTR': intrigues - favours, revealed plots, double agents (game/intrigue.h).
    inline constexpr uint32_t SECTION_TYPE_INTRIGUE = 0x52544E49;
    // 'BELF': the neighbour model's evidence and disinformation (game/beliefs.h).
    inline constexpr uint32_t SECTION_TYPE_BELIEFS = 0x464C4542;
    // 'EVTS': the new events - modifiers, situations (game/world_events.h).
    inline constexpr uint32_t SECTION_TYPE_WORLD_EVENTS = 0x53545645;
    // 'CRIS': open crises and their follow-ups (game/crises.h).
    inline constexpr uint32_t SECTION_TYPE_CRISES = 0x53495243;
    // 'REPU': stains and titles (game/reputation.h).
    inline constexpr uint32_t SECTION_TYPE_REPUTATION = 0x55504552;
    // 'VASL': vassals and overlords (game/vassals.h).
    inline constexpr uint32_t SECTION_TYPE_VASSALS = 0x4C534156;
    // 'SECR': secrets known (game/secrets.h).
    inline constexpr uint32_t SECTION_TYPE_SECRETS = 0x52434553;
    // 'CSPY': counter-intelligence (game/counterspy.h).
    inline constexpr uint32_t SECTION_TYPE_COUNTERSPY = 0x59505343;
    // 'CVNT': secret clauses and marriages (game/covenants.h).
    inline constexpr uint32_t SECTION_TYPE_COVENANTS = 0x544E5643;
    // 'HOST': hostages and captured leaders (game/hostages.h).
    inline constexpr uint32_t SECTION_TYPE_HOSTAGES = 0x54534F48;

    // The registered sections, in the order they are written.
    const s_rules_section* Rules_Sections(uint32_t* count);

    // The registered section of `type`, or nullptr.
    const s_rules_section* Find_Rules_Section(uint32_t type);

    // Forgets the state of every registered mechanic (a new game).
    void Clear_Rules_State();

} // namespace SAVEGAME
