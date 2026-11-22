#include "pch.h"

#include "game/advisor_view.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "game/advisor_intel.h"
#include "game/advisor_reports.h"
#include "game/aipower.h"
#include "game/harold.h"
#include "game/legacy_ids.h"
#include "game/loyalty.h"
#include "game/opinion.h"
#include "game/embargo.h"
#include "game/espionage.h"
#include "game/pacts.h"
#include "game/rivalry.h"
#include "game/spy_ranks.h"
#include "game/stability.h"
#include "game/shipstak.h"
#include "game/struct.h"

namespace advisor {

    namespace {
        // Ship status below this value is a live ship (colcalc Update_Player_Counts, aibuild).
        constexpr int32_t SHIP_STATUS_FIRST_NOT_ALIVE = 5;

        void Copy_Name(char* dest, const char* source, size_t source_size) {
            size_t length = 0;
            while (length < source_size && source[length] != '\0') {
                ++length;
            }
            if (length > static_cast<size_t>(VIEW_NAME_LENGTH - 1)) {
                length = VIEW_NAME_LENGTH - 1;
            }
            std::memcpy(dest, source, length);
            dest[length] = '\0';
        }

        void Set_Fact(s_fact* fact, int32_t value, e_fact_source source, e_confidence confidence, int32_t stardate) {
            fact->value = value;
            fact->source = source;
            fact->confidence = confidence;
            fact->stardate = stardate;
        }

        bool Ship_Is_Alive(const s_ship_data* ship) {
            return ship->owner >= 0 && ship->owner < MAX_PLAYERS && ship->status < SHIP_STATUS_FIRST_NOT_ALIVE;
        }

        // The measure the computer empires use for fleet power (aidata.cpp, strengths[]):
        // AIPOWER::Ship_Strength_Vs_Ships, combat ships only, taken against the observer's best
        // shields so own and visible enemy ships are measured on one scale.
        // In strategic combat that function refreshes the ship's design to the owner's best tech
        // (INITSHIP::Update_Strategic_Ship), the same idempotent refresh the AI performs each turn.
        int32_t Combat_Ship_Strength(const game::s_view* world, int32_t ship_index, uint8_t observer) {
            if (world->settings == nullptr || world->ships[ship_index].d.ship_type != SHIP_TYPE_COMBAT) {
                return 0;
            }
            return AIPOWER::Ship_Strength_Vs_Ships(
                world, legacy_world::Ship_Id(static_cast<uint32_t>(ship_index)), legacy_world::Player_Id(observer));
        }

        bool Star_Visited_By(const game::s_view* world, int32_t star_index, uint8_t player) {
            if (star_index < 0 || star_index >= world->Star_Count()) {
                return false;
            }
            return (world->stars[star_index].visited & (1u << (player & 0x1F))) != 0;
        }

        // Same reach as Compute_AI_Data (aidata.cpp): 45 map units per parsec of ship range from
        // any of our colonies, unless a black hole blocks the way.
        bool Star_In_Extended_Range_(const game::s_view* world, uint8_t player, int32_t star_index) {
            const int64_t reach = static_cast<int64_t>(world->players[player].ship_range) * 45;
            const int64_t reach_sq = reach * reach;
            for (int32_t i = 0; i < world->Colony_Count(); ++i) {
                const s_colony* colony = &world->colonies[i];
                if (colony->owner != static_cast<int8_t>(player) || colony->planet < 0
                    || colony->planet >= world->Planet_Count()) {
                    continue;
                }
                const int32_t home = world->planets[colony->planet].star_index;
                if (home < 0 || home >= world->Star_Count()) {
                    continue;
                }
                const int64_t dx = static_cast<int64_t>(world->stars[home].x) - world->stars[star_index].x;
                const int64_t dy = static_cast<int64_t>(world->stars[home].y) - world->stars[star_index].y;
                if (dx * dx + dy * dy <= reach_sq
                    && struct_::Read_Bit_Field(world->stars[home].black_hole_blocks, static_cast<uint16_t>(star_index)) == 0) {
                    return true;
                }
            }
            return false;
        }

        // The AI colonization targets (aimove.cpp Get_Colonizable_Planets): uncolonized planets of
        // the regular type, or with `outpost_sites` the other types (asteroids, gas giants). Unlike
        // the AI we count only stars this empire has visited.
        int32_t Count_Free_Planets_In_Reach_(const game::s_view* world, uint8_t player, bool outpost_sites = false) {
            int32_t count = 0;
            for (int32_t i = 0; i < world->Planet_Count(); ++i) {
                const s_planet_data& planet = world->planets[i];
                if (planet.colony_index != -1 || (planet.planet_type == PLANET_TYPE_PLANET) == outpost_sites
                    || !Star_Visited_By(world, planet.star_index, player)) {
                    continue;
                }
                if (Star_In_Extended_Range_(world, player, planet.star_index)) {
                    ++count;
                }
            }
            return count;
        }

        void Fill_Self(const game::s_view* world, uint8_t player, s_view_self* self) {
            const s_player& plr = world->players[player];

            self->player = player;
            Copy_Name(self->race_name, plr.race_name, sizeof(plr.race_name));
            Copy_Name(self->leader_name, plr.name, sizeof(plr.name));
            self->objective = plr.objectives;
            self->personality = plr.personality;
            self->stardate = world->Stardate();
            self->treasury_bc = plr.bc;
            // Cached by COLCALC::Update_Player_Stats on the same rule the screens use.
            self->income_bc = plr.surplus_bc;
            self->industry = plr.industry_produced;
            self->research = plr.research_produced;  // the game stores 0x7FFF here for a special case
            self->food_surplus = plr.surplus_food;
            self->population = plr.total_pop;
            self->race = Race_Leanings(plr.traits);
            self->command_points = plr.command_points;
            self->command_points_used = plr.command_points_used;
            self->stars_total = world->Star_Count();

            for (int32_t i = 0; i < world->Star_Count(); ++i) {
                if (Star_Visited_By(world, i, player)) {
                    ++self->stars_explored;
                }
            }
            for (int32_t i = 0; i < world->Colony_Count(); ++i) {
                if (world->colonies[i].owner == static_cast<int8_t>(player)) {
                    ++self->colonies;
                }
            }
            self->free_planets_in_reach = Count_Free_Planets_In_Reach_(world, player);
            self->free_outpost_sites_in_reach = Count_Free_Planets_In_Reach_(world, player, true);
            for (int32_t i = 0; i < world->Colony_Count(); ++i) {
                if (world->colonies[i].owner == static_cast<int8_t>(player) && world->colonies[i].outpost_flag != 0) {
                    ++self->outposts;
                }
            }
            for (int32_t i = 0; i < world->Colony_Count(); ++i) {
                if (world->colonies[i].owner != static_cast<int8_t>(player) || world->colonies[i].outpost_flag != 0) continue;
                const int32_t value = stability::Of(static_cast<int16_t>(i));
                if (value < 0) ++self->unstable_colonies;
                self->worst_stability = std::min(self->worst_stability, value);
            }
            {
                int32_t sum = 0;
                for (int16_t i = 0; i < MAX_LEADERS; ++i) {
                    const int32_t value = loyalty::Of(i);
                    if (value == loyalty::UNTRACKED || world->leaders == nullptr ||
                        world->leaders[i].player_index != static_cast<int8_t>(player)) {
                        continue;
                    }
                    self->lowest_loyalty = self->loyal_leaders == 0 ? value : std::min(self->lowest_loyalty, value);
                    sum += value;
                    ++self->loyal_leaders;
                }
                if (self->loyal_leaders > 0) self->average_loyalty = sum / self->loyal_leaders;
            }
            for (int32_t i = 0; i < TECH_APP_COUNT; ++i) {
                if (plr.tech_applications[i] == TECH_RESEARCH_STATUS_RESEARCHED) {
                    ++self->techs_known;
                }
            }
            for (int32_t i = 0; i < world->Ship_Count(); ++i) {
                const s_ship_data* ship = &world->ships[i];
                if (Ship_Is_Alive(ship) && ship->owner == static_cast<int8_t>(player)) {
                    ++self->ships;
                    const int32_t strength = Combat_Ship_Strength(world, i, player);
                    self->fleet_strength += strength;
                    if (strength > 0) {
                        ++self->combat_ships;
                    }
                }
            }
        }

        void Fill_Contact(const game::s_view* world, uint8_t player, uint8_t other, s_view_contact* contact) {
            const s_player& me = world->players[player];
            const s_player& them = world->players[other];
            const int32_t now = world->Stardate();

            contact->player = other;
            Copy_Name(contact->race_name, them.race_name, sizeof(them.race_name));
            Copy_Name(contact->leader_name, them.name, sizeof(them.name));
            contact->personality = them.personality;

            Set_Fact(&contact->relations, me.relations[other], FACT_DIPLOMACY, CONFIDENCE_HIGH, now);
            contact->our_rival = rivalry::Rival_Of(player) == other ? 1 : 0;
            contact->their_rival = rivalry::Rival_Of(other) == player ? 1 : 0;
            contact->pact = pacts::Has_Pact(player, other) ? 1 : 0;
            contact->we_guarantee = pacts::Guarantees(player, other) ? 1 : 0;
            contact->they_guarantee = pacts::Guarantees(other, player) ? 1 : 0;
            contact->our_embargo = embargo::Participates(player, other) ? 1 : 0;
            contact->their_embargo = embargo::Participates(other, player) ? 1 : 0;
            contact->network_points = static_cast<int16_t>(espionage::Points(player, other));
            static_assert(sizeof(contact->opinion) == opinion::REASON_COUNT, "one opinion slot per reason");
            for (int32_t r = 0; r < opinion::REASON_COUNT; ++r) {
                contact->opinion[r] = static_cast<int8_t>(opinion::Modifier(player, other, static_cast<opinion::e_reason>(r)));
            }
            Set_Fact(&contact->treaty, me.treaty[other], FACT_DIPLOMACY, CONFIDENCE_HIGH, now);
            Set_Fact(&contact->trade_treaty, me.trade_treaty[other], FACT_DIPLOMACY, CONFIDENCE_HIGH, now);
            Set_Fact(&contact->research_treaty, me.research_treaty[other], FACT_DIPLOMACY, CONFIDENCE_HIGH, now);

            int32_t known_colonies = 0;
            for (int32_t i = 0; i < world->Colony_Count(); ++i) {
                const s_colony* colony = &world->colonies[i];
                if (colony->owner != static_cast<int8_t>(other) || colony->planet < 0
                    || colony->planet >= world->Planet_Count()) {
                    continue;
                }
                if (Star_Visited_By(world, world->planets[colony->planet].star_index, player)) {
                    ++known_colonies;
                }
            }
            Set_Fact(&contact->known_colonies, known_colonies, FACT_OWN_RECORDS, CONFIDENCE_MEDIUM, now);

            int32_t visible_ships = 0;
            int32_t visible_strength = 0;
            int32_t visible_combat = 0;
            for (int32_t i = 0; i < world->Ship_Count(); ++i) {
                const s_ship_data* ship = &world->ships[i];
                if (!Ship_Is_Alive(ship) || ship->owner != static_cast<int8_t>(other)) {
                    continue;
                }
                if (!SHIPSTAK::Is_Ship_Detected_By(static_cast<int16_t>(i), player)) {
                    continue;
                }
                ++visible_ships;
                const int32_t strength = Combat_Ship_Strength(world, i, player);
                visible_strength += strength;
                if (strength > 0) {
                    ++visible_combat;
                }
            }
            Set_Fact(&contact->visible_ships, visible_ships, FACT_SENSORS, CONFIDENCE_HIGH, now);
            Set_Fact(&contact->visible_fleet_strength, visible_strength, FACT_SENSORS, CONFIDENCE_HIGH, now);
            Set_Fact(&contact->visible_combat_ships, visible_combat, FACT_SENSORS, CONFIDENCE_HIGH, now);

            // ship_war_losses[x] / colony_war_losses[x]: losses suffered in war with x (assumed).
            Set_Fact(&contact->our_ship_losses, me.ship_war_losses[other], FACT_OWN_RECORDS, CONFIDENCE_HIGH, now);
            Set_Fact(&contact->their_ship_losses, them.ship_war_losses[player], FACT_OWN_RECORDS, CONFIDENCE_HIGH, now);
            Set_Fact(&contact->our_colony_losses, me.colony_war_losses[other], FACT_OWN_RECORDS, CONFIDENCE_HIGH, now);

            // s_player::spies keeps the mission in its two high bits; the count is the low six.
            contact->our_spies = static_cast<uint8_t>(BILL::Get_Their_Spy_Number(player, other));
            for (int32_t k = 0; k < world->Player_Count() && k < MAX_PLAYERS; ++k) {
                if (k != other && them.treaty[k] >= 4 && them.treaty[k] <= 6) {
                    contact->at_war_with = static_cast<uint8_t>(contact->at_war_with | (1u << k));
                }
                if (k != other && them.treaty[k] == 2) {
                    contact->allied_with = static_cast<uint8_t>(contact->allied_with | (1u << k));
                }
            }

            contact->in_range = me.within_range[other] == 1 ? 1 : 0;
            // Council votes come from population (population / 10). Without spies: an estimate from
            // the colonies we know and our own population per populated colony; without either, unknown.
            if (contact->known_colonies.value > 0) {
                int32_t our_populated = 0;
                for (int32_t i = 0; i < world->Colony_Count(); ++i) {
                    if (world->colonies[i].owner == static_cast<int8_t>(player) && world->colonies[i].outpost_flag == 0) {
                        ++our_populated;
                    }
                }
                const int32_t per_colony = our_populated > 0 ? me.total_pop / our_populated : 0;
                Set_Fact(&contact->population, contact->known_colonies.value * per_colony, FACT_OWN_RECORDS, CONFIDENCE_LOW, now);
            }
            const s_seen_ships& seen = Seen_Ships(player, static_cast<uint8_t>(other));
            contact->seen_warships = seen.ships;
            contact->seen_threat = static_cast<uint8_t>(Seen_Threat(seen));
            contact->seen_missile_screen = Seen_Missile_Screen(seen) ? 1 : 0;
            const s_military_report report = Military_Report(world, player, static_cast<uint8_t>(other));
            if (report.sent) {
                const int64_t credited = std::min<int64_t>(report.claimed * report.credibility / 100, INT32_MAX);
                Set_Fact(&contact->reported_power, static_cast<int32_t>(credited), FACT_DIPLOMACY,
                    report.credibility >= 70 ? CONFIDENCE_MEDIUM : CONFIDENCE_LOW, now);
            }

            // Intel network: our spies there report the whole empire, not only what we have seen.
            if (contact->our_spies > 0) {
                // A resident there (spy ranks) makes our intelligence exact.
                const e_confidence spy_confidence = spy_ranks::Residents(player, other) > 0 ? CONFIDENCE_HIGH : CONFIDENCE_MEDIUM;
                int32_t colonies = 0;
                for (int32_t i = 0; i < world->Colony_Count(); ++i) {
                    if (world->colonies[i].owner == static_cast<int8_t>(other)) {
                        ++colonies;
                    }
                }
                int32_t ships = 0;
                int32_t strength = 0;
                int32_t combat = 0;
                for (int32_t i = 0; i < world->Ship_Count(); ++i) {
                    const s_ship_data* ship = &world->ships[i];
                    if (Ship_Is_Alive(ship) && ship->owner == static_cast<int8_t>(other)) {
                        ++ships;
                        const int32_t ship_strength = Combat_Ship_Strength(world, i, player);
                        strength += ship_strength;
                        if (ship_strength > 0) {
                            ++combat;
                        }
                    }
                }
                Set_Fact(&contact->visible_combat_ships, combat, FACT_SPY, spy_confidence, now);
                int32_t techs = 0;
                for (int32_t i = 0; i < TECH_APP_COUNT; ++i) {
                    if (them.tech_applications[i] == TECH_RESEARCH_STATUS_RESEARCHED) {
                        ++techs;
                    }
                }
                Set_Fact(&contact->known_colonies, colonies, FACT_SPY, spy_confidence, now);
                Set_Fact(&contact->visible_ships, ships, FACT_SPY, spy_confidence, now);
                Set_Fact(&contact->visible_fleet_strength, strength, FACT_SPY, spy_confidence, now);
                Set_Fact(&contact->techs_known, techs, FACT_SPY, spy_confidence, now);
                Set_Fact(&contact->population, them.total_pop, FACT_SPY, spy_confidence, now);
            }
        }

        void Append_Format(std::string* out, const char* format, ...) {
            char buffer[512];
            va_list args;
            va_start(args, format);
            const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            if (written > 0) {
                out->append(buffer, std::min<size_t>(static_cast<size_t>(written), sizeof(buffer) - 1));
            }
        }

        void Append_Tag(std::string* out, const s_fact& fact) {
            Append_Format(out, " [%s, %d, %s]", Fact_Source_Name(fact.source), static_cast<int>(fact.stardate),
                          Confidence_Name(fact.confidence));
        }

        void Append_Fact_Line(std::string* out, const char* label, const s_fact& fact) {
            Append_Format(out, "   - %s %d", label, static_cast<int>(fact.value));
            Append_Tag(out, fact);
            out->push_back('\n');
        }
    } // namespace

    bool Build_Empire_View(const game::s_view* world, uint8_t player, s_empire_view* out) {
        if (out == nullptr) {
            return false;
        }
        *out = s_empire_view{};
        if (world == nullptr || world->players == nullptr || world->ships == nullptr || world->stars == nullptr
            || world->colonies == nullptr || world->planets == nullptr) {
            return false;
        }
        if (player >= world->Player_Count() || player >= MAX_PLAYERS || world->players[player].eliminated != 0) {
            return false;
        }

        Fill_Self(world, player, &out->self);

        for (int32_t other = 0; other < world->Player_Count() && other < MAX_PLAYERS; ++other) {
            if (other == player || world->players[player].contact[other] == 0 || world->players[other].eliminated != 0) {
                continue;
            }
            Fill_Contact(world, player, static_cast<uint8_t>(other), &out->contacts[out->contact_count]);
            ++out->contact_count;
        }
        return true;
    }

    void Format_Empire_Summary(const s_empire_view* view, std::string* out) {
        if (out == nullptr) {
            return;
        }
        out->clear();
        if (view == nullptr) {
            return;
        }
        const s_view_self& self = view->self;

        Append_Format(out, "STARDATE %d. You advise the %s (player %d). Leader: %s. Personality: %s. Objective: %s.\n",
                      static_cast<int>(self.stardate), self.race_name, static_cast<int>(self.player), self.leader_name,
                      Personality_Name(self.personality), Objective_Name(self.objective));
        Append_Format(out, "OUR STATE: %d colonies (%d outposts), population %d, industry %d/turn, research %d RP/turn, "
                           "income %+d BC/turn, treasury %d BC, food %+d.\n",
                      static_cast<int>(self.colonies), static_cast<int>(self.outposts), static_cast<int>(self.population),
                      static_cast<int>(self.industry),
                      static_cast<int>(self.research), static_cast<int>(self.income_bc),
                      static_cast<int>(self.treasury_bc), static_cast<int>(self.food_surplus));
        if (self.unstable_colonies > 0) {
            Append_Format(out, "Unrest: %d colonies unstable, the worst at stability %d (unrest lowers morale; deep unrest may end in secession).\n",
                          static_cast<int>(self.unstable_colonies), static_cast<int>(self.worst_stability));
        }
        if (self.loyal_leaders > 0) {
            Append_Format(out, "Leaders: %d, loyalty %d on average, the lowest %d (of 100).\n",
                          static_cast<int>(self.loyal_leaders), static_cast<int>(self.average_loyalty),
                          static_cast<int>(self.lowest_loyalty));
        }
        Append_Format(out, "Fleet: %d ships, strength %d. Command points: %d of %d used. Explored: %d of %d stars, "
                           "%d free planets and %d outpost sites in reach.\n",
                      static_cast<int>(self.ships), static_cast<int>(self.fleet_strength),
                      static_cast<int>(self.command_points_used), static_cast<int>(self.command_points),
                      static_cast<int>(self.stars_explored), static_cast<int>(self.stars_total),
                      static_cast<int>(self.free_planets_in_reach), static_cast<int>(self.free_outpost_sites_in_reach));

        Append_Format(out, "Technologies known: %d.\n", static_cast<int>(self.techs_known));
        const s_race_leanings& race = self.race;
        if (race.war != 0 || race.diplomacy != 0 || race.spying != 0 || race.growth != 0 || race.stealth != 0) {
            Append_Format(out, "Race leanings: war %+d, diplomacy %+d, spying %+d, growth %+d, stealth %+d.\n",
                          static_cast<int>(race.war), static_cast<int>(race.diplomacy), static_cast<int>(race.spying),
                          static_cast<int>(race.growth), static_cast<int>(race.stealth));
        }

        out->append("CONTACTS:");
        if (view->contact_count <= 0) {
            out->append(" none\n");
            return;
        }
        out->push_back('\n');

        const int32_t count = view->contact_count < MAX_PLAYERS ? view->contact_count : MAX_PLAYERS;
        for (int32_t i = 0; i < count; ++i) {
            const s_view_contact& contact = view->contacts[i];
            Append_Format(out, "%d. %s (player %d). Leader: %s. Personality: %s.\n", static_cast<int>(i + 1),
                          contact.race_name, static_cast<int>(contact.player), contact.leader_name,
                          Personality_Name(contact.personality));
            Append_Fact_Line(out, "relations", contact.relations);
            {
                bool any = false;
                for (int32_t r = 0; r < opinion::REASON_COUNT; ++r) {
                    if (contact.opinion[r] == 0) continue;
                    Append_Format(out, any ? "; %s %+d" : "   - why we feel so: %s %+d",
                                  opinion::Reason_Name(static_cast<opinion::e_reason>(r)), static_cast<int>(contact.opinion[r]));
                    any = true;
                }
                if (any) out->append(" [memory, fading]\n");
            }
            if (contact.our_rival != 0) out->append("   - rivalry: we declared them our rival\n");
            if (contact.their_rival != 0) out->append("   - rivalry: they declared us their rival\n");
            if (contact.pact != 0) out->append("   - defence pact with us: each goes to war with whoever attacks the other\n");
            if (contact.we_guarantee != 0) out->append("   - we guarantee their independence\n");
            if (contact.they_guarantee != 0) out->append("   - they guarantee our independence\n");
            if (contact.our_embargo != 0) out->append("   - embargo: we hold one on them (no trade or research treaties)\n");
            if (contact.their_embargo != 0) out->append("   - embargo: they hold one on us (no trade or research treaties)\n");
            if (contact.network_points > 0) {
                Append_Format(out, "   - our spy network there: %d points\n", static_cast<int>(contact.network_points));
            }
            Append_Format(out, "   - treaty %s", Treaty_Name(contact.treaty.value));
            Append_Tag(out, contact.treaty);
            out->push_back('\n');
            Append_Format(out, "   - trade treaty %s", contact.trade_treaty.value != 0 ? "yes" : "no");
            Append_Tag(out, contact.trade_treaty);
            out->push_back('\n');
            Append_Format(out, "   - research treaty %s", contact.research_treaty.value != 0 ? "yes" : "no");
            Append_Tag(out, contact.research_treaty);
            out->push_back('\n');
            Append_Fact_Line(out, "known colonies", contact.known_colonies);
            if (contact.population.source != FACT_UNKNOWN) {
                Append_Fact_Line(out, "population", contact.population);
            }
            Append_Fact_Line(out, "visible ships", contact.visible_ships);
            Append_Fact_Line(out, "visible fleet strength", contact.visible_fleet_strength);
            Append_Fact_Line(out, "ships we lost to them", contact.our_ship_losses);
            Append_Fact_Line(out, "ships they lost to us", contact.their_ship_losses);
            Append_Fact_Line(out, "colonies we lost to them", contact.our_colony_losses);
            if (contact.techs_known.source != FACT_UNKNOWN) {
                Append_Fact_Line(out, "technologies known", contact.techs_known);
            }
            Append_Format(out, "   - our spies in their empire: %d\n", static_cast<int>(contact.our_spies));
            if (contact.reported_power.source == FACT_DIPLOMACY) {
                Append_Fact_Line(out, "reported fighting power near us, as far as we believe it", contact.reported_power);
            }
            if (contact.seen_warships > 0) {
                static const char* const threats[] = {"mixed weapons", "mostly missiles", "mostly fighters", "mostly beams"};
                Append_Format(out, "   - seen in battle: %d warships (fading memory), %s%s\n",
                              static_cast<int>(contact.seen_warships),
                              threats[contact.seen_threat < 4 ? contact.seen_threat : 0],
                              contact.seen_missile_screen != 0 ? ", strong missile screen" : "");
            }
        }
    }

    const char* Objective_Name(uint8_t objective) {
        switch (objective) {
            case PLAYER_OBJECTIVE_DIPLOMATIC: return "diplomatic";
            case PLAYER_OBJECTIVE_MILITARIST: return "militarist";
            case PLAYER_OBJECTIVE_EXPANSIONIST: return "expansionist";
            case PLAYER_OBJECTIVE_TECHNOLOGIST: return "technologist";
            case PLAYER_OBJECTIVE_INDUSTRIALIST: return "industrialist";
            case PLAYER_OBJECTIVE_ECOLOGIST: return "ecologist";
            case PLAYER_OBJECTIVE_HUMAN: return "human";
            default: return "unknown";
        }
    }

    const char* Personality_Name(uint8_t personality) {
        switch (personality) {
            case PLAYER_PERSONALITY_XENOPHOBIC: return "xenophobic";
            case PLAYER_PERSONALITY_RUTHLESS: return "ruthless";
            case PLAYER_PERSONALITY_AGGRESSIVE: return "aggressive";
            case PLAYER_PERSONALITY_ERRATIC: return "erratic";
            case PLAYER_PERSONALITY_HONORABLE: return "honorable";
            case PLAYER_PERSONALITY_PACIFIST: return "pacifist";
            case PLAYER_PERSONALITY_DISHONORED: return "dishonored";
            default: return "unknown";
        }
    }

    // s_player::treaty: 0 none, 1 non-aggression, 2 alliance, 3 peace (diplomac.cpp), 4 and above
    // are the war states (npcdiplo/aimove treat 4-6 as hostile; 6 is TOTAL_WAR in diplomac.cpp).
    const char* Treaty_Name(int32_t treaty) {
        switch (treaty) {
            case 0: return "no treaty";
            case 1: return "non-aggression pact";
            case 2: return "alliance";
            case 3: return "peace";
            case 4: return "war";
            case 5: return "war";
            case 6: return "total war";
            default: return "unknown";
        }
    }

    const char* Fact_Source_Name(uint8_t source) {
        switch (source) {
            case FACT_OWN_RECORDS: return "own records";
            case FACT_DIPLOMACY: return "diplomacy";
            case FACT_SENSORS: return "sensors";
            case FACT_BATTLE: return "battle";
            case FACT_SPY: return "spy";
            case FACT_RUMOR: return "rumor";
            default: return "unknown";
        }
    }

    const char* Confidence_Name(uint8_t confidence) {
        switch (confidence) {
            case CONFIDENCE_LOW: return "low";
            case CONFIDENCE_MEDIUM: return "medium";
            case CONFIDENCE_HIGH: return "high";
            default: return "none";
        }
    }

} // namespace advisor
