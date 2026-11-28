#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "../fixtures/texts_fixture.h"
#include "game/choices.h"
#include "game/dice.h"
#include "game/events.h"
#include "game/intrigue.h"
#include "game/opinion.h"
#include "game/serialization.h"
#include "game/techdata.h"
#include "game/world_events.h"

#include <memory>
#include <string>

namespace {
    struct s_events_scope {
        test::s_saved_value<bool> enabled{world_events::enabled};
        test::s_saved_value<int32_t> galaxy{world_events::galaxy_chance};
        test::s_saved_value<int32_t> empire{world_events::empire_chance};
        test::s_saved_value<bool> fate{intrigue::spy_fate};

        s_events_scope() {
            world_events::Clear();
            intrigue::Clear();
            world_events::enabled = true;
            world_events::galaxy_chance = 0;  // Turn starts nothing by chance in these tests
            world_events::empire_chance = 0;
            intrigue::spy_fate = true;
        }
        ~s_events_scope() {
            world_events::Clear();
            intrigue::Clear();
        }
    };

    // Two empires; player 0 holds colonies 0 (the capital, 8 people, ocean) and 1 (4 people).
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 2;
        MOX::NUM_COLONIES = 2;
        MOX::stardate = 35001;
        for (int i = 0; i < 2; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            for (int j = 0; j < 2; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
        state->colonies[0].owner = 0;
        state->colonies[0].n_pops = 8;
        state->colonies[0].planet = 0;
        state->colonies[0].buildings[BUILDING_CAPITOL] = 1;
        state->colonies[1].owner = 0;
        state->colonies[1].n_pops = 4;
        state->colonies[1].planet = 1;
        state->planets[0].climate = PLANET_CLIMATE_OCEAN;
        state->planets[0].colony_index = 0;
        state->planets[0].mineral_class = MINERAL_RESOURCE_ABUNDANT;
        state->planets[1].climate = PLANET_CLIMATE_DESERT;
        state->planets[1].colony_index = 1;
        state->planets[1].mineral_class = MINERAL_RESOURCE_POOR;
    }
} // namespace

DOCTEST_TEST_SUITE("new events") {

DOCTEST_TEST_CASE("a golden age pays a fifth of the surplus for 15 turns; one galaxy modifier at a time") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].surplus_bc = 50;
    DOCTEST_REQUIRE(world_events::Start_Galaxy_Event(world_events::KIND_GOLDEN_AGE));
    DOCTEST_CHECK_FALSE(world_events::Start_Galaxy_Event(world_events::KIND_TOURIST_BOOM));
    world_events::Turn();
    DOCTEST_CHECK(state.players[0].bc == 10);
    MOX::stardate = 35001 + world_events::GALAXY_MODIFIER_TURNS;
    world_events::Turn();
    DOCTEST_CHECK(state.players[0].bc == 10);
    DOCTEST_CHECK(world_events::Find(world_events::KIND_GOLDEN_AGE, -1) == nullptr);
}

DOCTEST_TEST_CASE("race modifiers: fair wind, duel of honour, warm current, clutch; they run out") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_FAIR_WIND));
    DOCTEST_CHECK_FALSE(world_events::Start_Race_Event(0, world_events::RACE_FAIR_WIND));  // already blowing
    DOCTEST_CHECK(world_events::Speed_Bonus(0) == 1);
    DOCTEST_CHECK(world_events::Speed_Bonus(1) == 0);
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_DUEL_OF_HONOUR));
    DOCTEST_CHECK(world_events::Morale_Bonus(&state.colonies[1]) == 1);
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_WARM_CURRENT));
    DOCTEST_CHECK(world_events::Food2_Bonus(&state.colonies[0]) == 2);
    DOCTEST_CHECK(world_events::Food2_Bonus(&state.colonies[1]) == 0);  // not an ocean world
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_CLUTCH));
    DOCTEST_CHECK(world_events::Population_Boom(0));
    DOCTEST_CHECK(world_events::Population_Boom(1));
    MOX::stardate = 35001 + world_events::MODIFIER_TURNS;
    DOCTEST_CHECK(world_events::Speed_Bonus(0) == 0);
    DOCTEST_CHECK_FALSE(world_events::Population_Boom(0));
    world_events::enabled = false;
    MOX::stardate = 35002;
    DOCTEST_CHECK(world_events::Speed_Bonus(0) == 0);
}

DOCTEST_TEST_CASE("one-off race events: bargain, network failure, plate shift, prophecy, double agent") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_GOOD_BARGAIN));
    DOCTEST_CHECK(state.players[0].bc == 50);
    state.players[0].research_accumulated = 100;
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_NETWORK_FAILURE));
    DOCTEST_CHECK((state.players[0].research_accumulated == 50 || state.players[0].research_accumulated == 200));
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_PLATE_SHIFT));
    DOCTEST_CHECK(state.planets[0].mineral_class == MINERAL_RESOURCE_RICH);  // the most populous colony
    MOX::NUM_STARS = 3;
    state.planets[0].star_index = 0;
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_PROPHECY));
    DOCTEST_CHECK((state.stars[1].visited & 1) != 0);
    DOCTEST_CHECK((state.stars[2].visited & 1) != 0);
    DOCTEST_CHECK_FALSE(world_events::Start_Race_Event(0, world_events::RACE_DOUBLE_AGENT));  // no spies here
    state.players[1].spies[0] = 2;
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_DOUBLE_AGENT));
    DOCTEST_CHECK(intrigue::Has_Double_Agent(0, 1));
}

DOCTEST_TEST_CASE("bankruptcy: the scale grows in debt, sells a building at 75, recovers out of debt") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.colonies[0].buildings[BUILDING_HYDROPONIC_FARM] = 1;
    state.players[0].bc = -10;
    for (int i = 0; i < 7; ++i) {
        MOX::stardate = 35001 + i;
        world_events::Turn();
    }
    const world_events::s_entry* scale = world_events::Find(world_events::KIND_BANKRUPTCY, 0);
    DOCTEST_REQUIRE(scale != nullptr);
    DOCTEST_CHECK(scale->value == 70);
    MOX::stardate = 35008;
    world_events::Turn();
    DOCTEST_CHECK(state.colonies[0].buildings[BUILDING_HYDROPONIC_FARM] == 0);
    DOCTEST_CHECK(state.players[0].bc > -10);
    state.players[0].bc = 100;
    MOX::stardate = 35009;
    world_events::Turn();
    DOCTEST_CHECK(world_events::Find(world_events::KIND_BANKRUPTCY, 0)->value == 75);
}

DOCTEST_TEST_CASE("a mutiny lowers morale and stability for 10 turns; never at the capital") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].eliminated = 1;  // nobody to lead the rebels: the unrest alone
    DOCTEST_REQUIRE(world_events::Start_Empire_Event(world_events::KIND_MUTINY, 0));
    DOCTEST_CHECK(world_events::Unrest(1) == world_events::MUTINY_UNREST);
    DOCTEST_CHECK(world_events::Unrest(0) == 0);
    DOCTEST_CHECK(world_events::Morale_Bonus(&state.colonies[1]) == world_events::MUTINY_MORALE);
    DOCTEST_CHECK_FALSE(world_events::Start_Empire_Event(world_events::KIND_MUTINY, 0));  // colony 1 already
    MOX::stardate = 35001 + world_events::MODIFIER_TURNS;
    DOCTEST_CHECK(world_events::Unrest(1) == 0);
}

DOCTEST_TEST_CASE("a warm place: experience now, the leader keeps its post for 20 turns") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK_FALSE(world_events::Start_Empire_Event(world_events::KIND_WARM_PLACE, 0));  // no governor
    s_leader_data& leader = state.leaders[3];
    leader.player_index = 0;
    leader.type = 1;    // colony leader
    leader.status = 1;  // assigned
    leader.xp = 10;
    DOCTEST_REQUIRE(world_events::Start_Empire_Event(world_events::KIND_WARM_PLACE, 0));
    DOCTEST_CHECK(leader.xp == 10 + world_events::WARM_PLACE_EXPERIENCE);
    DOCTEST_CHECK(world_events::Leader_Locked(3));
    DOCTEST_CHECK_FALSE(world_events::Leader_Locked(2));
    MOX::stardate = 35001 + world_events::WARM_PLACE_TURNS;
    DOCTEST_CHECK_FALSE(world_events::Leader_Locked(3));
}

DOCTEST_TEST_CASE("an epidemic: research cures it, the antidote ends it, it spreads to the nearest colony") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::NUM_STARS = 2;
    state.planets[0].star_index = 0;
    state.planets[1].star_index = 1;
    state.colonies[1].production[ECON_RESEARCH] = 30;
    state.colonies[1].n_pops = 2;
    // Only colony 1 can fall ill: colony 0 is not populated for this test.
    state.colonies[0].n_pops = 0;
    DOCTEST_REQUIRE(world_events::Start_Empire_Event(world_events::KIND_EPIDEMIC, 0));
    DOCTEST_CHECK(world_events::Epidemic(1));
    const world_events::s_entry* entry = world_events::Find(world_events::KIND_EPIDEMIC, 0);
    DOCTEST_REQUIRE(entry != nullptr);
    DOCTEST_CHECK(entry->value == world_events::EPIDEMIC_CURE_BASE + 20);
    world_events::Turn();  // 70 - 30
    world_events::Turn();  // 40 - 30
    DOCTEST_CHECK(world_events::Epidemic(1));
    world_events::Turn();  // cured
    DOCTEST_CHECK_FALSE(world_events::Epidemic(1));
    state.players[0].tech_applications[TECH_APP_UNIVERSAL_ANTIDOTE] = TECH_RESEARCH_STATUS_RESEARCHED;
    DOCTEST_CHECK_FALSE(world_events::Start_Empire_Event(world_events::KIND_EPIDEMIC, 0));
    state.players[0].tech_applications[TECH_APP_UNIVERSAL_ANTIDOTE] = 0;
    // Spreading: colony 1 ill, colony 0 healthy and populated: it catches it.
    state.colonies[0].n_pops = 8;
    DOCTEST_REQUIRE(world_events::Start_Empire_Event(world_events::KIND_EPIDEMIC, 0));
    const int16_t ill = world_events::Epidemic(1) ? 1 : 0;
    DOCTEST_REQUIRE(world_events::Spread_Epidemic(ill));
    DOCTEST_CHECK((world_events::Epidemic(0) && world_events::Epidemic(1)));
    DOCTEST_CHECK_FALSE(world_events::Spread_Epidemic(ill));  // no healthy colony left
}

DOCTEST_TEST_CASE("bounties: computers at war put a price on the enemy; pirates raid it, then sell protection") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].treaty[1] = state.players[1].treaty[0] = 4;
    state.players[0].bc = 2000;
    state.players[1].bc = 500;
    state.players[1].n_freighters = 5;
    MOX::stardate = 35002;
    world_events::Turn();
    DOCTEST_CHECK(state.players[0].bc == 1800);
    DOCTEST_CHECK(state.players[1].bc == 450);
    DOCTEST_REQUIRE(world_events::Find(world_events::KIND_BOUNTY, 1) != nullptr);
    DOCTEST_CHECK(world_events::Find(world_events::KIND_BOUNTY, 1)->value == 200);
    DOCTEST_REQUIRE(world_events::Find(world_events::KIND_BOUNTY, 0) != nullptr);
    DOCTEST_CHECK(world_events::Find(world_events::KIND_BOUNTY, 0)->value == 50);
    MOX::stardate = 35007;
    world_events::Turn();
    DOCTEST_CHECK(world_events::Find(world_events::KIND_BOUNTY, 1) == nullptr);  // the highest price is raided
    DOCTEST_CHECK(world_events::Find(world_events::KIND_BOUNTY, 0) != nullptr);  // 50 is too little
    DOCTEST_CHECK(state.players[1].n_freighters == 3);
    DOCTEST_REQUIRE(world_events::Find(world_events::KIND_PROTECTION, 1) != nullptr);
    const int32_t before = state.players[1].bc;
    MOX::stardate = 35008;
    world_events::Turn();
    DOCTEST_CHECK(state.players[1].bc == before - world_events::PROTECTION_FEE);
    state.players[1].bc = 5;  // cannot pay: the raids return
    MOX::stardate = 35009;
    world_events::Turn();
    DOCTEST_CHECK(world_events::Find(world_events::KIND_PROTECTION, 1) == nullptr);
}

DOCTEST_TEST_CASE("off: nothing starts, nothing is kept") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    world_events::enabled = false;
    state.players[0].bc = -10;
    DOCTEST_CHECK_FALSE(world_events::Start_Galaxy_Event(world_events::KIND_GOLDEN_AGE));
    DOCTEST_CHECK_FALSE(world_events::Start_Race_Event(0, world_events::RACE_FAIR_WIND));
    world_events::Turn();
    DOCTEST_CHECK(world_events::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(world_events::Start_Race_Event(0, world_events::RACE_FAIR_WIND));
    auto buffer = std::make_unique<uint8_t[]>(world_events::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), world_events::Save_Size(), "test overflow");
    world_events::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == world_events::Save_Size());
    world_events::Clear();
    SERIAL::Reader_ reader(buffer.get(), world_events::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(world_events::Parse_Save(&reader, &error));
    DOCTEST_CHECK(world_events::Empty());
    world_events::Commit_Save();
    DOCTEST_CHECK(world_events::Speed_Bonus(0) == 1);
    buffer[2] = 99;  // an unknown kind in the first entry
    SERIAL::Reader_ bad(buffer.get(), world_events::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(world_events::Parse_Save(&bad, &error));
    DOCTEST_CHECK(world_events::Speed_Bonus(0) == 1);
}

namespace {
    // Hired pirates work without the other new events.
    struct s_pirates_scope : s_events_scope {
        test::s_saved_value<bool> hired{world_events::hired_pirates};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<uint8_t> roll_kind{dice::roll_kind};

        s_pirates_scope() {
            opinion::Clear();
            world_events::enabled = false;
            world_events::hired_pirates = true;
            opinion::enabled = true;
            dice::roll_kind = dice::ROLL_TWO_D10;
        }
        ~s_pirates_scope() {
            opinion::Clear();
        }
    };

    const int32_t PIRATE_TRIUMPH[2] = {10, 10};
    const int32_t PIRATE_CATASTROPHE[2] = {1, 1};

    // Player 0's colonies sit at stars 1 (8 people) and 2 (4 people); player 1 has spies there. (The
    // fixture's empty ship records all sit at star 0 and would count as warships there.)
    void Pirate_Setup_(test::s_game_state* state) {
        Setup_(state);
        MOX::NUM_STARS = 3;
        state->planets[0].star_index = 1;
        state->planets[1].star_index = 2;
        state->stars[1].has_colony = 0x1;
        state->stars[2].has_colony = 0x1;
        state->players[1].bc = 1000;
        state->players[1].spies[0] = 2;
        state->players[0].bc = 1000;
        state->players[0].n_freighters = 10;
    }
} // namespace

DOCTEST_TEST_CASE("one processing: pirates at a star take a freighter from each empire there; a raid stops at zero") {
    s_pirates_scope scope;
    test::s_game_state state;
    Pirate_Setup_(&state);
    state.stars[1].has_colony = 0x3;
    state.players[1].n_freighters = 0;
    int16_t strength = 40;
    DOCTEST_CHECK(EVENTS::Pirates_At_Star_Turn(1, &strength, 40));  // full strength: a sure strike; no warships
    DOCTEST_CHECK(state.players[0].n_freighters == 9);
    DOCTEST_CHECK(state.players[1].n_freighters == 0);
    DOCTEST_CHECK(strength == 40);
    state.players[1].bc = 30;
    EVENTS::Pirate_Raid_Loss(1, 50);
    DOCTEST_CHECK(state.players[1].bc == 0);
}

DOCTEST_TEST_CASE("hired pirates: a paid raid costs the target; an exposed hire is remembered") {
    s_pirates_scope scope;
    test::s_game_state state;
    Pirate_Setup_(&state);
    state.players[1].spies[0] = 0;
    DOCTEST_CHECK(world_events::Hire_Pirates_With_Dice(1, 0, world_events::HIRE_RAID, PIRATE_TRIUMPH) == world_events::HIRE_NOT_POSSIBLE);
    DOCTEST_CHECK(state.players[1].bc == 1000);  // no spies, no known pirate cache: nothing paid
    state.stars[2].system_special = SYSTEM_SPECIAL_PIRATE_CACHE;
    state.stars[2].visited = 0x2;  // a known cache is enough
    DOCTEST_REQUIRE(world_events::Hire_Pirates_With_Dice(1, 0, world_events::HIRE_RAID, PIRATE_TRIUMPH) == world_events::HIRE_DONE);
    DOCTEST_CHECK(state.players[1].bc == 1000 - world_events::RAID_PRICE);
    DOCTEST_CHECK(state.players[0].bc < 1000);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_RAIDED_US) == 0);
    state.players[1].spies[0] = 2;
    DOCTEST_CHECK(world_events::Hire_Pirates_With_Dice(1, 0, world_events::HIRE_RAID, PIRATE_CATASTROPHE) == world_events::HIRE_EXPOSED);
    DOCTEST_CHECK(state.players[1].spies[0] == 1);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_RAIDED_US) == opinion::RAIDED_OPINION);
}

DOCTEST_TEST_CASE("hired pirates: armed pirates sit at the target's biggest colony, harass its trade, leave in time") {
    s_pirates_scope scope;
    test::s_game_state state;
    Pirate_Setup_(&state);
    DOCTEST_REQUIRE(world_events::Hire_Pirates_With_Dice(1, 0, world_events::HIRE_ARM, PIRATE_TRIUMPH) == world_events::HIRE_DONE);
    DOCTEST_CHECK(state.players[1].bc == 1000 - 1000 * world_events::ARM_PERCENT / 100);
    const world_events::s_entry* pirates = world_events::Find(world_events::KIND_HIRED_PIRATES, 0);
    DOCTEST_REQUIRE(pirates != nullptr);
    DOCTEST_CHECK(pirates->target == 1);
    DOCTEST_CHECK(pirates->hirer == 1);
    DOCTEST_CHECK(pirates->value == pirates->extra);
    DOCTEST_CHECK(pirates->value >= 100);  // the classic strength and half the price
    DOCTEST_CHECK(world_events::Pirates_Harass(0));
    DOCTEST_CHECK_FALSE(world_events::Pirates_Harass(1));
    DOCTEST_CHECK(EVENTS::Event_Check_Pirate_Activity(0) == 1);  // trade.cpp halves the treaties' goal
    world_events::Turn();  // full strength: a sure strike
    DOCTEST_CHECK(state.players[0].n_freighters == 9);
    MOX::stardate += world_events::HIRED_PIRATE_TURNS;
    world_events::Turn();
    int32_t count = 0;
    const world_events::s_entry* entries = world_events::Entries(&count);
    for (int32_t i = 0; i < count; ++i) DOCTEST_CHECK(entries[i].kind != world_events::KIND_HIRED_PIRATES);
    DOCTEST_CHECK_FALSE(world_events::Pirates_Harass(0));
}

DOCTEST_TEST_CASE("hired pirates off: nothing is hired, nothing harasses") {
    s_pirates_scope scope;
    test::s_game_state state;
    Pirate_Setup_(&state);
    world_events::hired_pirates = false;
    DOCTEST_CHECK(world_events::Hire_Pirates_With_Dice(1, 0, world_events::HIRE_ARM, PIRATE_TRIUMPH) == world_events::HIRE_NOT_POSSIBLE);
    DOCTEST_CHECK(state.players[1].bc == 1000);
    DOCTEST_CHECK_FALSE(world_events::Pirates_Harass(0));
    DOCTEST_CHECK(EVENTS::Event_Check_Pirate_Activity(0) == 0);
}

DOCTEST_TEST_CASE("the events payload v2 keeps the hirer; a v1 payload still loads") {
    s_pirates_scope scope;
    test::s_game_state state;
    Pirate_Setup_(&state);
    world_events::s_entry entry;
    entry.kind = world_events::KIND_HIRED_PIRATES;
    entry.player = 0;
    entry.target = 1;
    entry.started = 35001;
    entry.ends = 35031;
    entry.value = 50;
    entry.extra = 60;
    entry.hirer = 1;
    DOCTEST_REQUIRE(world_events::Add(entry));
    const uint32_t size = world_events::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    world_events::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    world_events::Clear();
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(world_events::Parse_Save(&reader, &error));
    world_events::Commit_Save();
    const world_events::s_entry* back = world_events::Find(world_events::KIND_HIRED_PIRATES, 0);
    DOCTEST_REQUIRE(back != nullptr);
    DOCTEST_CHECK(back->hirer == 1);
    DOCTEST_CHECK(back->extra == 60);

    // Version 1: 14 bytes an entry, no hirer; a golden age in the first slot.
    const uint32_t v1_size = 2 + world_events::MAX_ENTRIES * 14;
    auto v1 = std::make_unique<uint8_t[]>(v1_size);
    SERIAL::Writer_ old_writer(v1.get(), v1_size, "test overflow");
    old_writer.Write_Uint16(1);
    for (int32_t i = 0; i < world_events::MAX_ENTRIES; ++i) {
        old_writer.Write_Uint8(i == 0 ? world_events::KIND_GOLDEN_AGE : world_events::KIND_NONE);
        old_writer.Write_Int8(-1);
        old_writer.Write_Int16(-1);
        old_writer.Write_Int32(35001);
        old_writer.Write_Int32(0);
        old_writer.Write_Int16(0);
    }
    DOCTEST_REQUIRE(old_writer.Length() == v1_size);
    SERIAL::Reader_ old_reader(v1.get(), v1_size, "test underflow", true);
    DOCTEST_REQUIRE(world_events::Parse_Save(&old_reader, &error));
    world_events::Commit_Save();
    world_events::enabled = true;  // a golden age is one of the other new events
    const world_events::s_entry* golden = world_events::Find(world_events::KIND_GOLDEN_AGE, -1);
    DOCTEST_REQUIRE(golden != nullptr);
    DOCTEST_CHECK(golden->hirer == -1);
    DOCTEST_CHECK(world_events::Find(world_events::KIND_HIRED_PIRATES, 0) == nullptr);
}

DOCTEST_TEST_CASE("star flare: no ship leaves the star and the colonies there lose the turn's industry; it ends") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::NUM_STARS = 3;
    state.planets[0].star_index = 0;
    state.planets[1].star_index = 1;
    state.colonies[0].production[ECON_INDUSTRY] = 10;
    state.colonies[0].production_spent = 25;
    state.colonies[1].production[ECON_INDUSTRY] = 10;
    state.colonies[1].production_spent = 25;
    DOCTEST_REQUIRE(world_events::Start_Galaxy_Event(world_events::KIND_STAR_FLARE));
    const world_events::s_entry* flare = nullptr;
    int32_t count = 0;
    const world_events::s_entry* all = world_events::Entries(&count);
    for (int32_t i = 0; i < count; ++i) {
        if (all[i].kind == world_events::KIND_STAR_FLARE) flare = &all[i];
    }
    DOCTEST_REQUIRE(flare != nullptr);
    const int16_t star = flare->target;
    DOCTEST_REQUIRE((star == 0 || star == 1));
    const int32_t turns = flare->ends - flare->started;
    DOCTEST_CHECK(turns >= world_events::FLARE_MIN_TURNS);
    DOCTEST_CHECK(turns <= world_events::FLARE_MIN_TURNS + world_events::FLARE_SPAN_TURNS);
    DOCTEST_CHECK(world_events::Star_Grounded(star));
    DOCTEST_CHECK_FALSE(world_events::Star_Grounded(2));
    DOCTEST_CHECK_FALSE(world_events::Star_Dark(star));  // a flare does not black out the colony
    world_events::Turn();
    DOCTEST_CHECK(state.colonies[star].production_spent == 15);
    DOCTEST_CHECK(state.colonies[1 - star].production_spent == 25);
    DOCTEST_CHECK(world_events::Start_Galaxy_Event(world_events::KIND_STAR_FLARE));  // the other star
    DOCTEST_CHECK_FALSE(world_events::Start_Galaxy_Event(world_events::KIND_STAR_FLARE));  // at most one flare a star
    MOX::stardate = 35001 + world_events::FLARE_MIN_TURNS + world_events::FLARE_SPAN_TURNS;
    world_events::Turn();
    DOCTEST_CHECK_FALSE(world_events::Star_Grounded(star));
}

DOCTEST_TEST_CASE("system EMP: the star is dark and grounded, then it ends") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::NUM_STARS = 3;
    state.planets[0].star_index = 0;
    state.planets[1].star_index = 0;
    DOCTEST_REQUIRE(world_events::Start_Galaxy_Event(world_events::KIND_SYSTEM_EMP));
    DOCTEST_CHECK(world_events::Star_Dark(0));
    DOCTEST_CHECK(world_events::Star_Grounded(0));
    DOCTEST_CHECK_FALSE(world_events::Star_Dark(1));
    DOCTEST_CHECK_FALSE(world_events::Start_Galaxy_Event(world_events::KIND_SYSTEM_EMP));  // only star 0 has colonies
    MOX::stardate = 35001 + world_events::EMP_MIN_TURNS + world_events::EMP_SPAN_TURNS;
    world_events::Turn();
    DOCTEST_CHECK_FALSE(world_events::Star_Dark(0));
    DOCTEST_CHECK_FALSE(world_events::Star_Grounded(0));
}

DOCTEST_TEST_CASE("galactic slowdown cuts ship speed by a quarter, sensor failure halves scanner range; one at a time; neutral when off") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK(world_events::Scaled_Speed(8) == 8);
    DOCTEST_CHECK(world_events::Scanner_Range(9) == 9);
    DOCTEST_REQUIRE(world_events::Start_Galaxy_Event(world_events::KIND_GALACTIC_SLOWDOWN));
    DOCTEST_CHECK_FALSE(world_events::Start_Galaxy_Event(world_events::KIND_SENSOR_FAILURE));
    DOCTEST_CHECK_FALSE(world_events::Start_Galaxy_Event(world_events::KIND_GOLDEN_AGE));
    DOCTEST_CHECK(world_events::Scaled_Speed(8) == 6);
    DOCTEST_CHECK(world_events::Scaled_Speed(1) == 1);
    DOCTEST_CHECK(world_events::Scanner_Range(9) == 9);
    world_events::enabled = false;
    DOCTEST_CHECK(world_events::Scaled_Speed(8) == 8);
    world_events::enabled = true;
    MOX::stardate = 35001 + world_events::SLOWDOWN_MIN_TURNS + world_events::SLOWDOWN_SPAN_TURNS;
    world_events::Turn();
    DOCTEST_CHECK(world_events::Scaled_Speed(8) == 8);
    DOCTEST_REQUIRE(world_events::Start_Galaxy_Event(world_events::KIND_SENSOR_FAILURE));
    DOCTEST_CHECK(world_events::Scanner_Range(9) == 4);
    DOCTEST_CHECK(world_events::Scaled_Speed(8) == 8);
    MOX::stardate += world_events::SENSOR_MIN_TURNS + world_events::SENSOR_SPAN_TURNS;
    world_events::Turn();
    DOCTEST_CHECK(world_events::Scanner_Range(9) == 9);
}

DOCTEST_TEST_CASE("a comet strikes a colony of the empire and leaves a richer deposit") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    const int32_t before = state.planets[0].mineral_class + state.planets[1].mineral_class;
    DOCTEST_REQUIRE(world_events::Start_Empire_Event(world_events::KIND_COMET_ORE, 0));
    DOCTEST_CHECK(state.planets[0].mineral_class + state.planets[1].mineral_class == before + 1);
    DOCTEST_CHECK_FALSE(world_events::Start_Empire_Event(world_events::KIND_COMET_ORE, 1));  // no colony
    world_events::enabled = false;
    DOCTEST_CHECK_FALSE(world_events::Start_Empire_Event(world_events::KIND_COMET_ORE, 0));
}

DOCTEST_TEST_CASE("the new modifier kinds round-trip in the v2 payload") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::NUM_STARS = 3;
    state.planets[0].star_index = 0;
    state.planets[1].star_index = 1;
    DOCTEST_REQUIRE(world_events::Start_Galaxy_Event(world_events::KIND_STAR_FLARE));
    DOCTEST_REQUIRE(world_events::Start_Galaxy_Event(world_events::KIND_SYSTEM_EMP));
    DOCTEST_REQUIRE(world_events::Start_Galaxy_Event(world_events::KIND_GALACTIC_SLOWDOWN));
    const uint32_t size = world_events::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    world_events::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    world_events::Clear();
    DOCTEST_CHECK(world_events::Empty());
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(world_events::Parse_Save(&reader, &error));
    world_events::Commit_Save();
    DOCTEST_CHECK(world_events::Find(world_events::KIND_GALACTIC_SLOWDOWN, -1) != nullptr);
    DOCTEST_CHECK((world_events::Star_Grounded(0) || world_events::Star_Grounded(1)));
    DOCTEST_CHECK((world_events::Star_Dark(0) || world_events::Star_Dark(1)));
}

}

DOCTEST_TEST_CASE("discoverer: the first empire to explore a star is paid once per planet; nothing for stars known at load") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::NUM_STARS = 4;
    MOX::planet_count = 3;
    state.planets[0].star_index = 1;
    state.planets[1].star_index = 1;
    state.planets[2].star_index = 2;
    state.planets[2].planet_special = 5;
    state.stars[0].visited = 0x1;  // home star, known from the start
    world_events::Turn();  // the baseline: nothing is paid
    DOCTEST_CHECK(world_events::Explored_Valid());
    DOCTEST_CHECK(world_events::Star_Explored(0));
    DOCTEST_CHECK(state.players[0].bc == 0);
    state.stars[1].visited = 0x2 | 0x1;  // both arrive: the lower bit wins
    state.stars[2].visited = 0x2;
    world_events::Turn();
    DOCTEST_CHECK(state.players[0].bc == 2 * world_events::DISCOVERER_BC);
    DOCTEST_CHECK(state.players[1].bc == world_events::DISCOVERER_SPECIAL_BC);
    world_events::Turn();  // once only
    DOCTEST_CHECK(state.players[0].bc == 2 * world_events::DISCOVERER_BC);
    DOCTEST_CHECK(state.players[1].bc == world_events::DISCOVERER_SPECIAL_BC);
    state.stars[3].system_special = 3;
    state.planets[3].star_index = 3;
    MOX::planet_count = 4;
    state.stars[3].visited = 0x2;
    world_events::Turn();
    DOCTEST_CHECK(state.players[1].bc == 2 * world_events::DISCOVERER_SPECIAL_BC);
    world_events::enabled = false;
    state.stars[1].visited = 0;
    world_events::Clear();
    world_events::Turn();
    DOCTEST_CHECK(world_events::Empty());
}

DOCTEST_TEST_CASE("an old save pays no discoverer bonus for the stars already visited") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::NUM_STARS = 2;
    MOX::planet_count = 1;
    state.planets[0].star_index = 1;
    state.stars[1].visited = 0x1;
    const uint32_t v2_size = 2 + world_events::MAX_ENTRIES * 17;
    auto v2 = std::make_unique<uint8_t[]>(v2_size);
    SERIAL::Writer_ writer(v2.get(), v2_size, "test overflow");
    writer.Write_Uint16(2);
    for (int32_t i = 0; i < world_events::MAX_ENTRIES; ++i) {
        writer.Write_Uint8(i == 0 ? world_events::KIND_GOLDEN_AGE : world_events::KIND_NONE);
        writer.Write_Int8(-1);
        writer.Write_Int16(-1);
        writer.Write_Int32(35001);
        writer.Write_Int32(0);
        writer.Write_Int16(0);
        writer.Write_Int8(-1);
        writer.Write_Int16(0);
    }
    DOCTEST_REQUIRE(writer.Length() == v2_size);
    SERIAL::Reader_ reader(v2.get(), v2_size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(world_events::Parse_Save(&reader, &error));
    world_events::Commit_Save();
    DOCTEST_CHECK_FALSE(world_events::Explored_Valid());
    world_events::Turn();
    DOCTEST_CHECK(state.players[0].bc == 0);
    DOCTEST_CHECK(world_events::Star_Explored(1));
}

DOCTEST_TEST_CASE("the events payload v3 round-trips the explored stars") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::NUM_STARS = 3;
    state.stars[2].visited = 0x1;
    world_events::Turn();
    DOCTEST_REQUIRE(world_events::Star_Explored(2));
    DOCTEST_CHECK_FALSE(world_events::Empty());  // the section is written for the explored stars alone
    const uint32_t size = world_events::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    world_events::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    world_events::Clear();
    DOCTEST_CHECK_FALSE(world_events::Star_Explored(2));
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(world_events::Parse_Save(&reader, &error));
    world_events::Commit_Save();
    DOCTEST_CHECK(world_events::Explored_Valid());
    DOCTEST_CHECK(world_events::Star_Explored(2));
    DOCTEST_CHECK_FALSE(world_events::Star_Explored(1));
}

DOCTEST_TEST_CASE("ancient wreck: 1 to 10 unknown weapon or shield applications, the cheapest first, nothing already known") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;  // a human's designs only refresh their equipment
    const auto is_weapon_or_shield = [](int32_t app) {
        for (int32_t i = 0; i < WEAPON_COUNT; ++i) {
            if (TECHDATA::weapons[i].tech_app_id == app) return true;
        }
        for (int32_t i = 0; i < SHIELD_COUNT; ++i) {
            if (TECHDATA::shields[i].tech_app_id == app) return true;
        }
        return false;
    };
    int32_t pool = 0;
    int32_t known = -1;
    for (int32_t a = 1; a < TECH_APP_COUNT; ++a) {
        if (!is_weapon_or_shield(a) || !COLCALC::Player_May_Get_Tech_App(&state.players[0], a)) continue;
        ++pool;
        if (known < 0) known = a;
    }
    DOCTEST_REQUIRE(pool > 10);
    state.players[0].tech_applications[known] = TECH_RESEARCH_STATUS_RESEARCHED;
    for (int round = 0; round < 8; ++round) {
        DOCTEST_REQUIRE(world_events::Start_Empire_Event(world_events::KIND_ANCIENT_WRECK, 0));
        int32_t got = 0;
        for (int32_t a = 1; a < TECH_APP_COUNT; ++a) {
            if (state.players[0].tech_applications[a] != TECH_RESEARCH_STATUS_RESEARCHED) continue;
            DOCTEST_CHECK(is_weapon_or_shield(a));
            ++got;
        }
        DOCTEST_CHECK(got >= 2);  // the known one and at least one new
        for (int32_t a = 1; a < TECH_APP_COUNT; ++a) state.players[0].tech_applications[a] = 0;
        state.players[0].tech_applications[known] = TECH_RESEARCH_STATUS_RESEARCHED;
    }
    // One wreck gives at most ten.
    DOCTEST_REQUIRE(world_events::Start_Empire_Event(world_events::KIND_ANCIENT_WRECK, 0));
    int32_t added = 0;
    for (int32_t a = 1; a < TECH_APP_COUNT; ++a) added += state.players[0].tech_applications[a] == TECH_RESEARCH_STATUS_RESEARCHED ? 1 : 0;
    DOCTEST_CHECK(added - 1 <= world_events::WRECK_MAX_APPS);
    world_events::enabled = false;
    DOCTEST_CHECK_FALSE(world_events::Start_Empire_Event(world_events::KIND_ANCIENT_WRECK, 0));
}

DOCTEST_TEST_CASE("a mutiny puts the colony through the engine's rebellion; the rebels may take it") {
    s_events_scope scope;
    test::s_game_state state;
    Setup_(&state);
    int32_t taken = 0;
    for (int round = 0; round < 60 && taken == 0; ++round) {
        state.colonies[1].owner = 0;
        state.colonies[1].military[0] = 0;
        state.colonies[1].military[1] = 0;
        world_events::Clear();
        DOCTEST_REQUIRE(world_events::Start_Empire_Event(world_events::KIND_MUTINY, 0));
        if (state.colonies[1].owner == 1) {
            ++taken;
            DOCTEST_CHECK(world_events::Unrest(1) == 0);  // the unrest left with the old owner
        } else {
            DOCTEST_CHECK(world_events::Unrest(1) == world_events::MUTINY_UNREST);
        }
    }
    DOCTEST_CHECK(taken > 0);
}

// ---- The choice window (human-ui.md section 5, slice R1, part D): a human under a bounty is offered protection.

namespace {
    struct s_protection_scope {
        test::s_saved_value<bool> on{choices::enabled};
        s_protection_scope() {
            choices::Clear();
            choices::enabled = true;
            test::Load_Base_Texts();
        }
        ~s_protection_scope() {
            choices::Clear();
        }
    };

    // The bounty of the test above, with player 1 the human; the pirates raid at stardate 35007.
    void Raid_The_Human_(test::s_game_state* state) {
        Setup_(state);
        state->players[1].objectives = PLAYER_OBJECTIVE_HUMAN;
        state->players[0].treaty[1] = state->players[1].treaty[0] = 4;
        state->players[0].bc = 2000;
        state->players[1].bc = 500;
        state->players[1].n_freighters = 5;
        MOX::stardate = 35002;
        world_events::Turn();
        MOX::stardate = 35007;
        world_events::Turn();
    }
}

DOCTEST_TEST_CASE("choices: a raided human is offered protection and pays only when it agrees") {
    for (int option = 0; option < 2; ++option) {
        DOCTEST_CAPTURE(option);
        s_events_scope scope;
        s_protection_scope window;
        test::s_game_state state;
        Raid_The_Human_(&state);
        const int32_t after_raid = state.players[1].bc;
        DOCTEST_CHECK(after_raid < 500);  // the raid, as for a computer
        DOCTEST_CHECK(state.players[1].n_freighters == 3);
        DOCTEST_CHECK(world_events::Find(world_events::KIND_PROTECTION, 1) == nullptr);  // nothing agreed yet
        DOCTEST_REQUIRE(choices::Count(1) == 1);
        const choices::s_choice c = choices::State()->choice[0];
        DOCTEST_CHECK(c.module == choices::MODULE_WORLD_EVENTS);
        choices::s_text text;
        DOCTEST_REQUIRE(world_events::Choice_Text(c, &text));
        DOCTEST_CHECK(text.count == 2);
        DOCTEST_CHECK(std::string(text.body).size() > 40);
        DOCTEST_REQUIRE(choices::Apply_Pending(1, option));
        const world_events::s_entry* protection = world_events::Find(world_events::KIND_PROTECTION, 1);
        if (option == 0) {
            DOCTEST_REQUIRE(protection != nullptr);
            DOCTEST_CHECK(protection->value == world_events::PROTECTION_FEE);
            DOCTEST_CHECK(protection->ends == 35007 + world_events::PROTECTION_TURNS);
            MOX::stardate = 35008;
            world_events::Turn();
            DOCTEST_CHECK(state.players[1].bc == after_raid - world_events::PROTECTION_FEE);  // paid as a computer pays
        } else {
            DOCTEST_CHECK(protection == nullptr);
        }
    }
}

DOCTEST_TEST_CASE("choices: an unaffordable protection says so and does nothing; a gone subject drops the offer") {
    s_events_scope scope;
    s_protection_scope window;
    test::s_game_state state;
    Raid_The_Human_(&state);
    DOCTEST_REQUIRE(choices::Count(1) == 1);
    const choices::s_choice c = choices::State()->choice[0];
    state.players[1].bc = world_events::PROTECTION_FEE - 1;
    choices::s_text text;
    DOCTEST_REQUIRE(world_events::Choice_Text(c, &text));
    DOCTEST_CHECK(std::string(text.option[0]).find("not enough") != std::string::npos);
    DOCTEST_REQUIRE(choices::Apply_Pending(1, 0));
    DOCTEST_CHECK(world_events::Find(world_events::KIND_PROTECTION, 1) == nullptr);
    state.players[1].bc = 500;
    world_events::s_entry protection;
    protection.kind = world_events::KIND_PROTECTION;
    protection.player = 1;
    protection.value = world_events::PROTECTION_FEE;
    DOCTEST_REQUIRE(world_events::Add(protection));
    DOCTEST_CHECK_FALSE(world_events::Choice_Text(c, &text));  // already protected
}

DOCTEST_TEST_CASE("choices: with the setting off a raided human gets neither the offer nor the fee") {
    s_events_scope scope;
    test::s_game_state state;
    test::s_saved_value<bool> on{choices::enabled};
    choices::Clear();
    choices::enabled = false;
    Raid_The_Human_(&state);
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(world_events::Find(world_events::KIND_PROTECTION, 1) == nullptr);
}

DOCTEST_TEST_CASE("choices: a raided computer pays for protection at once and is not asked") {
    s_events_scope scope;
    s_protection_scope window;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].treaty[1] = state.players[1].treaty[0] = 4;
    state.players[0].bc = 2000;
    state.players[1].bc = 500;
    MOX::stardate = 35002;
    world_events::Turn();
    MOX::stardate = 35007;
    world_events::Turn();
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(world_events::Find(world_events::KIND_PROTECTION, 1) != nullptr);
}
