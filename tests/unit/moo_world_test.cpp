#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/events.h"
#include "game/moo_world.h"
#include "game/opinion.h"
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
        test::s_saved_value<EVENTS::s_pirate_raid> raid{EVENTS::pirate_raid};

        s_scope() {
            moo_world::Clear();
            pirate_world::Clear();
            opinion::Clear();
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
    DOCTEST_CHECK(moo_world::State()->record[0].extra == 26);
    DOCTEST_CHECK(moo_world::State()->record[1].player == 1);
    DOCTEST_CHECK(moo_world::State()->record[1].ends == 35004);

    // Layout: version (2), count (1), unite turns (2), then 19 bytes a record: kind 0, player 1, target 2-3,
    // other 4-5, started 6-9, ends 10-13, option 14, value 15-16, extra 17-18. Record 0 at 5, record 1 at 24.
    const auto refuse = [&](size_t offset, uint8_t value) {
        auto bad = std::make_unique<uint8_t[]>(size);
        std::copy(buffer.get(), buffer.get() + size, bad.get());
        bad[offset] = value;
        SERIAL::Reader_ r(bad.get(), size, "test underflow", true);
        return !moo_world::Parse_Save(&r, &error);
    };
    DOCTEST_CHECK(refuse(5, 9));             // a bad kind
    DOCTEST_CHECK(refuse(5, 0));
    DOCTEST_CHECK(refuse(5 + 1, 0x7F));      // a bad empire
    DOCTEST_CHECK(refuse(24 + 1, 0xFF));     // an epidemic without an empire
    DOCTEST_CHECK(refuse(5 + 3, 0x7F));      // a star out of range
    DOCTEST_CHECK(refuse(5 + 5, 0x7F));      // a second star out of range
    DOCTEST_CHECK(refuse(5 + 9, 0x80));      // a negative stardate
    DOCTEST_CHECK(refuse(5 + 16, 0x7F));     // a bad value
    DOCTEST_CHECK(refuse(4, 0x80));          // a negative unite counter
    DOCTEST_CHECK(refuse(2, 30));            // too many records
    DOCTEST_CHECK(refuse(0, 7));             // the version
    SERIAL::Reader_ r4(buffer.get(), size - 1, "test underflow", true);
    DOCTEST_CHECK_FALSE(moo_world::Parse_Save(&r4, &error));
    DOCTEST_CHECK(moo_world::State()->count == 2);  // the failed parses left the state alone
}
