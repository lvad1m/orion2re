#include "pch.h"

#include "doctest/doctest.h"
#include "game/shipstak.h"
#include "../fixtures/game_state.h"

namespace {
    struct s_detection_state : test::s_game_state {
        test::s_saved_array<s_ship_node, MAX_SHIPS> nodes{MOX::ship_node};
        test::s_saved_array<int16_t, MAX_SHIPS> starts{MOX::ship_stack_start};
        decltype(SHIPSTAK::scan_bonus) scan = SHIPSTAK::scan_bonus;
        decltype(SHIPSTAK::stealth_bonus) stealth = SHIPSTAK::stealth_bonus;
        decltype(SHIPSTAK::stealth_size_penalty) penalty = SHIPSTAK::stealth_size_penalty;
        int16_t stack_count = MOX::ship_stack_count;

        ~s_detection_state() {
            SHIPSTAK::scan_bonus = scan;
            SHIPSTAK::stealth_bonus = stealth;
            SHIPSTAK::stealth_size_penalty = penalty;
            MOX::ship_stack_count = stack_count;
        }

        // Ship 0: enemy (owner 1) alone in stack 0. Ship 1: observer's ship (owner 0).
        void Reset() {
            memset(MOX::ship, 0, sizeof(s_ship_data) * MAX_SHIPS);
            memset(MOX::star, 0xff, sizeof(s_star_data) * (MAX_STARS + 1));
            memset(MOX::planet, 0, sizeof(s_planet_data) * MAX_PLANETS);
            memset(MOX::player, 0, sizeof(s_player) * MAX_PLAYERS);
            memset(MOX::colony, 0, sizeof(s_colony) * MAX_COLONIES);
            for (int i = 0; i <= MAX_STARS; ++i) {
                MOX::star[i].visited = 0;
            }
            memset(MOX::ship_node, 0xff, sizeof(MOX::ship_node));
            memset(MOX::ship_stack_start, 0xff, sizeof(MOX::ship_stack_start));
            SHIPSTAK::scan_bonus = {};
            SHIPSTAK::stealth_bonus = {};
            SHIPSTAK::stealth_size_penalty = SHIPSTAK::e_stealth_size_penalty::none;

            MOX::PLAYER_NUM = 0;
            MOX::NUM_PLAYERS = 2;
            MOX::NUM_STARS = 2;
            MOX::NUM_SHIPS = 2;
            MOX::NUM_COLONIES = 0;
            MOX::ship_stack_count = 1;
            MOX::ship_stack_start[0] = 0;
            MOX::ship_node[0].ship_idx = 0;
            MOX::ship_node[0].next_node = -1;
            MOX::ship[0].owner = 1;
            MOX::ship[1].owner = 0;
            MOX::ship[1].status = 0;
        }

        // Runs the stack filter and returns whether ship 0 survived.
        bool Kept_By_Remove() {
            SHIPSTAK::Remove_Non_Detected_Ships();
            return MOX::ship_stack_count == 1 && MOX::ship_stack_start[0] == 0;
        }

        void Check_Agrees() {
            const bool pure = SHIPSTAK::Is_Ship_Detected_By(0, static_cast<uint8_t>(MOX::PLAYER_NUM));
            DOCTEST_CHECK(pure == Kept_By_Remove());
        }
    };
}

DOCTEST_TEST_CASE_FIXTURE(s_detection_state, "detection: own ship is always detected") {
    Reset();
    DOCTEST_CHECK(SHIPSTAK::Is_Ship_Detected_By(1, 0));
    // Both ships start at (0, 0): a ship in the same place as one of ours is always seen
    // (range 0 uses scan_bonus.fleet), exactly as Remove_Non_Detected_Ships decides.
    DOCTEST_CHECK(SHIPSTAK::Is_Ship_Detected_By(0, 0));
    // Far away and without scanners the enemy ship is not seen.
    MOX::ship[0].x = 600;
    DOCTEST_CHECK_FALSE(SHIPSTAK::Is_Ship_Detected_By(0, 0));
    Check_Agrees();
}

DOCTEST_TEST_CASE_FIXTURE(s_detection_state, "detection: scanner level versus distance") {
    const int slots[] = {-1, TECH_APP_SPACE_SCANNER, TECH_APP_TACHYON_SCANNER,
        TECH_APP_NEUTRON_SCANNER, TECH_APP_SENSORS};
    for (int tech = 0; tech < 5; ++tech) {
        for (int16_t dist : {0, 30, 31, 90, 150}) {
            Reset();
            MOX::ship[0].x = dist;
            SHIPSTAK::scan_bonus.no_scanner = 1;
            SHIPSTAK::scan_bonus.space_scanner = 2;
            SHIPSTAK::scan_bonus.tachyon_scanner = 3;
            SHIPSTAK::scan_bonus.neutron_scanner = 4;
            SHIPSTAK::scan_bonus.sensors = 5;
            SHIPSTAK::scan_bonus.fleet = 6;
            if (slots[tech] >= 0) {
                MOX::player[0].tech_applications[slots[tech]] = TECH_RESEARCH_STATUS_RESEARCHED;
            }
            DOCTEST_CAPTURE(tech);
            DOCTEST_CAPTURE(dist);
            Check_Agrees();
        }
    }
}

DOCTEST_TEST_CASE_FIXTURE(s_detection_state, "detection: observer ship range, status and scanner specials") {
    for (int status : {0, 2, 3}) {
        for (bool battle : {false, true}) {
            for (int16_t dist : {0, 60, 120}) {
                Reset();
                MOX::ship[0].x = 0;
                MOX::ship[1].x = dist;
                MOX::ship[1].status = static_cast<decltype(MOX::ship[1].status)>(status);
                if (battle) {
                    struct_::Set_Bit_Field(MOX::ship[1].d.special_device_flags, SPECIAL_BATTLE_SCANNER);
                }
                SHIPSTAK::scan_bonus.no_scanner = 3;
                SHIPSTAK::scan_bonus.battle_scanner = 2;
                SHIPSTAK::scan_bonus.fleet = 1;
                DOCTEST_CAPTURE(status);
                DOCTEST_CAPTURE(battle);
                DOCTEST_CAPTURE(dist);
                Check_Agrees();
            }
        }
    }
}

DOCTEST_TEST_CASE_FIXTURE(s_detection_state, "detection: colony with and without bases") {
    for (int bases = 0; bases < 4; ++bases) {
        for (int16_t dist : {0, 60, 120}) {
            Reset();
            MOX::NUM_COLONIES = 1;
            MOX::colony[0].owner = 0;
            MOX::colony[0].planet = 0;
            MOX::planet[0].star_index = 0;
            MOX::star[0].x = 0;
            MOX::star[0].y = 0;
            MOX::ship[0].x = dist;
            MOX::ship[1].status = 3;
            SHIPSTAK::scan_bonus.no_scanner = 1;
            SHIPSTAK::scan_bonus.planet = 2;
            SHIPSTAK::scan_bonus.star_base = 2;
            SHIPSTAK::scan_bonus.battlestation = 1;
            SHIPSTAK::scan_bonus.star_fortress = 1;
            if (bases == 1) {
                MOX::colony[0].buildings[BUILDING_STAR_BASE] = 1;
            }
            if (bases == 2) {
                MOX::colony[0].buildings[BUILDING_BATTLESTATION] = 1;
            }
            if (bases == 3) {
                MOX::colony[0].buildings[BUILDING_STAR_FORTRESS] = 1;
            }
            DOCTEST_CAPTURE(bases);
            DOCTEST_CAPTURE(dist);
            Check_Agrees();
        }
    }
}

DOCTEST_TEST_CASE_FIXTURE(s_detection_state, "detection: stealth devices, trait and travelling penalty") {
    const int specials[] = {-1, SPECIAL_STEALTH_FIELD, SPECIAL_CLOAKING_DEVICE, SPECIAL_PHASING_CLOAK};
    for (int device = 0; device < 4; ++device) {
        for (bool stealthy : {false, true}) {
            for (int status : {0, 1}) {
                for (int pen = 0; pen < 3; ++pen) {
                    Reset();
                    MOX::ship[0].x = 90;
                    MOX::ship[0].status = static_cast<decltype(MOX::ship[0].status)>(status);
                    MOX::ship[0].d.size = SHIP_SIZE_MEDIUM;
                    MOX::player[1].traits[TRAIT_STEALTHY_SHIPS] = stealthy ? 1 : 0;
                    if (specials[device] >= 0) {
                        struct_::Set_Bit_Field(MOX::ship[0].d.special_device_flags, specials[device]);
                    }
                    SHIPSTAK::stealth_bonus.stealthy_ships = 1;
                    SHIPSTAK::stealth_bonus.stealth_field = 2;
                    SHIPSTAK::stealth_bonus.cloaking_device = 3;
                    SHIPSTAK::stealth_bonus.phasing_cloak = 4;
                    SHIPSTAK::stealth_size_penalty = static_cast<SHIPSTAK::e_stealth_size_penalty>(pen);
                    SHIPSTAK::scan_bonus.no_scanner = 2;
                    SHIPSTAK::scan_bonus.fleet = 1;
                    MOX::ship[1].x = 0;
                    DOCTEST_CAPTURE(device);
                    DOCTEST_CAPTURE(stealthy);
                    DOCTEST_CAPTURE(status);
                    DOCTEST_CAPTURE(pen);
                    Check_Agrees();
                }
            }
        }
    }
}

DOCTEST_TEST_CASE_FIXTURE(s_detection_state, "detection: monsters") {
    for (int16_t dist : {0, 30, 90, 300}) {
        Reset();
        MOX::ship[0].owner = MAX_PLAYERS;
        MOX::ship[0].x = dist;
        MOX::ship[0].d.speed = 1;
        SHIPSTAK::scan_bonus.no_scanner = 5;
        SHIPSTAK::scan_bonus.fleet = 6;
        DOCTEST_CAPTURE(dist);
        Check_Agrees();
    }
}

DOCTEST_TEST_CASE_FIXTURE(s_detection_state, "detection: stationary owner above MAX_PLAYERS needs a visit") {
    for (bool visited : {false, true}) {
        Reset();
        MOX::ship[0].owner = static_cast<decltype(MOX::ship[0].owner)>(MAX_PLAYERS + 1);
        MOX::ship[0].d.speed = 0;
        MOX::ship[0].location = 1;
        if (visited) {
            MOX::star[1].visited = 1;
        }
        DOCTEST_CAPTURE(visited);
        DOCTEST_CHECK(SHIPSTAK::Is_Ship_Detected_By(0, 0) == visited);
        Check_Agrees();
    }

    // The same owner, moving, falls under the ordinary scanner rule.
    Reset();
    MOX::ship[0].owner = static_cast<decltype(MOX::ship[0].owner)>(MAX_PLAYERS + 1);
    MOX::ship[0].d.speed = 1;
    MOX::ship[0].x = 0;
    MOX::ship[1].x = 0;
    SHIPSTAK::scan_bonus.fleet = 1;
    Check_Agrees();
}

DOCTEST_TEST_CASE_FIXTURE(s_detection_state, "detection: other observer without side effects") {
    Reset();
    // Player 1 owns ships 0 and 2; player 0 owns ship 1, far away from them.
    MOX::NUM_SHIPS = 3;
    MOX::ship[2].owner = 1;
    MOX::ship[2].status = 0;
    MOX::ship[2].x = 0;
    MOX::ship[1].x = 600;
    SHIPSTAK::scan_bonus.no_scanner = 2;
    SHIPSTAK::scan_bonus.fleet = 3;

    const int16_t player = MOX::PLAYER_NUM;
    const int16_t count = MOX::ship_stack_count;
    const std::vector<s_ship_node> nodes_before(MOX::ship_node, MOX::ship_node + MAX_SHIPS);
    const std::vector<int16_t> starts_before(MOX::ship_stack_start, MOX::ship_stack_start + MAX_SHIPS);

    // Own ships are always seen; ship 1 is 20 range steps from player 1's nearest ship.
    DOCTEST_CHECK(SHIPSTAK::Is_Ship_Detected_By(0, 1));
    DOCTEST_CHECK(SHIPSTAK::Is_Ship_Detected_By(2, 1));
    DOCTEST_CHECK(SHIPSTAK::Is_Ship_Detected_By(1, 0));
    DOCTEST_CHECK_FALSE(SHIPSTAK::Is_Ship_Detected_By(1, 1));
    DOCTEST_CHECK_FALSE(SHIPSTAK::Is_Ship_Detected_By(0, 0));

    // Bring ship 1 next to player 1's scout: player 1 now detects it, player 0 still sees only its own.
    MOX::ship[1].x = 0;
    DOCTEST_CHECK(SHIPSTAK::Is_Ship_Detected_By(1, 1));
    DOCTEST_CHECK(SHIPSTAK::Best_Detection_At(1, 0, 0) == 3);
    MOX::ship[1].x = 600;

    DOCTEST_CHECK(MOX::PLAYER_NUM == player);
    DOCTEST_CHECK(MOX::ship_stack_count == count);
    DOCTEST_CHECK(std::equal(starts_before.begin(), starts_before.end(), MOX::ship_stack_start));
    for (int i = 0; i < MAX_SHIPS; ++i) {
        DOCTEST_CHECK(nodes_before[i].ship_idx == MOX::ship_node[i].ship_idx);
        DOCTEST_CHECK(nodes_before[i].next_node == MOX::ship_node[i].next_node);
    }
}
