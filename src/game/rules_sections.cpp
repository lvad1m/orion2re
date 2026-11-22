#include "pch.h"

#include "game/rules_sections.h"

#include "game/opinion.h"

namespace SAVEGAME {

    namespace {
        bool Opinion_Has_State_() {
            return opinion::enabled && !opinion::Empty();
        }

        const s_rules_section sections[] = {
            {SECTION_TYPE_OPINION, 1, "opinion modifiers", Opinion_Has_State_, opinion::Save_Size, opinion::Write_Save,
                opinion::Parse_Save, opinion::Commit_Save, opinion::Discard_Save, opinion::Clear},
        };
    } // namespace

    const s_rules_section* Rules_Sections(uint32_t* count) {
        if (count != nullptr) *count = static_cast<uint32_t>(sizeof(sections) / sizeof(sections[0]));
        return sections;
    }

    const s_rules_section* Find_Rules_Section(uint32_t type) {
        for (const s_rules_section& section : sections) {
            if (section.type == type) return &section;
        }
        return nullptr;
    }

    void Clear_Rules_State() {
        for (const s_rules_section& section : sections) section.clear();
    }

} // namespace SAVEGAME
