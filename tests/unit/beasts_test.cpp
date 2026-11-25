#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/beasts.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/serialization.h"

#include <memory>
#include <string>

namespace {
    constexpr char crystal_name[] = "Crystal";
    constexpr char hydra_name[] = "Hydra";

    struct s_beasts_scope {
        test::s_saved_value<bool> enabled{beasts::enabled};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};

        // The monsters' designs take their names from the loaded race names.
        char* saved_names[20];

        s_beasts_scope() {
            for (int i = 0; i < 20; ++i) saved_names[i] = MOX::race_names[i];
            MOX::race_names[16] = const_cast<char*>(crystal_name);
            MOX::race_names[19] = const_cast<char*>(hydra_name);
            beasts::Clear();
            opinion::Clear();
            beasts::enabled = true;
            opinion::enabled = true;
        }
        ~s_beasts_scope() {
            for (int i = 0; i < 20; ++i) MOX::race_names[i] = saved_names[i];
            beasts::Clear();
            opinion::Clear();
        }
    };

    // Three computer empires, 1000 BC each. Stars 0-2 (x = 0, 10, 20) hold their colonies (one each); stars 3-5
    // (x = 30, 40, 50) are neutral, star 4 is an asteroid belt. Players have visited their own stars only. All
    // ship slots are free (status 5, owner MAX_PLAYERS, as the classic table keeps them).
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 3;
        MOX::NUM_STARS = 6;
        MOX::NUM_COLONIES = 3;
        MOX::NUM_SHIPS = 0;
        MOX::stardate = 35001;
        for (int p = 0; p < 3; ++p) {
            state->players[p].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[p].personality = PLAYER_PERSONALITY_ERRATIC;
            state->players[p].bc = 1000;
            state->players[p].eliminated = 0;
        }
        for (int s = 0; s < 6; ++s) {
            state->stars[s].x = static_cast<int16_t>(10 * s);
            state->stars[s].y = 0;
            state->stars[s].has_colony = s < 3 ? static_cast<uint8_t>(1 << s) : 0;
            state->stars[s].visited = s < 3 ? static_cast<uint8_t>(1 << s) : 0;
        }
        for (int c = 0; c < 3; ++c) {
            state->planets[c].star_index = static_cast<int16_t>(c);
            state->planets[c].colony_index = static_cast<int16_t>(c);
            state->planets[c].planet_type = PLANET_TYPE_PLANET;
            state->colonies[c].owner = static_cast<int8_t>(c);
            state->colonies[c].planet = static_cast<int16_t>(c);
        }
        state->planets[3].star_index = 4;
        state->planets[3].colony_index = -1;
        state->planets[3].planet_type = PLANET_TYPE_ASTEROID;
        MOX::planet_count = 4;
        for (auto& ship : state->ships) {
            ship.owner = MAX_PLAYERS;
            ship.status = 5;
            ship.location = -1;
        }
    }

    void Put_Ship_(test::s_game_state* state, int idx, int owner, int star, int cost, int size) {
        state->ships[idx].owner = static_cast<int8_t>(owner);
        state->ships[idx].status = 0;
        state->ships[idx].location = static_cast<int16_t>(star);
        state->ships[idx].officer_index = -1;
        state->ships[idx].d.cost = static_cast<int16_t>(cost);
        state->ships[idx].d.size = static_cast<int8_t>(size);
        if (idx >= MOX::NUM_SHIPS) MOX::NUM_SHIPS = static_cast<int16_t>(idx + 1);
    }

    void Put_Record_(beasts::e_kind kind, int player, int target, int other, int option, int value, int ends) {
        beasts::s_record r;
        r.kind = kind;
        r.player = static_cast<int8_t>(player);
        r.target = static_cast<int16_t>(target);
        r.other = static_cast<int16_t>(other);
        r.option = static_cast<int8_t>(option);
        r.value = static_cast<int16_t>(value);
        r.started = MOX::stardate;
        r.ends = ends;
        beasts::State()->record[beasts::State()->count++] = r;
    }

    // A seed whose next two Random(100) draws give `a` and `b` (b < 0: only the first is looked at).
    void Seed_For_(uint32_t a, int32_t b = -1) {
        for (uint32_t seed = 1; seed < 1000000; ++seed) {
            game_random::Set_Random_Seed(seed);
            if (game_random::Random(100) != a) continue;
            if (b >= 0 && game_random::Random(100) != static_cast<uint32_t>(b)) continue;
            game_random::Set_Random_Seed(seed);
            return;
        }
    }

    // A seed whose next `draws` draws of Random(1000) all miss every chance of a new event.
    void Calm_(int draws) {
        for (uint32_t seed = 1; seed < 100000; ++seed) {
            game_random::Set_Random_Seed(seed);
            bool calm = true;
            for (int i = 0; i < draws; ++i) calm = calm && game_random::Random(1000) > static_cast<uint32_t>(beasts::EMPIRE_CHANCE);
            if (calm) {
                game_random::Set_Random_Seed(seed);
                return;
            }
        }
    }

    const beasts::s_record* Find_(beasts::e_kind kind) {
        const beasts::s_state* s = beasts::State();
        for (int32_t i = 0; i < s->count; ++i) {
            if (s->record[i].kind == kind) return &s->record[i];
        }
        return nullptr;
    }

    void Run_(int turns) {
        for (int i = 0; i < turns; ++i) {
            ++MOX::stardate;
            beasts::Run_Records();
        }
    }

    bool Monster_At_(int type, int star) {
        for (int i = 0; i < MOX::NUM_SHIPS; ++i) {
            if (MOX::ship[i].owner == type && MOX::ship[i].status < 5 && MOX::ship[i].location == star) return true;
        }
        return false;
    }
} // namespace

DOCTEST_TEST_SUITE("beasts") {

DOCTEST_TEST_CASE("flag off: nothing starts, nothing is kept, no draw from game_random")
{
    s_beasts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Put_Ship_(&state, 0, 0, 0, 100, 1);
    beasts::enabled = false;
    for (int k = beasts::KIND_HARVESTERS; k <= beasts::KIND_LAST; ++k) {
        DOCTEST_CHECK_FALSE(beasts::Start(static_cast<beasts::e_kind>(k), k >= beasts::KIND_HIJACK ? 0 : -1));
    }
    const uint32_t seed = game_random::Get_Random_Seed();
    for (int i = 0; i < 20; ++i) beasts::Turn();
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
    DOCTEST_CHECK(beasts::Empty());
    DOCTEST_CHECK(beasts::At(beasts::KIND_HARVESTERS, -1) == nullptr);
    DOCTEST_CHECK(state.players[0].bc == 1000);
    DOCTEST_CHECK(state.ships[0].status == 0);
}

DOCTEST_TEST_CASE("Turn starts nothing on a calm seed")
{
    s_beasts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Calm_(5);
    beasts::Turn();
    DOCTEST_CHECK(beasts::Empty());
}

DOCTEST_TEST_CASE("harvesters strip colonies, grow, move on, are salvaged and split")
{
    s_beasts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(beasts::Start(beasts::KIND_HARVESTERS, -1));
    const beasts::s_record* r = Find_(beasts::KIND_HARVESTERS);
    DOCTEST_REQUIRE(r != nullptr);
    DOCTEST_CHECK(r->target >= 0);
    DOCTEST_CHECK(r->target < 3);
    DOCTEST_CHECK(r->value == beasts::HARVESTER_STRENGTH);
    beasts::Clear();
    beasts::enabled = true;

    // One colony pays every HARVEST_PERIOD turns, the machines grow.
    Put_Record_(beasts::KIND_HARVESTERS, -1, 0, -1, 0, beasts::HARVESTER_STRENGTH, 0);
    Run_(beasts::HARVEST_PERIOD - 1);
    DOCTEST_CHECK(state.players[0].bc == 1000);
    Run_(1);
    DOCTEST_CHECK(state.players[0].bc == 1000 - beasts::HARVEST_BC);
    DOCTEST_CHECK(state.players[1].bc == 1000);
    DOCTEST_CHECK(Find_(beasts::KIND_HARVESTERS)->value == beasts::HARVESTER_STRENGTH + 1);

    // The colony is gone: they go to the nearest colony star.
    state.stars[0].has_colony = 0;
    Run_(1);
    DOCTEST_CHECK(Find_(beasts::KIND_HARVESTERS)->target == 1);
    state.stars[0].has_colony = 1;

    // Warships wear them down; the empire with most ship points salvages them.
    beasts::Clear();
    beasts::enabled = true;
    Put_Record_(beasts::KIND_HARVESTERS, -1, 0, -1, 0, beasts::HARVESTER_STRENGTH, 0);
    Put_Ship_(&state, 0, 1, 0, 50, 7);  // 8 points against 6
    Run_(1);
    DOCTEST_CHECK(beasts::Empty());
    DOCTEST_CHECK(state.players[1].bc == 1000 + beasts::SALVAGE_BC);

    // Left alone they split in two; the new half goes to a colony star without them.
    state.ships[0].status = 5;
    state.players[1].bc = 1000;
    Put_Record_(beasts::KIND_HARVESTERS, -1, 0, -1, 0, beasts::HARVESTER_STRENGTH, 0);
    Run_(beasts::SPLIT_TURNS);
    DOCTEST_REQUIRE(beasts::State()->count == 2);
    DOCTEST_CHECK(beasts::State()->record[0].target == 0);
    DOCTEST_CHECK(beasts::State()->record[1].target == 1);
    DOCTEST_CHECK(beasts::State()->record[0].value == beasts::State()->record[1].value);
    DOCTEST_CHECK(beasts::State()->record[0].value >= beasts::HARVESTER_STRENGTH);
}

DOCTEST_TEST_CASE("ambush: a lone ship is swallowed and lost, freed when it leaves or a second ship comes")
{
    s_beasts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(beasts::Start(beasts::KIND_AMBUSH, -1));
    DOCTEST_REQUIRE(Find_(beasts::KIND_AMBUSH) != nullptr);
    DOCTEST_CHECK(Find_(beasts::KIND_AMBUSH)->target == 4);  // the only belt without a colony
    DOCTEST_CHECK_FALSE(beasts::Start(beasts::KIND_AMBUSH, -1));  // one belt, one predator

    // A roll above the chance: the ship is spared.
    Put_Ship_(&state, 0, 1, 4, 30, 1);
    Seed_For_(100);
    Run_(1);
    DOCTEST_CHECK(Find_(beasts::KIND_AMBUSH)->option == 0);
    // A roll within it: swallowed; nobody comes: lost after AMBUSH_SAVE_TURNS turns.
    Seed_For_(1);
    Run_(1);
    DOCTEST_REQUIRE(Find_(beasts::KIND_AMBUSH)->option == 1);
    DOCTEST_CHECK(Find_(beasts::KIND_AMBUSH)->player == 1);
    DOCTEST_CHECK(Find_(beasts::KIND_AMBUSH)->other == 0);
    Run_(beasts::AMBUSH_SAVE_TURNS);
    DOCTEST_CHECK(state.ships[0].status == 5);
    DOCTEST_CHECK(Find_(beasts::KIND_AMBUSH)->option == 0);

    // Swallowed again, the ship breaks free by leaving the star.
    Put_Ship_(&state, 1, 2, 4, 30, 1);
    Seed_For_(1);
    Run_(1);
    DOCTEST_REQUIRE(Find_(beasts::KIND_AMBUSH)->option == 1);
    state.ships[1].location = 3;
    Run_(1);
    DOCTEST_CHECK(Find_(beasts::KIND_AMBUSH)->option == 0);
    DOCTEST_CHECK(state.ships[1].status == 0);

    // Swallowed once more, a second ship of the owner drives the predator off.
    state.ships[1].location = 4;
    Seed_For_(1);
    Run_(1);
    DOCTEST_REQUIRE(Find_(beasts::KIND_AMBUSH)->option == 1);
    Put_Ship_(&state, 2, 2, 4, 10, 0);
    Run_(1);
    DOCTEST_CHECK(Find_(beasts::KIND_AMBUSH) == nullptr);
    DOCTEST_CHECK(state.ships[1].status == 0);

    // A predator with no prey leaves after its time.
    beasts::Clear();
    beasts::enabled = true;
    state.ships[1].status = 5;
    state.ships[2].status = 5;
    DOCTEST_REQUIRE(beasts::Start(beasts::KIND_AMBUSH, -1));
    Run_(beasts::AMBUSH_TURNS);
    DOCTEST_CHECK(beasts::Empty());
}

DOCTEST_TEST_CASE("stolen flagship: the monster, the reward for destroying it, the unleashed hydra")
{
    s_beasts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[2].objectives = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_CHECK_FALSE(beasts::Start(beasts::KIND_HIJACK, 0));  // no ship
    Put_Ship_(&state, 0, 0, 0, 20, 1);
    DOCTEST_CHECK_FALSE(beasts::Start(beasts::KIND_HIJACK, 0));  // too cheap
    Put_Ship_(&state, 1, 0, 0, 100, 2);
    Put_Ship_(&state, 2, 2, 5, 100, 2);
    DOCTEST_CHECK_FALSE(beasts::Start(beasts::KIND_HIJACK, 2));  // a human's ship is never stolen
    DOCTEST_REQUIRE(beasts::Start(beasts::KIND_HIJACK, 0));
    DOCTEST_CHECK(state.ships[1].status == 5);  // the dearest one
    DOCTEST_CHECK(state.ships[0].status == 0);
    DOCTEST_CHECK(Monster_At_(NONPLAYER_SHIP_TYPE_CRYSTAL, 0));
    const beasts::s_record* r = Find_(beasts::KIND_HIJACK);
    DOCTEST_REQUIRE(r != nullptr);
    DOCTEST_CHECK(r->player == 0);
    DOCTEST_CHECK(r->target == 0);
    DOCTEST_CHECK(r->value == 100);
    DOCTEST_CHECK_FALSE(beasts::Start(beasts::KIND_HIJACK, 0));  // one at a time

    // A rival's fleet destroys the monster: reward, and the owner is grateful.
    for (int i = 0; i < MOX::NUM_SHIPS; ++i) {
        if (MOX::ship[i].owner == NONPLAYER_SHIP_TYPE_CRYSTAL) MOX::ship[i].status = 5;
    }
    Put_Ship_(&state, 3, 1, 0, 80, 3);
    Run_(1);
    DOCTEST_CHECK(Find_(beasts::KIND_HIJACK) == nullptr);
    DOCTEST_CHECK(state.players[1].bc == 1000 + beasts::HIJACK_REWARD + 50);
    DOCTEST_CHECK(opinion::Relations(0, 1) > 0);

    // The owner's own fleet gets the whole cost back.
    state.ships[3].status = 5;
    Put_Ship_(&state, 4, 0, 0, 100, 2);
    DOCTEST_REQUIRE(beasts::Start(beasts::KIND_HIJACK, 0));
    for (int i = 0; i < MOX::NUM_SHIPS; ++i) {
        if (MOX::ship[i].owner == NONPLAYER_SHIP_TYPE_CRYSTAL) MOX::ship[i].status = 5;
    }
    Put_Ship_(&state, 5, 0, 0, 10, 1);
    const int bc = state.players[0].bc;
    Run_(1);
    DOCTEST_CHECK(state.players[0].bc == bc + beasts::HIJACK_REWARD + 100);

    // Not destroyed in time: the thieves unleash a hydra.
    state.ships[5].status = 5;
    Put_Ship_(&state, 6, 0, 0, 100, 2);
    DOCTEST_REQUIRE(beasts::Start(beasts::KIND_HIJACK, 0));
    DOCTEST_CHECK_FALSE(Monster_At_(NONPLAYER_SHIP_TYPE_HYDRA, 0));
    Run_(beasts::HIJACK_TURNS);
    DOCTEST_CHECK(Find_(beasts::KIND_HIJACK) == nullptr);
    DOCTEST_CHECK(Monster_At_(NONPLAYER_SHIP_TYPE_HYDRA, 0));
}

DOCTEST_TEST_CASE("wormhole: reward and danger grow with each use, it collapses, bold computers try it more")
{
    s_beasts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[2].objectives = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_CHECK_FALSE(beasts::Start(beasts::KIND_WORMHOLE, 2));  // not offered to a human
    DOCTEST_REQUIRE(beasts::Start(beasts::KIND_WORMHOLE, 0));
    const beasts::s_record* r = Find_(beasts::KIND_WORMHOLE);
    DOCTEST_REQUIRE(r != nullptr);
    DOCTEST_CHECK(r->player == 0);
    DOCTEST_CHECK(r->target == 0);
    DOCTEST_CHECK(r->other >= 1);  // an unvisited star
    DOCTEST_CHECK(r->other != 0);
    DOCTEST_CHECK(r->ends == MOX::stardate + beasts::WORM_TURNS);
    DOCTEST_CHECK_FALSE(beasts::Start(beasts::KIND_WORMHOLE, 0));

    // No ship at the entrance: nothing happens. A pacifist tries rarely (a roll of 5 stays home).
    Put_Ship_(&state, 0, 0, 0, 50, 1);
    state.players[0].personality = PLAYER_PERSONALITY_PACIFIST;
    Seed_For_(5);
    Run_(1);
    DOCTEST_CHECK(Find_(beasts::KIND_WORMHOLE)->value == 0);
    // An erratic one goes with the same roll; the guardians let it by: reward, the exit becomes known.
    state.players[0].personality = PLAYER_PERSONALITY_ERRATIC;
    const int16_t exit = Find_(beasts::KIND_WORMHOLE)->other;
    DOCTEST_CHECK(((state.stars[exit].visited >> 0) & 1) == 0);
    Seed_For_(5, 100);
    Run_(1);
    DOCTEST_CHECK(Find_(beasts::KIND_WORMHOLE)->value == 1);
    DOCTEST_CHECK(state.players[0].bc == 1000 + beasts::WORM_REWARD);
    DOCTEST_CHECK(((state.stars[exit].visited >> 0) & 1) == 1);
    DOCTEST_CHECK(state.ships[0].status == 0);

    // The guardians sink the ship (a roll within the danger).
    Seed_For_(1, 1);
    Run_(1);
    DOCTEST_CHECK(state.ships[0].status == 5);
    DOCTEST_CHECK(Find_(beasts::KIND_WORMHOLE)->value == 2);

    // After WORM_MAX_USES uses it collapses; the later rewards are larger.
    Put_Ship_(&state, 1, 0, 0, 50, 1);
    const int bc = state.players[0].bc;
    Seed_For_(1, 100);
    Run_(1);
    DOCTEST_CHECK(state.players[0].bc == bc + beasts::WORM_REWARD + 2 * beasts::WORM_REWARD_STEP);
    Seed_For_(1, 100);
    Run_(1);
    DOCTEST_CHECK(Find_(beasts::KIND_WORMHOLE) == nullptr);

    // Unused, it collapses after its time; so does one whose colony is gone.
    DOCTEST_REQUIRE(beasts::Start(beasts::KIND_WORMHOLE, 0));
    state.ships[1].status = 5;
    Run_(beasts::WORM_TURNS);
    DOCTEST_CHECK(beasts::Empty());
    DOCTEST_REQUIRE(beasts::Start(beasts::KIND_WORMHOLE, 1));
    state.stars[1].has_colony = 0;
    Run_(1);
    DOCTEST_CHECK(beasts::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused")
{
    s_beasts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Put_Record_(beasts::KIND_HARVESTERS, -1, 0, -1, 0, 6, 0);
    Put_Record_(beasts::KIND_AMBUSH, 1, 4, 0, 1, 3, 35041);
    Put_Record_(beasts::KIND_HIJACK, 0, 1, -1, 0, 100, 35031);
    Put_Record_(beasts::KIND_WORMHOLE, 2, 2, 4, 0, 1, 35041);
    const uint32_t size = beasts::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    beasts::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    beasts::Clear();
    beasts::enabled = true;
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(beasts::Parse_Save(&reader, &error));
    DOCTEST_CHECK(beasts::Empty());
    beasts::Commit_Save();
    DOCTEST_REQUIRE(beasts::State()->count == 4);
    DOCTEST_CHECK(beasts::State()->record[1].kind == beasts::KIND_AMBUSH);
    DOCTEST_CHECK(beasts::State()->record[1].other == 0);
    DOCTEST_CHECK(beasts::State()->record[3].other == 4);
    DOCTEST_CHECK(beasts::State()->record[3].ends == 35041);

    // Layout: version (2), count (1), then 19 bytes a record: kind 0, player 1, target 2-3, other 4-5,
    // started 6-9, ends 10-13, option 14, value 15-16, extra 17-18. Record 0 starts at 3, record 1 at 22,
    // record 2 at 41, record 3 at 60.
    const auto refuse = [&](size_t offset, uint8_t value) {
        auto bad = std::make_unique<uint8_t[]>(size);
        std::copy(buffer.get(), buffer.get() + size, bad.get());
        bad[offset] = value;
        SERIAL::Reader_ r(bad.get(), size, "test underflow", true);
        return !beasts::Parse_Save(&r, &error);
    };
    DOCTEST_CHECK(refuse(3, 9));         // a bad kind
    DOCTEST_CHECK(refuse(3, 0));
    DOCTEST_CHECK(refuse(4, 0));         // harvesters have no empire
    DOCTEST_CHECK(refuse(41 + 1, 9));    // hijack: a bad empire
    DOCTEST_CHECK(refuse(3 + 3, 0x7F));  // a star out of range
    DOCTEST_CHECK(refuse(22 + 5, 0x7F)); // ambush: a bad ship
    DOCTEST_CHECK(refuse(60 + 5, 0x7F)); // wormhole: a bad exit
    DOCTEST_CHECK(refuse(3 + 5, 0));     // harvesters: a stray party (other 0)
    DOCTEST_CHECK(refuse(3 + 14, 1));    // harvesters: a stray option
    DOCTEST_CHECK(refuse(22 + 14, 2));   // ambush: a bad option
    DOCTEST_CHECK(refuse(3 + 16, 0x80)); // a negative value
    DOCTEST_CHECK(refuse(3 + 17, 1));    // a stray extra
    DOCTEST_CHECK(refuse(3 + 9, 0x80));  // a negative stardate
    DOCTEST_CHECK(refuse(2, 30));        // too many records
    DOCTEST_CHECK(refuse(0, 7));         // the version
    SERIAL::Reader_ r4(buffer.get(), size - 1, "test underflow", true);
    DOCTEST_CHECK_FALSE(beasts::Parse_Save(&r4, &error));
    DOCTEST_CHECK(beasts::State()->count == 4);  // the failed parses left the state alone
}


DOCTEST_TEST_CASE("harvesters have a base at a star without colonies; each loss strengthens the next swarm while it stands")
{
    s_beasts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(beasts::Start(beasts::KIND_HARVESTERS, -1));
    const beasts::s_record* base = Find_(beasts::KIND_BASE);
    DOCTEST_REQUIRE(base != nullptr);
    DOCTEST_CHECK(base->target == 3);  // the nearest star without a colony
    DOCTEST_CHECK(base->value == beasts::BASE_STRENGTH);
    DOCTEST_CHECK(beasts::State()->count == 2);
    beasts::Clear();
    beasts::enabled = true;

    // Harvesters at star 0 are destroyed: the base's level rises and the swarm comes after COUNTER_DELAY turns.
    Put_Record_(beasts::KIND_HARVESTERS, -1, 0, -1, 0, beasts::HARVESTER_STRENGTH, 0);
    Put_Record_(beasts::KIND_BASE, -1, 3, -1, 0, beasts::BASE_STRENGTH, 0);
    Put_Ship_(&state, 0, 1, 0, 50, 7);
    Run_(1);
    DOCTEST_CHECK(state.players[1].bc == 1000 + beasts::SALVAGE_BC);
    DOCTEST_REQUIRE(Find_(beasts::KIND_BASE) != nullptr);
    DOCTEST_CHECK(Find_(beasts::KIND_BASE)->extra == 1);
    DOCTEST_CHECK(Find_(beasts::KIND_HARVESTERS) == nullptr);
    state.ships[0].status = 5;
    Run_(beasts::COUNTER_DELAY - 1);
    DOCTEST_CHECK(Find_(beasts::KIND_HARVESTERS) == nullptr);
    Run_(1);
    DOCTEST_REQUIRE(Find_(beasts::KIND_HARVESTERS) != nullptr);
    DOCTEST_CHECK(Find_(beasts::KIND_HARVESTERS)->target == 0);
    DOCTEST_CHECK(Find_(beasts::KIND_HARVESTERS)->value == beasts::HARVESTER_STRENGTH + beasts::COUNTER_STEP);
    DOCTEST_CHECK(Find_(beasts::KIND_BASE)->ends == 0);

    // The stronger swarm is beaten too: the level rises again.
    Put_Ship_(&state, 0, 1, 0, 50, 20);
    Run_(1);
    DOCTEST_CHECK(Find_(beasts::KIND_HARVESTERS) == nullptr);
    DOCTEST_CHECK(Find_(beasts::KIND_BASE)->extra == 2);
    state.ships[0].status = 5;
    Run_(beasts::COUNTER_DELAY);
    DOCTEST_REQUIRE(Find_(beasts::KIND_HARVESTERS) != nullptr);
    DOCTEST_CHECK(Find_(beasts::KIND_HARVESTERS)->value == beasts::HARVESTER_STRENGTH + 2 * beasts::COUNTER_STEP);

    // A swarm that arrives by chance is stronger as well.
    beasts::State()->count = 1;  // drop the swarm, keep the base (record 0 is the base after the earlier removals)
    beasts::State()->record[0] = *Find_(beasts::KIND_BASE);
    DOCTEST_REQUIRE(beasts::Start(beasts::KIND_HARVESTERS, -1));
    DOCTEST_CHECK(Find_(beasts::KIND_HARVESTERS)->value == beasts::HARVESTER_STRENGTH + 2 * beasts::COUNTER_STEP);
    DOCTEST_CHECK(beasts::State()->count == 2);  // and no second base
}

DOCTEST_TEST_CASE("destroying the base ends the escalation and pays the victor")
{
    s_beasts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Put_Record_(beasts::KIND_BASE, -1, 3, -1, 0, beasts::BASE_STRENGTH, 0);
    beasts::State()->record[0].extra = 2;
    Put_Record_(beasts::KIND_HARVESTERS, -1, 0, -1, 0, beasts::HARVESTER_STRENGTH, 0);
    Put_Ship_(&state, 0, 2, 3, 80, 10);  // 11 points against 10
    Run_(1);
    DOCTEST_CHECK(Find_(beasts::KIND_BASE) == nullptr);
    DOCTEST_CHECK(state.players[2].bc == 1000 + beasts::BASE_BC);
    // Now a lost swarm brings no counter-strike.
    state.ships[0].status = 5;
    Put_Ship_(&state, 1, 1, 0, 50, 7);
    Run_(1);
    DOCTEST_CHECK(Find_(beasts::KIND_HARVESTERS) == nullptr);
    state.ships[1].status = 5;
    Run_(beasts::COUNTER_DELAY + 1);
    DOCTEST_CHECK(beasts::Empty());
}

DOCTEST_TEST_CASE("the base record round-trips; version 1 is still read; bad bases are refused")
{
    s_beasts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Put_Record_(beasts::KIND_HARVESTERS, -1, 0, -1, 0, 6, 0);
    const auto write = [&]() {
        const uint32_t size = beasts::Save_Size();
        auto buffer = std::make_unique<uint8_t[]>(size);
        SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
        beasts::Write_Save(&writer);
        return std::make_pair(std::move(buffer), size);
    };
    std::string error;
    {
        // A version 1 payload (no base) loads.
        auto [buffer, size] = write();
        buffer[0] = 1;
        SERIAL::Reader_ r(buffer.get(), size, "test underflow", true);
        DOCTEST_CHECK(beasts::Parse_Save(&r, &error));
        beasts::Discard_Save();
    }
    Put_Record_(beasts::KIND_BASE, -1, 3, 0, 0, 7, 35010);
    beasts::State()->record[1].extra = 2;
    auto [buffer, size] = write();
    {
        beasts::Clear();
        beasts::enabled = true;
        SERIAL::Reader_ r(buffer.get(), size, "test underflow", true);
        DOCTEST_REQUIRE(beasts::Parse_Save(&r, &error));
        beasts::Commit_Save();
        DOCTEST_REQUIRE(beasts::State()->count == 2);
        DOCTEST_CHECK(beasts::State()->record[1].kind == beasts::KIND_BASE);
        DOCTEST_CHECK(beasts::State()->record[1].extra == 2);
        DOCTEST_CHECK(beasts::State()->record[1].other == 0);
        DOCTEST_CHECK(beasts::State()->record[1].ends == 35010);
    }
    // Record 1 starts at 3 + 19 = 22: kind 22, target 24-25, other 26-27, extra 39-40.
    const auto refuse = [&](size_t offset, uint8_t value) {
        auto bad = std::make_unique<uint8_t[]>(size);
        std::copy(buffer.get(), buffer.get() + size, bad.get());
        bad[offset] = value;
        SERIAL::Reader_ r(bad.get(), size, "test underflow", true);
        return !beasts::Parse_Save(&r, &error);
    };
    DOCTEST_CHECK(refuse(0, 1));                    // a base in a version 1 payload
    DOCTEST_CHECK(refuse(22 + 17, 6));              // a level past the top
    DOCTEST_CHECK(refuse(22 + 5, 0x7F));            // a strike star out of range (high byte)
    DOCTEST_CHECK(refuse(22 + 1, 1));               // a base with an empire
    DOCTEST_CHECK(refuse(22 + 14, 1));              // a base with an option
}
}
