#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/beasts.h"
#include "game/bill.h"
#include "game/news.h"
#include "game/serialization.h"

#include <memory>
#include <string>

namespace {
    struct s_news_scope {
        test::s_saved_value<bool> enabled{news::enabled};
        test::s_saved_value<bool> intelligence{news::intelligence};
        test::s_saved_value<bool> beasts_enabled{beasts::enabled};

        s_news_scope() {
            news::Clear();
            news::enabled = true;
            news::intelligence = true;
            beasts::Clear();
            beasts::enabled = true;
        }
        ~s_news_scope() {
            news::Clear();
            beasts::Clear();
        }
    };

    // Four living empires. Player 0 knows 1 only, player 1 knows 0 and 2, player 2 knows 1, player 3 knows nobody.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 4;
        MOX::stardate = 35100;
        MOX::NUM_SHIPS = 0;
        for (int i = 0; i < 4; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[i].eliminated = 0;
            state->players[i].sneak_attack_player = -1;
            for (int j = 0; j < 4; ++j) state->players[i].contact[j] = 0;
        }
        state->players[0].contact[1] = 1;
        state->players[1].contact[0] = 1;
        state->players[1].contact[2] = 1;
        state->players[2].contact[1] = 1;
    }

    // A warship of `owner` standing at `star`, `weapons` mounts of the first weapon type.
    void Ship_(test::s_game_state* state, int index, int owner, int star) {
        s_ship_data& ship = state->ships[index];
        ship = s_ship_data{};
        ship.owner = static_cast<int8_t>(owner);
        ship.status = 0;
        ship.location = static_cast<int16_t>(star);
        ship.d.ship_type = SHIP_TYPE_COMBAT;
        if (index + 1 > MOX::NUM_SHIPS) MOX::NUM_SHIPS = static_cast<int16_t>(index + 1);
    }
} // namespace

DOCTEST_TEST_SUITE("news") {

DOCTEST_TEST_CASE("facts are recorded and seen only with contact") {
    s_news_scope scope;
    test::s_game_state state;
    Setup_(&state);
    news::Add(news::KIND_WAR, 1, 2, 0);
    news::Add(news::KIND_COLONY_FOUNDED, 3, -1, 7);
    news::Add(news::KIND_COUNCIL, -1, -1, 0);
    DOCTEST_CHECK(news::Count() == 3);
    news::s_item out[8];
    // Player 0 meets only player 1: the war of 1 on 2 is news to it, player 3's colony is not, the council is.
    DOCTEST_CHECK(news::Recent_For(0, out, 8) == 2);
    DOCTEST_CHECK(out[0].kind == news::KIND_COUNCIL);
    DOCTEST_CHECK(out[1].kind == news::KIND_WAR);
    // Player 3 meets nobody: only the galaxy-wide fact and its own colony.
    DOCTEST_CHECK(news::Recent_For(3, out, 8) == 2);
    DOCTEST_CHECK(out[1].kind == news::KIND_COLONY_FOUNDED);
    // Player 2 is named in the war.
    DOCTEST_CHECK(news::Recent_For(2, out, 8) == 2);
    DOCTEST_CHECK(news::Describe(*news::Nth_Newest(2)) == "stardate 35100: player 1 declared war on player 2");
}

DOCTEST_TEST_CASE("the ring wraps and keeps the newest facts") {
    s_news_scope scope;
    test::s_game_state state;
    Setup_(&state);
    for (int i = 0; i < news::NEWS_ITEMS + 5; ++i) news::Add(news::KIND_COLONY_FOUNDED, 1, -1, i);
    DOCTEST_CHECK(news::Count() == news::NEWS_ITEMS);
    DOCTEST_CHECK(news::Nth_Newest(0)->value == news::NEWS_ITEMS + 4);
    DOCTEST_CHECK(news::Nth_Newest(news::NEWS_ITEMS - 1)->value == 5);
    DOCTEST_CHECK(news::Nth_Newest(news::NEWS_ITEMS) == nullptr);
}

DOCTEST_TEST_CASE("flags off: nothing is recorded") {
    s_news_scope scope;
    test::s_game_state state;
    Setup_(&state);
    news::enabled = false;
    news::Add(news::KIND_WAR, 1, 2, 0);
    news::Turn();
    DOCTEST_CHECK(news::Empty());
    news::s_item out[8];
    DOCTEST_CHECK(news::Recent_For(1, out, 8) == 0);
    // Intelligence alone records nothing without the news.
    news::intelligence = true;
    BILL::Set_Their_Spy_Number(0, 1, 3);
    DOCTEST_CHECK_FALSE(news::Report_Gathering(0, 1));
    MOX::stardate = 35105;
    news::Turn();
    DOCTEST_CHECK(news::Empty());
}

DOCTEST_TEST_CASE("ratings rank the empires by population, fleet and technologies") {
    s_news_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].total_pop = 10;
    state.players[1].total_pop = 30;
    state.players[2].total_pop = 20;
    state.players[3].total_pop = 20;
    Ship_(&state, 0, 2, 4);
    Ship_(&state, 1, 2, 4);
    Ship_(&state, 2, 0, 1);
    state.players[3].eliminated = 1;
    state.players[3].total_pop = 99;
    for (int i = 0; i < 3; ++i) state.players[0].tech_applications[i] = TECH_RESEARCH_STATUS_RESEARCHED;
    state.players[1].tech_applications[0] = TECH_RESEARCH_STATUS_RESEARCHED;
    MOX::stardate = 35100;  // a multiple of RATING_PERIOD
    news::Turn();
    const news::s_ratings& r = news::Ratings();
    DOCTEST_REQUIRE(r.stardate == 35100);
    DOCTEST_CHECK(r.order[news::RATING_POPULATION][0] == 1);
    DOCTEST_CHECK(r.order[news::RATING_POPULATION][1] == 2);
    DOCTEST_CHECK(r.order[news::RATING_POPULATION][2] == 0);
    DOCTEST_CHECK(r.order[news::RATING_POPULATION][3] == -1);  // the eliminated one is not ranked
    DOCTEST_CHECK(r.order[news::RATING_FLEET][0] == 2);
    DOCTEST_CHECK(r.order[news::RATING_FLEET][1] == 0);
    DOCTEST_CHECK(r.order[news::RATING_TECHNOLOGY][0] == 0);
    DOCTEST_CHECK(r.order[news::RATING_TECHNOLOGY][1] == 1);
    const news::s_item* fact = news::Nth_Newest(0);
    DOCTEST_REQUIRE(fact != nullptr);
    DOCTEST_CHECK(fact->kind == news::KIND_RATINGS);
    DOCTEST_CHECK(fact->subject == 1);
    DOCTEST_CHECK(fact->object == 2);
    DOCTEST_CHECK(fact->value == 0);
    // Not on the rating turn: no new ranking.
    const int32_t count = news::Count();
    MOX::stardate = 35101;
    news::Turn();
    DOCTEST_CHECK(news::Count() == count);
}

DOCTEST_TEST_CASE("an elimination is noticed after the first look")
{
    s_news_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::stardate = 35101;
    news::Turn();  // the baseline
    DOCTEST_CHECK(news::Count() == 0);
    state.players[2].eliminated = 1;
    MOX::stardate = 35102;
    news::Turn();
    DOCTEST_REQUIRE(news::Count() == 1);
    DOCTEST_CHECK(news::Nth_Newest(0)->kind == news::KIND_ELIMINATED);
    DOCTEST_CHECK(news::Nth_Newest(0)->subject == 2);
    news::Turn();
    DOCTEST_CHECK(news::Count() == 1);
}

DOCTEST_TEST_CASE("intelligence: fleets gather, only with spies, only for the reader") {
    s_news_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Ship_(&state, 0, 1, 6);
    Ship_(&state, 1, 1, 6);
    Ship_(&state, 2, 1, 9);
    // Two thirds of player 1's fleet stands at star 6.
    DOCTEST_CHECK(news::Gathering_Star(1) == 6);
    DOCTEST_CHECK_FALSE(news::Report_Gathering(0, 1));  // no spies
    BILL::Set_Their_Spy_Number(0, 1, 1);
    DOCTEST_CHECK(news::Report_Gathering(0, 1));
    DOCTEST_CHECK_FALSE(news::Report_Gathering(0, 1));  // already told
    news::s_item out[8];
    DOCTEST_CHECK(news::Recent_For(0, out, 8) == 1);
    DOCTEST_CHECK(out[0].kind == news::KIND_FLEETS_GATHER);
    DOCTEST_CHECK(out[0].value == 6);
    DOCTEST_CHECK(news::Recent_For(1, out, 8) == 0);  // the subject does not hear it
    DOCTEST_CHECK(news::Recent_For(2, out, 8) == 0);
    // A fleet scattered over four stars is no gathering.
    Ship_(&state, 3, 1, 10);
    Ship_(&state, 4, 1, 11);
    Ship_(&state, 5, 1, 12);
    Ship_(&state, 6, 1, 13);
    DOCTEST_CHECK(news::Gathering_Star(1) == -1);
}

DOCTEST_TEST_CASE("intelligence: eyes on, per spy and only with a plan") {
    s_news_scope scope;
    test::s_game_state state;
    Setup_(&state);
    BILL::Set_Their_Spy_Number(0, 1, 2);  // 40 per mille
    DOCTEST_CHECK_FALSE(news::Report_Eyes_On(0, 1, 0));  // no plan
    state.players[1].sneak_attack_player = 2;
    DOCTEST_CHECK_FALSE(news::Report_Eyes_On(0, 1, 40));
    DOCTEST_CHECK(news::Report_Eyes_On(0, 1, 39));
    news::s_item out[8];
    DOCTEST_REQUIRE(news::Recent_For(0, out, 8) == 1);
    DOCTEST_CHECK(out[0].kind == news::KIND_EYES_ON);
    DOCTEST_CHECK(out[0].subject == 1);
    DOCTEST_CHECK(out[0].object == 2);
    DOCTEST_CHECK(news::Recent_For(2, out, 8) == 0);  // the victim is not told by the reader's spies
    // Without the intelligence flag nothing.
    news::intelligence = false;
    state.players[1].sneak_attack_player = 3;
    BILL::Set_Their_Spy_Number(0, 1, 5);
    DOCTEST_CHECK_FALSE(news::Report_Eyes_On(0, 1, 0));
}

DOCTEST_TEST_CASE("the payload round trips and bad payloads are refused") {
    s_news_scope scope;
    test::s_game_state state;
    Setup_(&state);
    news::Add(news::KIND_WAR, 1, 2, 0);
    news::Add(news::KIND_FLEETS_GATHER, 1, -1, 6, 0);
    MOX::stardate = 35100;
    news::Make_Ratings();
    const uint32_t size = news::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    news::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    const int32_t count = news::Count();
    news::Clear();
    DOCTEST_CHECK(news::Empty());
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(news::Parse_Save(&reader, &error));
    DOCTEST_CHECK(news::Empty());  // not committed yet
    news::Commit_Save();
    DOCTEST_CHECK(news::Count() == count);
    DOCTEST_CHECK(news::Nth_Newest(count - 1)->kind == news::KIND_WAR);
    DOCTEST_CHECK(news::Nth_Newest(1)->kind == news::KIND_FLEETS_GATHER);
    DOCTEST_CHECK(news::Nth_Newest(1)->reader == 0);
    DOCTEST_CHECK(news::Ratings().stardate == 35100);
    DOCTEST_CHECK(news::Ratings().order[news::RATING_POPULATION][0] == 0);
    // A new fact goes after the loaded ones.
    news::Add(news::KIND_PEACE, 0, 1, 0);
    DOCTEST_CHECK(news::Nth_Newest(0)->kind == news::KIND_PEACE);
    DOCTEST_CHECK(news::Count() == count + 1);

    auto bad_kind = std::make_unique<uint8_t[]>(size);
    std::copy(buffer.get(), buffer.get() + size, bad_kind.get());
    bad_kind[size - 10 * count + 4] = 99;  // the first fact's kind
    SERIAL::Reader_ r1(bad_kind.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(news::Parse_Save(&r1, &error));

    auto bad_empire = std::make_unique<uint8_t[]>(size);
    std::copy(buffer.get(), buffer.get() + size, bad_empire.get());
    bad_empire[size - 10 * count + 5] = 100;  // the first fact's subject
    SERIAL::Reader_ r2(bad_empire.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(news::Parse_Save(&r2, &error));

    SERIAL::Reader_ r3(buffer.get(), size - 1, "test underflow", true);
    DOCTEST_CHECK_FALSE(news::Parse_Save(&r3, &error));
}


DOCTEST_TEST_CASE("intelligence: a wormhole of the subject, only with spies, only for the reader") {
    s_news_scope scope;
    test::s_game_state state;
    Setup_(&state);
    beasts::s_record wormhole;
    wormhole.kind = beasts::KIND_WORMHOLE;
    wormhole.player = 1;
    wormhole.target = 2;
    wormhole.other = 4;
    wormhole.started = MOX::stardate;
    wormhole.ends = MOX::stardate + 40;
    beasts::State()->record[beasts::State()->count++] = wormhole;
    DOCTEST_CHECK_FALSE(news::Report_Wormhole(0, 1));  // no spies
    BILL::Set_Their_Spy_Number(0, 1, 1);
    DOCTEST_CHECK_FALSE(news::Report_Wormhole(0, 2));  // player 2 holds none
    DOCTEST_CHECK(news::Report_Wormhole(0, 1));
    DOCTEST_CHECK_FALSE(news::Report_Wormhole(0, 1));  // already told
    news::s_item out[8];
    DOCTEST_REQUIRE(news::Recent_For(0, out, 8) == 1);
    DOCTEST_CHECK(out[0].kind == news::KIND_WORMHOLE);
    DOCTEST_CHECK(out[0].subject == 1);
    DOCTEST_CHECK(out[0].value == 4);
    DOCTEST_CHECK(news::Recent_For(1, out, 8) == 0);
    DOCTEST_CHECK(news::Recent_For(2, out, 8) == 0);
    DOCTEST_CHECK_FALSE(news::Describe(out[0]).empty());
    // The turn's spy round tells it too (stardate % 10 == 5); without events beasts or the intelligence flag, not.
    news::Clear();
    MOX::stardate = 35105;
    news::Turn();
    DOCTEST_CHECK(news::Recent_For(0, out, 8) >= 1);
    news::Clear();
    beasts::enabled = false;
    DOCTEST_CHECK_FALSE(news::Report_Wormhole(0, 1));
    beasts::enabled = true;
    news::intelligence = false;
    DOCTEST_CHECK_FALSE(news::Report_Wormhole(0, 1));
}

DOCTEST_TEST_CASE("the wormhole fact round-trips; a version 1 payload cannot hold it") {
    s_news_scope scope;
    test::s_game_state state;
    Setup_(&state);
    news::Add(news::KIND_WORMHOLE, 1, -1, 4, 0);
    const uint32_t size = news::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    news::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    news::Clear();
    std::string error;
    {
        SERIAL::Reader_ r(buffer.get(), size, "test underflow", true);
        DOCTEST_REQUIRE(news::Parse_Save(&r, &error));
        news::Commit_Save();
        DOCTEST_REQUIRE(news::Count() == 1);
        DOCTEST_CHECK(news::Nth_Newest(0)->kind == news::KIND_WORMHOLE);
        DOCTEST_CHECK(news::Nth_Newest(0)->value == 4);
    }
    buffer[0] = 1;
    SERIAL::Reader_ r1(buffer.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(news::Parse_Save(&r1, &error));
    news::Add(news::KIND_WAR, 1, 2, 0);
}
}
