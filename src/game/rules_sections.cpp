#include "pch.h"

#include "game/rules_sections.h"

#include "game/embargo.h"
#include "game/opinion.h"
#include "game/pacts.h"
#include "game/rivalry.h"

namespace SAVEGAME {

    namespace {
        bool Opinion_Has_State_() {
            return opinion::enabled && !opinion::Empty();
        }

        bool Rivalry_Has_State_() {
            return rivalry::enabled && !rivalry::Empty();
        }

        bool Pacts_Has_State_() {
            return pacts::enabled && !pacts::Empty();
        }

        bool Embargo_Has_State_() {
            return embargo::enabled && !embargo::Empty();
        }

        const s_rules_section sections[] = {
            {SECTION_TYPE_OPINION, 1, "opinion modifiers", Opinion_Has_State_, opinion::Save_Size, opinion::Write_Save,
                opinion::Parse_Save, opinion::Commit_Save, opinion::Discard_Save, opinion::Clear},
            {SECTION_TYPE_RIVALRY, 1, "rivalries", Rivalry_Has_State_, rivalry::Save_Size, rivalry::Write_Save,
                rivalry::Parse_Save, rivalry::Commit_Save, rivalry::Discard_Save, rivalry::Clear},
            {SECTION_TYPE_PACTS, 1, "defence pacts", Pacts_Has_State_, pacts::Save_Size, pacts::Write_Save,
                pacts::Parse_Save, pacts::Commit_Save, pacts::Discard_Save, pacts::Clear},
            {SECTION_TYPE_EMBARGO, 1, "embargoes", Embargo_Has_State_, embargo::Save_Size, embargo::Write_Save,
                embargo::Parse_Save, embargo::Commit_Save, embargo::Discard_Save, embargo::Clear},
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
