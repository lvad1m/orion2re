#pragma once

#include <cstdint>
#include <string>

#include "game/advisor_directive.h"
#include "game/advisor_view.h"

// Turn journal (docs/LocalLLM/README.md, decision of 20.11.2026): one JSON line per computer
// empire per turn, for self-play analysis and debugging. Written only when advisor::journal is
// not JOURNAL_OFF. File: <user state directory>/advisor_journal/<session>.jsonl (portable mode, e.g. Windows:
// <executable directory>/advisor_journal/). Writing never
// touches game state; a failed write disables the journal for the session with one warning.
namespace advisor {

    struct s_journal_entry {
        int32_t stardate = 0;
        uint8_t player = 0;
        const s_directive* directive = nullptr;  // may be null: no directive this turn
        bool accepted = false;                   // passed Validate_Directive
        std::string rejection;                   // reason when not accepted
        std::string rule_note;                   // rules source: which rule fired, on which numbers
        const s_empire_view* view = nullptr;     // written only in JOURNAL_FULL
    };

    // Builds one JSON object (no trailing newline) for `entry` in `mode`. JOURNAL_DECISIONS
    // writes stardate, player, source, accepted, rejection, rule_note and the directive fields;
    // JOURNAL_FULL adds "summary": Format_Empire_Summary of the view.
    void Format_Journal_Line(const s_journal_entry* entry, uint8_t mode, std::string* out);

    // Escapes a string for a JSON string literal (quotes not included).
    void Append_Json_Escaped(const std::string& text, std::string* out);

    // Appends one line to the session file; opens it on first use.
    void Write_Journal_Line(const std::string& line);

    // Closes the session file; the next write starts a new session file.
    void Close_Journal();

    // Overrides the directory the session file goes to (tests, tools). Empty = default
    // (<user state directory>/advisor_journal/). Takes effect for the next session file.
    void Set_Journal_Directory(const std::string& directory);

} // namespace advisor
