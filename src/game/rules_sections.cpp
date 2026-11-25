#include "pch.h"

#include "game/rules_sections.h"

#include "game/beliefs.h"
#include "game/crises.h"
#include "game/embargo.h"
#include "game/espionage.h"
#include "game/intrigue.h"
#include "game/loyalty.h"
#include "game/opinion.h"
#include "game/pacts.h"
#include "game/reputation.h"
#include "game/resolutions.h"
#include "game/rivalry.h"
#include "game/counterspy.h"
#include "game/covenants.h"
#include "game/hostages.h"
#include "game/news.h"
#include "game/projects.h"
#include "game/wanderers.h"
#include "game/incidents.h"
#include "game/overtures.h"
#include "game/pirate_world.h"
#include "game/beasts.h"
#include "game/discoveries.h"
#include "game/heroes.h"
#include "game/moo_world.h"
#include "game/secrets.h"
#include "game/spy_ops.h"
#include "game/spy_ranks.h"
#include "game/stability.h"
#include "game/vassals.h"
#include "game/world_events.h"

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

        bool Intrigue_Has_State_() {
            return intrigue::Any_Enabled() && !intrigue::Empty();
        }

        bool Beliefs_Has_State_() {
            return (beliefs::neighbour_model || beliefs::disinformation) && !beliefs::Empty();
        }

        bool World_Events_Has_State_() {
            return (world_events::enabled || world_events::hired_pirates) && !world_events::Empty();
        }

        bool Crises_Has_State_() {
            return (crises::enabled || crises::situations || crises::crisis_front) && !crises::Empty();
        }

        bool Reputation_Has_State_() {
            return reputation::enabled && !reputation::Empty();
        }

        bool Vassals_Has_State_() {
            return vassals::enabled && !vassals::Empty();
        }

        bool Secrets_Has_State_() {
            return secrets::enabled && !secrets::Empty();
        }

        bool Counterspy_Has_State_() {
            return (counterspy::mole_hunt || counterspy::canary_trap || counterspy::interrogation || counterspy::guards) &&
                !counterspy::Empty();
        }

        bool Covenants_Has_State_() {
            return (covenants::secret_clauses || covenants::marriages) && !covenants::Empty();
        }

        bool Hostages_Has_State_() {
            return (hostages::hostage_swaps || hostages::prisoners) && !hostages::Empty();
        }

        bool News_Has_State_() {
            return (news::enabled || news::intelligence) && !news::Empty();
        }

        bool Projects_Has_State_() {
            return projects::enabled && !projects::Empty();
        }

        bool Wanderers_Has_State_() {
            return wanderers::enabled && !wanderers::Empty();
        }

        bool Incidents_Has_State_() {
            return incidents::enabled && !incidents::Empty();
        }

        bool Overtures_Has_State_() {
            return overtures::enabled && !overtures::Empty();
        }

        bool Pirate_World_Has_State_() {
            return pirate_world::enabled && !pirate_world::Empty();
        }

        bool Beasts_Has_State_() {
            return beasts::enabled && !beasts::Empty();
        }

        bool Discoveries_Has_State_() {
            return discoveries::enabled && !discoveries::Empty();
        }

        bool Heroes_Has_State_() {
            return heroes::enabled && !heroes::Empty();
        }

        bool Moo_World_Has_State_() {
            return moo_world::enabled && !moo_world::Empty();
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
            {SECTION_TYPE_INTRIGUE, 1, "intrigues", Intrigue_Has_State_, intrigue::Save_Size, intrigue::Write_Save,
                intrigue::Parse_Save, intrigue::Commit_Save, intrigue::Discard_Save, intrigue::Clear},
            {SECTION_TYPE_BELIEFS, 1, "beliefs", Beliefs_Has_State_, beliefs::Save_Size, beliefs::Write_Save,
                beliefs::Parse_Save, beliefs::Commit_Save, beliefs::Discard_Save, beliefs::Clear},
            {SECTION_TYPE_WORLD_EVENTS, 1, "new events", World_Events_Has_State_, world_events::Save_Size,
                world_events::Write_Save, world_events::Parse_Save, world_events::Commit_Save, world_events::Discard_Save,
                world_events::Clear},
            {SECTION_TYPE_CRISES, 1, "crises", Crises_Has_State_, crises::Save_Size, crises::Write_Save,
                crises::Parse_Save, crises::Commit_Save, crises::Discard_Save, crises::Clear},
            {SECTION_TYPE_REPUTATION, 1, "stains and titles", Reputation_Has_State_, reputation::Save_Size,
                reputation::Write_Save, reputation::Parse_Save, reputation::Commit_Save, reputation::Discard_Save,
                reputation::Clear},
            {SECTION_TYPE_VASSALS, 1, "vassals", Vassals_Has_State_, vassals::Save_Size, vassals::Write_Save,
                vassals::Parse_Save, vassals::Commit_Save, vassals::Discard_Save, vassals::Clear},
            {SECTION_TYPE_SECRETS, 1, "secrets", Secrets_Has_State_, secrets::Save_Size, secrets::Write_Save,
                secrets::Parse_Save, secrets::Commit_Save, secrets::Discard_Save, secrets::Clear},
            {SECTION_TYPE_COUNTERSPY, 1, "counterspy", Counterspy_Has_State_, counterspy::Save_Size, counterspy::Write_Save,
                counterspy::Parse_Save, counterspy::Commit_Save, counterspy::Discard_Save, counterspy::Clear},
            {SECTION_TYPE_COVENANTS, 1, "covenants", Covenants_Has_State_, covenants::Save_Size, covenants::Write_Save,
                covenants::Parse_Save, covenants::Commit_Save, covenants::Discard_Save, covenants::Clear},
            {SECTION_TYPE_HOSTAGES, 1, "hostages", Hostages_Has_State_, hostages::Save_Size, hostages::Write_Save,
                hostages::Parse_Save, hostages::Commit_Save, hostages::Discard_Save, hostages::Clear},
            {SECTION_TYPE_NEWS, 1, "news", News_Has_State_, news::Save_Size, news::Write_Save, news::Parse_Save,
                news::Commit_Save, news::Discard_Save, news::Clear},
            {SECTION_TYPE_PROJECTS, 1, "projects", Projects_Has_State_, projects::Save_Size, projects::Write_Save,
                projects::Parse_Save, projects::Commit_Save, projects::Discard_Save, projects::Clear},
            {SECTION_TYPE_WANDERERS, 1, "wanderers", Wanderers_Has_State_, wanderers::Save_Size, wanderers::Write_Save,
                wanderers::Parse_Save, wanderers::Commit_Save, wanderers::Discard_Save, wanderers::Clear},
            {SECTION_TYPE_INCIDENTS, 1, "incidents", Incidents_Has_State_, incidents::Save_Size, incidents::Write_Save,
                incidents::Parse_Save, incidents::Commit_Save, incidents::Discard_Save, incidents::Clear},
            {SECTION_TYPE_OVERTURES, 1, "overtures", Overtures_Has_State_, overtures::Save_Size, overtures::Write_Save,
                overtures::Parse_Save, overtures::Commit_Save, overtures::Discard_Save, overtures::Clear},
            {SECTION_TYPE_PIRATE_WORLD, 1, "pirate world", Pirate_World_Has_State_, pirate_world::Save_Size, pirate_world::Write_Save,
                pirate_world::Parse_Save, pirate_world::Commit_Save, pirate_world::Discard_Save, pirate_world::Clear},
            {SECTION_TYPE_BEASTS, 1, "beasts", Beasts_Has_State_, beasts::Save_Size, beasts::Write_Save,
                beasts::Parse_Save, beasts::Commit_Save, beasts::Discard_Save, beasts::Clear},
            {SECTION_TYPE_DISCOVERIES, 1, "discoveries", Discoveries_Has_State_, discoveries::Save_Size, discoveries::Write_Save,
                discoveries::Parse_Save, discoveries::Commit_Save, discoveries::Discard_Save, discoveries::Clear},
            {SECTION_TYPE_HEROES, 1, "heroes", Heroes_Has_State_, heroes::Save_Size, heroes::Write_Save,
                heroes::Parse_Save, heroes::Commit_Save, heroes::Discard_Save, heroes::Clear},
            {SECTION_TYPE_MOO_WORLD, 1, "moo world", Moo_World_Has_State_, moo_world::Save_Size, moo_world::Write_Save,
                moo_world::Parse_Save, moo_world::Commit_Save, moo_world::Discard_Save, moo_world::Clear},
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
