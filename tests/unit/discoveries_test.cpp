#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/dice.h"
#include "game/discoveries.h"
#include "game/projects.h"
#include "game/random.h"
#include "game/serialization.h"

#include <memory>
#include <string>

namespace {
    constexpr char eel_name[] = "Eel";

    struct s_scope {
        test::s_saved_value<bool> enabled{discoveries::enabled};
        test::s_saved_value<uint8_t> roll_kind{dice::roll_kind};
        char* saved_names[20];

        s_scope() {
            for (int i = 0; i < 20; ++i) saved_names[i] = MOX::race_names[i];
            MOX::race_names[18] = const_cast<char*>(eel_name);  // the guardians' design takes its name from here
            discoveries::Clear();
            projects::Clear();
            discoveries::enabled = true;
            dice::roll_kind = dice::ROLL_TWO_D10;
        }
        ~s_scope() {
            for (int i = 0; i < 20; ++i) MOX::race_names[i] = saved_names[i];
            discoveries::Clear();
        }
    };

    constexpr int32_t TRIUMPH[2] = {10, 10};
    constexpr int32_t SUCCESS[2] = {6, 6};        // 12
    constexpr int32_t FAILURE[2] = {5, 4};        // 9
    constexpr int32_t CATASTROPHE[2] = {1, 1};
    constexpr int32_t EIGHT[2] = {4, 4};

    // Two computer empires with 1000 BC. Colony 0 of player 0 at star 0, colony 1 of player 1 at star 1 (4 people
    // each). Star 2 is neutral. Player 0 has explored stars 0 and 2, player 1 star 1 only.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 2;
        MOX::NUM_STARS = 3;
        MOX::NUM_COLONIES = 2;
        MOX::NUM_SHIPS = 0;
        MOX::stardate = 35001;
        for (int p = 0; p < 2; ++p) {
            state->players[p].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[p].personality = PLAYER_PERSONALITY_ERRATIC;
            state->players[p].bc = 1000;
            state->players[p].eliminated = 0;
            state->players[p].research_accumulated = 0;
        }
        for (int s = 0; s < 3; ++s) {
            state->stars[s].x = static_cast<int16_t>(10 * s);
            state->stars[s].y = 0;
            state->stars[s].system_special = SYSTEM_SPECIAL_NO_SPECIAL;
        }
        state->stars[0].visited = 1;
        state->stars[2].visited = 1;
        state->stars[1].visited = 2;
        for (int c = 0; c < 2; ++c) {
            state->stars[c].has_colony = static_cast<uint8_t>(1 << c);
            state->planets[c].star_index = static_cast<int16_t>(c);
            state->planets[c].colony_index = static_cast<int16_t>(c);
            state->planets[c].planet_type = PLANET_TYPE_PLANET;
            state->colonies[c].owner = static_cast<int8_t>(c);
            state->colonies[c].planet = static_cast<int16_t>(c);
            state->colonies[c].n_pops = 4;
        }
        MOX::planet_count = 2;
        for (auto& ship : state->ships) {
            ship.owner = MAX_PLAYERS;
            ship.status = 5;
            ship.location = -1;
        }
    }

    // Player 0 is human so that a granted application only refreshes designs (as in the projects test).
    void Human_(test::s_game_state* state) {
        state->players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    }

    int Monsters_At_(int star) {
        int n = 0;
        for (int i = 0; i < MOX::NUM_SHIPS; ++i) {
            n += (MOX::ship[i].owner >= MAX_PLAYERS && MOX::ship[i].status < 5 && MOX::ship[i].location == star) ? 1 : 0;
        }
        return n;
    }

    void Run_(int turns) {
        for (int i = 0; i < turns; ++i) {
            ++MOX::stardate;
            discoveries::Run_Records();
        }
    }

    bool Known_(const test::s_game_state& state, int16_t app) {
        return state.players[0].tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED;
    }

    void Seed_For_(uint32_t draw_min, uint32_t draw_max) {
        for (uint32_t seed = 1; seed < 1000000; ++seed) {
            game_random::Set_Random_Seed(seed);
            const uint32_t v = game_random::Random(100);
            if (v < draw_min || v > draw_max) continue;
            game_random::Set_Random_Seed(seed);
            return;
        }
    }
} // namespace

DOCTEST_TEST_SUITE("discoveries") {

DOCTEST_TEST_CASE("flag off: nothing starts, nothing is kept, no draw from game_random")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    discoveries::enabled = false;
    for (int k = discoveries::KIND_POWER_PLANT; k <= discoveries::KIND_LAST; ++k) {
        DOCTEST_CHECK_FALSE(discoveries::Start(static_cast<discoveries::e_kind>(k), 0));
    }
    const uint32_t seed = game_random::Get_Random_Seed();
    for (int i = 0; i < 20; ++i) discoveries::Turn();
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
    DOCTEST_CHECK(discoveries::Empty());
    DOCTEST_CHECK(discoveries::Find(discoveries::KIND_VAULT, -1, -1) == nullptr);
    DOCTEST_CHECK(state.players[0].bc == 1000);
}

DOCTEST_TEST_CASE("a computer chooses by personality, a human takes the first option")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].personality = PLAYER_PERSONALITY_PACIFIST;
    state.players[1].personality = PLAYER_PERSONALITY_AGGRESSIVE;
    DOCTEST_CHECK(discoveries::Choose(discoveries::KIND_POWER_PLANT, 0) == discoveries::OPT_POWER_TEST);
    DOCTEST_CHECK(discoveries::Choose(discoveries::KIND_POWER_PLANT, 1) == discoveries::OPT_POWER_INSTALL);
    DOCTEST_CHECK(discoveries::Choose(discoveries::KIND_SYMBIONTS, 0) == discoveries::OPT_SYMB_STUDY);
    DOCTEST_CHECK(discoveries::Choose(discoveries::KIND_SYMBIONTS, 1) == discoveries::OPT_SYMB_WAKE);
    DOCTEST_CHECK(discoveries::Choose(discoveries::KIND_NATIVES, 0) == discoveries::OPT_NAT_LEAVE);
    DOCTEST_CHECK(discoveries::Choose(discoveries::KIND_NATIVES, 1) == discoveries::OPT_NAT_WORK);
    state.players[1].personality = PLAYER_PERSONALITY_ERRATIC;
    DOCTEST_CHECK(discoveries::Choose(discoveries::KIND_POWER_PLANT, 1) == discoveries::OPT_POWER_DISASSEMBLE);
    DOCTEST_CHECK(discoveries::Choose(discoveries::KIND_SYMBIONTS, 1) == discoveries::OPT_SYMB_BOND);
    DOCTEST_CHECK(discoveries::Choose(discoveries::KIND_NATIVES, 1) == discoveries::OPT_NAT_TEACH);
    Human_(&state);
    DOCTEST_CHECK(discoveries::Choose(discoveries::KIND_POWER_PLANT, 0) == 0);
    DOCTEST_CHECK(discoveries::Choose(discoveries::KIND_NATIVES, 0) == 0);

    // The choice is logged in the record: a human's find takes option 0, a computer's its own.
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_POWER_PLANT, 0));
    DOCTEST_CHECK(discoveries::State()->record[0].option == 0);
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_POWER_PLANT, 1));
    DOCTEST_CHECK(discoveries::State()->record[1].option == discoveries::OPT_POWER_DISASSEMBLE);
}

DOCTEST_TEST_CASE("power plant: test, install, take apart (success and blast)")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Human_(&state);
    // Test on a dead world.
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_POWER_PLANT, 0, 0, discoveries::OPT_POWER_TEST));
    DOCTEST_CHECK(state.players[0].bc == 1000 - discoveries::POWER_TEST_COST);
    DOCTEST_CHECK(discoveries::Used(0));
    DOCTEST_CHECK_FALSE(discoveries::Start(discoveries::KIND_POWER_PLANT, 0, 0, 0));  // one find a star
    Run_(discoveries::POWER_TURNS - 1);
    DOCTEST_CHECK(discoveries::State()->count == 1);
    Run_(1);
    DOCTEST_CHECK(discoveries::State()->count == 0);
    DOCTEST_CHECK(state.players[0].research_accumulated == discoveries::POWER_TEST_RESEARCH);

    // Install on the colony of player 1: a factory, one person less.
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_POWER_PLANT, 1, 1, discoveries::OPT_POWER_INSTALL));
    Run_(discoveries::POWER_TURNS);
    DOCTEST_CHECK(discoveries::State()->count == 0);
    DOCTEST_CHECK(state.colonies[1].buildings[BUILDING_AUTOMATED_FACTORY] == 1);
    DOCTEST_CHECK(state.colonies[1].n_pops == 3);
}

DOCTEST_TEST_CASE("power plant taken apart: a technology or a blast")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Human_(&state);
    const int16_t app = projects::Cheapest_Unknown_App(0);
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_POWER_PLANT, 0, 0, discoveries::OPT_POWER_DISASSEMBLE));
    discoveries::Check_With_Dice(0, TRIUMPH);
    DOCTEST_CHECK(discoveries::State()->count == 0);
    if (app >= 0) {
        DOCTEST_CHECK(Known_(state, app));
    } else {
        DOCTEST_CHECK(state.players[0].research_accumulated == discoveries::APP_FALLBACK_RESEARCH);
    }
    DOCTEST_CHECK(state.players[0].bc == 1000 + discoveries::POWER_BRILLIANT_BC);

    // A plain failure: nothing.
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_POWER_PLANT, 1, 1, discoveries::OPT_POWER_DISASSEMBLE));
    discoveries::Check_With_Dice(0, FAILURE);
    DOCTEST_CHECK(discoveries::State()->count == 0);
    DOCTEST_CHECK(state.colonies[1].n_pops == 4);

    // A catastrophe: the blast hits the colony (at least one hit), the record is gone.
    state.stars[2].has_colony = 1;
    state.planets[2].star_index = 2;
    state.planets[2].colony_index = 2;
    state.planets[2].planet_type = PLANET_TYPE_PLANET;
    state.colonies[2].owner = 0;
    state.colonies[2].planet = 2;
    state.colonies[2].n_pops = 4;
    state.colonies[2].buildings[BUILDING_AUTOMATED_FACTORY] = 1;
    MOX::NUM_COLONIES = 3;
    MOX::planet_count = 3;
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_POWER_PLANT, 0, 2, discoveries::OPT_POWER_DISASSEMBLE));
    discoveries::Check_With_Dice(0, CATASTROPHE);
    DOCTEST_CHECK(discoveries::State()->count == 0);
    DOCTEST_CHECK((state.colonies[2].n_pops < 4 || state.colonies[2].buildings[BUILDING_AUTOMATED_FACTORY] == 0));
}

DOCTEST_TEST_CASE("symbionts: study, wake (a hostile guardian), bond (a volunteer lost)")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Human_(&state);
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_SYMBIONTS, 0, 0, discoveries::OPT_SYMB_STUDY));
    discoveries::Check_With_Dice(0, SUCCESS);
    DOCTEST_CHECK(state.players[0].research_accumulated == discoveries::SYMB_STUDY_RESEARCH);
    DOCTEST_CHECK(discoveries::State()->count == 0);

    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_SYMBIONTS, 1, 1, discoveries::OPT_SYMB_WAKE));
    discoveries::Check_With_Dice(0, FAILURE);
    DOCTEST_CHECK(Monsters_At_(1) == 0);  // a plain failure: they sleep on
    state.stars[1].system_special = SYSTEM_SPECIAL_NO_SPECIAL;
    discoveries::State()->used[0] = 0;
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_SYMBIONTS, 1, 1, discoveries::OPT_SYMB_WAKE));
    discoveries::Check_With_Dice(0, CATASTROPHE);
    DOCTEST_CHECK(Monsters_At_(1) == 1);

    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_SYMBIONTS, 0, 2, discoveries::OPT_SYMB_BOND) == false);  // no colony at star 2
    discoveries::State()->used[0] = 0;
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_SYMBIONTS, 0, 0, discoveries::OPT_SYMB_BOND));
    discoveries::Check_With_Dice(0, CATASTROPHE);
    DOCTEST_CHECK(state.colonies[0].n_pops == 3);
}

DOCTEST_TEST_CASE("natives: leave, teach, work (revolt and quiet)")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.stars[0].system_special = SYSTEM_SPECIAL_NATIVES;
    state.stars[1].system_special = SYSTEM_SPECIAL_NATIVES;
    state.stars[2].system_special = SYSTEM_SPECIAL_NATIVES;
    DOCTEST_CHECK_FALSE(discoveries::Start(discoveries::KIND_NATIVES, 0, 2, 0));  // no colony there
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_NATIVES, 0, 0, discoveries::OPT_NAT_LEAVE));
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_NATIVES, 1, 1, discoveries::OPT_NAT_TEACH));
    DOCTEST_CHECK(state.players[1].bc == 1000 - discoveries::NATIVES_TEACH_COST);
    Run_(discoveries::NATIVES_TURNS);
    DOCTEST_CHECK(discoveries::State()->count == 0);
    DOCTEST_CHECK(state.players[0].bc == 1000 + discoveries::NATIVES_GIFT_BC);
    DOCTEST_CHECK(state.colonies[1].buildings[BUILDING_RESEARCH_LAB] == 1);

    // Workers: paid now; a revolt costs a person and BC, a quiet end costs nothing.
    discoveries::State()->used[0] = 0;
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_NATIVES, 0, 0, discoveries::OPT_NAT_WORK));
    DOCTEST_CHECK(state.players[0].bc == 1000 + discoveries::NATIVES_GIFT_BC + discoveries::NATIVES_WORK_BC);
    Seed_For_(1, 1);
    const int32_t before = state.players[0].bc;
    Run_(discoveries::NATIVES_TURNS);
    DOCTEST_CHECK(state.players[0].bc == before - discoveries::NATIVES_REVOLT_BC);
    DOCTEST_CHECK(state.colonies[0].n_pops == 3);

    discoveries::State()->used[0] = 0;
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_NATIVES, 0, 0, discoveries::OPT_NAT_WORK));
    Seed_For_(100, 100);
    const int32_t before2 = state.players[0].bc;
    Run_(discoveries::NATIVES_TURNS);
    DOCTEST_CHECK(state.players[0].bc == before2);
    DOCTEST_CHECK(state.colonies[0].n_pops == 3);
}

DOCTEST_TEST_CASE("vaults: a limited set, the best explorers open them more easily")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Human_(&state);
    // Player 0 has explored more stars than player 1: +2, so 8 + 2 = 10 opens the vault; player 1 fails with 8.
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_VAULT, 1, 1));
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_VAULT, 0, 0));
    DOCTEST_CHECK_FALSE(discoveries::Start(discoveries::KIND_VAULT, 0, 1));  // not explored by player 0 (and used)
    DOCTEST_CHECK(discoveries::State()->vaults_found == 2);
    discoveries::Check_With_Dice(0, EIGHT);  // player 1's vault
    DOCTEST_CHECK(state.players[1].bc == 1000);
    DOCTEST_CHECK(discoveries::State()->count == 1);
    discoveries::Check_With_Dice(0, EIGHT);
    DOCTEST_CHECK(state.players[0].bc == 1000 + discoveries::VAULT_BC);
    DOCTEST_CHECK(discoveries::State()->count == 0);

    // Only VAULTS_TOTAL ever.
    discoveries::State()->vaults_found = discoveries::VAULTS_TOTAL;
    DOCTEST_CHECK_FALSE(discoveries::Start(discoveries::KIND_VAULT, 0, 2));
}

DOCTEST_TEST_CASE("tombs: the dig wakes a guardian, the victor is paid, an unbeaten guardian is left")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Human_(&state);
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;  // the victor's granted technology only refreshes designs
    DOCTEST_CHECK_FALSE(discoveries::Start(discoveries::KIND_TOMB, 1, 1));  // no ancient site
    state.stars[1].system_special = SYSTEM_SPECIAL_ANCIENT_ARTIFACTS;
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_TOMB, 1, 1));
    Run_(discoveries::TOMB_DIG_TURNS);
    DOCTEST_REQUIRE(discoveries::State()->count == 1);
    DOCTEST_CHECK(discoveries::State()->record[0].stage == 1);
    DOCTEST_CHECK(Monsters_At_(1) == 1);
    Run_(3);
    DOCTEST_CHECK(discoveries::State()->count == 1);

    // Player 1's warship kills the guardian.
    state.ships[5].owner = 1;
    state.ships[5].status = 0;
    state.ships[5].location = 1;
    state.ships[5].d.size = 2;
    MOX::NUM_SHIPS = 6;
    for (int i = 0; i < 5; ++i) {
        if (state.ships[i].owner >= MAX_PLAYERS) state.ships[i].status = 5;
    }
    Run_(1);
    DOCTEST_CHECK(discoveries::State()->count == 0);
    DOCTEST_CHECK(state.players[1].bc == 1000 + discoveries::TOMB_BC);

    // A guardian nobody beats: the tomb is given up after TOMB_GUARD_TURNS.
    state.stars[0].system_special = SYSTEM_SPECIAL_ANCIENT_ARTIFACTS;
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_TOMB, 0, 0));
    Run_(discoveries::TOMB_DIG_TURNS);
    DOCTEST_REQUIRE(discoveries::State()->count == 1);
    DOCTEST_CHECK(Monsters_At_(0) == 1);
    Run_(discoveries::TOMB_GUARD_TURNS);
    DOCTEST_CHECK(discoveries::State()->count == 0);
    DOCTEST_CHECK(Monsters_At_(0) == 1);
    DOCTEST_CHECK(state.players[0].bc == 1000);
}

DOCTEST_TEST_CASE("a find is lost with its colony; a new find starts by chance, one a turn")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(discoveries::Start(discoveries::KIND_SYMBIONTS, 0, 0, discoveries::OPT_SYMB_STUDY));
    state.colonies[0].owner = 1;
    Run_(1);
    DOCTEST_CHECK(discoveries::State()->count == 0);
    state.colonies[0].owner = 0;

    discoveries::Clear();
    discoveries::enabled = true;
    // A seed whose first Random(1000) is a hit.
    for (uint32_t seed = 1; seed < 100000; ++seed) {
        game_random::Set_Random_Seed(seed);
        if (game_random::Random(1000) <= static_cast<uint32_t>(discoveries::DISCOVERY_CHANCE)) {
            game_random::Set_Random_Seed(seed);
            break;
        }
    }
    discoveries::Turn();
    DOCTEST_CHECK(discoveries::State()->count == 1);
    DOCTEST_CHECK(discoveries::Active(0) == 1);
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    discoveries::State()->count = 3;
    {
        discoveries::s_record& a = discoveries::State()->record[0];
        a.kind = discoveries::KIND_POWER_PLANT; a.player = 0; a.star = 0; a.other = 0; a.started = 35001; a.ends = 35005; a.option = 1;
        discoveries::s_record& b = discoveries::State()->record[1];
        b.kind = discoveries::KIND_VAULT; b.player = 1; b.star = 1; b.other = -1; b.started = 35001; b.ends = 35007;
        discoveries::s_record& c = discoveries::State()->record[2];
        c.kind = discoveries::KIND_TOMB; c.player = 0; c.star = 0; c.other = 0; c.started = 35001; c.ends = 35090; c.stage = 1;
    }
    discoveries::State()->vaults_found = 1;
    discoveries::State()->used[0] = 3;
    const uint32_t size = discoveries::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    discoveries::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    discoveries::Clear();
    discoveries::enabled = true;
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(discoveries::Parse_Save(&reader, &error));
    DOCTEST_CHECK(discoveries::Empty());
    discoveries::Commit_Save();
    DOCTEST_REQUIRE(discoveries::State()->count == 3);
    DOCTEST_CHECK(discoveries::State()->record[0].option == 1);
    DOCTEST_CHECK(discoveries::State()->record[1].other == -1);
    DOCTEST_CHECK(discoveries::State()->record[2].stage == 1);
    DOCTEST_CHECK(discoveries::State()->vaults_found == 1);
    DOCTEST_CHECK(discoveries::Used(0));
    DOCTEST_CHECK(discoveries::Used(1));

    // Layout: version (2), count (1), vaults (1), then 18 bytes a record: kind 0, player 1, star 2-3, other 4-5,
    // started 6-9, ends 10-13, option 14, stage 15, value 16-17. Record 0 at 4, record 1 at 22, record 2 at 40.
    const auto refuse = [&](size_t offset, uint8_t value) {
        auto bad = std::make_unique<uint8_t[]>(size);
        std::copy(buffer.get(), buffer.get() + size, bad.get());
        bad[offset] = value;
        SERIAL::Reader_ r(bad.get(), size, "test underflow", true);
        return !discoveries::Parse_Save(&r, &error);
    };
    DOCTEST_CHECK(refuse(4, 9));          // a bad kind
    DOCTEST_CHECK(refuse(4, 0));
    DOCTEST_CHECK(refuse(5, 0x7F));       // a bad empire
    DOCTEST_CHECK(refuse(4 + 3, 0x7F));   // a star out of range
    DOCTEST_CHECK(refuse(22 + 4, 0));     // a vault with a colony
    DOCTEST_CHECK(refuse(4 + 5, 0x7F));   // a colony out of range
    DOCTEST_CHECK(refuse(4 + 14, 3));     // a bad option
    DOCTEST_CHECK(refuse(22 + 14, 1));    // a vault with an option
    DOCTEST_CHECK(refuse(4 + 15, 1));     // a stage outside a tomb
    DOCTEST_CHECK(refuse(40 + 15, 2));    // a bad tomb stage
    DOCTEST_CHECK(refuse(4 + 16, 1));     // a stray value
    DOCTEST_CHECK(refuse(4 + 9, 0x80));   // a negative stardate
    DOCTEST_CHECK(refuse(2, 30));         // too many records
    DOCTEST_CHECK(refuse(3, 9));          // too many vaults
    DOCTEST_CHECK(refuse(0, 7));          // the version
    SERIAL::Reader_ r4(buffer.get(), size - 1, "test underflow", true);
    DOCTEST_CHECK_FALSE(discoveries::Parse_Save(&r4, &error));
    DOCTEST_CHECK(discoveries::State()->count == 3);  // the failed parses left the state alone
}

}
