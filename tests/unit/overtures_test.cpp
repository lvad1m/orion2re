#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/colcalc.h"
#include "game/intrigue.h"
#include "game/opinion.h"
#include "game/overtures.h"
#include "game/random.h"
#include "game/serialization.h"
#include "game/techdata.h"

#include <memory>
#include <string>

namespace {
    constexpr int8_t TREATY_WAR = 4;
    constexpr int8_t TREATY_ALLIANCE = 2;

    struct s_overtures_scope {
        test::s_saved_value<bool> enabled{overtures::enabled};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};

        s_overtures_scope() {
            overtures::Clear();
            opinion::Clear();
            overtures::enabled = true;
            opinion::enabled = true;
        }
        ~s_overtures_scope() {
            overtures::Clear();
            opinion::Clear();
        }
    };

    // Four empires that know each other, at peace, 1000 BC each; player 0 has five ships of costs 50, 10, 30,
    // 20 and 40 (ships 0-4). The empires are humans (so that a gift of technology leaves their designs alone)
    // unless a test makes them computers.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 4;
        MOX::NUM_COLONIES = 0;
        MOX::NUM_SHIPS = 5;
        MOX::stardate = 35001;
        for (int p = 0; p < 4; ++p) {
            state->players[p].objectives = PLAYER_OBJECTIVE_HUMAN;
            state->players[p].personality = PLAYER_PERSONALITY_ERRATIC;
            state->players[p].bc = 1000;
            for (int q = 0; q < 4; ++q) state->players[p].contact[q] = p == q ? 0 : 1;
        }
        const int16_t cost[5] = {50, 10, 30, 20, 40};
        for (int s = 0; s < 5; ++s) {
            state->ships[s].owner = 0;
            state->ships[s].status = 0;
            state->ships[s].location = 0;
            state->ships[s].officer_index = -1;
            state->ships[s].d.cost = cost[s];
        }
    }

    void Computers_(test::s_game_state* state) {
        for (int p = 0; p < 4; ++p) state->players[p].objectives = PLAYER_OBJECTIVE_MILITARIST;
    }

    void War_(test::s_game_state* state, int a, int b) {
        state->players[a].treaty[b] = state->players[b].treaty[a] = TREATY_WAR;
    }

    bool At_War_(const test::s_game_state& state, int a, int b) {
        return state.players[a].treaty[b] >= TREATY_WAR;
    }

    int32_t Alive_Ships_(const test::s_game_state& state) {
        int32_t n = 0;
        for (int s = 0; s < 5; ++s) n += state.ships[s].status != 5 ? 1 : 0;
        return n;
    }

    int32_t Known_(const test::s_game_state& state, int p) {
        int32_t n = 0;
        for (int a = 1; a < TECH_APP_COUNT; ++a) n += state.players[p].tech_applications[a] == TECH_RESEARCH_STATUS_RESEARCHED ? 1 : 0;
        return n;
    }

    // An application the first empire may get (all four share the fixture's defaults).
    int32_t Some_App_(test::s_game_state* state) {
        for (int a = 1; a < TECH_APP_COUNT; ++a) {
            if (COLCALC::Player_May_Get_Tech_App(&state->players[0], a)) return a;
        }
        return -1;
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

    // A seed whose next `draws` draws of Random(1000) are all above the empire chance (no event starts).
    void Calm_(int draws) {
        for (uint32_t seed = 1; seed < 100000; ++seed) {
            game_random::Set_Random_Seed(seed);
            bool calm = true;
            for (int i = 0; i < draws; ++i) calm = calm && game_random::Random(1000) > static_cast<uint32_t>(overtures::EMPIRE_CHANCE);
            if (calm) {
                game_random::Set_Random_Seed(seed);
                return;
            }
        }
    }
} // namespace

DOCTEST_TEST_SUITE("overtures") {

DOCTEST_TEST_CASE("flag off: nothing starts, nothing is kept, no draw from game_random")
{
    s_overtures_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Computers_(&state);
    War_(&state, 0, 1);
    overtures::enabled = false;
    for (int k = overtures::KIND_HAND; k <= overtures::KIND_LAST; ++k) {
        DOCTEST_CHECK_FALSE(overtures::Start(static_cast<overtures::e_kind>(k), 0));
    }
    DOCTEST_CHECK(overtures::Choose(overtures::KIND_HAND, 0) == 0);
    const uint32_t seed = game_random::Get_Random_Seed();
    for (int i = 0; i < 20; ++i) overtures::Turn();
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
    DOCTEST_CHECK(overtures::Empty());
    DOCTEST_CHECK(Alive_Ships_(state) == 5);
    DOCTEST_CHECK(state.players[0].bc == 1000);
}

DOCTEST_TEST_CASE("iteration 1: an empire draw of 8 per mille now starts an attempt (it was calm at 6)")
{
    s_overtures_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Seed_For_(1000, 8);
    const uint32_t start = game_random::Get_Random_Seed();
    overtures::Turn();
    const uint32_t after = game_random::Get_Random_Seed();
    game_random::Set_Random_Seed(start);
    for (int i = 0; i < 4; ++i) game_random::Random(1000);  // a calm turn: one draw for each of the four empires
    DOCTEST_CHECK(after != game_random::Get_Random_Seed());
}

DOCTEST_TEST_CASE("a human takes the first option, a computer picks by personality")
{
    s_overtures_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].personality = PLAYER_PERSONALITY_RUTHLESS;
    DOCTEST_CHECK(overtures::Choose(overtures::KIND_LEAK, 0) == 0);  // a human
    state.players[0].objectives = PLAYER_OBJECTIVE_MILITARIST;
    DOCTEST_CHECK(overtures::Choose(overtures::KIND_LEAK, 0) == overtures::LEAK_SUPPRESS);
    state.players[0].personality = PLAYER_PERSONALITY_PACIFIST;
    DOCTEST_CHECK(overtures::Choose(overtures::KIND_HAND, 0) == overtures::HAND_ACCEPT);
    state.players[0].personality = PLAYER_PERSONALITY_ERRATIC;
    DOCTEST_CHECK(overtures::Choose(overtures::KIND_HAND, 0) == overtures::HAND_BARGAIN);
    // Start applies the choice: an aggressive computer in a rich galaxy seizes the delegation.
    state.players[0].personality = PLAYER_PERSONALITY_AGGRESSIVE;
    DOCTEST_REQUIRE(overtures::Start(overtures::KIND_HAND, 0));
    DOCTEST_CHECK(state.players[0].bc == 1000 + overtures::HAND_LOOT);
    DOCTEST_CHECK(Alive_Ships_(state) == 5);
}

DOCTEST_TEST_CASE("outstretched hand: accept, bargain, seize")
{
    s_overtures_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(Some_App_(&state) > 0);
    // Accept: the three cheapest ships (10, 20, 30) go, two applications come.
    DOCTEST_REQUIRE(overtures::Start(overtures::KIND_HAND, 0, overtures::HAND_ACCEPT));
    DOCTEST_CHECK(Alive_Ships_(state) == 2);
    DOCTEST_CHECK(state.ships[1].status == 5);
    DOCTEST_CHECK(state.ships[3].status == 5);
    DOCTEST_CHECK(state.ships[2].status == 5);
    DOCTEST_CHECK(state.ships[0].status != 5);
    DOCTEST_CHECK(Known_(state, 0) >= overtures::HAND_APPS);
    // Too few ships left: it cannot start.
    DOCTEST_CHECK_FALSE(overtures::Start(overtures::KIND_HAND, 0, overtures::HAND_ACCEPT));
    // Bargain.
    for (int s = 0; s < 5; ++s) state.ships[s].status = 0;
    const int32_t known = Known_(state, 0);
    DOCTEST_REQUIRE(overtures::Start(overtures::KIND_HAND, 0, overtures::HAND_BARGAIN));
    DOCTEST_CHECK(Alive_Ships_(state) == 4);
    DOCTEST_CHECK(Known_(state, 0) == known + 1);
    DOCTEST_CHECK(state.players[0].bc == 1000 + overtures::HAND_BARGAIN_BC);
    // Seize: money, no ship lost, every empire in contact thinks the worse of it.
    state.players[0].bc = 1000;
    for (int s = 0; s < 5; ++s) state.ships[s].status = 0;
    Computers_(&state);
    DOCTEST_REQUIRE(overtures::Start(overtures::KIND_HAND, 0, overtures::HAND_SEIZE));
    DOCTEST_CHECK(Alive_Ships_(state) == 5);
    DOCTEST_CHECK(state.players[0].bc == 1000 + overtures::HAND_LOOT);
    DOCTEST_CHECK(opinion::Relations(1, 0) < 0);
    DOCTEST_CHECK(opinion::Relations(3, 0) < 0);
}

DOCTEST_TEST_CASE("leaked discovery: share, sell, suppress")
{
    s_overtures_scope scope;
    test::s_game_state state;
    Setup_(&state);
    const int32_t app = Some_App_(&state);
    DOCTEST_REQUIRE(app > 0);
    // Nobody lacks anything: no leak.
    state.players[0].tech_applications[app] = TECH_RESEARCH_STATUS_RESEARCHED;
    for (int p = 1; p < 4; ++p) state.players[p].tech_applications[app] = TECH_RESEARCH_STATUS_RESEARCHED;
    DOCTEST_CHECK_FALSE(overtures::Start(overtures::KIND_LEAK, 0, overtures::LEAK_SHARE));
    for (int p = 1; p < 4; ++p) state.players[p].tech_applications[app] = 0;
    // Share: every empire in contact (up to three) gets it. (The empires are humans here: a computer's gift
    // refreshes its ship designs, which the fixture cannot do; humans hold no opinions, so the opinion is not
    // checked.)
    DOCTEST_REQUIRE(overtures::Start(overtures::KIND_LEAK, 0, overtures::LEAK_SHARE));
    for (int p = 1; p < 4; ++p) {
        DOCTEST_CHECK(state.players[p].tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED);
    }
    // Sell: the richest buyer pays.
    for (int p = 1; p < 4; ++p) state.players[p].tech_applications[app] = 0;
    state.players[2].bc = 2000;
    DOCTEST_REQUIRE(overtures::Start(overtures::KIND_LEAK, 0, overtures::LEAK_SELL));
    DOCTEST_CHECK(state.players[2].bc == 2000 - overtures::LEAK_PRICE);
    DOCTEST_CHECK(state.players[0].bc == 1000 + overtures::LEAK_PRICE);
    DOCTEST_CHECK(state.players[2].tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED);
    DOCTEST_CHECK(state.players[1].tech_applications[app] != TECH_RESEARCH_STATUS_RESEARCHED);
    DOCTEST_CHECK(state.players[3].tech_applications[app] != TECH_RESEARCH_STATUS_RESEARCHED);
    // Suppress: it costs money and nothing leaks.
    state.players[2].tech_applications[app] = 0;
    state.players[0].bc = 1000;
    DOCTEST_REQUIRE(overtures::Start(overtures::KIND_LEAK, 0, overtures::LEAK_SUPPRESS));
    DOCTEST_CHECK(state.players[0].bc == 1000 - overtures::LEAK_SUPPRESS_COST);
    for (int p = 1; p < 4; ++p) DOCTEST_CHECK(state.players[p].tech_applications[app] != TECH_RESEARCH_STATUS_RESEARCHED);
    DOCTEST_CHECK(overtures::Empty());
}

DOCTEST_TEST_CASE("mediator: a third power makes peace between two warring computers; both owe it a favour, nothing is struck")
{
    s_overtures_scope scope;
    test::s_saved_value<bool> favours_flag{intrigue::favours};
    intrigue::Clear();
    intrigue::favours = true;
    test::s_game_state state;
    Setup_(&state);
    // A human at war is not driven.
    War_(&state, 1, 2);
    DOCTEST_CHECK_FALSE(overtures::Start(overtures::KIND_MEDIATOR, 1));
    Computers_(&state);
    // Ships for the strike: player 1 and 2 own ships of their own.
    MOX::NUM_SHIPS = 9;
    for (int s = 5; s < 9; ++s) {
        state.ships[s].owner = s < 7 ? 1 : 2;
        state.ships[s].status = 0;
        state.ships[s].d.cost = static_cast<int16_t>(s * 10);
    }
    // No mediator: the only others, 0 and 3, are at war with player 1.
    War_(&state, 0, 1);
    War_(&state, 3, 1);
    DOCTEST_CHECK_FALSE(overtures::Start(overtures::KIND_MEDIATOR, 1));
    DOCTEST_CHECK(overtures::Empty());
    state.players[0].treaty[1] = state.players[1].treaty[0] = 0;
    state.players[3].treaty[1] = state.players[1].treaty[3] = 0;
    DOCTEST_REQUIRE(overtures::Start(overtures::KIND_MEDIATOR, 1));
    DOCTEST_REQUIRE(overtures::State()->count == 1);
    const overtures::s_record rec = overtures::State()->record[0];
    DOCTEST_CHECK(rec.kind == overtures::KIND_MEDIATOR);
    DOCTEST_CHECK(rec.target == 1);
    DOCTEST_CHECK(rec.other == 2);
    DOCTEST_CHECK((rec.player == 0 || rec.player == 3));
    // The same pair is not pressed twice.
    DOCTEST_CHECK_FALSE(overtures::Start(overtures::KIND_MEDIATOR, 2));
    // A turn whose draw is above the chance: the war goes on, the record stays.
    Seed_For_(100, 100);
    overtures::Run_Records();
    DOCTEST_CHECK(At_War_(state, 1, 2));
    DOCTEST_CHECK(overtures::State()->count == 1);
    // A draw of 1 brings peace and gratitude.
    Seed_For_(100, 1);
    overtures::Run_Records();
    DOCTEST_CHECK_FALSE(At_War_(state, 1, 2));
    DOCTEST_CHECK(overtures::Empty());
    DOCTEST_CHECK(opinion::Relations(1, rec.player) == overtures::PEACE_OPINION);
    DOCTEST_CHECK(opinion::Relations(2, rec.player) == overtures::PEACE_OPINION);
    // "Услуги от обоих": each side owes the mediator a favour.
    DOCTEST_CHECK(intrigue::Favours(1, rec.player) == 1);
    DOCTEST_CHECK(intrigue::Favours(2, rec.player) == 1);
    DOCTEST_CHECK(intrigue::Favours(rec.player, 1) == 0);
    // Pressed to the end of the turns without peace: the mediator gives up, nothing is struck and nothing owed.
    War_(&state, 1, 2);
    DOCTEST_REQUIRE(overtures::Start(overtures::KIND_MEDIATOR, 1));
    const int16_t mediator = overtures::State()->record[0].player;
    MOX::stardate += overtures::PEACE_TURNS;
    Seed_For_(100, 100);
    overtures::Run_Records();
    DOCTEST_CHECK(overtures::Empty());
    DOCTEST_CHECK(At_War_(state, 1, 2));
    for (int s = 5; s < 9; ++s) DOCTEST_CHECK(state.ships[s].status != 5);  // no patrol strike: every ship lives
    DOCTEST_CHECK(opinion::Relations(1, mediator) == overtures::PEACE_OPINION);  // the old thanks only, no new grudge
    DOCTEST_CHECK(intrigue::Favours(1, mediator) == 1);  // and no second favour
    // A war that ends by itself closes the mediation.
    War_(&state, 1, 2);
    DOCTEST_REQUIRE(overtures::Start(overtures::KIND_MEDIATOR, 1));
    state.players[1].treaty[2] = state.players[2].treaty[1] = 0;
    overtures::Run_Records();
    DOCTEST_CHECK(overtures::Empty());
    intrigue::Clear();
}

DOCTEST_TEST_CASE("paid offensive: the payer pays an ally that declares war; the deal is judged later")
{
    s_overtures_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Computers_(&state);
    War_(&state, 0, 1);
    // Player 3 is allied with the enemy, player 2 is a free ally: 2 is paid.
    state.players[3].treaty[1] = state.players[1].treaty[3] = TREATY_ALLIANCE;
    DOCTEST_REQUIRE(overtures::Start(overtures::KIND_OFFENSIVE, 0, overtures::OFFENSIVE_STANDARD));
    const int32_t payment = 1000 / 100 * overtures::PAYMENT_PERCENT[overtures::OFFENSIVE_STANDARD];
    DOCTEST_CHECK(state.players[0].bc == 1000 - payment);
    DOCTEST_CHECK(state.players[2].bc == 1000 + payment);
    DOCTEST_CHECK(At_War_(state, 2, 1));
    DOCTEST_REQUIRE(overtures::State()->count == 1);
    DOCTEST_CHECK(overtures::State()->record[0].target == 2);
    DOCTEST_CHECK(overtures::State()->record[0].value == payment);
    // Judged at the end: the ally still at war is remembered well.
    const int32_t before = opinion::Relations(0, 2);
    overtures::Run_Records();
    DOCTEST_CHECK(overtures::State()->count == 1);
    MOX::stardate += overtures::OFFENSIVE_TURNS;
    overtures::Run_Records();
    DOCTEST_CHECK(overtures::Empty());
    DOCTEST_CHECK(opinion::Relations(0, 2) == before + overtures::OFFENSIVE_OPINION);
    // The other outcome: the ally made peace on the money.
    state.players[0].bc = 1000;
    state.players[2].treaty[1] = state.players[1].treaty[2] = 0;
    DOCTEST_REQUIRE(overtures::Start(overtures::KIND_OFFENSIVE, 0, overtures::OFFENSIVE_MODEST));
    const int32_t before2 = opinion::Relations(0, 2);
    state.players[2].treaty[1] = state.players[1].treaty[2] = 0;
    MOX::stardate += overtures::OFFENSIVE_TURNS;
    overtures::Run_Records();
    DOCTEST_CHECK(overtures::Empty());
    DOCTEST_CHECK(opinion::Relations(0, 2) == before2 - overtures::OFFENSIVE_OPINION);
    // A poor payer, a human payer, or nobody willing: it cannot start.
    state.players[0].bc = overtures::PAYMENT_MIN - 1;
    DOCTEST_CHECK_FALSE(overtures::Start(overtures::KIND_OFFENSIVE, 0, overtures::OFFENSIVE_MODEST));
    state.players[0].bc = 1000;
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_CHECK_FALSE(overtures::Start(overtures::KIND_OFFENSIVE, 0, overtures::OFFENSIVE_MODEST));
    state.players[0].objectives = PLAYER_OBJECTIVE_MILITARIST;
    state.players[2].treaty[0] = state.players[0].treaty[2] = TREATY_WAR;  // the only free ally is at war with the payer
    state.players[3].treaty[1] = state.players[1].treaty[3] = TREATY_ALLIANCE;
    state.players[3].treaty[2] = state.players[2].treaty[3] = TREATY_ALLIANCE;
    DOCTEST_CHECK_FALSE(overtures::Start(overtures::KIND_OFFENSIVE, 0, overtures::OFFENSIVE_MODEST));
}

DOCTEST_TEST_CASE("a paid ally already at war only takes the money; a record whose party is gone ends")
{
    s_overtures_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Computers_(&state);
    War_(&state, 0, 1);
    War_(&state, 2, 1);
    state.players[3].treaty[1] = state.players[1].treaty[3] = TREATY_ALLIANCE;
    DOCTEST_REQUIRE(overtures::Start(overtures::KIND_OFFENSIVE, 0, overtures::OFFENSIVE_GENEROUS));
    DOCTEST_CHECK(state.players[2].bc == 1000 + 1000 / 100 * overtures::PAYMENT_PERCENT[overtures::OFFENSIVE_GENEROUS]);
    DOCTEST_CHECK(overtures::State()->count == 1);
    state.players[2].eliminated = 1;
    overtures::Run_Records();
    DOCTEST_CHECK(overtures::Empty());
}

DOCTEST_TEST_CASE("Turn starts nothing on a calm seed")
{
    s_overtures_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Computers_(&state);
    War_(&state, 0, 1);
    Calm_(4);
    overtures::Turn();
    DOCTEST_CHECK(overtures::Empty());
    DOCTEST_CHECK(state.players[0].bc == 1000);
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused")
{
    s_overtures_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Computers_(&state);
    War_(&state, 0, 1);
    state.players[3].treaty[1] = state.players[1].treaty[3] = TREATY_ALLIANCE;
    DOCTEST_REQUIRE(overtures::Start(overtures::KIND_OFFENSIVE, 0, overtures::OFFENSIVE_STANDARD));
    War_(&state, 1, 2);
    DOCTEST_REQUIRE(overtures::Start(overtures::KIND_MEDIATOR, 1));
    DOCTEST_REQUIRE(overtures::State()->count == 2);
    const uint32_t size = overtures::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    overtures::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    overtures::Clear();
    overtures::enabled = true;
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(overtures::Parse_Save(&reader, &error));
    DOCTEST_CHECK(overtures::Empty());
    overtures::Commit_Save();
    DOCTEST_REQUIRE(overtures::State()->count == 2);
    DOCTEST_CHECK(overtures::State()->record[0].kind == overtures::KIND_OFFENSIVE);
    DOCTEST_CHECK(overtures::State()->record[0].target == 2);
    DOCTEST_CHECK(overtures::State()->record[1].kind == overtures::KIND_MEDIATOR);
    DOCTEST_CHECK(overtures::State()->record[1].target == 1);

    // Layout: version (2), count (1), then 17 bytes a record: kind 0, player 1, target 2-3, other 4-5,
    // started 6-9, ends 10-13, option 14, value 15-16.
    const auto refuse = [&](size_t offset, uint8_t value) {
        auto bad = std::make_unique<uint8_t[]>(size);
        std::copy(buffer.get(), buffer.get() + size, bad.get());
        bad[offset] = value;
        SERIAL::Reader_ r(bad.get(), size, "test underflow", true);
        return !overtures::Parse_Save(&r, &error);
    };
    DOCTEST_CHECK(refuse(3, 1));         // a kind that is no record
    DOCTEST_CHECK(refuse(3, 9));
    DOCTEST_CHECK(refuse(4, 9));         // player
    DOCTEST_CHECK(refuse(5, 9));         // target
    DOCTEST_CHECK(refuse(7, 9));         // other
    DOCTEST_CHECK(refuse(7, 0));         // the same empire twice (other == player)
    DOCTEST_CHECK(refuse(17, 5));        // option
    DOCTEST_CHECK(refuse(19, 0x80));     // value (the high byte makes it negative)
    DOCTEST_CHECK(refuse(2, 30));        // too many records
    DOCTEST_CHECK(refuse(0, 7));         // the version
    SERIAL::Reader_ r4(buffer.get(), size - 1, "test underflow", true);
    DOCTEST_CHECK_FALSE(overtures::Parse_Save(&r4, &error));
    DOCTEST_CHECK(overtures::State()->count == 2);  // the failed parses left the state alone
}

}
