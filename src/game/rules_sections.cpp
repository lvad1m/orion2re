#include "pch.h"

#include "game/rules_sections.h"

#include "game/embargo.h"
#include "game/espionage.h"
#include "game/loyalty.h"
#include "game/opinion.h"
#include "game/pacts.h"
#include "game/resolutions.h"
#include "game/rivalry.h"
#include "game/spy_ops.h"
#include "game/spy_ranks.h"
#include "game/stability.h"

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

        bool Resolutions_Has_State_() {
            return resolutions::Active() && !resolutions::Empty();
        }

        bool Stability_Has_State_() {
            return stability::enabled && !stability::Empty();
        }

        bool Loyalty_Has_State_() {
            return loyalty::enabled && !loyalty::Empty();
        }

        bool Espionage_Has_State_() {
            return (espionage::network || espionage::counterintelligence) && !espionage::Empty();
        }

        bool Spy_Ops_Has_State_() {
            return spy_ops::operations != 0 && !spy_ops::Empty();
        }

        bool Spy_Ranks_Has_State_() {
            return (spy_ranks::counter_agents || spy_ranks::ranks) && !spy_ranks::Empty();
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
            {SECTION_TYPE_RESOLUTIONS, 1, "council resolutions", Resolutions_Has_State_, resolutions::Save_Size,
                resolutions::Write_Save, resolutions::Parse_Save, resolutions::Commit_Save, resolutions::Discard_Save,
                resolutions::Clear},
            {SECTION_TYPE_STABILITY, 1, "colony stability", Stability_Has_State_, stability::Save_Size, stability::Write_Save,
                stability::Parse_Save, stability::Commit_Save, stability::Discard_Save, stability::Clear},
            {SECTION_TYPE_LOYALTY, 1, "leader loyalty", Loyalty_Has_State_, loyalty::Save_Size, loyalty::Write_Save,
                loyalty::Parse_Save, loyalty::Commit_Save, loyalty::Discard_Save, loyalty::Clear},
            {SECTION_TYPE_ESPIONAGE, 1, "spy network", Espionage_Has_State_, espionage::Save_Size, espionage::Write_Save,
                espionage::Parse_Save, espionage::Commit_Save, espionage::Discard_Save, espionage::Clear},
            {SECTION_TYPE_SPY_OPS, 1, "spy operations", Spy_Ops_Has_State_, spy_ops::Save_Size, spy_ops::Write_Save,
                spy_ops::Parse_Save, spy_ops::Commit_Save, spy_ops::Discard_Save, spy_ops::Clear},
            {SECTION_TYPE_SPY_RANKS, 1, "counter-agents and spy ranks", Spy_Ranks_Has_State_, spy_ranks::Save_Size,
                spy_ranks::Write_Save, spy_ranks::Parse_Save, spy_ranks::Commit_Save, spy_ranks::Discard_Save, spy_ranks::Clear},
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
