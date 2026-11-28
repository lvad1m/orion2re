#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "../fixtures/texts_fixture.h"
#include "game/choices.h"
#include "game/colcalc.h"
#include "game/events.h"
#include "game/mox.h"
#include "game/moo_world.h"
#include "game/opinion.h"
#include "game/pacts.h"
#include "game/pop.h"
#include "game/ultimatums.h"
#include "game/pirate_world.h"
#include "game/random.h"
#include "game/serialization.h"
#include "game/world_events.h"

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <string>

namespace {
    struct s_scope {
        test::s_saved_value<bool> enabled{moo_world::enabled};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<bool> pirates_enabled{pirate_world::enabled};
        test::s_saved_value<bool> pacts_enabled{pacts::enabled};
        test::s_saved_value<int16_t> pressure{MOX::antarans.attack_pressure};
        test::s_saved_value<EVENTS::s_pirate_raid> raid{EVENTS::pirate_raid};

        s_scope() {
            moo_world::Clear();
            pirate_world::Clear();
            opinion::Clear();
            pacts::Clear();
            MOX::antarans.attack_pressure = 0;
            moo_world::enabled = true;
            pirate_world::enabled = true;
            opinion::enabled = true;
            EVENTS::pirate_raid = {1, 29};
            for (int i = 0; i < world_events::MAX_ENTRIES; ++i) world_events::Entry_Slots()[i] = world_events::s_entry{};
        }
        ~s_scope() {
            moo_world::Clear();
            pirate_world::Clear();
            opinion::Clear();
            pacts::Clear();
            for (int i = 0; i < world_events::MAX_ENTRIES; ++i) world_events::Entry_Slots()[i] = world_events::s_entry{};
        }
    };

    struct s_draw {
        uint32_t n;
        uint32_t lo;
        uint32_t hi;
    };

    // Sets a seed whose next draws Random(n) fall in [lo, hi], one range per draw.
    void Seed_For_(std::initializer_list<s_draw> draws) {
        for (uint32_t seed = 1; seed < 1000000; ++seed) {
            game_random::Set_Random_Seed(seed);
            bool ok = true;
            for (const s_draw& d : draws) {
                const uint32_t v = game_random::Random(d.n);
                if (v < d.lo || v > d.hi) {
                    ok = false;
                    break;
                }
            }
            if (!ok) continue;
            game_random::Set_Random_Seed(seed);
            return;
        }
    }

    // Three computer empires that know each other, 1000 BC and 5 freighters each. Stars 0-2 (x = 0, 10, 20) hold
    // their colonies (one each); stars 3-5 (x = 30, 40, 50) are neutral; star 6 is far away (x = 5000). Every ship
    // is retired (status 5), the tests put their own into orbit.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 3;
        MOX::NUM_STARS = 7;
        MOX::NUM_COLONIES = 3;
        MOX::NUM_SHIPS = 8;
        MOX::stardate = 35001;
        for (int p = 0; p < 3; ++p) {
            state->players[p].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[p].personality = PLAYER_PERSONALITY_ERRATIC;
            state->players[p].bc = 1000;
            state->players[p].n_freighters = 5;
            state->players[p].eliminated = 0;
            for (int q = 0; q < 3; ++q) {
                state->players[p].contact[q] = p == q ? 0 : 1;
                state->players[p].spies[q] = 0;
            }
        }
        for (int s = 0; s < 7; ++s) {
            state->stars[s].x = static_cast<int16_t>(s == 6 ? 5000 : 10 * s);
            state->stars[s].y = 0;
            state->stars[s].has_colony = s < 3 ? static_cast<uint8_t>(1 << s) : 0;
        }
        for (int c = 0; c < 3; ++c) {
            state->planets[c].star_index = static_cast<int16_t>(c);
            state->planets[c].colony_index = static_cast<int16_t>(c);
            state->colonies[c].owner = static_cast<int8_t>(c);
            state->colonies[c].planet = static_cast<int16_t>(c);
            state->colonies[c].outpost_flag = 0;
            state->colonies[c].n_pops = 3;
        }
        for (auto& ship : state->ships) ship.status = 5;
    }

    void Put_Ship_(test::s_game_state* state, int ship, int owner, int star, int size = 0) {
        state->ships[ship].owner = static_cast<int8_t>(owner);
        state->ships[ship].status = 0;
        state->ships[ship].location = static_cast<int16_t>(star);
        state->ships[ship].officer_index = -1;
        state->ships[ship].d.size = static_cast<int16_t>(size);
    }

    void Put_Fleet_(test::s_game_state* state, int owner, int star) {
        for (int i = 0; i < 3; ++i) Put_Ship_(state, i, owner, star);
    }

    void Put_Lair_(int star, int value) {
        pirate_world::s_record r;
        r.kind = pirate_world::KIND_LAIR;
        r.target = static_cast<int16_t>(star);
        r.value = r.extra = static_cast<int16_t>(value);
        r.started = MOX::stardate;
        pirate_world::State()->record[pirate_world::State()->count++] = r;
    }

    void Put_Hired_(int star, int value) {
        world_events::s_entry e;
        e.kind = world_events::KIND_HIRED_PIRATES;
        e.player = 0;
        e.target = static_cast<int16_t>(star);
        e.started = MOX::stardate;
        e.ends = MOX::stardate + 30;
        e.value = e.extra = static_cast<int16_t>(value);
        e.hirer = 1;
        world_events::Entry_Slots()[0] = e;
    }

    const moo_world::s_record* Find_(moo_world::e_kind kind) {
        const moo_world::s_state* s = moo_world::State();
        for (int32_t i = 0; i < s->count; ++i) {
            if (s->record[i].kind == kind) return &s->record[i];
        }
        return nullptr;
    }

    void Run_(int turns) {
        for (int i = 0; i < turns; ++i) {
            ++MOX::stardate;
            moo_world::Run_Records();
        }
    }
}

DOCTEST_TEST_CASE("flag off: nothing starts, nothing is kept, nothing draws")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Put_Fleet_(&state, 0, 3);
    state.players[0].spies[1] = 30;
    Put_Lair_(3, 8);
    Put_Lair_(4, 9);
    moo_world::enabled = false;

    game_random::Set_Random_Seed(77);
    const uint32_t expected = game_random::Random(1000);
    game_random::Set_Random_Seed(77);
    for (int i = 0; i < 30; ++i) moo_world::Turn();
    DOCTEST_CHECK(game_random::Random(1000) == expected);
    DOCTEST_CHECK(moo_world::Empty());
    DOCTEST_CHECK(state.players[0].spies[1] == 30);
    DOCTEST_CHECK_FALSE(moo_world::Start_Epidemic(0, 3, 3));
    DOCTEST_CHECK_FALSE(moo_world::Start_Scandal(0, 1));
    DOCTEST_CHECK_FALSE(moo_world::Raise_King(3, 20));
    DOCTEST_CHECK_FALSE(moo_world::Star_Grounded(3));
    DOCTEST_CHECK_FALSE(moo_world::King_Near(0));
    DOCTEST_CHECK(moo_world::Choose(0) == 0);
    DOCTEST_CHECK(pirate_world::State()->count == 2);  // the lairs are untouched
}

DOCTEST_TEST_CASE("an epidemic grounds a star for its turns, then ends")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(moo_world::Start_Epidemic(1, 3, 3));
    DOCTEST_CHECK(moo_world::Star_Grounded(3));
    DOCTEST_CHECK_FALSE(moo_world::Star_Grounded(4));
    DOCTEST_CHECK_FALSE(moo_world::Start_Epidemic(1, 3, 2));  // the star is grounded already
    DOCTEST_CHECK_FALSE(moo_world::Start_Epidemic(1, 4, 0));
    DOCTEST_CHECK_FALSE(moo_world::Start_Epidemic(1, -1, 3));
    Run_(2);
    DOCTEST_CHECK(moo_world::Star_Grounded(3));
    Run_(1);
    DOCTEST_CHECK_FALSE(moo_world::Star_Grounded(3));
    DOCTEST_CHECK(moo_world::Empty());
}

DOCTEST_TEST_CASE("an epidemic starts by chance in a fleet of three, lasting two to five turns")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Put_Ship_(&state, 0, 0, 3);
    Put_Ship_(&state, 1, 0, 3);
    Seed_For_({{1000, 1, 3}});
    moo_world::Turn();
    DOCTEST_CHECK(moo_world::Empty());  // two ships are no fleet

    Put_Ship_(&state, 2, 0, 3);
    Seed_For_({{1000, 1, 3}, {4, 4, 4}});
    moo_world::Turn();
    const moo_world::s_record* r = Find_(moo_world::KIND_FLEET_EPIDEMIC);
    DOCTEST_REQUIRE(r != nullptr);
    DOCTEST_CHECK(r->player == 0);
    DOCTEST_CHECK(r->target == 3);
    DOCTEST_CHECK(r->ends - r->started == 5);
    DOCTEST_CHECK(moo_world::Star_Grounded(3));

    moo_world::Clear();
    moo_world::enabled = true;
    Seed_For_({{1000, 300, 1000}});
    moo_world::Turn();
    DOCTEST_CHECK(moo_world::Empty());  // the chance missed
}

DOCTEST_TEST_CASE("a human fleet falls ill too; the biggest fleet is the one")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[2].objectives = PLAYER_OBJECTIVE_HUMAN;
    Put_Fleet_(&state, 2, 4);
    Put_Ship_(&state, 3, 2, 5);
    Put_Ship_(&state, 4, 2, 5);
    Put_Ship_(&state, 5, 2, 5);
    Put_Ship_(&state, 6, 2, 5);
    Seed_For_({{1000, 1, 3}, {4, 1, 1}});
    moo_world::Turn();
    const moo_world::s_record* r = Find_(moo_world::KIND_FLEET_EPIDEMIC);
    DOCTEST_REQUIRE(r != nullptr);
    DOCTEST_CHECK(r->player == 2);
    DOCTEST_CHECK(r->target == 5);
    DOCTEST_CHECK(r->ends - r->started == 2);
}

DOCTEST_TEST_CASE("a spy scandal costs the owner its spies and is remembered")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].spies[1] = 0x40 | 5;  // 5 spies; the upper bits belong to other things
    state.players[0].spies[2] = 3;
    DOCTEST_REQUIRE(moo_world::Start_Scandal(0, 1));
    DOCTEST_CHECK((state.players[0].spies[1] & 0x3F) == 0);
    DOCTEST_CHECK((state.players[0].spies[1] & 0xC0) == 0x40);
    DOCTEST_CHECK((state.players[0].spies[2] & 0x3F) == 3);  // its network in the third empire is untouched
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_CAUGHT_SPYING) == moo_world::SCANDAL_OPINION);
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_BAD_RUMOURS) == moo_world::SCANDAL_RUMOUR);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_BAD_RUMOURS) == 0);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_CAUGHT_SPYING) == 0);
    DOCTEST_CHECK_FALSE(moo_world::Start_Scandal(0, 1));  // no spies left
    DOCTEST_CHECK_FALSE(moo_world::Start_Scandal(1, 1));
}

DOCTEST_TEST_CASE("rumours reach only the empires in contact with the owner")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].spies[1] = 4;
    state.players[0].contact[2] = 0;
    state.players[2].contact[0] = 0;
    DOCTEST_REQUIRE(moo_world::Start_Scandal(0, 1));
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_BAD_RUMOURS) == 0);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_CAUGHT_SPYING) == moo_world::SCANDAL_OPINION);
}

DOCTEST_TEST_CASE("the scandal chance grows with the number of spies")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].spies[1] = 30;  // 60 per mille
    Seed_For_({{1000, 61, 1000}});
    moo_world::Turn();
    DOCTEST_CHECK((state.players[0].spies[1] & 0x3F) == 30);  // the draw missed
    Seed_For_({{1000, 31, 60}});
    moo_world::Turn();
    DOCTEST_CHECK((state.players[0].spies[1] & 0x3F) == 0);   // a draw a small network would have survived
    state.players[0].spies[1] = 1;  // 2 per mille
    Seed_For_({{1000, 31, 60}});
    moo_world::Turn();
    DOCTEST_CHECK((state.players[0].spies[1] & 0x3F) == 1);
}

DOCTEST_TEST_CASE("the pirates unite after UNITE_TURNS: lairs and hired pirates become a king")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Put_Lair_(3, 8);
    Put_Lair_(4, 12);
    Put_Hired_(5, 6);
    for (int i = 0; i < moo_world::UNITE_TURNS - 1; ++i) moo_world::Turn();
    DOCTEST_CHECK(Find_(moo_world::KIND_PIRATE_KING) == nullptr);
    DOCTEST_CHECK(moo_world::State()->unite_turns == moo_world::UNITE_TURNS - 1);
    moo_world::Turn();
    const moo_world::s_record* king = Find_(moo_world::KIND_PIRATE_KING);
    DOCTEST_REQUIRE(king != nullptr);
    DOCTEST_CHECK(king->target == 4);          // the strongest lair
    DOCTEST_CHECK(king->value == 26);          // the sum
    DOCTEST_CHECK(pirate_world::State()->count == 0);
    DOCTEST_CHECK(world_events::Entry_Slots()[0].kind == world_events::KIND_NONE);
    DOCTEST_CHECK(moo_world::State()->unite_turns == 0);
    DOCTEST_CHECK_FALSE(moo_world::Raise_King(3, 10));  // one king at a time
}

DOCTEST_TEST_CASE("one pirate presence does not unite; a break resets the count")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Put_Lair_(3, 8);
    for (int i = 0; i < 30; ++i) moo_world::Turn();
    DOCTEST_CHECK(moo_world::Empty());
    Put_Lair_(4, 8);
    for (int i = 0; i < 5; ++i) moo_world::Turn();
    DOCTEST_CHECK(moo_world::State()->unite_turns == 5);
    pirate_world::State()->count = 1;  // one lair is put down
    moo_world::Turn();
    DOCTEST_CHECK(moo_world::State()->unite_turns == 0);
}

DOCTEST_TEST_CASE("two hired pirate bands unite where there is no lair")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Put_Hired_(5, 6);
    world_events::s_entry second = world_events::Entry_Slots()[0];
    second.target = 4;
    second.value = 9;
    world_events::Entry_Slots()[1] = second;
    moo_world::State()->unite_turns = moo_world::UNITE_TURNS - 1;
    moo_world::Turn();
    const moo_world::s_record* king = Find_(moo_world::KIND_PIRATE_KING);
    DOCTEST_REQUIRE(king != nullptr);
    DOCTEST_CHECK(king->target == 4);
    DOCTEST_CHECK(king->value == 15);
}

DOCTEST_TEST_CASE("the king asks for tribute: pay, haggle or refuse by personality")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].personality = PLAYER_PERSONALITY_HONORABLE;
    state.players[1].personality = PLAYER_PERSONALITY_ERRATIC;
    state.players[2].personality = PLAYER_PERSONALITY_RUTHLESS;
    DOCTEST_CHECK(moo_world::Choose(0) == 0);
    DOCTEST_CHECK(moo_world::Choose(1) == 1);
    DOCTEST_CHECK(moo_world::Choose(2) == 2);
    DOCTEST_REQUIRE(moo_world::Raise_King(3, 20));
    DOCTEST_CHECK(moo_world::King_Near(0));
    Run_(moo_world::KING_PERIOD - 1);
    DOCTEST_CHECK(state.players[0].bc == 1000);  // nothing before the period
    // The turn of the demand: the pirates' own draw, then the haggle (accepted), then the raid.
    Seed_For_({{100, 1, 100}, {100, 1, 50}});
    Run_(1);
    DOCTEST_CHECK(state.players[0].bc == 800);   // 20 percent paid, no raid
    DOCTEST_CHECK(state.players[0].n_freighters == 5);
    DOCTEST_CHECK(state.players[1].bc == 900);   // haggled: half, accepted
    DOCTEST_CHECK(state.players[1].n_freighters == 5);
    DOCTEST_CHECK(state.players[2].bc < 1000);   // refused: raided
    DOCTEST_CHECK(state.players[2].n_freighters == 5 - moo_world::KING_FREIGHTERS);
    DOCTEST_CHECK(Find_(moo_world::KIND_PIRATE_KING) != nullptr);
}

DOCTEST_TEST_CASE("a failed haggle and a poor treasury mean a raid; a distant empire is left alone")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].personality = PLAYER_PERSONALITY_ERRATIC;
    state.players[1].personality = PLAYER_PERSONALITY_HONORABLE;
    state.players[1].bc = 39;
    state.stars[2].x = 5000;  // player 2's colony is out of reach
    DOCTEST_REQUIRE(moo_world::Raise_King(3, 20));
    DOCTEST_CHECK_FALSE(moo_world::King_Near(2));
    Run_(moo_world::KING_PERIOD - 1);
    Seed_For_({{100, 1, 100}, {100, 52, 100}});
    Run_(1);
    DOCTEST_CHECK(state.players[0].bc < 1000);
    DOCTEST_CHECK(state.players[0].n_freighters == 3);
    DOCTEST_CHECK(state.players[1].bc <= 39);
    DOCTEST_CHECK(state.players[1].n_freighters == 3);
    DOCTEST_CHECK(state.players[2].bc == 1000);
    DOCTEST_CHECK(state.players[2].n_freighters == 5);
}

DOCTEST_TEST_CASE("a human pays the tribute")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.players[1].personality = PLAYER_PERSONALITY_RUTHLESS;
    DOCTEST_CHECK(moo_world::Choose(1) == 0);
    DOCTEST_REQUIRE(moo_world::Raise_King(3, 20));
    Run_(moo_world::KING_PERIOD);
    DOCTEST_CHECK(state.players[1].bc == 800);
    DOCTEST_CHECK(state.players[1].n_freighters == 5);
}

DOCTEST_TEST_CASE("warships wear the king down and the winner takes the loot")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].personality = PLAYER_PERSONALITY_HONORABLE;
    DOCTEST_REQUIRE(moo_world::Raise_King(3, 20));
    Put_Ship_(&state, 0, 1, 3, 29);   // 30 ship points at the king's star
    Put_Ship_(&state, 1, 2, 3, 3);
    const int32_t before = state.players[1].bc;
    Run_(1);
    DOCTEST_CHECK(Find_(moo_world::KIND_PIRATE_KING) == nullptr);
    DOCTEST_CHECK(state.players[1].bc == before + moo_world::KING_LOOT + moo_world::KING_LOOT_PER_STRENGTH * 20);
    DOCTEST_CHECK(state.players[2].bc == 1000);
}

DOCTEST_TEST_CASE("a king nobody fights disbands after KING_TURNS; a weak fleet only wears him down")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].personality = PLAYER_PERSONALITY_HONORABLE;
    state.players[1].personality = PLAYER_PERSONALITY_HONORABLE;
    state.players[2].personality = PLAYER_PERSONALITY_HONORABLE;
    DOCTEST_REQUIRE(moo_world::Raise_King(3, 20));
    Put_Ship_(&state, 0, 1, 3, 4);  // 5 points a turn
    Run_(1);
    const moo_world::s_record* king = Find_(moo_world::KIND_PIRATE_KING);
    DOCTEST_REQUIRE(king != nullptr);
    DOCTEST_CHECK(king->value == 15);
    state.ships[0].status = 5;
    Run_(moo_world::KING_TURNS - 2);
    DOCTEST_CHECK(Find_(moo_world::KIND_PIRATE_KING) != nullptr);
    Run_(1);
    DOCTEST_CHECK(moo_world::Empty());
}

DOCTEST_TEST_CASE("the records stop when an empire is gone; a full table refuses")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(moo_world::Start_Epidemic(1, 3, 5));
    state.players[1].eliminated = 1;
    Run_(1);
    DOCTEST_CHECK(moo_world::Empty());
    state.players[1].eliminated = 0;
    for (int i = 0; i < moo_world::MAX_RECORDS; ++i) DOCTEST_CHECK(moo_world::Start_Epidemic(0, static_cast<int16_t>(i), 5));
    DOCTEST_CHECK_FALSE(moo_world::Start_Epidemic(0, 100, 5));
    DOCTEST_CHECK_FALSE(moo_world::Raise_King(3, 20));
}

DOCTEST_TEST_CASE("payload round trip and bad values refused")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    moo_world::State()->count = 2;
    moo_world::State()->unite_turns = 7;
    moo_world::State()->exile_mask = 0x04;
    moo_world::State()->guilty_mask = 0x03;
    moo_world::State()->agreed_mask = 0x01;
    moo_world::State()->last_scout = 35010;
    {
        moo_world::s_record& a = moo_world::State()->record[0];
        a.kind = moo_world::KIND_PIRATE_KING; a.player = -1; a.target = 4; a.other = -1; a.started = 35001; a.ends = 35061; a.value = 15; a.extra = 26;
        moo_world::s_record& b = moo_world::State()->record[1];
        b.kind = moo_world::KIND_FLEET_EPIDEMIC; b.player = 1; b.target = 3; b.other = -1; b.started = 35001; b.ends = 35004;
    }
    const uint32_t size = moo_world::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    moo_world::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    moo_world::Clear();
    moo_world::enabled = true;
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(moo_world::Parse_Save(&reader, &error));
    DOCTEST_CHECK(moo_world::Empty());
    moo_world::Commit_Save();
    DOCTEST_REQUIRE(moo_world::State()->count == 2);
    DOCTEST_CHECK(moo_world::State()->unite_turns == 7);
    DOCTEST_CHECK(moo_world::State()->exile_mask == 0x04);
    DOCTEST_CHECK(moo_world::State()->guilty_mask == 0x03);
    DOCTEST_CHECK(moo_world::State()->agreed_mask == 0x01);
    DOCTEST_CHECK(moo_world::State()->last_scout == 35010);
    DOCTEST_CHECK(moo_world::State()->record[0].extra == 26);
    DOCTEST_CHECK(moo_world::State()->record[1].player == 1);
    DOCTEST_CHECK(moo_world::State()->record[1].ends == 35004);

    // Layout: version (2), count (1), unite turns (2), three masks (6), last scout (4), then 19 bytes a record:
    // kind 0, player 1, target 2-3, other 4-5, started 6-9, ends 10-13, option 14, value 15-16, extra 17-18.
    // Record 0 at 15, record 1 at 34.
    const auto refuse = [&](size_t offset, uint8_t value) {
        auto bad = std::make_unique<uint8_t[]>(size);
        std::copy(buffer.get(), buffer.get() + size, bad.get());
        bad[offset] = value;
        SERIAL::Reader_ r(bad.get(), size, "test underflow", true);
        return !moo_world::Parse_Save(&r, &error);
    };
    DOCTEST_CHECK(refuse(15, 9));            // a bad kind
    DOCTEST_CHECK(refuse(15, 0));
    DOCTEST_CHECK(refuse(15 + 1, 0x7F));     // a bad empire
    DOCTEST_CHECK(refuse(34 + 1, 0xFF));     // an epidemic without an empire
    DOCTEST_CHECK(refuse(15 + 3, 0x7F));     // a star out of range
    DOCTEST_CHECK(refuse(15 + 5, 0x7F));     // a second star out of range
    DOCTEST_CHECK(refuse(15 + 9, 0x80));     // a negative stardate
    DOCTEST_CHECK(refuse(15 + 16, 0x7F));    // a bad value
    DOCTEST_CHECK(refuse(4, 0x80));          // a negative unite counter
    DOCTEST_CHECK(refuse(6, 0xFF));          // an empire mask with a bit beyond the empires
    DOCTEST_CHECK(refuse(14, 0x80));         // a negative scout stardate
    DOCTEST_CHECK(refuse(2, 30));            // too many records
    DOCTEST_CHECK(refuse(0, 7));             // the version
    SERIAL::Reader_ r4(buffer.get(), size - 1, "test underflow", true);
    DOCTEST_CHECK_FALSE(moo_world::Parse_Save(&r4, &error));
    DOCTEST_CHECK(moo_world::State()->count == 2);  // the failed parses left the state alone
}

DOCTEST_TEST_CASE("payload version 1 is still read; refugee and scout records are checked")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    // A version 1 payload: version, count, unite turns, one epidemic record.
    constexpr uint32_t v1_size = 2 + 1 + 2 + 19;
    uint8_t v1[v1_size];
    {
        SERIAL::Writer_ w(v1, v1_size, "test overflow");
        w.Write_Uint16(1);
        w.Write_Uint8(1);
        w.Write_Int16(9);
        w.Write_Uint8(moo_world::KIND_FLEET_EPIDEMIC);
        w.Write_Int8(1);
        w.Write_Int16(3);
        w.Write_Int16(-1);
        w.Write_Int32(35001);
        w.Write_Int32(35004);
        w.Write_Int8(0);
        w.Write_Int16(0);
        w.Write_Int16(0);
        DOCTEST_REQUIRE(w.Length() == v1_size);
    }
    SERIAL::Reader_ reader(v1, v1_size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(moo_world::Parse_Save(&reader, &error));
    moo_world::Commit_Save();
    DOCTEST_CHECK(moo_world::State()->count == 1);
    DOCTEST_CHECK(moo_world::State()->unite_turns == 9);
    DOCTEST_CHECK(moo_world::State()->exile_mask == 0);
    DOCTEST_CHECK(moo_world::State()->last_scout == 0);

    // Refugees: the aggressor and the race are checked.
    moo_world::Clear();
    moo_world::enabled = true;
    moo_world::s_record r;
    r.kind = moo_world::KIND_REFUGEES;
    r.player = 0;
    r.option = 1;
    r.target = 0;
    r.started = 35001;
    r.ends = 35004;
    r.value = 0;
    moo_world::State()->count = 1;
    moo_world::State()->record[0] = r;
    const auto round_trip = [&]() {
        const uint32_t size = moo_world::Save_Size();
        auto buffer = std::make_unique<uint8_t[]>(size);
        SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
        moo_world::Write_Save(&writer);
        SERIAL::Reader_ rd(buffer.get(), size, "test underflow", true);
        return moo_world::Parse_Save(&rd, &error);
    };
    DOCTEST_CHECK(round_trip());
    moo_world::State()->record[0].option = -1;
    DOCTEST_CHECK_FALSE(round_trip());
    moo_world::State()->record[0].option = 1;
    moo_world::State()->record[0].value = 12;
    DOCTEST_CHECK_FALSE(round_trip());
    moo_world::State()->record[0].value = 3;
    moo_world::State()->record[0].player = -1;
    DOCTEST_CHECK_FALSE(round_trip());
}

DOCTEST_TEST_CASE("flag off: the five events of the second part do nothing")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    moo_world::enabled = false;
    state.players[2].eliminated = 1;
    state.leaders[5].player_index = 2;
    state.leaders[5].status = 0;
    state.players[0].biological_weapons_flag[1] = 1;
    MOX::antarans.attack_pressure = 9;
    game_random::Set_Random_Seed(77);
    const uint32_t first = game_random::Random(1000000);
    game_random::Set_Random_Seed(77);
    DOCTEST_CHECK_FALSE(moo_world::Start_Exile(2));
    DOCTEST_CHECK_FALSE(moo_world::Start_Defection(0));
    DOCTEST_CHECK_FALSE(moo_world::Start_Scout(0, 0));
    DOCTEST_CHECK_FALSE(moo_world::Council_Demand(0));
    DOCTEST_CHECK(moo_world::Refugee_Choice(0) == 0);
    moo_world::On_Colony_Taken(&state.colonies[0], 0, 1);
    moo_world::Turn();
    DOCTEST_CHECK(moo_world::Empty());
    DOCTEST_CHECK_FALSE(moo_world::Scout_Near(0));
    DOCTEST_CHECK(state.leaders[5].player_index == 2);
    DOCTEST_CHECK(game_random::Random(1000000) == first);
}

DOCTEST_TEST_CASE("famous exile: the best leader of a fallen empire is hired by the computer that liked it most")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    opinion::Add(1, 2, opinion::REASON_DEFENDED_US, 30);  // empire 1 admired the empire while it lived
    state.players[2].eliminated = 1;
    for (int i : {5, 6}) {
        s_leader_data& l = state.leaders[i];
        l.player_index = 2;
        l.status = 0;
        l.type = LEADER_TYPE_SHIP;
        l.skill_value = 1;
        l.xp = static_cast<int16_t>(i == 6 ? 1000 : 0);
        l.location = -1;
    }
    const int32_t bc = state.players[1].bc;
    DOCTEST_REQUIRE(moo_world::Start_Exile(2));
    DOCTEST_CHECK(state.leaders[6].player_index == 1);
    DOCTEST_CHECK(state.leaders[6].status == 0);
    DOCTEST_CHECK(state.players[1].bc < bc);
    DOCTEST_CHECK(state.leaders[5].player_index == 2);  // only the best one offers service
    DOCTEST_CHECK_FALSE(moo_world::Start_Exile(2));       // once
    DOCTEST_CHECK_FALSE(moo_world::Empty());               // the exile mask is kept
}

DOCTEST_TEST_CASE("famous exile: nobody who can pay, the leader waits on the hire list; no leader, nothing")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[2].eliminated = 1;
    for (auto& p : state.players) p.bc = 0;
    s_leader_data& l = state.leaders[5];
    l.player_index = 2;
    l.status = 1;
    l.type = LEADER_TYPE_SHIP;
    l.skill_value = 1;
    l.xp = 100;
    l.location = -1;
    moo_world::Turn();  // the rule finds the dead empire by itself
    DOCTEST_CHECK(l.status == -1);
    DOCTEST_CHECK(l.player_index == -1);
    // A second dead empire without leaders.
    state.players[1].eliminated = 1;
    DOCTEST_CHECK_FALSE(moo_world::Start_Exile(1));
    DOCTEST_CHECK_FALSE(moo_world::Start_Exile(0));  // alive
}

DOCTEST_TEST_CASE("scientist defection: an unstable empire loses its scientist to the neighbour it likes best, with a technology")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    s_leader_data& sci = state.leaders[7];
    sci.player_index = 0;
    sci.status = 0;
    sci.type = LEADER_TYPE_COLONY;
    sci.general_skills = LEADER_GENERAL_SKILL_RESEARCHER;
    sci.skill_value = 1;
    sci.location = -1;
    int app = -1;
    for (int a = 1; a < TECH_APP_COUNT && app < 0; ++a) {
        if (COLCALC::Player_May_Get_Tech_App(&state.players[1], a)) app = a;
    }
    DOCTEST_REQUIRE(app > 0);
    state.players[0].tech_applications[app] = TECH_RESEARCH_STATUS_RESEARCHED;
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;  // a human's designs only refresh their equipment (no allocator here)
    state.players[2].objectives = PLAYER_OBJECTIVE_HUMAN;
    // Stable colonies: nothing happens.
    state.colonies[0].morale = 0;
    DOCTEST_CHECK_FALSE(moo_world::Start_Defection(0));
    state.colonies[0].morale = static_cast<int8_t>(moo_world::DEFECT_MORALE);
    opinion::Add(0, 2, opinion::REASON_DEFENDED_US, 30);  // empire 2 is the one it likes
    state.players[0].spies[2] = 2;                        // its spies saw who took it
    const int tech_before = state.players[2].tech_applications[app];
    DOCTEST_REQUIRE(moo_world::Start_Defection(0));
    DOCTEST_CHECK(sci.player_index == 2);
    DOCTEST_CHECK(state.players[2].tech_applications[app] != tech_before);
    DOCTEST_CHECK(state.players[1].tech_applications[app] == 0);
    DOCTEST_CHECK(opinion::Modifier(0, 2, opinion::REASON_PLOTTED_AGAINST_US) == moo_world::DEFECT_OPINION);
    DOCTEST_CHECK_FALSE(moo_world::Start_Defection(0));  // no scientist left
}

DOCTEST_TEST_CASE("scientist defection: unseen it is not remembered; a human owner keeps its scientist; the chance draws")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    s_leader_data& sci = state.leaders[7];
    sci.player_index = 0;
    sci.status = 1;
    sci.type = LEADER_TYPE_COLONY;
    sci.special_skills = LEADER_COLONY_SKILL_SCIENCE_LEADER;
    sci.skill_value = 1;
    sci.location = -1;
    int app = -1;
    for (int a = 1; a < TECH_APP_COUNT && app < 0; ++a) {
        if (COLCALC::Player_May_Get_Tech_App(&state.players[1], a)) app = a;
    }
    DOCTEST_REQUIRE(app > 0);
    state.players[0].tech_applications[app] = TECH_RESEARCH_STATUS_RESEARCHED;
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;  // a human's designs only refresh their equipment (no allocator here)
    state.players[2].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.colonies[0].morale = static_cast<int8_t>(moo_world::DEFECT_MORALE);
    // A bad draw: nothing.
    Seed_For_({{1000, 5, 1000}});
    ++MOX::stardate;
    moo_world::Turn();
    DOCTEST_CHECK(sci.player_index == 0);
    // A good draw.
    Seed_For_({{1000, 1, 4}});
    ++MOX::stardate;
    moo_world::Turn();
    DOCTEST_REQUIRE(sci.player_index != 0);
    DOCTEST_CHECK(sci.status == 0);
    DOCTEST_CHECK(opinion::Modifier(0, sci.player_index, opinion::REASON_PLOTTED_AGAINST_US) == 0);
    // A human owner is not driven by this step.
    sci.player_index = 0;
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_CHECK_FALSE(moo_world::Start_Defection(0));
}

DOCTEST_TEST_CASE("the council's demand: agree or refuse by personality, and a repeat is a broken word")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].personality = PLAYER_PERSONALITY_HONORABLE;
    state.players[1].personality = PLAYER_PERSONALITY_RUTHLESS;
    state.players[0].biological_weapons_flag[2] = 1;
    state.players[1].biological_weapons_flag[2] = 1;
    ++MOX::stardate;
    moo_world::Turn();
    // The honorable agrees: a small good memory with the others.
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_DID_US_A_SERVICE) == moo_world::COUNCIL_AGREE_OPINION);
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_DID_US_A_SERVICE) == moo_world::COUNCIL_AGREE_OPINION);
    // The ruthless refuses: bad rumours with everybody who knows it.
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_BAD_RUMOURS) == moo_world::COUNCIL_REFUSE_OPINION);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_BAD_RUMOURS) == moo_world::COUNCIL_REFUSE_OPINION);
    // Not told again while it goes on.
    ++MOX::stardate;
    moo_world::Turn();
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_BAD_RUMOURS) == moo_world::COUNCIL_REFUSE_OPINION);
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_DID_US_A_SERVICE) == moo_world::COUNCIL_AGREE_OPINION);
    // The weapons rest, then the honorable uses them again: a broken word.
    state.players[0].biological_weapons_flag[2] = 0;
    ++MOX::stardate;
    moo_world::Turn();
    state.players[0].biological_weapons_flag[2] = 1;
    ++MOX::stardate;
    moo_world::Turn();
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_BROKE_ITS_WORD) == moo_world::COUNCIL_BROKE_OPINION);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_BROKE_ITS_WORD) == moo_world::COUNCIL_BROKE_OPINION);
    DOCTEST_CHECK((moo_world::State()->agreed_mask & 1) == 0);
}

DOCTEST_TEST_CASE("the council's demand: a human agrees; an empire out of contact does not hear")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.players[0].personality = PLAYER_PERSONALITY_RUTHLESS;  // a human is not driven by its personality
    state.players[0].contact[2] = 0;
    state.players[2].contact[0] = 0;
    DOCTEST_REQUIRE(moo_world::Council_Demand(0));
    DOCTEST_CHECK((moo_world::State()->agreed_mask & 1) != 0);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_DID_US_A_SERVICE) == moo_world::COUNCIL_AGREE_OPINION);
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_DID_US_A_SERVICE) == 0);
}

DOCTEST_TEST_CASE("the Antaran scout: seen under pressure, once in a cooldown; neighbours draw together")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    // No pressure, no scout.
    ++MOX::stardate;
    moo_world::Turn();
    DOCTEST_CHECK(Find_(moo_world::KIND_ANTARAN_SCOUT) == nullptr);
    MOX::antarans.attack_pressure = moo_world::SCOUT_PRESSURE;
    ++MOX::stardate;
    moo_world::Turn();
    const moo_world::s_record* scout = Find_(moo_world::KIND_ANTARAN_SCOUT);
    DOCTEST_REQUIRE(scout != nullptr);
    const int victim = scout->player;
    DOCTEST_CHECK(moo_world::Scout_Near(static_cast<int16_t>(victim)));
    DOCTEST_CHECK(moo_world::State()->last_scout == MOX::stardate);
    // The two others (the pacts are off) lean to each other.
    int others[2];
    int n = 0;
    for (int p = 0; p < 3; ++p) {
        if (p != victim) others[n++] = p;
    }
    DOCTEST_CHECK(opinion::Modifier(static_cast<int16_t>(others[0]), static_cast<int16_t>(others[1]), opinion::REASON_DID_US_A_SERVICE) ==
                  moo_world::SCOUT_OPINION);
    DOCTEST_CHECK(opinion::Modifier(static_cast<int16_t>(others[1]), static_cast<int16_t>(others[0]), opinion::REASON_DID_US_A_SERVICE) ==
                  moo_world::SCOUT_OPINION);
    DOCTEST_CHECK(opinion::Modifier(static_cast<int16_t>(victim), static_cast<int16_t>(others[0]), opinion::REASON_DID_US_A_SERVICE) == 0);
    // Cooldown: no second scout; the first one passes.
    Run_(moo_world::SCOUT_TURNS);
    DOCTEST_CHECK(Find_(moo_world::KIND_ANTARAN_SCOUT) == nullptr);
    DOCTEST_CHECK_FALSE(moo_world::Scout_Near(static_cast<int16_t>(victim)));
    moo_world::Turn();
    DOCTEST_CHECK(Find_(moo_world::KIND_ANTARAN_SCOUT) == nullptr);
    MOX::stardate += moo_world::SCOUT_COOLDOWN;
    moo_world::Turn();
    DOCTEST_CHECK(Find_(moo_world::KIND_ANTARAN_SCOUT) != nullptr);
}

DOCTEST_TEST_CASE("the Antaran scout: with the pacts on the neighbours sign one; empires at war do not")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    pacts::enabled = true;
    DOCTEST_REQUIRE(moo_world::Start_Scout(0, 0));
    DOCTEST_CHECK(pacts::Has_Pact(1, 2));
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(0, 1));
    pacts::Clear();
    state.players[1].treaty[2] = 4;  // at war
    moo_world::Clear();
    moo_world::enabled = true;
    DOCTEST_REQUIRE(moo_world::Start_Scout(0, 0));
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
    // Far apart (no colony of 1 near the scout): no answer either.
    pacts::Clear();
    state.players[1].treaty[2] = 0;
    state.stars[1].x = 5000;
    moo_world::Clear();
    moo_world::enabled = true;
    DOCTEST_REQUIRE(moo_world::Start_Scout(0, 0));
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
}

DOCTEST_TEST_CASE("refugees: a third empire takes them in and the aggressor remembers it")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.planets[2].size = 4;  // a big world: room for more people
    state.colonies[2].climate = 8;
    const int cap = COLCALC::Planet_Max_Population_For_Player(&state.planets[2], 2);
    DOCTEST_REQUIRE(cap >= 2);
    state.colonies[2].n_pops = static_cast<uint8_t>(cap - 1);
    state.players[2].personality = PLAYER_PERSONALITY_HONORABLE;
    POP::Set_Race(&state.colonies[0].pop[0], 0);
    moo_world::On_Colony_Taken(&state.colonies[0], 0, 1);  // empire 1 took the colony of empire 0
    const moo_world::s_record* r = Find_(moo_world::KIND_REFUGEES);
    DOCTEST_REQUIRE(r != nullptr);
    DOCTEST_CHECK(r->player == 0);
    DOCTEST_CHECK(r->option == 1);
    Run_(moo_world::REFUGEE_DELAY - 1);
    DOCTEST_CHECK(Find_(moo_world::KIND_REFUGEES) != nullptr);
    DOCTEST_CHECK(state.colonies[2].n_pops == cap - 1);
    Run_(1);
    DOCTEST_CHECK(Find_(moo_world::KIND_REFUGEES) == nullptr);
    DOCTEST_CHECK(state.colonies[2].n_pops == cap);
    DOCTEST_CHECK(POP::Get_Race(state.colonies[2].pop[cap - 1]) == 0);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_BAD_RUMOURS) == moo_world::REFUGEE_AGGRESSOR_OPINION);
    DOCTEST_CHECK(opinion::Modifier(0, 2, opinion::REASON_ABANDONED_US) == 0);
}

DOCTEST_TEST_CASE("refugees: a ruthless empire turns them away and the old owner remembers it; a human takes them in")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.planets[2].size = 4;  // a big world: room for more people
    state.colonies[2].climate = 8;
    const int cap = COLCALC::Planet_Max_Population_For_Player(&state.planets[2], 2);
    DOCTEST_REQUIRE(cap >= 2);
    state.colonies[2].n_pops = static_cast<uint8_t>(cap - 1);
    state.players[2].personality = PLAYER_PERSONALITY_RUTHLESS;
    DOCTEST_CHECK(moo_world::Refugee_Choice(2) == 1);
    moo_world::On_Colony_Taken(&state.colonies[0], 0, 1);
    Run_(moo_world::REFUGEE_DELAY);
    DOCTEST_CHECK(state.colonies[2].n_pops == cap - 1);
    DOCTEST_CHECK(opinion::Modifier(0, 2, opinion::REASON_ABANDONED_US) == moo_world::REFUGEE_REFUSED_OPINION);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_BAD_RUMOURS) == 0);
    // A human takes them in whatever its personality says.
    state.players[2].objectives = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_CHECK(moo_world::Refugee_Choice(2) == 0);
    moo_world::On_Colony_Taken(&state.colonies[0], 0, 1);
    Run_(moo_world::REFUGEE_DELAY);
    DOCTEST_CHECK(state.colonies[2].n_pops == cap);
}

DOCTEST_TEST_CASE("refugees: no room anywhere, an outpost, or the same owner: nothing")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.planets[2].size = 4;  // a big world: room for more people
    state.colonies[2].climate = 8;
    const int cap = COLCALC::Planet_Max_Population_For_Player(&state.planets[2], 2);
    state.colonies[2].n_pops = static_cast<uint8_t>(cap);  // full
    moo_world::On_Colony_Taken(&state.colonies[0], 0, 1);
    Run_(moo_world::REFUGEE_DELAY);
    DOCTEST_CHECK(Find_(moo_world::KIND_REFUGEES) == nullptr);
    DOCTEST_CHECK(state.colonies[2].n_pops == cap);
    DOCTEST_CHECK(opinion::Modifier(0, 2, opinion::REASON_ABANDONED_US) == 0);
    state.colonies[0].outpost_flag = 1;
    moo_world::On_Colony_Taken(&state.colonies[0], 0, 1);
    DOCTEST_CHECK(Find_(moo_world::KIND_REFUGEES) == nullptr);
    state.colonies[0].outpost_flag = 0;
    moo_world::On_Colony_Taken(&state.colonies[0], 0, 0);
    DOCTEST_CHECK(Find_(moo_world::KIND_REFUGEES) == nullptr);
}

DOCTEST_TEST_CASE("council's demand: a bio-weapon strike marks the flag of its victim (ab-review 3.4), the demand follows")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].personality = PLAYER_PERSONALITY_HONORABLE;
    // The flag off: no mark.
    moo_world::enabled = false;
    moo_world::On_Bio_Weapon(0, 1);
    DOCTEST_CHECK(state.players[0].biological_weapons_flag[1] == 0);
    moo_world::enabled = true;
    // Empire 0 used them at star 1, where empire 1 has its colony.
    moo_world::On_Bio_Weapon(0, 1);
    DOCTEST_CHECK(state.players[0].biological_weapons_flag[1] == 1);
    DOCTEST_CHECK(state.players[0].biological_weapons_flag[2] == 0);
    // A star without a colony of another empire: the attacker's own slot.
    moo_world::On_Bio_Weapon(0, 4);
    DOCTEST_CHECK(state.players[0].biological_weapons_flag[0] == 1);
    ++MOX::stardate;
    moo_world::Turn();
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_DID_US_A_SERVICE) == moo_world::COUNCIL_AGREE_OPINION);
    DOCTEST_CHECK((moo_world::State()->agreed_mask & 1) != 0);
}

DOCTEST_TEST_CASE("refugees: a colony destroyed (a stellar converter) sends them too (ab-review 3.6); the flag off: nothing")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    moo_world::enabled = false;
    moo_world::On_Colony_Destroyed(&state.colonies[0], 1);
    DOCTEST_CHECK(moo_world::Empty());
    moo_world::enabled = true;
    state.planets[2].size = 4;
    state.colonies[2].climate = 8;
    const int cap = COLCALC::Planet_Max_Population_For_Player(&state.planets[2], 2);
    DOCTEST_REQUIRE(cap >= 2);
    state.colonies[2].n_pops = static_cast<uint8_t>(cap - 1);
    state.players[2].personality = PLAYER_PERSONALITY_HONORABLE;
    POP::Set_Race(&state.colonies[0].pop[0], 0);
    moo_world::On_Colony_Destroyed(&state.colonies[0], 1);
    const moo_world::s_record* r = Find_(moo_world::KIND_REFUGEES);
    DOCTEST_REQUIRE(r != nullptr);
    DOCTEST_CHECK(r->player == 0);
    DOCTEST_CHECK(r->option == 1);
    state.colonies[0].n_pops = 0;  // the planet is gone before the refugees arrive
    Run_(moo_world::REFUGEE_DELAY);
    DOCTEST_CHECK(state.colonies[2].n_pops == cap);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_BAD_RUMOURS) == moo_world::REFUGEE_AGGRESSOR_OPINION);
}

namespace {
    // The famous exile (leader 6 of the fallen empire 2) is hired by empire 1, which liked the dead empire; colony 0
    // of empire 0 still holds people of the dead empire's race.
    void Setup_Pretender_(test::s_game_state* state) {
        Setup_(state);
        opinion::Add(1, 2, opinion::REASON_DEFENDED_US, 30);
        state->players[2].eliminated = 1;
        s_leader_data& l = state->leaders[6];
        l.player_index = 2;
        l.status = 0;
        l.type = LEADER_TYPE_SHIP;
        l.skill_value = 1;
        l.xp = 1000;
        l.location = -1;
        POP::Set_Race(&state->colonies[0].pop[0], 2);
        state->players[1].treaty[0] = state->players[0].treaty[1] = 3;  // peace
    }
}

DOCTEST_TEST_SUITE("pretender in exile") {

DOCTEST_TEST_CASE("the hired exile claims the former colonies: a lawful cause for war and unrest there")
{
    s_scope scope;
    test::s_game_state state;
    Setup_Pretender_(&state);
    DOCTEST_REQUIRE(moo_world::Start_Exile(2));
    const moo_world::s_record* r = Find_(moo_world::KIND_PRETENDER);
    DOCTEST_REQUIRE(r != nullptr);
    DOCTEST_CHECK(r->player == 2);
    DOCTEST_CHECK(r->option == 1);
    DOCTEST_CHECK(r->value == 6);
    DOCTEST_CHECK(r->ends == MOX::stardate + moo_world::PRETENDER_TURNS);
    DOCTEST_CHECK(moo_world::Is_Former_Colony(0, 2));
    DOCTEST_CHECK_FALSE(moo_world::Is_Former_Colony(1, 2));
    DOCTEST_CHECK(moo_world::Pretender_Cause(1, 0));
    DOCTEST_CHECK_FALSE(moo_world::Pretender_Cause(0, 1));  // the holder has no cause against the supporter
    DOCTEST_CHECK_FALSE(moo_world::Pretender_Cause(1, 2));
    DOCTEST_CHECK(ultimatums::Has_Cause(1, 0));              // the ultimatums' reading sees it with its own flag off
    DOCTEST_CHECK(moo_world::Unrest(0) == moo_world::PRETENDER_UNREST);
    DOCTEST_CHECK(moo_world::Unrest(1) == 0);
    // A former colony held by the supporter itself is not shaken.
    POP::Set_Race(&state.colonies[1].pop[0], 2);
    DOCTEST_CHECK(moo_world::Unrest(1) == 0);
}

DOCTEST_TEST_CASE("a computer supporter declares its lawful war once on the holder; the claim lapses with time")
{
    s_scope scope;
    test::s_game_state state;
    Setup_Pretender_(&state);
    DOCTEST_REQUIRE(moo_world::Start_Exile(2));
    moo_world::Run_Records();
    DOCTEST_CHECK(state.players[1].treaty[0] >= 4);
    DOCTEST_REQUIRE(Find_(moo_world::KIND_PRETENDER) != nullptr);
    DOCTEST_CHECK(Find_(moo_world::KIND_PRETENDER)->extra == 1);
    MOX::stardate += moo_world::PRETENDER_TURNS;
    moo_world::Run_Records();
    DOCTEST_CHECK(Find_(moo_world::KIND_PRETENDER) == nullptr);
    DOCTEST_CHECK_FALSE(moo_world::Pretender_Cause(1, 0));
    DOCTEST_CHECK(moo_world::Unrest(0) == 0);
}

DOCTEST_TEST_CASE("an honorable supporter keeps the cause but does not declare; the claim ends without the leader or the colonies")
{
    s_scope scope;
    test::s_game_state state;
    Setup_Pretender_(&state);
    state.players[1].personality = PLAYER_PERSONALITY_HONORABLE;
    DOCTEST_REQUIRE(moo_world::Start_Exile(2));
    moo_world::Run_Records();
    DOCTEST_CHECK(state.players[1].treaty[0] == 3);
    DOCTEST_CHECK(moo_world::Pretender_Cause(1, 0));
    state.colonies[0].n_pops = 0;  // nothing left to claim
    moo_world::Run_Records();
    DOCTEST_CHECK(Find_(moo_world::KIND_PRETENDER) == nullptr);

    moo_world::Clear();
    state.colonies[0].n_pops = 3;
    state.players[1].bc = 1000;
    state.leaders[6].player_index = 2;
    state.leaders[6].status = 0;
    state.players[2].eliminated = 1;
    DOCTEST_REQUIRE(moo_world::Start_Exile(2));
    DOCTEST_REQUIRE(Find_(moo_world::KIND_PRETENDER) != nullptr);
    state.leaders[6].status = -1;  // the supporter lost the leader
    state.leaders[6].player_index = -1;
    moo_world::Run_Records();
    DOCTEST_CHECK(Find_(moo_world::KIND_PRETENDER) == nullptr);
}

DOCTEST_TEST_CASE("no former colonies, nothing; the flag off, nothing and no draw")
{
    s_scope scope;
    test::s_game_state state;
    Setup_Pretender_(&state);
    POP::Set_Race(&state.colonies[0].pop[0], 0);  // nobody holds people of the dead empire
    DOCTEST_REQUIRE(moo_world::Start_Exile(2));
    DOCTEST_CHECK(Find_(moo_world::KIND_PRETENDER) == nullptr);

    moo_world::Clear();
    POP::Set_Race(&state.colonies[0].pop[0], 2);
    state.leaders[6].player_index = 2;
    state.leaders[6].status = 0;
    state.players[1].bc = 1000;
    DOCTEST_REQUIRE(moo_world::Start_Exile(2));
    moo_world::enabled = false;
    game_random::Set_Random_Seed(7);
    const uint32_t first = game_random::Random(1000000);
    game_random::Set_Random_Seed(7);
    DOCTEST_CHECK_FALSE(moo_world::Pretender_Cause(1, 0));
    DOCTEST_CHECK(moo_world::Unrest(0) == 0);
    moo_world::Run_Records();
    DOCTEST_CHECK(game_random::Random(1000000) == first);
}

DOCTEST_TEST_CASE("the pretender record round-trips (no star) and bad pretenders are refused")
{
    s_scope scope;
    test::s_game_state state;
    Setup_Pretender_(&state);
    moo_world::State()->count = 1;
    moo_world::s_record& a = moo_world::State()->record[0];
    a.kind = moo_world::KIND_PRETENDER; a.player = 2; a.target = -1; a.other = -1; a.started = 35001; a.ends = 35041; a.option = 1; a.value = 6; a.extra = 1;
    const uint32_t size = moo_world::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    moo_world::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    moo_world::Clear();
    moo_world::enabled = true;
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(moo_world::Parse_Save(&reader, &error));
    moo_world::Commit_Save();
    DOCTEST_REQUIRE(moo_world::State()->count == 1);
    DOCTEST_CHECK(moo_world::State()->record[0].target == -1);
    DOCTEST_CHECK(moo_world::State()->record[0].option == 1);
    DOCTEST_CHECK(moo_world::State()->record[0].value == 6);
    // Layout: record at 15: kind 15, player 16, target 17-18, other 19-20, started 21-24, ends 25-28, option 29,
    // value 30-31, extra 32-33.
    const auto refuse = [&](size_t offset, uint8_t value) {
        auto bad = std::make_unique<uint8_t[]>(size);
        std::copy(buffer.get(), buffer.get() + size, bad.get());
        bad[offset] = value;
        SERIAL::Reader_ r(bad.get(), size, "test underflow", true);
        return !moo_world::Parse_Save(&r, &error);
    };
    DOCTEST_CHECK(refuse(29, 0xFF));  // no supporter
    DOCTEST_CHECK(refuse(16, 0xFF));  // no fallen empire
    DOCTEST_CHECK(refuse(32, 2));     // a bad war flag
}

}


// ---- The choice window (human-ui.md section 5, slice R1, part C): a human's option is chosen later.

namespace {
    // What a scenario leaves behind: money, population, opinions, the council's mask, the table and the draws.
    std::string Ch_Signature_(const test::s_game_state& state) {
        char buffer[512];
        int n = snprintf(buffer, sizeof(buffer), "bc %d/%d/%d fr %d/%d/%d pops %d/%d/%d mask %d records %d op %d %d %d %d %d, leader %d", (int)state.players[0].bc,
            (int)state.players[1].bc, (int)state.players[2].bc, (int)state.players[0].n_freighters, (int)state.players[1].n_freighters,
            (int)state.players[2].n_freighters, (int)state.colonies[0].n_pops, (int)state.colonies[1].n_pops, (int)state.colonies[2].n_pops,
            (int)moo_world::State()->agreed_mask, (int)moo_world::State()->count, (int)opinion::Modifier(1, 2, opinion::REASON_BAD_RUMOURS),
            (int)opinion::Modifier(0, 2, opinion::REASON_ABANDONED_US), (int)opinion::Modifier(1, 0, opinion::REASON_DID_US_A_SERVICE),
            (int)opinion::Modifier(1, 0, opinion::REASON_BAD_RUMOURS), (int)opinion::Modifier(2, 0, opinion::REASON_DID_US_A_SERVICE),
            (int)state.leaders[7].player_index);
        snprintf(buffer + n, sizeof(buffer) - n, ", seed %u", (unsigned)game_random::Get_Random_Seed());
        return buffer;
    }

    // Only player 1 is within reach of the king (star 3).
    void Ch_Tribute_Setup_(test::s_game_state* state) {
        Setup_(state);
        state->stars[0].x = 5000;
        state->stars[2].x = 5000;
    }

    // 0: a computer of the personality of the option; 1: a human with the setting on, the option picked later;
    // 2: a human with the setting off.
    std::string Ch_Tribute_(int option, int mode) {
        s_scope scope;
        test::s_saved_value<bool> choices_on{choices::enabled};
        choices::Clear();
        test::s_game_state state;
        Ch_Tribute_Setup_(&state);
        state.players[1].personality = option == 0 ? PLAYER_PERSONALITY_HONORABLE : option == 1 ? PLAYER_PERSONALITY_ERRATIC : PLAYER_PERSONALITY_RUTHLESS;
        if (mode != 0) state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;
        DOCTEST_REQUIRE(moo_world::Raise_King(3, 20));
        Run_(moo_world::KING_PERIOD - 1);
        Seed_For_({{100, 1, 100}, {100, 1, 50}});
        choices::enabled = mode == 1;
        Run_(1);
        if (mode == 1) {
            DOCTEST_REQUIRE(choices::Apply_Pending(1, option));
        } else {
            DOCTEST_CHECK(choices::Empty());
        }
        const std::string result = Ch_Signature_(state);
        choices::Clear();
        return result;
    }

    // Empire 2 holds a colony with room; the colony of empire 0 is taken by empire 1; the refugees arrive.
    std::string Ch_Refugees_(int option, int mode) {
        s_scope scope;
        test::s_saved_value<bool> choices_on{choices::enabled};
        choices::Clear();
        test::s_game_state state;
        Setup_(&state);
        state.planets[2].size = 4;
        state.colonies[2].climate = 8;
        const int cap = COLCALC::Planet_Max_Population_For_Player(&state.planets[2], 2);
        DOCTEST_REQUIRE(cap >= 2);
        state.colonies[2].n_pops = static_cast<uint8_t>(cap - 1);
        state.players[2].personality = option == 0 ? PLAYER_PERSONALITY_HONORABLE : PLAYER_PERSONALITY_RUTHLESS;
        if (mode != 0) state.players[2].objectives = PLAYER_OBJECTIVE_HUMAN;
        POP::Set_Race(&state.colonies[0].pop[0], 0);
        game_random::Set_Random_Seed(4242);
        moo_world::On_Colony_Taken(&state.colonies[0], 0, 1);
        choices::enabled = mode == 1;
        Run_(moo_world::REFUGEE_DELAY);
        if (mode == 1) {
            DOCTEST_REQUIRE(choices::Apply_Pending(2, option));
        } else {
            DOCTEST_CHECK(choices::Empty());
        }
        const std::string result = Ch_Signature_(state);
        choices::Clear();
        return result;
    }

    std::string Ch_Council_(int option, int mode) {
        s_scope scope;
        test::s_saved_value<bool> choices_on{choices::enabled};
        choices::Clear();
        test::s_game_state state;
        Setup_(&state);
        state.players[0].personality = option == 0 ? PLAYER_PERSONALITY_HONORABLE : PLAYER_PERSONALITY_RUTHLESS;
        if (mode != 0) state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
        state.players[0].biological_weapons_flag[2] = 1;
        game_random::Set_Random_Seed(4242);
        choices::enabled = mode == 1;
        DOCTEST_REQUIRE(moo_world::Council_Demand(0));
        if (mode == 1) {
            DOCTEST_REQUIRE(choices::Apply_Pending(0, option));
        } else {
            DOCTEST_CHECK(choices::Empty());
        }
        const std::string result = Ch_Signature_(state);
        choices::Clear();
        return result;
    }

    // The leader of empire 0 who would defect (to the human 2, which it likes best).
    int Ch_Defection_Setup_(test::s_game_state* state) {
        Setup_(state);
        s_leader_data& sci = state->leaders[7];
        sci.player_index = 0;
        sci.status = 0;
        sci.type = LEADER_TYPE_COLONY;
        sci.general_skills = LEADER_GENERAL_SKILL_RESEARCHER;
        sci.skill_value = 1;
        sci.location = -1;
        int app = -1;
        for (int a = 1; a < TECH_APP_COUNT && app < 0; ++a) {
            if (COLCALC::Player_May_Get_Tech_App(&state->players[1], a)) app = a;
        }
        DOCTEST_REQUIRE(app > 0);
        state->players[0].tech_applications[app] = TECH_RESEARCH_STATUS_RESEARCHED;
        state->players[1].objectives = PLAYER_OBJECTIVE_HUMAN;
        state->players[2].objectives = PLAYER_OBJECTIVE_HUMAN;
        state->colonies[0].morale = static_cast<int8_t>(moo_world::DEFECT_MORALE);
        opinion::Add(0, 2, opinion::REASON_DEFENDED_US, 30);
        return app;
    }
} // namespace

DOCTEST_TEST_CASE("choices: Choice_Apply with each option equals the computer's path; the setting off leaves the human as before")
{
    for (int option = 0; option < 3; ++option) {
        DOCTEST_CAPTURE(option);
        DOCTEST_CHECK(Ch_Tribute_(option, 1) == Ch_Tribute_(option, 0));
    }
    DOCTEST_CHECK(Ch_Tribute_(0, 2) == Ch_Tribute_(0, 0));
    for (int option = 0; option < 2; ++option) {
        DOCTEST_CAPTURE(option);
        DOCTEST_CHECK(Ch_Refugees_(option, 1) == Ch_Refugees_(option, 0));
        DOCTEST_CHECK(Ch_Council_(option, 1) == Ch_Council_(option, 0));
    }
    DOCTEST_CHECK(Ch_Refugees_(0, 2) == Ch_Refugees_(0, 0));
    DOCTEST_CHECK(Ch_Council_(0, 2) == Ch_Council_(0, 0));
    // The paths really differ.
    DOCTEST_CHECK(Ch_Tribute_(0, 1) != Ch_Tribute_(2, 1));
    DOCTEST_CHECK(Ch_Refugees_(0, 1) != Ch_Refugees_(1, 1));
    DOCTEST_CHECK(Ch_Council_(0, 1) != Ch_Council_(1, 1));
}

DOCTEST_TEST_CASE("choices: a human with the setting on gets pending choices and no effect")
{
    s_scope scope;
    test::s_saved_value<bool> choices_on{choices::enabled};
    choices::Clear();
    test::s_game_state state;
    Ch_Tribute_Setup_(&state);
    test::Load_Base_Texts();
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;
    choices::enabled = true;
    // The king's tribute: nothing is paid until the human answers; a poor human is raided at once, with no window.
    DOCTEST_REQUIRE(moo_world::Raise_King(3, 20));
    Run_(moo_world::KING_PERIOD);
    DOCTEST_CHECK(state.players[1].bc == 1000);
    DOCTEST_CHECK(choices::Count(1) == 1);
    {
        const choices::s_choice c = choices::State()->choice[0];
        DOCTEST_CHECK(c.module == choices::MODULE_MOO_WORLD);
        DOCTEST_CHECK(c.kind == moo_world::CHOICE_TRIBUTE);
        choices::s_text text;
        DOCTEST_REQUIRE(moo_world::Choice_Text(c, &text));
        DOCTEST_CHECK(text.count == 3);
        DOCTEST_CHECK(std::string(text.option[0]) == "Pay 200 BC (the king leaves you in peace this time)");
        choices::Clear();
    }
    state.players[1].bc = moo_world::KING_TRIBUTE_MIN - 1;
    Run_(moo_world::KING_PERIOD);
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(state.players[1].n_freighters == 5 - moo_world::KING_FREIGHTERS);
    // The council's demand, with the human guilty.
    state.players[1].biological_weapons_flag[2] = 1;
    DOCTEST_REQUIRE(moo_world::Council_Demand(1));
    DOCTEST_CHECK(choices::Count(1) == 1);
    DOCTEST_CHECK(moo_world::State()->agreed_mask == 0);
    {
        const choices::s_choice c = choices::State()->choice[0];
        DOCTEST_CHECK(c.kind == moo_world::CHOICE_COUNCIL);
        choices::s_text text;
        DOCTEST_REQUIRE(moo_world::Choice_Text(c, &text));
        DOCTEST_CHECK(text.count == 2);
        DOCTEST_CHECK(std::string(text.option[0]) == "Promise to stop (goodwill +4; breaking the promise later: -20)");
    }
    choices::Clear();
    // A repeat after an agreement is a broken word at once, with no window.
    moo_world::State()->agreed_mask = 2;
    DOCTEST_REQUIRE(moo_world::Council_Demand(1));
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_BROKE_ITS_WORD) == moo_world::COUNCIL_BROKE_OPINION);
    // The flag off: the demand does nothing and nothing is queued.
    moo_world::enabled = false;
    DOCTEST_CHECK_FALSE(moo_world::Council_Demand(1));
    DOCTEST_CHECK(choices::Empty());
}

DOCTEST_TEST_CASE("choices: the refugees window names the people; a full queue accepts as before; a computer is unaffected")
{
    struct s_language_scope {
        test::s_saved_value<uint8_t> language{text_language::language};
    } language_scope;
    s_scope scope;
    test::s_saved_value<bool> choices_on{choices::enabled};
    choices::Clear();
    test::s_game_state state;
    Setup_(&state);
    test::Load_Base_Texts();
    state.planets[2].size = 4;
    state.colonies[2].climate = 8;
    const int cap = COLCALC::Planet_Max_Population_For_Player(&state.planets[2], 2);
    DOCTEST_REQUIRE(cap >= 2);
    state.colonies[2].n_pops = static_cast<uint8_t>(cap - 1);
    std::snprintf(state.players[0].race_name, sizeof(state.players[0].race_name), "Sakkra");
    std::snprintf(state.players[1].race_name, sizeof(state.players[1].race_name), "Klackon");
    POP::Set_Race(&state.colonies[0].pop[0], 0);
    state.players[2].objectives = PLAYER_OBJECTIVE_HUMAN;
    choices::enabled = true;
    moo_world::On_Colony_Taken(&state.colonies[0], 0, 1);
    Run_(moo_world::REFUGEE_DELAY);
    DOCTEST_CHECK(Find_(moo_world::KIND_REFUGEES) == nullptr);  // the record is done: the choice carries on
    DOCTEST_CHECK(state.colonies[2].n_pops == cap - 1);
    DOCTEST_REQUIRE(choices::Count(2) == 1);
    const choices::s_choice c = choices::State()->choice[0];
    DOCTEST_CHECK(c.kind == moo_world::CHOICE_REFUGEES);
    choices::s_text en;
    text_language::language = text_language::LANGUAGE_EN;
    DOCTEST_REQUIRE(moo_world::Choice_Text(c, &en));
    DOCTEST_CHECK(en.count == 2);
    DOCTEST_CHECK(std::string(en.option[0]) == "Take them in (+1 Sakkra person; the Klackon think worse of you)");
    DOCTEST_CHECK(std::string(en.option[1]) == "Turn them away (the Sakkra will remember it)");
    DOCTEST_CHECK(std::string(en.body).find("Sakkra") != std::string::npos);
    text_language::language = text_language::LANGUAGE_RU;
    choices::s_text ru;
    DOCTEST_REQUIRE(moo_world::Choice_Text(c, &ru));
    std::string all = std::string(ru.title) + ru.body;
    for (int i = 0; i < ru.count; ++i) all += ru.option[i];
    DOCTEST_CHECK(all.find('{') == std::string::npos);
    DOCTEST_CHECK(all.find('?') == std::string::npos);
    choices::Clear();
    // A full queue: the human accepts, as before.
    for (int i = 0; i < choices::MAX_PENDING; ++i) {
        choices::s_choice filler;
        filler.module = choices::MODULE_INCIDENTS;
        filler.kind = 1;
        filler.player = 1;
        DOCTEST_REQUIRE(choices::Add(filler));
    }
    moo_world::On_Colony_Taken(&state.colonies[0], 0, 1);
    Run_(moo_world::REFUGEE_DELAY);
    DOCTEST_CHECK(choices::Count(2) == 0);
    DOCTEST_CHECK(state.colonies[2].n_pops == cap);
    choices::Clear();
    // A computer host never asks.
    state.players[2].objectives = PLAYER_OBJECTIVE_MILITARIST;
    state.colonies[2].n_pops = 1;  // (the planet's limit may have moved with the refugees' race)
    moo_world::On_Colony_Taken(&state.colonies[0], 0, 1);
    Run_(moo_world::REFUGEE_DELAY);
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(state.colonies[2].n_pops == 2);
}

DOCTEST_TEST_CASE("choices: a scientist who asks to defect to a human waits for the human; take in or send back")
{
    int app = 0;
    {
        // The computer's path: the scientist goes over at once.
        s_scope scope;
        test::s_game_state state;
        app = Ch_Defection_Setup_(&state);
        DOCTEST_REQUIRE(moo_world::Start_Defection(0));
        DOCTEST_CHECK(state.leaders[7].player_index == 2);
        DOCTEST_CHECK(state.players[2].tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED);
    }
    s_scope scope;
    test::s_saved_value<bool> choices_on{choices::enabled};
    choices::Clear();
    test::s_game_state state;
    app = Ch_Defection_Setup_(&state);
    test::Load_Base_Texts();
    std::snprintf(state.players[0].race_name, sizeof(state.players[0].race_name), "Mrrshan");
    std::snprintf(state.leaders[7].name, sizeof(state.leaders[7].name), "Vexa");
    choices::enabled = true;
    const std::string before = Ch_Signature_(state);
    DOCTEST_REQUIRE(moo_world::Start_Defection(0));
    DOCTEST_CHECK(Ch_Signature_(state) == before);  // nothing happened yet (the leader stays, no technology)
    DOCTEST_CHECK(state.players[2].tech_applications[app] != TECH_RESEARCH_STATUS_RESEARCHED);
    DOCTEST_REQUIRE(choices::Count(2) == 1);
    const choices::s_choice c = choices::State()->choice[0];
    DOCTEST_CHECK(c.kind == moo_world::CHOICE_DEFECTION);
    choices::s_text text;
    DOCTEST_REQUIRE(moo_world::Choice_Text(c, &text));
    DOCTEST_CHECK(text.count == 2);
    DOCTEST_CHECK(std::string(text.body).find("Vexa") != std::string::npos);
    DOCTEST_CHECK(std::string(text.option[1]) == "Send home (Mrrshan will think better of you: +6)");
    // Take in: the same as the computer's path.
    choices::s_choice take = c;
    moo_world::Choice_Apply(take, 0);
    DOCTEST_CHECK(state.leaders[7].player_index == 2);
    DOCTEST_CHECK(state.players[2].tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED);
    // The leader is not the old owner's any more: the window is gone.
    DOCTEST_CHECK_FALSE(moo_world::Choice_Text(c, &text));
    choices::Clear();
    // Send back: the scientist stays and the old owner thinks better of the human.
    state.leaders[7].player_index = 0;
    state.players[2].tech_applications[app] = 0;
    moo_world::Choice_Apply(c, 1);
    DOCTEST_CHECK(state.leaders[7].player_index == 0);
    DOCTEST_CHECK(state.players[2].tech_applications[app] != TECH_RESEARCH_STATUS_RESEARCHED);
    DOCTEST_CHECK(opinion::Modifier(0, 2, opinion::REASON_DID_US_A_SERVICE) == moo_world::DEFECT_RETURN_OPINION);
    // The application learned elsewhere meanwhile: gone.
    state.players[2].tech_applications[app] = TECH_RESEARCH_STATUS_RESEARCHED;
    DOCTEST_CHECK_FALSE(moo_world::Choice_Text(c, &text));
    state.players[2].tech_applications[app] = 0;
    // With the setting off a human takes the scientist as before.
    choices::enabled = false;
    DOCTEST_REQUIRE(moo_world::Start_Defection(0));
    DOCTEST_CHECK(state.leaders[7].player_index == 2);
    DOCTEST_CHECK(choices::Empty());
}

DOCTEST_TEST_CASE("choices: a gone subject makes Choice_Text false and Choice_Apply do nothing")
{
    s_scope scope;
    test::s_saved_value<bool> choices_on{choices::enabled};
    choices::Clear();
    test::s_game_state state;
    Ch_Tribute_Setup_(&state);
    test::Load_Base_Texts();
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;
    choices::enabled = true;
    DOCTEST_REQUIRE(moo_world::Raise_King(3, 20));
    Run_(moo_world::KING_PERIOD);
    DOCTEST_REQUIRE(choices::Count(1) == 1);
    const choices::s_choice tribute = choices::State()->choice[0];
    choices::s_text text;
    // The king is gone: no tribute.
    moo_world::State()->count = 0;
    moo_world::State()->record[0] = moo_world::s_record{};
    DOCTEST_CHECK_FALSE(moo_world::Choice_Text(tribute, &text));
    moo_world::Choice_Apply(tribute, 0);
    DOCTEST_CHECK(state.players[1].bc == 1000);
    // Refugees: the colony changed hands.
    choices::s_choice refugees;
    refugees.module = choices::MODULE_MOO_WORLD;
    refugees.kind = moo_world::CHOICE_REFUGEES;
    refugees.player = 1;
    refugees.target = 1;
    refugees.other = 0;
    refugees.value = 2 * 256;
    state.planets[1].size = 4;
    state.colonies[1].climate = 8;
    state.colonies[1].n_pops = 1;
    DOCTEST_CHECK(moo_world::Choice_Text(refugees, &text));
    state.colonies[1].owner = 2;
    DOCTEST_CHECK_FALSE(moo_world::Choice_Text(refugees, &text));
    moo_world::Choice_Apply(refugees, 0);
    DOCTEST_CHECK(state.colonies[1].n_pops == 1);
    refugees.target = 99;
    DOCTEST_CHECK_FALSE(moo_world::Choice_Text(refugees, &text));
    // The council: not guilty any more.
    choices::s_choice council;
    council.module = choices::MODULE_MOO_WORLD;
    council.kind = moo_world::CHOICE_COUNCIL;
    council.player = 1;
    DOCTEST_CHECK_FALSE(moo_world::Choice_Text(council, &text));
    moo_world::Choice_Apply(council, 0);
    DOCTEST_CHECK((moo_world::State()->agreed_mask & 2) == 0);
    // A dead empire; a kind that does not exist.
    state.players[1].biological_weapons_flag[2] = 1;
    DOCTEST_CHECK(moo_world::Choice_Text(council, &text));
    state.players[1].eliminated = 1;
    DOCTEST_CHECK_FALSE(moo_world::Choice_Text(council, &text));
    state.players[1].eliminated = 0;
    council.kind = 77;
    DOCTEST_CHECK_FALSE(moo_world::Choice_Text(council, &text));
    choices::Clear();
}
