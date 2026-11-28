#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/choices.h"
#include "game/incidents.h"
#include "game/random.h"
#include "game/serialization.h"

#include <memory>
#include <string>

namespace {
    struct s_choices_scope {
        test::s_saved_value<bool> enabled{choices::enabled};
        test::s_saved_value<bool> incidents_enabled{incidents::enabled};
        test::s_saved_value<int8_t> game_type{MOX::game_type};
        test::s_saved_value<decltype(NETMOX::net.is_network_active)> network{NETMOX::net.is_network_active};

        s_choices_scope() {
            choices::Clear();
            incidents::Clear();
            choices::enabled = true;
            incidents::enabled = true;
            MOX::game_type = 0;
            NETMOX::net.is_network_active = 0;
        }
        ~s_choices_scope() {
            choices::Clear();
            incidents::Clear();
        }
    };

    // Player 0 is human with a colony (4 people on a rich planet), player 1 a computer with one.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 2;
        MOX::NUM_STARS = 2;
        MOX::NUM_COLONIES = 2;
        MOX::stardate = 35001;
        state->players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
        state->players[1].objectives = PLAYER_OBJECTIVE_MILITARIST;
        for (int p = 0; p < 2; ++p) {
            state->players[p].bc = 1000;
            state->players[p].contact[1 - p] = 1;
        }
        for (int c = 0; c < 2; ++c) {
            state->planets[c].star_index = static_cast<int16_t>(c);
            state->planets[c].colony_index = static_cast<int16_t>(c);
            state->planets[c].mineral_class = 2;
            state->colonies[c].owner = static_cast<int8_t>(c);
            state->colonies[c].planet = static_cast<int16_t>(c);
            state->colonies[c].n_pops = static_cast<uint8_t>(4 - c);
        }
    }

    choices::s_choice Painless_Choice_(int16_t player, int16_t colony) {
        choices::s_choice c;
        c.module = choices::MODULE_INCIDENTS;
        c.kind = incidents::KIND_PAINLESS;
        c.player = static_cast<int8_t>(player);
        c.target = colony;
        c.asked = 35001;
        return c;
    }
} // namespace

DOCTEST_TEST_SUITE("choices") {

DOCTEST_TEST_CASE("Ask_Human: only a living human with the setting on, outside a network game")
{
    s_choices_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK(choices::Ask_Human(0));
    DOCTEST_CHECK_FALSE(choices::Ask_Human(1));   // a computer
    DOCTEST_CHECK_FALSE(choices::Ask_Human(5));   // no such empire
    DOCTEST_CHECK_FALSE(choices::Ask_Human(-1));
    choices::enabled = false;
    DOCTEST_CHECK_FALSE(choices::Ask_Human(0));   // the setting is off
    choices::enabled = true;
    state.players[0].eliminated = 1;
    DOCTEST_CHECK_FALSE(choices::Ask_Human(0));   // eliminated
    state.players[0].eliminated = 0;
    MOX::game_type = 2;
    DOCTEST_CHECK_FALSE(choices::Ask_Human(0));   // a network game
    MOX::game_type = 0;
    NETMOX::net.is_network_active = 1;
    DOCTEST_CHECK_FALSE(choices::Ask_Human(0));
    NETMOX::net.is_network_active = 0;
    DOCTEST_CHECK(choices::Ask_Human(0));
}

DOCTEST_TEST_CASE("Add holds MAX_PENDING choices and refuses more")
{
    s_choices_scope scope;
    test::s_game_state state;
    Setup_(&state);
    for (int i = 0; i < choices::MAX_PENDING; ++i) DOCTEST_REQUIRE(choices::Add(Painless_Choice_(i % 2, static_cast<int16_t>(i % 2))));
    DOCTEST_CHECK_FALSE(choices::Add(Painless_Choice_(0, 0)));
    DOCTEST_CHECK(choices::Count(0) == choices::MAX_PENDING / 2);
    DOCTEST_CHECK(choices::Count(1) == choices::MAX_PENDING / 2);
    DOCTEST_CHECK_FALSE(choices::Empty());
    choices::Clear();
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(choices::Count(0) == 0);
}

DOCTEST_TEST_CASE("Apply_Pending applies the first choice of that player and removes it")
{
    s_choices_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK_FALSE(choices::Apply_Pending(0, 0));  // nothing waits
    DOCTEST_REQUIRE(choices::Add(Painless_Choice_(1, 1)));
    DOCTEST_REQUIRE(choices::Add(Painless_Choice_(0, 0)));
    DOCTEST_REQUIRE(choices::Add(Painless_Choice_(0, 0)));
    DOCTEST_CHECK(choices::Apply_Pending(0, incidents::PAINLESS_QUARANTINE));
    DOCTEST_CHECK(choices::Count(0) == 1);
    DOCTEST_CHECK(choices::Count(1) == 1);
    DOCTEST_REQUIRE(incidents::State()->count == 1);
    DOCTEST_CHECK(incidents::State()->incident[0].kind == incidents::KIND_PAINLESS);
    DOCTEST_CHECK(incidents::State()->incident[0].player == 0);
    DOCTEST_CHECK(incidents::Morale_Bonus(&state.colonies[0]) == incidents::PAINLESS_QUARANTINE_MORALE);
    // The second one of player 0 finds the colony busy with the first: its subject is not free any more.
    DOCTEST_CHECK(choices::Apply_Pending(0, incidents::PAINLESS_QUARANTINE));
    DOCTEST_CHECK(incidents::State()->count == 1);
    DOCTEST_CHECK(choices::Count(0) == 0);
    DOCTEST_CHECK(choices::Count(1) == 1);
}

DOCTEST_TEST_CASE("Has_Pending reports a waiting choice without consuming it; a gone subject is dropped")
{
    s_choices_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK(choices::Has_Pending(0, false) == 0);
    DOCTEST_REQUIRE(choices::Add(Painless_Choice_(0, 0)));
    DOCTEST_CHECK(choices::Has_Pending(0, false) != 0);
    DOCTEST_CHECK(choices::Count(0) == 1);       // the probe keeps it
    DOCTEST_CHECK(choices::Has_Pending(1, false) == 0);
    state.colonies[0].owner = 1;                 // the colony is lost
    DOCTEST_CHECK(choices::Has_Pending(0, false) == 0);
    DOCTEST_CHECK(choices::Empty());
    // With the setting off the step is silent and leaves the queue alone.
    state.colonies[0].owner = 0;
    DOCTEST_REQUIRE(choices::Add(Painless_Choice_(0, 0)));
    choices::enabled = false;
    DOCTEST_CHECK(choices::Has_Pending(0, false) == 0);
    DOCTEST_CHECK(choices::Count(0) == 1);
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused")
{
    s_choices_scope scope;
    test::s_game_state state;
    Setup_(&state);
    choices::s_choice a = Painless_Choice_(0, 1);
    a.other = 7;
    a.value = 123456;
    DOCTEST_REQUIRE(choices::Add(a));
    DOCTEST_REQUIRE(choices::Add(Painless_Choice_(1, 1)));
    const uint32_t size = choices::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    choices::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    choices::Clear();
    choices::enabled = true;
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(choices::Parse_Save(&reader, &error));
    DOCTEST_CHECK(choices::Empty());
    choices::Commit_Save();
    DOCTEST_REQUIRE(choices::State()->count == 2);
    const choices::s_choice& r = choices::State()->choice[0];
    DOCTEST_CHECK(r.module == choices::MODULE_INCIDENTS);
    DOCTEST_CHECK(r.kind == incidents::KIND_PAINLESS);
    DOCTEST_CHECK(r.player == 0);
    DOCTEST_CHECK(r.target == 1);
    DOCTEST_CHECK(r.other == 7);
    DOCTEST_CHECK(r.value == 123456);
    DOCTEST_CHECK(r.asked == 35001);
    DOCTEST_CHECK(choices::Has_Pending(1, false) != 0);  // a loaded save still asks

    // Record layout: version (2), count (1), then 15 bytes a record from offset 3: module 3, kind 4, player 5,
    // target 6-7, other 8-9, value 10-13, asked 14-17 (little endian).
    const auto refuse = [&](size_t offset, uint8_t value) {
        auto bad = std::make_unique<uint8_t[]>(size);
        std::copy(buffer.get(), buffer.get() + size, bad.get());
        bad[offset] = value;
        SERIAL::Reader_ r2(bad.get(), size, "test underflow", true);
        return !choices::Parse_Save(&r2, &error);
    };
    DOCTEST_CHECK(refuse(3, choices::MODULE_LAST + 1));  // module
    DOCTEST_CHECK(refuse(3, 0));
    DOCTEST_CHECK(refuse(5, 9));        // player
    DOCTEST_CHECK(refuse(5, 0xFF));
    DOCTEST_CHECK(refuse(7, 0x80));     // target: the high byte makes it negative below -1
    DOCTEST_CHECK(refuse(9, 0x80));     // other, the same
    DOCTEST_CHECK(refuse(17, 0x80));    // asked: negative
    DOCTEST_CHECK(refuse(2, 30));       // too many records
    DOCTEST_CHECK(refuse(0, 7));        // the version
    SERIAL::Reader_ r4(buffer.get(), size - 1, "test underflow", true);
    DOCTEST_CHECK_FALSE(choices::Parse_Save(&r4, &error));
    DOCTEST_CHECK(choices::State()->count == 2);  // the failed parses left the state alone
}

} // DOCTEST_TEST_SUITE
