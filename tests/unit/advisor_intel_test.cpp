#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/advisor.h"
#include "game/advisor_intel.h"
#include "game/serialization.h"

#include <memory>
#include <vector>

namespace {
    // Restores every advisor global the tests touch and clears the memory both ways.
    struct s_intel_scope {
        bool old_enabled = advisor::enabled;
        bool old_rules_seen_ships = advisor::rules_seen_ships;
        int32_t old_active_players = advisor::active_players;

        s_intel_scope() {
            advisor::Clear_Seen_Ships();
            advisor::enabled = true;
            advisor::rules_seen_ships = true;
            advisor::active_players = 255;
        }
        ~s_intel_scope() {
            advisor::Clear_Seen_Ships();
            advisor::enabled = old_enabled;
            advisor::rules_seen_ships = old_rules_seen_ships;
            advisor::active_players = old_active_players;
        }
    };

    void Set_Bit_(s_ship_design* design, int special) {
        design->special_device_flags[special >> 3] = static_cast<int8_t>(design->special_device_flags[special >> 3] | (1 << (special & 7)));
    }

    s_ship_design Beam_Ship_(int8_t lasers) {
        s_ship_design design = {};
        design.ship_type = SHIP_TYPE_COMBAT;
        design.ship_weapon[0].type = WEAPON_LASER_CANNON;
        design.ship_weapon[0].count = lasers;
        return design;
    }

    s_ship_design Missile_Ship_(int8_t racks) {
        s_ship_design design = {};
        design.ship_type = SHIP_TYPE_COMBAT;
        design.ship_weapon[0].type = WEAPON_NUCLEAR_MISSILE;
        design.ship_weapon[0].count = racks;
        return design;
    }

    uint16_t Damage_(int weapon, int count) {
        return static_cast<uint16_t>(TECHDATA::weapons[weapon].max_damage * count);
    }

    // Player 0 is human, 1..3 are computer empires. Stardate 35000.
    void Setup_World_() {
        MOX::NUM_PLAYERS = 4;
        MOX::stardate = 35000;
        MOX::player[0].objectives = PLAYER_OBJECTIVE_HUMAN;
        for (int i = 1; i < 4; ++i) MOX::player[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
    }
} // namespace

DOCTEST_TEST_SUITE("advisor intel memory") {

DOCTEST_TEST_CASE("a seen design is tallied by weapon class, missile screen and ship count") {
    s_intel_scope scope;
    DOCTEST_REQUIRE(TECHDATA::weapons[WEAPON_LASER_CANNON].type == WEAPON_TYPE_BEAM);
    DOCTEST_REQUIRE(TECHDATA::weapons[WEAPON_NUCLEAR_MISSILE].type == WEAPON_TYPE_MISSILE);
    DOCTEST_REQUIRE(TECHDATA::weapons[WEAPON_LASER_CANNON].max_damage > 0);

    s_ship_design design = Beam_Ship_(2);
    design.ship_weapon[1].type = WEAPON_NUCLEAR_MISSILE;
    design.ship_weapon[1].count = 3;
    design.ship_weapon[2].type = WEAPON_LASER_CANNON;  // point defense: a screen, not damage
    design.ship_weapon[2].count = 1;
    design.ship_weapon[2].specials = WEAPON_MOD_FLAG_POINT_DEFENSE;
    design.ship_weapon[3].type = WEAPON_ANTI_MISSILE_ROCKET;
    design.ship_weapon[3].count = 1;
    Set_Bit_(&design, SPECIAL_ECM_JAMMER);
    Set_Bit_(&design, SPECIAL_LIGHTNING_FIELD);

    advisor::Observe_Design(1, 2, design);
    const advisor::s_seen_ships& seen = advisor::Seen_Ships(1, 2);
    DOCTEST_CHECK(seen.weapon[advisor::SEEN_BEAM] == Damage_(WEAPON_LASER_CANNON, 2));
    DOCTEST_CHECK(seen.weapon[advisor::SEEN_MISSILE] == Damage_(WEAPON_NUCLEAR_MISSILE, 3));
    DOCTEST_CHECK(seen.missile_screen == 1 + 1 + 1 + 3);
    DOCTEST_CHECK(seen.ships == 1);

    // Nobody remembers their own ships, and invalid players are ignored.
    advisor::Observe_Design(2, 2, design);
    advisor::Observe_Design(MAX_PLAYERS, 2, design);
    advisor::Observe_Design(2, MAX_PLAYERS, design);
    DOCTEST_CHECK(advisor::Seen_Ships(2, 2).ships == 0);
    DOCTEST_CHECK(advisor::Seen_Ships(2, 1).ships == 0);
    DOCTEST_CHECK(advisor::Seen_Ships(MAX_PLAYERS, 2).ships == 0);
}

DOCTEST_TEST_CASE("the threat follows the share of ship-to-ship damage") {
    advisor::s_seen_ships seen;
    DOCTEST_CHECK(advisor::Seen_Threat(seen) == advisor::SEEN_THREAT_NONE);

    seen.ships = 4;
    seen.weapon[advisor::SEEN_MISSILE] = 50;
    seen.weapon[advisor::SEEN_BEAM] = 50;
    DOCTEST_CHECK(advisor::Seen_Threat(seen) == advisor::SEEN_THREAT_MISSILES);
    seen.weapon[advisor::SEEN_FIGHTER] = 60;
    DOCTEST_CHECK(advisor::Seen_Threat(seen) == advisor::SEEN_THREAT_FIGHTERS);

    seen = advisor::s_seen_ships{};
    seen.ships = 4;
    seen.weapon[advisor::SEEN_BEAM] = 40;
    seen.weapon[advisor::SEEN_TORPEDO] = 20;
    seen.weapon[advisor::SEEN_SPECIAL] = 40;
    DOCTEST_CHECK(advisor::Seen_Threat(seen) == advisor::SEEN_THREAT_BEAMS);  // 60 %
    seen.weapon[advisor::SEEN_SPECIAL] = 41;
    DOCTEST_CHECK(advisor::Seen_Threat(seen) == advisor::SEEN_THREAT_NONE);
    // Bombs do not threaten ships.
    seen.weapon[advisor::SEEN_SPECIAL] = 0;
    seen.weapon[advisor::SEEN_BOMB] = 1000;
    DOCTEST_CHECK(advisor::Seen_Threat(seen) == advisor::SEEN_THREAT_BEAMS);

    seen.missile_screen = 7;
    DOCTEST_CHECK_FALSE(advisor::Seen_Missile_Screen(seen));
    seen.missile_screen = 8;
    DOCTEST_CHECK(advisor::Seen_Missile_Screen(seen));
}

DOCTEST_TEST_CASE("counters keep their proportions at the limit and fade every ten turns") {
    s_intel_scope scope;
    s_ship_design design = Beam_Ship_(20);
    design.ship_weapon[1].type = WEAPON_NUCLEAR_MISSILE;
    design.ship_weapon[1].count = 10;
    const uint16_t beam = Damage_(WEAPON_LASER_CANNON, 20);
    const uint16_t missile = Damage_(WEAPON_NUCLEAR_MISSILE, 10);
    DOCTEST_REQUIRE(beam > 0);
    DOCTEST_REQUIRE(missile > 0);
    for (int i = 0; i < 5000; ++i) advisor::Observe_Design(1, 2, design);
    const advisor::s_seen_ships& seen = advisor::Seen_Ships(1, 2);
    DOCTEST_CHECK(seen.weapon[advisor::SEEN_BEAM] <= advisor::SEEN_LIMIT);
    DOCTEST_CHECK(seen.weapon[advisor::SEEN_BEAM] > advisor::SEEN_LIMIT / 4);
    // Shares survive saturation (within rounding of the halvings).
    const double seen_ratio = double(seen.weapon[advisor::SEEN_BEAM]) / seen.weapon[advisor::SEEN_MISSILE];
    DOCTEST_CHECK(seen_ratio == doctest::Approx(double(beam) / missile).epsilon(0.02));

    const uint16_t before = seen.weapon[advisor::SEEN_BEAM];
    advisor::Fade_Seen_Ships(35011);
    DOCTEST_CHECK(seen.weapon[advisor::SEEN_BEAM] == before);
    advisor::Fade_Seen_Ships(35010);
    DOCTEST_CHECK(seen.weapon[advisor::SEEN_BEAM] == before * 3 / 4);

    advisor::Clear_Seen_Ships();
    advisor::Observe_Design(1, 2, Beam_Ship_(1));
    DOCTEST_CHECK_FALSE(advisor::Seen_Memory_Empty());
    for (int i = 0; i < 10; ++i) advisor::Fade_Seen_Ships(35000 + 10 * i);
    DOCTEST_CHECK(advisor::Seen_Memory_Empty());  // small memories fade out completely
}

DOCTEST_TEST_CASE("both sides of a battle remember the other side's warships") {
    s_intel_scope scope;
    test::s_game_state state;
    Setup_World_();
    MOX::NUM_SHIPS = 5;
    state.ships[0].d = Beam_Ship_(4);
    state.ships[0].owner = 1;
    state.ships[1].d = Missile_Ship_(4);
    state.ships[1].owner = 2;
    state.ships[2].d = Beam_Ship_(4);
    state.ships[2].d.ship_type = SHIP_TYPE_COLONY;  // not a warship
    state.ships[2].owner = 2;
    state.ships[3].d = Beam_Ship_(4);
    state.ships[3].owner = MAX_PLAYERS;  // a monster: neither sees nor is remembered
    const int16_t attackers[] = {0, 3};
    const int16_t defenders[] = {1, 2, 9};  // 9 is past NUM_SHIPS

    advisor::enabled = false;
    advisor::Observe_Battle(attackers, 2, defenders, 3);
    DOCTEST_CHECK(advisor::Seen_Memory_Empty());
    advisor::enabled = true;
    advisor::rules_seen_ships = false;
    advisor::Observe_Battle(attackers, 2, defenders, 3);
    DOCTEST_CHECK(advisor::Seen_Memory_Empty());

    advisor::rules_seen_ships = true;
    advisor::Observe_Battle(attackers, 2, defenders, 3);
    DOCTEST_CHECK(advisor::Seen_Ships(1, 2).ships == 1);
    DOCTEST_CHECK(advisor::Seen_Ships(1, 2).weapon[advisor::SEEN_MISSILE] == Damage_(WEAPON_NUCLEAR_MISSILE, 4));
    DOCTEST_CHECK(advisor::Seen_Ships(1, 2).weapon[advisor::SEEN_BEAM] == 0);
    DOCTEST_CHECK(advisor::Seen_Ships(2, 1).ships == 1);
    DOCTEST_CHECK(advisor::Seen_Ships(2, 1).weapon[advisor::SEEN_BEAM] == Damage_(WEAPON_LASER_CANNON, 4));
    DOCTEST_CHECK(advisor::Seen_Ships(1, 1).ships == 0);
}

DOCTEST_TEST_CASE("design hooks answer only for advised computer empires") {
    s_intel_scope scope;
    test::s_game_state state;
    Setup_World_();
    for (int i = 0; i < 4; ++i) {
        advisor::Observe_Design(1, 2, Missile_Ship_(4));
        advisor::Observe_Design(0, 2, Missile_Ship_(4));
    }
    s_ship_design screened = Beam_Ship_(2);
    Set_Bit_(&screened, SPECIAL_WIDE_AREA_JAMMER);
    advisor::Observe_Design(1, 3, screened);

    DOCTEST_CHECK(advisor::Design_Seen_Threat(1, 2) == advisor::SEEN_THREAT_MISSILES);
    DOCTEST_CHECK(advisor::Design_Seen_Threat(1, 3) == advisor::SEEN_THREAT_BEAMS);
    DOCTEST_CHECK(advisor::Design_Avoid_Missiles(1, 3));
    DOCTEST_CHECK_FALSE(advisor::Design_Avoid_Missiles(1, 2));
    DOCTEST_CHECK(advisor::Design_Seen_Threat(0, 2) == advisor::SEEN_THREAT_NONE);  // human
    DOCTEST_CHECK(advisor::Design_Seen_Threat(1, -1) == advisor::SEEN_THREAT_NONE);
    DOCTEST_CHECK(advisor::Design_Seen_Threat(1, MAX_PLAYERS) == advisor::SEEN_THREAT_NONE);

    advisor::active_players = 255 & ~2;
    DOCTEST_CHECK(advisor::Design_Seen_Threat(1, 2) == advisor::SEEN_THREAT_NONE);
    DOCTEST_CHECK_FALSE(advisor::Design_Avoid_Missiles(1, 3));
    advisor::active_players = 255;
    advisor::rules_seen_ships = false;
    DOCTEST_CHECK(advisor::Design_Seen_Threat(1, 2) == advisor::SEEN_THREAT_NONE);
    advisor::rules_seen_ships = true;
    advisor::enabled = false;
    DOCTEST_CHECK_FALSE(advisor::Design_Avoid_Missiles(1, 3));
}

DOCTEST_TEST_CASE("the intel memory payload round-trips and rejects bad input") {
    s_intel_scope scope;
    advisor::Observe_Design(1, 2, Missile_Ship_(3));
    advisor::Observe_Design(5, 7, Beam_Ship_(6));
    const advisor::s_seen_ships expected_12 = advisor::Seen_Ships(1, 2);
    const advisor::s_seen_ships expected_57 = advisor::Seen_Ships(5, 7);

    std::vector<uint8_t> buffer(advisor::Seen_Save_Size());
    SERIAL::Writer_ writer(buffer.data(), static_cast<uint32_t>(buffer.size()), "test overflow");
    advisor::Write_Seen_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == buffer.size());
    DOCTEST_CHECK(advisor::Seen_Save_Size() == 2 + 8 * 8 * 8 * 2);

    advisor::Clear_Seen_Ships();
    auto parsed = std::make_unique<advisor::s_seen_ships[]>(MAX_PLAYERS * MAX_PLAYERS);
    {
        SERIAL::Reader_ reader(buffer.data(), static_cast<uint32_t>(buffer.size()), "test underflow", true);
        std::string error;
        DOCTEST_REQUIRE(advisor::Read_Seen_Save(&reader, parsed.get(), &error));
        DOCTEST_CHECK(advisor::Seen_Memory_Empty());  // parsing alone changes nothing
    }
    advisor::Commit_Seen_Save(parsed.get());
    DOCTEST_CHECK(advisor::Seen_Ships(1, 2).weapon[advisor::SEEN_MISSILE] == expected_12.weapon[advisor::SEEN_MISSILE]);
    DOCTEST_CHECK(advisor::Seen_Ships(1, 2).ships == expected_12.ships);
    DOCTEST_CHECK(advisor::Seen_Ships(5, 7).weapon[advisor::SEEN_BEAM] == expected_57.weapon[advisor::SEEN_BEAM]);

    std::string error;
    {
        std::vector<uint8_t> bad = buffer;
        bad[0] = 2;  // payload version
        SERIAL::Reader_ reader(bad.data(), static_cast<uint32_t>(bad.size()), "test underflow", true);
        DOCTEST_CHECK_FALSE(advisor::Read_Seen_Save(&reader, parsed.get(), &error));
    }
    {
        SERIAL::Reader_ reader(buffer.data(), static_cast<uint32_t>(buffer.size() - 2), "test underflow", true);
        DOCTEST_CHECK_FALSE(advisor::Read_Seen_Save(&reader, parsed.get(), &error));
    }
    {
        std::vector<uint8_t> bad = buffer;
        bad[2] = 0xFF;  // first counter 0xFFFF, above SEEN_LIMIT
        bad[3] = 0xFF;
        SERIAL::Reader_ reader(bad.data(), static_cast<uint32_t>(bad.size()), "test underflow", true);
        DOCTEST_CHECK_FALSE(advisor::Read_Seen_Save(&reader, parsed.get(), &error));
    }
}

} // DOCTEST_TEST_SUITE
