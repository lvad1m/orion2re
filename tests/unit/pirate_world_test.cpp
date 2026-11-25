#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/colcalc.h"
#include "game/crises.h"
#include "game/events.h"
#include "game/opinion.h"
#include "game/pirate_world.h"
#include "game/random.h"
#include "game/serialization.h"
#include "game/techdata.h"

#include <memory>
#include <string>

namespace {
    constexpr int8_t TREATY_WAR = 4;

    struct s_pirate_scope {
        test::s_saved_value<bool> enabled{pirate_world::enabled};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<EVENTS::s_pirate_raid> raid{EVENTS::pirate_raid};

        s_pirate_scope() {
            pirate_world::Clear();
            opinion::Clear();
            pirate_world::enabled = true;
            opinion::enabled = true;
            EVENTS::pirate_raid = {1, 29};  // a raid takes exactly 30 percent of the treasury
        }
        ~s_pirate_scope() {
            pirate_world::Clear();
            opinion::Clear();
        }
    };

    // Three computer empires that know each other at peace, 1000 BC each. Stars 0-2 (x = 0, 10, 20) hold their
    // colonies (4, 3 and 2 people); stars 3-5 (x = 30, 40, 50) are neutral. Player 1 has a ship at its colony's
    // star (ship 2, cost 5) and two lone ones at star 5 (ships 0 and 1, costs 30 and 10).
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 3;
        MOX::NUM_STARS = 6;
        MOX::NUM_COLONIES = 3;
        MOX::NUM_SHIPS = 3;
        MOX::stardate = 35001;
        for (int p = 0; p < 3; ++p) {
            state->players[p].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[p].personality = PLAYER_PERSONALITY_ERRATIC;
            state->players[p].bc = 1000;
            state->players[p].n_freighters = 5;
            for (int q = 0; q < 3; ++q) state->players[p].contact[q] = p == q ? 0 : 1;
        }
        for (int s = 0; s < 6; ++s) {
            state->stars[s].x = static_cast<int16_t>(10 * s);
            state->stars[s].y = 0;
            state->stars[s].has_colony = s < 3 ? static_cast<uint8_t>(1 << s) : 0;
            state->stars[s].system_special = 0;
        }
        for (int c = 0; c < 3; ++c) {
            state->planets[c].star_index = static_cast<int16_t>(c);
            state->planets[c].colony_index = static_cast<int16_t>(c);
            state->colonies[c].owner = static_cast<int8_t>(c);
            state->colonies[c].planet = static_cast<int16_t>(c);
            state->colonies[c].outpost_flag = 0;
            state->colonies[c].n_pops = static_cast<uint8_t>(4 - c);
        }
        // The classic Ship_Size_Points_At_Star reads every slot: unused ships are retired (status 5).
        for (auto& ship : state->ships) ship.status = 5;
        const int16_t cost[3] = {30, 10, 5};
        const int16_t where[3] = {5, 5, 1};
        for (int s = 0; s < 3; ++s) {
            state->ships[s].owner = 1;
            state->ships[s].status = 0;
            state->ships[s].location = where[s];
            state->ships[s].officer_index = -1;
            state->ships[s].d.cost = cost[s];
            state->ships[s].d.size = 0;
        }
    }

    void War_(test::s_game_state* state, int a, int b) {
        state->players[a].treaty[b] = state->players[b].treaty[a] = TREATY_WAR;
    }

    // A seed whose next Random(n) gives `value`.
    void Seed_For_(uint32_t n, uint32_t value) {
        for (uint32_t seed = 1; seed < 100000; ++seed) {
            game_random::Set_Random_Seed(seed);
            if (game_random::Random(n) == value) {
                game_random::Set_Random_Seed(seed);
                return;
            }
        }
    }

    // A seed whose next `draws` draws of Random(1000) all miss both chances of a new event.
    void Calm_(int draws) {
        for (uint32_t seed = 1; seed < 100000; ++seed) {
            game_random::Set_Random_Seed(seed);
            bool calm = true;
            for (int i = 0; i < draws; ++i) calm = calm && game_random::Random(1000) > static_cast<uint32_t>(pirate_world::LAIR_CHANCE);
            if (calm) {
                game_random::Set_Random_Seed(seed);
                return;
            }
        }
    }

    const pirate_world::s_record* Find_(pirate_world::e_kind kind) {
        const pirate_world::s_state* s = pirate_world::State();
        for (int32_t i = 0; i < s->count; ++i) {
            if (s->record[i].kind == kind) return &s->record[i];
        }
        return nullptr;
    }

    // Runs the records `turns` turns on.
    void Run_(int turns) {
        for (int i = 0; i < turns; ++i) {
            ++MOX::stardate;
            pirate_world::Run_Records();
        }
    }

    // A lair of the given strength at star 4 (a neutral one), written straight into the table.
    void Put_Lair_(int star, int value) {
        pirate_world::s_record r;
        r.kind = pirate_world::KIND_LAIR;
        r.target = static_cast<int16_t>(star);
        r.value = r.extra = static_cast<int16_t>(value);
        r.started = MOX::stardate;
        pirate_world::State()->record[pirate_world::State()->count++] = r;
    }

    int32_t Known_(const test::s_game_state& state, int p) {
        int32_t n = 0;
        for (int a = 1; a < TECH_APP_COUNT; ++a) n += state.players[p].tech_applications[a] == TECH_RESEARCH_STATUS_RESEARCHED ? 1 : 0;
        return n;
    }
} // namespace

DOCTEST_TEST_SUITE("pirate_world") {

DOCTEST_TEST_CASE("flag off: nothing starts, nothing is kept, no draw from game_random")
{
    s_pirate_scope scope;
    test::s_game_state state;
    Setup_(&state);
    War_(&state, 0, 1);
    pirate_world::enabled = false;
    for (int k = pirate_world::KIND_TRIBUTE; k <= pirate_world::KIND_LAST; ++k) {
        DOCTEST_CHECK_FALSE(pirate_world::Start(static_cast<pirate_world::e_kind>(k), 0));
    }
    DOCTEST_CHECK(pirate_world::Choose(pirate_world::KIND_TRIBUTE, 0) == 0);
    DOCTEST_CHECK_FALSE(pirate_world::Licensed(0));
    DOCTEST_CHECK_FALSE(pirate_world::Has_Haven());
    const uint32_t seed = game_random::Get_Random_Seed();
    for (int i = 0; i < 20; ++i) pirate_world::Turn();
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
    DOCTEST_CHECK(pirate_world::Empty());
    DOCTEST_CHECK(state.players[0].bc == 1000);
}

DOCTEST_TEST_CASE("a human takes the first option, a computer picks by personality")
{
    s_pirate_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].personality = PLAYER_PERSONALITY_RUTHLESS;
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_CHECK(pirate_world::Choose(pirate_world::KIND_SYNDICATE, 0) == 0);
    state.players[0].objectives = PLAYER_OBJECTIVE_MILITARIST;
    DOCTEST_CHECK(pirate_world::Choose(pirate_world::KIND_SYNDICATE, 0) == pirate_world::SYNDICATE_SEIZE);
    state.players[0].personality = PLAYER_PERSONALITY_PACIFIST;
    DOCTEST_CHECK(pirate_world::Choose(pirate_world::KIND_SYNDICATE, 0) == pirate_world::SYNDICATE_ARREST);
    state.players[0].personality = PLAYER_PERSONALITY_ERRATIC;
    DOCTEST_CHECK(pirate_world::Choose(pirate_world::KIND_SYNDICATE, 0) == pirate_world::SYNDICATE_STUDY);
    // Start applies the choice: an erratic computer studies the base.
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_SYNDICATE, 0));
    DOCTEST_REQUIRE(Find_(pirate_world::KIND_SYNDICATE) != nullptr);
    DOCTEST_CHECK(Find_(pirate_world::KIND_SYNDICATE)->option == pirate_world::SYNDICATE_STUDY);
}

DOCTEST_TEST_CASE("tribute: pay, haggle, refuse, and a rival who outbids")
{
    s_pirate_scope scope;
    test::s_game_state state;
    Setup_(&state);
    // Paying: 15 percent of 1000, no raid, nothing kept.
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_TRIBUTE, 0, pirate_world::TRIBUTE_PAY));
    DOCTEST_CHECK(state.players[0].bc == 1000 - 150);
    DOCTEST_CHECK(state.players[0].n_freighters == 5);
    DOCTEST_CHECK(pirate_world::Empty());
    // Refusing: a classic raid (30 percent here) and two freighters.
    state.players[1].bc = 1000;
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_TRIBUTE, 1, pirate_world::TRIBUTE_REFUSE));
    DOCTEST_CHECK(state.players[1].bc == 700);
    DOCTEST_CHECK(state.players[1].n_freighters == 5 - pirate_world::RAID_FREIGHTERS);
    // Haggling: half the price when the raiders agree, a raid when they do not.
    state.players[2].bc = 1000;
    Seed_For_(100, 10);
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_TRIBUTE, 2, pirate_world::TRIBUTE_HAGGLE));
    DOCTEST_CHECK(state.players[2].bc == 1000 - 75);
    state.players[2].bc = 1000;
    Seed_For_(100, 90);
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_TRIBUTE, 2, pirate_world::TRIBUTE_HAGGLE));
    DOCTEST_CHECK(state.players[2].bc == 700);
    // A rival at war with the payer outbids the tribute: it pays, the raid comes anyway.
    state.players[0].bc = 1000;
    War_(&state, 0, 1);
    state.players[1].bc = 1000;
    Seed_For_(100, 10);
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_TRIBUTE, 0, pirate_world::TRIBUTE_PAY));
    DOCTEST_CHECK(state.players[1].bc == 1000 - pirate_world::RIVAL_PRICE);
    DOCTEST_CHECK(state.players[0].bc == (1000 - 150) * 70 / 100);
    // A poor empire is not worth the trouble.
    state.players[0].bc = pirate_world::TRIBUTE_MIN - 1;
    DOCTEST_CHECK_FALSE(pirate_world::Start(pirate_world::KIND_TRIBUTE, 0, pirate_world::TRIBUTE_PAY));
}

DOCTEST_TEST_CASE("privateers sink lone ships for pay, then the unspent escrow comes back")
{
    s_pirate_scope scope;
    test::s_game_state state;
    Setup_(&state);
    // Not at war, a human, or a poor employer: no contract.
    DOCTEST_CHECK_FALSE(pirate_world::Start(pirate_world::KIND_PRIVATEERS, 0));
    War_(&state, 0, 1);
    state.players[0].bc = pirate_world::HIRE_KEEP_BC + pirate_world::PRIVATEER_ESCROW - 1;
    DOCTEST_CHECK_FALSE(pirate_world::Start(pirate_world::KIND_PRIVATEERS, 0));
    state.players[0].bc = 1000;
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_CHECK_FALSE(pirate_world::Start(pirate_world::KIND_PRIVATEERS, 0));
    state.players[0].objectives = PLAYER_OBJECTIVE_MILITARIST;
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_PRIVATEERS, 0));
    DOCTEST_CHECK(state.players[0].bc == 1000 - pirate_world::PRIVATEER_ESCROW);
    DOCTEST_CHECK_FALSE(pirate_world::Start(pirate_world::KIND_PRIVATEERS, 0));  // one contract an employer
    game_random::Set_Random_Seed(7);
    Run_(pirate_world::PRIVATEER_TURNS + 1);
    DOCTEST_CHECK(pirate_world::Empty());
    int32_t sunk = 0;
    for (int s = 0; s < 2; ++s) sunk += state.ships[s].status == 5 ? 1 : 0;
    DOCTEST_CHECK(sunk >= 1);
    DOCTEST_CHECK(state.ships[2].status == 0);  // the ship at its own colony is safe
    const int32_t left = pirate_world::PRIVATEER_ESCROW - sunk * pirate_world::PRIVATEER_BOUNTY;
    DOCTEST_CHECK(state.players[0].bc == 1000 - pirate_world::PRIVATEER_ESCROW + left * (100 - pirate_world::PRIVATEER_CUT) / 100);
}

DOCTEST_TEST_CASE("privateers: peace voids the contract")
{
    s_pirate_scope scope;
    test::s_game_state state;
    Setup_(&state);
    War_(&state, 0, 1);
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_PRIVATEERS, 0));
    state.players[0].treaty[1] = state.players[1].treaty[0] = 0;
    Run_(1);
    DOCTEST_CHECK(pirate_world::Empty());
    DOCTEST_CHECK(state.players[0].bc == 1000 - pirate_world::PRIVATEER_ESCROW + pirate_world::PRIVATEER_ESCROW * 80 / 100);
}

DOCTEST_TEST_CASE("slavers take people from an undefended colony and move on; warships drive them off")
{
    s_pirate_scope scope;
    test::s_game_state state;
    Setup_(&state);
    // A defended colony (a ship at its star) is no prey.
    DOCTEST_CHECK_FALSE(pirate_world::Start(pirate_world::KIND_SLAVERS, 1));
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_SLAVERS, 0));
    const pirate_world::s_record* r = Find_(pirate_world::KIND_SLAVERS);
    DOCTEST_REQUIRE(r != nullptr);
    DOCTEST_CHECK(r->target == 0);
    DOCTEST_CHECK(r->player == 0);
    game_random::Set_Random_Seed(11);
    Run_(pirate_world::SLAVER_TURNS + 1);
    DOCTEST_CHECK(pirate_world::Empty());
    DOCTEST_CHECK(state.colonies[0].n_pops < 4);
    DOCTEST_CHECK(state.colonies[0].n_pops >= 1);

    // Warships in orbit wear them down: strength 8, a ship of size 7 is 8 points.
    pirate_world::Clear();
    state.colonies[0].n_pops = 4;
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_SLAVERS, 0));
    state.ships[0].owner = 0;
    state.ships[0].location = 0;
    state.ships[0].d.size = 7;
    Run_(1);
    DOCTEST_CHECK(pirate_world::Empty());
    DOCTEST_CHECK(state.colonies[0].n_pops == 4);
}

DOCTEST_TEST_CASE("slavers: a buyer is remembered badly")
{
    s_pirate_scope scope;
    test::s_saved_value<bool> front{crises::crisis_front};
    test::s_game_state state;
    Setup_(&state);
    crises::Clear();
    crises::crisis_front = true;  // decision 3: the purchase feeds the threat of 'become a crisis'
    state.players[1].personality = PLAYER_PERSONALITY_HONORABLE;
    state.players[2].personality = PLAYER_PERSONALITY_RUTHLESS;
    // Room for one more person at the buyer's colony (the engine's own limit for its planet, if any).
    const int cap = COLCALC::Planet_Max_Population_For_Player(&state.planets[2], 2);
    state.colonies[2].n_pops = static_cast<uint8_t>(std::max(1, std::min(2, cap - 1)));
    const int before = state.colonies[2].n_pops;
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_SLAVERS, 0));
    game_random::Set_Random_Seed(11);
    Run_(pirate_world::SLAVER_TURNS + 1);
    if (cap > before) {
        DOCTEST_CHECK(state.colonies[2].n_pops > before);
        DOCTEST_CHECK(state.players[2].bc < 1000);
        DOCTEST_CHECK(opinion::Relations(0, 2) < 0);
        DOCTEST_CHECK(opinion::Relations(1, 2) < 0);
        DOCTEST_CHECK(crises::Threat(2) >= crises::THREAT_ATROCITY);  // one purchase or more
        DOCTEST_CHECK(crises::Threat(0) == 0);
    } else {
        DOCTEST_WARN_MESSAGE(false, "the test planet has no room: the sale is not checked");
    }
    crises::Clear();
}

DOCTEST_TEST_CASE("lairs: start at a pirate cache, grow, raid, become a haven, fall to warships")
{
    s_pirate_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.stars[4].system_special = SYSTEM_SPECIAL_PIRATE_CACHE;
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_LAIR, -1));
    const pirate_world::s_record* lair = Find_(pirate_world::KIND_LAIR);
    DOCTEST_REQUIRE(lair != nullptr);
    DOCTEST_CHECK(lair->target == 4);
    DOCTEST_CHECK(lair->value == pirate_world::LAIR_STRENGTH + pirate_world::LAIR_CACHE_BONUS);
    DOCTEST_CHECK_FALSE(pirate_world::Has_Haven());
    // It grows while left alone (every LAIR_GROW_TURNS turns) and finally is a haven.
    game_random::Set_Random_Seed(5);
    int safety = 0;
    while (!pirate_world::Has_Haven() && safety++ < 100) Run_(1);
    DOCTEST_CHECK(pirate_world::Has_Haven());
    DOCTEST_CHECK(Find_(pirate_world::KIND_LAIR)->value >= pirate_world::HAVEN_STRENGTH);
    // At the cap it raids the owner of the nearest colony (player 2) every LAIR_RAID_PERIOD turns for sure.
    pirate_world::Clear();
    Put_Lair_(4, pirate_world::LAIR_CAP);
    state.players[2].bc = 1000;
    MOX::stardate += pirate_world::LAIR_RAID_PERIOD - 1;
    Run_(1);
    DOCTEST_CHECK(state.players[2].bc == 700);
    // Holding a flag of convenience spares it.
    pirate_world::s_record licence;
    licence.kind = pirate_world::KIND_LICENCE;
    licence.player = 2;
    licence.started = MOX::stardate;
    licence.ends = MOX::stardate + 50;
    pirate_world::State()->record[pirate_world::State()->count++] = licence;
    DOCTEST_CHECK(pirate_world::Licensed(2));
    Run_(pirate_world::LAIR_RAID_PERIOD);
    DOCTEST_CHECK(state.players[2].bc == 700);
    // Warships (size 7 = 8 points a turn) wear a strength-12 lair down in two turns and loot it.
    pirate_world::Clear();
    Put_Lair_(4, 12);
    state.ships[0].owner = 0;
    state.ships[0].location = 4;
    state.ships[0].d.size = 7;
    Run_(1);
    DOCTEST_CHECK(Find_(pirate_world::KIND_LAIR) != nullptr);
    Run_(1);
    DOCTEST_CHECK(Find_(pirate_world::KIND_LAIR) == nullptr);
    DOCTEST_CHECK(state.players[0].bc == 1000 + pirate_world::LAIR_LOOT + pirate_world::LAIR_LOOT_PER_STRENGTH * 12);
}

DOCTEST_TEST_CASE("lairs: at most LAIR_MAX, only in neutral stars")
{
    s_pirate_scope scope;
    test::s_game_state state;
    Setup_(&state);
    for (int i = 0; i < pirate_world::LAIR_MAX; ++i) DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_LAIR, -1));
    DOCTEST_CHECK_FALSE(pirate_world::Start(pirate_world::KIND_LAIR, -1));
    const pirate_world::s_state* s = pirate_world::State();
    for (int32_t i = 0; i < s->count; ++i) DOCTEST_CHECK(s->record[i].target >= 3);
}

DOCTEST_TEST_CASE("syndicate: arrest, study and seize")
{
    s_pirate_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_SYNDICATE, 0, pirate_world::SYNDICATE_ARREST));
    DOCTEST_CHECK(state.players[0].bc == 1000 + pirate_world::SYNDICATE_LOOT);
    DOCTEST_CHECK(pirate_world::Empty());
    // Study: a trade bonus a turn until the end.
    state.players[0].bc = 1000;
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_SYNDICATE, 0, pirate_world::SYNDICATE_STUDY));
    DOCTEST_CHECK_FALSE(pirate_world::Start(pirate_world::KIND_SYNDICATE, 0, pirate_world::SYNDICATE_STUDY));
    Seed_For_(100, 50);
    Run_(1);
    DOCTEST_CHECK(state.players[0].bc == 1000 + pirate_world::STUDY_BC);
    Seed_For_(100, 2);  // the syndicate finds the mole
    Run_(1);
    DOCTEST_CHECK(pirate_world::Empty());
    // Seize: income, and every SEIZE_PERIOD turns a raid on the richest rival (player 2 here).
    pirate_world::Clear();
    state.players[0].bc = 1000;
    state.players[2].bc = 1500;
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_SYNDICATE, 0, pirate_world::SYNDICATE_SEIZE));
    Run_(pirate_world::SEIZE_PERIOD - 1);
    DOCTEST_CHECK(state.players[0].bc == 1000 + (pirate_world::SEIZE_PERIOD - 1) * pirate_world::SEIZE_BC);
    Run_(1);
    DOCTEST_CHECK(state.players[2].bc == 1500 - 450);
    DOCTEST_CHECK(state.players[0].bc == 1000 + pirate_world::SEIZE_PERIOD * pirate_world::SEIZE_BC + 450 / 2);
    // The base is spent when its turns pass.
    Run_(pirate_world::SYNDICATE_TURNS);
    DOCTEST_CHECK(pirate_world::Empty());
}

DOCTEST_TEST_CASE("smuggler: jail him, grant an amnesty, or use him")
{
    s_pirate_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_SMUGGLER, 0, pirate_world::SMUGGLER_REFUSE));
    DOCTEST_CHECK(state.players[0].bc == 1000 + pirate_world::SMUGGLER_FINE);
    DOCTEST_CHECK(pirate_world::Empty());
    // An amnesty: a crackdown (the weak lair is gone, the strong one weakened) and 60 BC; he deceives in 25 %.
    state.players[0].bc = 1000;
    Put_Lair_(3, pirate_world::CRACKDOWN_LOSS);
    Put_Lair_(4, 20);
    Seed_For_(100, 90);
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_SMUGGLER, 0, pirate_world::SMUGGLER_AMNESTY));
    DOCTEST_CHECK(state.players[0].bc == 1000 + pirate_world::AMNESTY_BC);
    DOCTEST_REQUIRE(pirate_world::State()->count == 1);
    DOCTEST_CHECK(pirate_world::State()->record[0].target == 4);
    DOCTEST_CHECK(pirate_world::State()->record[0].value == 20 - pirate_world::CRACKDOWN_LOSS);
    Seed_For_(100, 10);
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_SMUGGLER, 0, pirate_world::SMUGGLER_AMNESTY));
    DOCTEST_CHECK(pirate_world::State()->count == 2);  // the deceived empire got a new lair
    // Using him: money and a new lair for certain.
    pirate_world::Clear();
    state.players[0].bc = 1000;
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_SMUGGLER, 0, pirate_world::SMUGGLER_USE));
    DOCTEST_CHECK(state.players[0].bc == 1000 + pirate_world::USE_BC);
    DOCTEST_CHECK(Find_(pirate_world::KIND_LAIR) != nullptr);
}

DOCTEST_TEST_CASE("haven: defectors, a flag of convenience, a black market")
{
    s_pirate_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;  // (a gift of technology leaves a human's designs alone)
    DOCTEST_CHECK_FALSE(pirate_world::Start(pirate_world::KIND_HAVEN, 0));  // no haven yet
    Put_Lair_(4, pirate_world::HAVEN_STRENGTH);
    DOCTEST_REQUIRE(pirate_world::Has_Haven());
    // A human takes the first option: defectors.
    const int known = Known_(state, 0);
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_HAVEN, 0));
    DOCTEST_CHECK(state.players[0].bc == 1000 - pirate_world::HAVEN_DEFECTORS_BC);
    DOCTEST_CHECK(Known_(state, 0) == known + 1);
    // The flag of convenience: paid once, a record of its own.
    state.players[0].bc = 1000;
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_HAVEN, 0, pirate_world::HAVEN_FLAG));
    DOCTEST_CHECK(pirate_world::Licensed(0));
    DOCTEST_CHECK(state.players[0].bc == 1000 - pirate_world::HAVEN_FLAG_BC);
    DOCTEST_CHECK_FALSE(pirate_world::Start(pirate_world::KIND_HAVEN, 0, pirate_world::HAVEN_FLAG));
    Run_(pirate_world::HAVEN_FLAG_TURNS);
    DOCTEST_CHECK_FALSE(pirate_world::Licensed(0));
    // The black market: two applications for 150 BC; the others hear bad rumours.
    state.players[0].bc = 1000;
    const int known2 = Known_(state, 0);
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_HAVEN, 0, pirate_world::HAVEN_BLACK_MARKET));
    DOCTEST_CHECK(state.players[0].bc == 1000 - pirate_world::HAVEN_MARKET_BC);
    DOCTEST_CHECK(Known_(state, 0) == known2 + pirate_world::HAVEN_MARKET_APPS);
    DOCTEST_CHECK(opinion::Relations(1, 0) < 0);
    // Too poor for the market: nothing happens.
    state.players[0].bc = pirate_world::HAVEN_MARKET_BC - 1;
    DOCTEST_CHECK_FALSE(pirate_world::Start(pirate_world::KIND_HAVEN, 0, pirate_world::HAVEN_BLACK_MARKET));
}

DOCTEST_TEST_CASE("Turn starts nothing on a calm seed")
{
    s_pirate_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Calm_(4);
    pirate_world::Turn();
    DOCTEST_CHECK(pirate_world::Empty());
    DOCTEST_CHECK(state.players[0].bc == 1000);
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused")
{
    s_pirate_scope scope;
    test::s_game_state state;
    Setup_(&state);
    War_(&state, 0, 1);
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_LAIR, -1));
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_PRIVATEERS, 0));
    DOCTEST_REQUIRE(pirate_world::Start(pirate_world::KIND_SLAVERS, 0));
    DOCTEST_REQUIRE(pirate_world::State()->count == 3);
    const uint32_t size = pirate_world::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    pirate_world::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    pirate_world::Clear();
    pirate_world::enabled = true;
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(pirate_world::Parse_Save(&reader, &error));
    DOCTEST_CHECK(pirate_world::Empty());
    pirate_world::Commit_Save();
    DOCTEST_REQUIRE(pirate_world::State()->count == 3);
    DOCTEST_CHECK(pirate_world::State()->record[0].kind == pirate_world::KIND_LAIR);
    DOCTEST_CHECK(pirate_world::State()->record[0].target >= 3);
    DOCTEST_CHECK(pirate_world::State()->record[1].kind == pirate_world::KIND_PRIVATEERS);
    DOCTEST_CHECK(pirate_world::State()->record[1].other == 1);
    DOCTEST_CHECK(pirate_world::State()->record[2].kind == pirate_world::KIND_SLAVERS);

    // Layout: version (2), count (1), then 19 bytes a record: kind 0, player 1, target 2-3, other 4-5,
    // started 6-9, ends 10-13, option 14, value 15-16, extra 17-18. Record 0 starts at 3, record 1 at 22,
    // record 2 at 41.
    const auto refuse = [&](size_t offset, uint8_t value) {
        auto bad = std::make_unique<uint8_t[]>(size);
        std::copy(buffer.get(), buffer.get() + size, bad.get());
        bad[offset] = value;
        SERIAL::Reader_ r(bad.get(), size, "test underflow", true);
        return !pirate_world::Parse_Save(&r, &error);
    };
    DOCTEST_CHECK(refuse(3, 1));         // a kind that is no record (tribute)
    DOCTEST_CHECK(refuse(3, 9));
    DOCTEST_CHECK(refuse(4, 0));         // a lair has no empire
    DOCTEST_CHECK(refuse(23, 9));        // privateers: a bad empire
    DOCTEST_CHECK(refuse(26, 0));        // privateers: the same empire twice
    DOCTEST_CHECK(refuse(24, 3));        // privateers: a stray star
    DOCTEST_CHECK(refuse(6, 0x7F));      // lair: a star out of range
    DOCTEST_CHECK(refuse(20, 0));        // lair: no strength
    DOCTEST_CHECK(refuse(17, 0xFF));     // a negative option
    DOCTEST_CHECK(refuse(38, 0x80));     // privateers: a negative value
    DOCTEST_CHECK(refuse(2, 30));        // too many records
    DOCTEST_CHECK(refuse(0, 7));         // the version
    SERIAL::Reader_ r4(buffer.get(), size - 1, "test underflow", true);
    DOCTEST_CHECK_FALSE(pirate_world::Parse_Save(&r4, &error));
    DOCTEST_CHECK(pirate_world::State()->count == 3);  // the failed parses left the state alone
}

}
