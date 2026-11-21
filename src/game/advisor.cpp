#include "pch.h"

#include "game/advisor.h"

#include "game/advisor_apply.h"
#include "game/advisor_directive.h"
#include "game/advisor_intel.h"
#include "game/advisor_journal.h"
#include "game/advisor_reports.h"
#include "game/advisor_sources.h"
#include "game/advisor_view.h"

namespace advisor {

    // The configuration globals live in advisor_config.cpp, so the config bindings link
    // without pulling in the advisor itself.

    namespace {
        // Rules first, then the language model; the first source with a directive wins.
        bool Ask_Sources_(const s_empire_view* view, s_directive* out, std::string* note) {
            if (Rules_Directive(view, out, note)) return true;
            return Llm_Directive(view, out, note);
        }
    } // namespace

    void Run_Turn(const game::s_view* world) {
        if (!enabled || world == nullptr || world->players == nullptr) return;
        const int32_t stardate = world->Stardate();
        Fade_Seen_Ships(stardate);
        Check_Military_Reports(world);
        int32_t player_count = world->Player_Count();
        if (player_count > MAX_PLAYERS) player_count = MAX_PLAYERS;

        for (int32_t i = 0; i < player_count; i++) {
            const s_player* player = &world->players[i];
            if (player->eliminated != 0 || player->objectives == PLAYER_OBJECTIVE_HUMAN) continue;
            const uint8_t index = (uint8_t)i;

            s_empire_view view;
            if (!Build_Empire_View(world, index, &view)) continue;

            s_journal_entry entry;
            entry.stardate = stardate;
            entry.player = index;
            entry.view = &view;

            s_directive fresh;
            const s_directive* stored = Get_Directive(index);
            if (((active_players >> i) & 1) == 0) {
                // Not steered (self-play bench): play the original AI, keep the journal.
                if (stored != nullptr && stored->source != SOURCE_NONE) {
                    Clear_Directive(&fresh);
                    Set_Directive(index, &fresh);
                }
            } else if (stored != nullptr && Is_Active(stored, stardate)) {
                entry.directive = stored;
                entry.accepted = true;
                entry.rule_note = "directive still active";
            } else if (Ask_Sources_(&view, &fresh, &entry.rule_note)) {
                entry.directive = &fresh;
                std::string reason;
                if (Validate_Directive(world, index, &fresh, &reason)) {
                    Set_Directive(index, &fresh);
                    entry.accepted = true;
                } else {
                    entry.rejection = reason;
                }
            }

            if (journal != JOURNAL_OFF) {
                std::string line;
                Format_Journal_Line(&entry, journal, &line);
                Write_Journal_Line(line);
            }
        }

        Apply_Directives(world);
    }

    void Reset() {
        Clear_All_Directives();
        Clear_Seen_Ships();
        Close_Journal();
    }

} // namespace advisor
