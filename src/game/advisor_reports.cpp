#include "pch.h"

#include "game/advisor_reports.h"

#include <algorithm>
#include <vector>

#include "game/advisor.h"
#include "game/aipower.h"
#include "game/diplomacy_memory.h"
#include "game/legacy_ids.h"
#include "game/shipstak.h"

// Nothing in this file draws from game_random.
namespace advisor {
    namespace {
        // Ship status below this value is a live ship (colcalc Update_Player_Counts; advisor_view.cpp).
        constexpr int32_t SHIP_STATUS_FIRST_NOT_ALIVE = 5;

        bool At_War_(const s_player& a, int32_t b) {
            return a.treaty[b] >= 4 && a.treaty[b] <= 6;
        }

        bool Steered_Computer_(const game::s_view* world, uint8_t player) {
            if (player >= MAX_PLAYERS || player >= world->Player_Count()) return false;
            if (((active_players >> player) & 1) == 0) return false;
            const s_player& p = world->players[player];
            return p.eliminated == 0 && p.objectives != PLAYER_OBJECTIVE_HUMAN;
        }
    } // namespace

    int32_t Report_Exaggeration_Percent(uint8_t personality) {
        switch (personality) {
            case PLAYER_PERSONALITY_AGGRESSIVE:
            case PLAYER_PERSONALITY_XENOPHOBIC:
                return 150;
            case PLAYER_PERSONALITY_RUTHLESS:
            case PLAYER_PERSONALITY_ERRATIC:
                return 200;
            default:
                return 100;  // honorable, pacifist
        }
    }

    int32_t Report_Credibility_Percent(int32_t trust) {
        return std::clamp<int32_t>(50 + trust, 10, 100);
    }

    s_military_report Military_Report(const game::s_view* world, uint8_t receiver, uint8_t sender) {
        s_military_report report;
        if (!enabled || !Phase(PHASE_PART_REPORTS) || world == nullptr || world->players == nullptr) return report;
        if (!Steered_Computer_(world, receiver) || sender == receiver || sender >= MAX_PLAYERS || sender >= world->Player_Count()) {
            return report;
        }
        const s_player& to = world->players[receiver];
        const s_player& from = world->players[sender];
        if (from.eliminated != 0 || from.objectives == PLAYER_OBJECTIVE_HUMAN || to.contact[sender] != 1 || At_War_(to, sender)) {
            return report;
        }
        bool at_war = false;
        for (int32_t k = 0; k < world->Player_Count() && k < MAX_PLAYERS; ++k) {
            if (k != receiver && k != sender && world->players[k].eliminated == 0 && At_War_(from, k)) at_war = true;
        }
        if (!at_war) return report;

        // The receiver's colonies, by star position.
        std::vector<std::pair<int32_t, int32_t>> homes;
        for (int32_t i = 0; i < world->Colony_Count(); ++i) {
            const s_colony& colony = world->colonies[i];
            if (colony.owner != static_cast<int8_t>(receiver) || colony.planet < 0 || colony.planet >= world->Planet_Count()) continue;
            const int32_t star = world->planets[colony.planet].star_index;
            if (star < 0 || star >= world->Star_Count()) continue;
            homes.emplace_back(world->stars[star].x, world->stars[star].y);
        }
        const int64_t radius_sq = static_cast<int64_t>(REPORT_RADIUS) * REPORT_RADIUS;
        int64_t strength = 0;
        int64_t ships = 0;
        int64_t seen_strength = 0;
        int64_t seen_ships = 0;
        for (int32_t i = 0; i < world->Ship_Count(); ++i) {
            const s_ship_data& ship = world->ships[i];
            if (ship.owner != static_cast<int8_t>(sender) || ship.status >= SHIP_STATUS_FIRST_NOT_ALIVE ||
                ship.d.ship_type != SHIP_TYPE_COMBAT) {
                continue;
            }
            bool near = false;
            for (const auto& home : homes) {
                const int64_t dx = static_cast<int64_t>(ship.x) - home.first;
                const int64_t dy = static_cast<int64_t>(ship.y) - home.second;
                if (dx * dx + dy * dy <= radius_sq) {
                    near = true;
                    break;
                }
            }
            if (!near) continue;
            const int64_t value = AIPOWER::Ship_Strength_Vs_Ships(
                world, legacy_world::Ship_Id(static_cast<uint32_t>(i)), legacy_world::Player_Id(receiver));
            strength += value;
            ++ships;
            if (SHIPSTAK::Is_Ship_Detected_By(static_cast<int16_t>(i), receiver)) {
                seen_strength += value;
                ++seen_ships;
            }
        }
        report.sent = true;
        report.claimed = strength * (ships + 1) * Report_Exaggeration_Percent(from.personality) / 100;
        report.detected = seen_strength * (seen_ships + 1);
        report.credibility = Report_Credibility_Percent(to.trust_worthiness[sender]);
        return report;
    }

    void Check_Military_Reports(const game::s_view* world) {
        if (!enabled || !Phase(PHASE_PART_REPORTS) || world == nullptr || world->players == nullptr) return;
        if (world->Stardate() % REPORT_CHECK_PERIOD != 0) return;
        const int32_t players = std::min<int32_t>(world->Player_Count(), MAX_PLAYERS);
        for (int32_t receiver = 0; receiver < players; ++receiver) {
            if (!Steered_Computer_(world, static_cast<uint8_t>(receiver))) continue;
            for (int32_t sender = 0; sender < players; ++sender) {
                const s_military_report report = Military_Report(world, static_cast<uint8_t>(receiver), static_cast<uint8_t>(sender));
                if (!report.sent || report.claimed <= 0 || 100 * report.claimed <= REPORT_LIE_PERCENT * report.detected) continue;
                // Caught: what the scanners see is far less than claimed. Trust never drops past the
                // floor the diplomacy memory keeps (classic int8 trust would wrap).
                if (world->players[receiver].trust_worthiness[sender] - REPORT_LIE_TRUST < diplomemory::TRUST_FLOOR) continue;
                diplomemory::Lower_Trust(static_cast<int16_t>(receiver), static_cast<int16_t>(sender), REPORT_LIE_TRUST);
            }
        }
    }

} // namespace advisor
