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

    // The registered sections, in the order they are written.
    const s_rules_section* Rules_Sections(uint32_t* count);

    // The registered section of `type`, or nullptr.
    const s_rules_section* Find_Rules_Section(uint32_t type);

    // Forgets the state of every registered mechanic (a new game).
    void Clear_Rules_State();

} // namespace SAVEGAME
