#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/combat_dice.h"

#include <string>
#include <vector>

namespace {
    struct s_dice_scope {
        test::s_saved_value<bool> log{combat_dice::combat_log};
        test::s_saved_value<bool> crits{combat_dice::combat_crits};
        s_combat_data* old_data = MOX::combat_data;
        int16_t old_count = MOX::combat_total_ship_count;
        std::vector<s_combat_data> ships = std::vector<s_combat_data>(3);

        s_dice_scope() {
            MOX::combat_data = ships.data();
            MOX::combat_total_ship_count = 3;
            for (s_combat_data& ship : ships) {
                for (int16_t& arc : ship.shield_arc_current) arc = 5;
                ship.movement_left = 4;
                for (uint8_t& ready : ship.weapon_ready_flags) ready = 1;
            }
        }
        ~s_dice_scope() {
            MOX::combat_data = old_data;
            MOX::combat_total_ship_count = old_count;
        }
    };
} // namespace

DOCTEST_TEST_SUITE("dice in battle") {

DOCTEST_TEST_CASE("a failure lasts the turn: shields, drive or computer of the struck ship only") {
    s_dice_scope scope;
    combat_dice::Apply_Failure(1, combat_dice::SYSTEM_SHIELDS);
    DOCTEST_CHECK(MOX::combat_data[1].shield_arc_current[2] == 0);
    DOCTEST_CHECK(MOX::combat_data[2].shield_arc_current[2] == 5);
    combat_dice::Apply_Failure(2, combat_dice::SYSTEM_DRIVE);
    DOCTEST_CHECK(MOX::combat_data[2].movement_left == 0);
    combat_dice::Apply_Failure(2, combat_dice::SYSTEM_COMPUTER);
    DOCTEST_CHECK(MOX::combat_data[2].weapon_ready_flags[0] == 0);
    combat_dice::Apply_Failure(0, combat_dice::SYSTEM_DRIVE);  // the planet: nothing
    DOCTEST_CHECK(MOX::combat_data[0].movement_left == 4);
}

DOCTEST_TEST_CASE("critical hits only with the flag, never on the planet") {
    s_dice_scope scope;
    combat_dice::combat_crits = false;
    DOCTEST_CHECK(combat_dice::On_Critical_Hit(1, 2) == combat_dice::SYSTEM_COUNT);
    combat_dice::combat_crits = true;
    DOCTEST_CHECK(combat_dice::On_Critical_Hit(1, 0) == combat_dice::SYSTEM_COUNT);
    const combat_dice::e_system system = combat_dice::On_Critical_Hit(1, 2);
    DOCTEST_REQUIRE(system < combat_dice::SYSTEM_COUNT);
    const s_combat_data& ship = MOX::combat_data[2];
    const bool failed = ship.shield_arc_current[0] == 0 || ship.movement_left == 0 || ship.weapon_ready_flags[0] == 0;
    DOCTEST_CHECK(failed);
    DOCTEST_CHECK(std::string(combat_dice::System_Name(system)).size() > 0);
}

DOCTEST_TEST_CASE("the log changes nothing in the battle") {
    s_dice_scope scope;
    combat_dice::combat_log = true;
    combat_dice::Log_Beam_Shot(1, 2, 0, 97, 20, 60, 4, 1, 4);
    combat_dice::Log_Beam_Shot(1, 2, 0, 10, 20, 60, 0, 1, 4);
    combat_dice::Log_Ordnance_Hit(1, 2, 0, 55, 3);
    DOCTEST_CHECK(MOX::combat_data[2].shield_arc_current[0] == 5);
    DOCTEST_CHECK(MOX::combat_data[2].movement_left == 4);
}

}
