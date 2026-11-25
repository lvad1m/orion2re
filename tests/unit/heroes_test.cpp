#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/crises.h"
#include "game/dice.h"
#include "game/heroes.h"
#include "game/loyalty.h"
#include "game/random.h"
#include "game/serialization.h"
#include "game/spy_ops.h"

#include <initializer_list>
#include <memory>
#include <string>

namespace {
    struct s_scope {
        test::s_saved_value<bool> enabled{heroes::enabled};
        test::s_saved_value<bool> loyalty_enabled{loyalty::enabled};
        test::s_saved_value<int32_t> operations{spy_ops::operations};

        s_scope() {
            heroes::Clear();
            loyalty::Clear();
            crises::Clear();
            spy_ops::Clear();
            heroes::enabled = true;
            loyalty::enabled = true;
            spy_ops::operations = 0;
        }
        ~s_scope() {
            heroes::Clear();
            loyalty::Clear();
            crises::Clear();
            spy_ops::Clear();
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

    constexpr int32_t SHIP_LEADER = 5;     // a ship captain of player 0 at ship 0
    constexpr int32_t TELEPATH = 7;        // a telepath of player 0
    constexpr int32_t VICTIM_LEADER = 9;   // a governor of player 1
    constexpr int32_t HERO = 12;           // a famous leader nobody holds
    constexpr int32_t HERO2 = 13;

    // Three computer empires with 1000 BC and mutual contact. Ship 0 of player 0 carries leader 5.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 3;
        MOX::NUM_STARS = 2;
        MOX::NUM_COLONIES = 0;
        MOX::NUM_SHIPS = 1;
        MOX::stardate = 35001;
        for (int p = 0; p < 3; ++p) {
            state->players[p].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[p].personality = PLAYER_PERSONALITY_ERRATIC;
            state->players[p].bc = 1000;
            state->players[p].eliminated = 0;
            for (int o = 0; o < 3; ++o) state->players[p].contact[o] = 1;
        }
        state->ships[0].owner = 0;
        state->ships[0].status = 0;
        state->ships[0].location = 0;
        state->ships[0].officer_index = SHIP_LEADER;
        s_leader_data& cap = state->leaders[SHIP_LEADER];
        cap.player_index = 0;
        cap.status = 1;
        cap.type = LEADER_TYPE_SHIP;
        cap.location = 0;
        s_leader_data& tp = state->leaders[TELEPATH];
        tp.player_index = 0;
        tp.status = 0;
        tp.type = LEADER_TYPE_SHIP;
        tp.general_skills = LEADER_GENERAL_SKILL_TELEPATH;
        s_leader_data& gov = state->leaders[VICTIM_LEADER];
        gov.player_index = 1;
        gov.status = 1;
        gov.type = LEADER_TYPE_COLONY;
        gov.location = -1;
    }

    void Famous_(test::s_game_state* state, int leader) {
        s_leader_data& l = state->leaders[leader];
        l.player_index = -1;
        l.status = -1;
        l.type = LEADER_TYPE_SHIP;
        l.general_skills = LEADER_GENERAL_SKILL_FAMOUS;
        l.xp = 0;
        l.skill_value = 1;
    }

    void Run_(int turns) {
        for (int i = 0; i < turns; ++i) {
            ++MOX::stardate;
            heroes::Run_Records();
        }
    }

    void Track_(int leader, int value) {
        loyalty::Record(static_cast<int16_t>(leader))->loyalty = static_cast<int8_t>(value);
        loyalty::Record(static_cast<int16_t>(leader))->owner = 0;
    }
} // namespace

DOCTEST_TEST_SUITE("heroes") {

DOCTEST_TEST_CASE("flag off: nothing starts, nothing is kept, no draw from game_random, the hook is classic")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Famous_(&state, HERO);
    heroes::enabled = false;
    DOCTEST_CHECK(heroes::On_Crisis_Solved(0) == -1);
    DOCTEST_CHECK_FALSE(heroes::Start(heroes::KIND_MIND, 1, VICTIM_LEADER, 0));
    DOCTEST_CHECK_FALSE(heroes::Start(heroes::KIND_STIMULANT, 0, SHIP_LEADER));
    const uint32_t seed = game_random::Get_Random_Seed();
    for (int i = 0; i < 20; ++i) {
        ++MOX::stardate;
        heroes::Turn();
    }
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
    DOCTEST_CHECK(heroes::Empty());
    DOCTEST_CHECK(state.players[0].bc == 1000);
    DOCTEST_CHECK(state.leaders[HERO].xp == 0);
    DOCTEST_CHECK(state.leaders[HERO].status == -1);
}

DOCTEST_TEST_CASE("a crisis solved brilliantly hires a free leader with an ability, free of charge, without dice")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Famous_(&state, HERO);                                   // famous: preferred
    state.leaders[HERO2].player_index = -1;                  // a plain leader with two abilities
    state.leaders[HERO2].status = -1;
    state.leaders[HERO2].type = LEADER_TYPE_SHIP;
    state.leaders[HERO2].general_skills = LEADER_GENERAL_SKILL_DIPLOMAT | LEADER_GENERAL_SKILL_RESEARCHER;
    const uint32_t seed = game_random::Get_Random_Seed();
    const int16_t hired = heroes::On_Crisis_Solved(0);
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
    DOCTEST_REQUIRE(hired == HERO);
    DOCTEST_CHECK(state.leaders[HERO].player_index == 0);
    DOCTEST_CHECK(state.players[0].bc == 1000);
    DOCTEST_CHECK(heroes::Empty());                          // no record is kept
    // Next time the other one is the best that is left.
    DOCTEST_CHECK(heroes::On_Crisis_Solved(0) == HERO2);
    // Nobody with an ability is left on the list.
    DOCTEST_CHECK(heroes::On_Crisis_Solved(0) == -1);
}

DOCTEST_TEST_CASE("the crisis hook: a brilliant or triumphant result hires, a plain success does not")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Famous_(&state, HERO);
    test::s_saved_value<bool> crises_enabled{crises::enabled};
    test::s_saved_value<uint8_t> kind{dice::roll_kind};
    test::s_saved_value<int32_t> margin{dice::brilliant_margin};
    dice::brilliant_margin = 5;
    crises::enabled = true;
    dice::roll_kind = dice::ROLL_TWO_D10;
    DOCTEST_REQUIRE(crises::Open(0, crises::CRISIS_ASTEROID, 0, 5));
    crises::Slots()[0].option = 2;                           // a ready option, cost 0
    crises::Slots()[0].due = MOX::stardate;
    const int32_t plain[2] = {6, 6};                         // 12 + bonus: a success, not brilliant
    crises::Resolve_With_Dice(0, plain);
    DOCTEST_CHECK(state.leaders[HERO].player_index == -1);
    DOCTEST_REQUIRE(crises::Open(0, crises::CRISIS_ASTEROID, 0, 5));
    crises::Slots()[0].option = 2;
    const int32_t triumph[2] = {10, 10};
    crises::Resolve_With_Dice(0, triumph);
    DOCTEST_CHECK(state.leaders[HERO].player_index == 0);
}

DOCTEST_TEST_CASE("a retired manager record of an old save is dropped at once")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    heroes::State()->count = 1;
    heroes::s_record& a = heroes::State()->record[0];
    a.kind = heroes::KIND_MANAGER; a.player = 0; a.leader = -1; a.other = -1; a.started = 35001; a.ends = 35013;
    Run_(1);
    DOCTEST_CHECK(heroes::State()->count == 0);
}

DOCTEST_TEST_CASE("mind control: tribute to the master, freed by chance, ends, leaves the bribe slot clean")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    spy_ops::operations = 1 << spy_ops::OP_BRIBERY;
    DOCTEST_CHECK_FALSE(heroes::Start(heroes::KIND_MIND, 1, VICTIM_LEADER, 1));   // its own master
    DOCTEST_CHECK_FALSE(heroes::Start(heroes::KIND_MIND, 1, SHIP_LEADER, 0));     // not the victim's leader
    DOCTEST_REQUIRE(heroes::Start(heroes::KIND_MIND, 1, VICTIM_LEADER, 0));
    DOCTEST_CHECK(heroes::Controlled(VICTIM_LEADER));
    DOCTEST_CHECK(spy_ops::Bribed_By(VICTIM_LEADER) == 0);
    DOCTEST_CHECK_FALSE(heroes::Start(heroes::KIND_STIMULANT, 1, VICTIM_LEADER));  // already tied

    Seed_For_({{1000, 400, 1000}});
    Run_(1);
    DOCTEST_CHECK(state.players[1].bc == 1000 - heroes::MIND_TRIBUTE);
    DOCTEST_CHECK(state.players[0].bc == 1000 + heroes::MIND_TRIBUTE);
    DOCTEST_CHECK(heroes::Controlled(VICTIM_LEADER));

    Seed_For_({{1000, 1, 25}});
    Run_(1);
    DOCTEST_CHECK_FALSE(heroes::Controlled(VICTIM_LEADER));
    DOCTEST_CHECK(spy_ops::Bribed_By(VICTIM_LEADER) == -1);
}

DOCTEST_TEST_CASE("decision 3: a mind control that comes to light (the leader breaks free) feeds the threat; a fading one does not")
{
    s_scope scope;
    test::s_saved_value<bool> front{crises::become_crisis};
    test::s_game_state state;
    Setup_(&state);
    crises::become_crisis = true;
    DOCTEST_REQUIRE(heroes::Start(heroes::KIND_MIND, 1, VICTIM_LEADER, 0));
    heroes::State()->record[0].ends = MOX::stardate + 1;
    Seed_For_({{1000, 400, 1000}});
    Run_(1);  // it fades by itself, unseen
    DOCTEST_CHECK(heroes::Empty());
    DOCTEST_CHECK(crises::Threat(0) == 0);
    DOCTEST_REQUIRE(heroes::Start(heroes::KIND_MIND, 1, VICTIM_LEADER, 0));
    Seed_For_({{1000, 1, 25}});
    Run_(1);  // the leader breaks free: the master is known for it
    DOCTEST_CHECK_FALSE(heroes::Controlled(VICTIM_LEADER));
    DOCTEST_CHECK(crises::Threat(0) == crises::THREAT_ATROCITY);
    DOCTEST_CHECK(crises::Threat(1) == 0);
    crises::become_crisis = false;
    crises::Clear();
    DOCTEST_REQUIRE(heroes::Start(heroes::KIND_MIND, 1, VICTIM_LEADER, 0));
    Seed_For_({{1000, 1, 25}});
    Run_(1);
    DOCTEST_CHECK(crises::Extra_Empty());  // flag off: nothing kept
}

DOCTEST_TEST_CASE("mind control ends by itself and when the leader is gone")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(heroes::Start(heroes::KIND_MIND, 1, VICTIM_LEADER, 0));
    heroes::State()->record[0].ends = MOX::stardate + 1;
    Seed_For_({{1000, 400, 1000}});
    Run_(1);
    DOCTEST_CHECK(heroes::Empty());

    DOCTEST_REQUIRE(heroes::Start(heroes::KIND_MIND, 1, VICTIM_LEADER, 0));
    state.leaders[VICTIM_LEADER].status = -2;
    Run_(1);
    DOCTEST_CHECK(heroes::Empty());

    state.leaders[VICTIM_LEADER].status = 1;
    DOCTEST_REQUIRE(heroes::Start(heroes::KIND_MIND, 1, VICTIM_LEADER, 2));
    state.players[2].eliminated = 1;
    Run_(1);
    DOCTEST_CHECK(heroes::Empty());
}

DOCTEST_TEST_CASE("the kind of a quirk follows the loyalty")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Track_(SHIP_LEADER, 80);
    DOCTEST_CHECK(heroes::Quirk_For(SHIP_LEADER, 0) == heroes::KIND_STIMULANT);
    DOCTEST_CHECK(heroes::Quirk_For(SHIP_LEADER, 1) == heroes::KIND_PACIFISM);
    Track_(SHIP_LEADER, 50);
    DOCTEST_CHECK(heroes::Quirk_For(SHIP_LEADER, 0) == heroes::KIND_PARANOIA);
    DOCTEST_CHECK(heroes::Quirk_For(SHIP_LEADER, 1) == heroes::KIND_STIMULANT);
    Track_(SHIP_LEADER, 10);
    DOCTEST_CHECK(heroes::Quirk_For(SHIP_LEADER, 0) == heroes::KIND_PARANOIA);
    DOCTEST_CHECK(heroes::Quirk_For(SHIP_LEADER, 1) == heroes::KIND_SABOTAGE);
}

DOCTEST_TEST_CASE("a stimulant gives experience, then a crash of loyalty")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Track_(SHIP_LEADER, 60);
    DOCTEST_REQUIRE(heroes::Start(heroes::KIND_STIMULANT, 0, SHIP_LEADER));
    Run_(heroes::STIM_STEP);
    DOCTEST_CHECK(state.leaders[SHIP_LEADER].xp == heroes::STIM_XP);
    DOCTEST_CHECK(loyalty::Of(SHIP_LEADER) == 60);
    Run_(heroes::STIM_TURNS - heroes::STIM_STEP);
    DOCTEST_CHECK(state.leaders[SHIP_LEADER].xp == 2 * heroes::STIM_XP);
    DOCTEST_CHECK(loyalty::Of(SHIP_LEADER) == 60 + heroes::STIM_CRASH);
    DOCTEST_CHECK(heroes::Empty());
}

DOCTEST_TEST_CASE("paranoia lowers loyalty and the leader leaves his post")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Track_(SHIP_LEADER, 50);
    DOCTEST_REQUIRE(heroes::Start(heroes::KIND_PARANOIA, 0, SHIP_LEADER));
    Run_(heroes::PARANOIA_STEP);
    DOCTEST_CHECK(loyalty::Of(SHIP_LEADER) == 50 + heroes::PARANOIA_LOYALTY);
    DOCTEST_CHECK(state.leaders[SHIP_LEADER].status == 1);
    Run_(2 * heroes::PARANOIA_STEP);
    DOCTEST_CHECK(state.leaders[SHIP_LEADER].status == 0);  // pool, not dismissed
    DOCTEST_CHECK(state.leaders[SHIP_LEADER].player_index == 0);
    DOCTEST_CHECK(heroes::Empty());
}

DOCTEST_TEST_CASE("paranoia of a leader with low loyalty ends at once at the low mark")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Track_(SHIP_LEADER, 27);
    DOCTEST_REQUIRE(heroes::Start(heroes::KIND_PARANOIA, 0, SHIP_LEADER));
    Run_(heroes::PARANOIA_STEP);
    DOCTEST_CHECK(state.leaders[SHIP_LEADER].status == 0);
    DOCTEST_CHECK(heroes::Empty());
}

DOCTEST_TEST_CASE("a pacifist captain of a computer keeps leaving his post, then writes a treatise")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(heroes::Start(heroes::KIND_PACIFISM, 0, SHIP_LEADER));
    Run_(1);
    DOCTEST_CHECK(state.leaders[SHIP_LEADER].status == 0);
    state.leaders[SHIP_LEADER].status = 1;  // the AI gives him a post again
    Run_(1);
    DOCTEST_CHECK(state.leaders[SHIP_LEADER].status == 0);
    Run_(heroes::PACIFISM_TURNS - 2);
    DOCTEST_CHECK(state.leaders[SHIP_LEADER].xp == heroes::PACIFISM_XP);
    DOCTEST_CHECK(heroes::Empty());

    // A human's captain and a colony leader are not touched.
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_CHECK_FALSE(heroes::Start(heroes::KIND_PACIFISM, 0, SHIP_LEADER));
    DOCTEST_CHECK_FALSE(heroes::Start(heroes::KIND_PACIFISM, 1, VICTIM_LEADER));
}

DOCTEST_TEST_CASE("sabotage robs the treasury and lowers loyalty at the end")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Track_(SHIP_LEADER, 40);
    DOCTEST_REQUIRE(heroes::Start(heroes::KIND_SABOTAGE, 0, SHIP_LEADER));
    Run_(heroes::SABOTAGE_TURNS - 1);
    DOCTEST_CHECK(state.players[0].bc == 1000);
    Run_(1);
    DOCTEST_CHECK(state.players[0].bc == 1000 - heroes::SABOTAGE_BC);
    DOCTEST_CHECK(loyalty::Of(SHIP_LEADER) == 40 + heroes::SABOTAGE_LOYALTY);
    DOCTEST_CHECK(heroes::Empty());
}

DOCTEST_TEST_CASE("quirks refuse a leader nobody holds and a full table; an event ends with the leader")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK_FALSE(heroes::Start(heroes::KIND_PARANOIA, 0, 40));           // a free leader
    DOCTEST_CHECK_FALSE(heroes::Start(heroes::KIND_PARANOIA, 1, SHIP_LEADER));  // another empire's
    DOCTEST_REQUIRE(heroes::Start(heroes::KIND_SABOTAGE, 0, SHIP_LEADER));
    state.leaders[SHIP_LEADER].status = -2;
    Run_(1);
    DOCTEST_CHECK(heroes::Empty());
    DOCTEST_CHECK(state.players[0].bc == 1000);
}

DOCTEST_TEST_CASE("a quirk starts by chance and the pick draws only with the flag on")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Track_(SHIP_LEADER, 20);
    state.leaders[TELEPATH].status = -1;  // no telepath: no mind control draws
    state.leaders[TELEPATH].player_index = -1;
    Seed_For_({{1000, 1, 6}, {2, 1, 2}});
    heroes::Turn();
    DOCTEST_REQUIRE(heroes::State()->count == 1);
    const heroes::s_record& r = heroes::State()->record[0];
    DOCTEST_CHECK(r.player == 0);
    const bool right_kind = r.kind == heroes::KIND_PARANOIA || r.kind == heroes::KIND_SABOTAGE;
    DOCTEST_CHECK(right_kind);
}

DOCTEST_TEST_CASE("rival heroes: famous leaders nobody holds grow to a cap")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Famous_(&state, HERO);
    for (auto& p : state.players) p.bc = 0;
    MOX::stardate = 35003;  // the phase
    Seed_For_({{1000, 100, 1000}});
    heroes::Run_Records();
    DOCTEST_CHECK(state.leaders[HERO].xp == heroes::RIVAL_XP);
    DOCTEST_CHECK(state.leaders[HERO].status == -1);
    DOCTEST_CHECK(heroes::State()->grown[HERO] == heroes::RIVAL_XP);
    DOCTEST_CHECK_FALSE(heroes::Empty());

    // Off the phase nothing happens.
    MOX::stardate = 35004;
    heroes::Run_Records();
    DOCTEST_CHECK(state.leaders[HERO].xp == heroes::RIVAL_XP);

    for (int i = 0; i < 30; ++i) {
        Seed_For_({{1000, 100, 1000}});
        heroes::Rival_Step(true);
    }
    DOCTEST_CHECK(state.leaders[HERO].xp == heroes::RIVAL_XP_CAP);
    DOCTEST_CHECK(heroes::State()->grown[HERO] == heroes::RIVAL_XP_CAP);
}

DOCTEST_TEST_CASE("rival heroes die")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Famous_(&state, HERO);
    for (auto& p : state.players) p.bc = 0;
    Seed_For_({{1000, 1, 20}});
    heroes::Rival_Step(true);
    DOCTEST_CHECK(state.leaders[HERO].status == -2);
    int16_t list[8];
    DOCTEST_CHECK(heroes::Famous_Leaders(-1, list, 8) == 0);
    DOCTEST_CHECK_FALSE(heroes::Is_Famous(HERO));
}

DOCTEST_TEST_CASE("rival heroes are hired by the richest computer that can pay, and only then")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Famous_(&state, HERO);
    state.players[0].bc = 100;
    state.players[1].bc = 900;
    state.players[2].bc = 500;
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;  // a human is never offered the hero
    int16_t list[8];
    DOCTEST_CHECK(heroes::Famous_Leaders(-1, list, 8) == 1);
    DOCTEST_CHECK(list[0] == HERO);

    Seed_For_({{1000, 100, 1000}, {100, 1, 35}});
    heroes::Rival_Step(true);
    DOCTEST_CHECK(state.leaders[HERO].player_index == 2);
    DOCTEST_CHECK(state.leaders[HERO].status == 0);
    DOCTEST_CHECK(state.players[2].bc < 500);
    DOCTEST_CHECK(heroes::Famous_Leaders(-1, list, 8) == 0);
    DOCTEST_CHECK(heroes::Famous_Leaders(2, list, 8) == 1);
    DOCTEST_CHECK(heroes::Famous_Leaders(0, list, 8) == 0);
}

DOCTEST_TEST_CASE("rival heroes stay free when the draw says no or nobody can pay")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Famous_(&state, HERO);
    Famous_(&state, HERO2);
    state.leaders[HERO].xp = 0;
    Seed_For_({{1000, 100, 1000}, {100, 60, 100}, {1000, 100, 1000}, {100, 60, 100}});
    heroes::Rival_Step(true);
    DOCTEST_CHECK(state.leaders[HERO].player_index == -1);
    DOCTEST_CHECK(state.leaders[HERO2].player_index == -1);
    DOCTEST_CHECK(state.players[1].bc == 1000);
}

DOCTEST_TEST_CASE("the query for famous leaders works with the flag off")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Famous_(&state, HERO);
    state.leaders[SHIP_LEADER].general_skills = LEADER_GENERAL_SKILL_FAMOUS2;
    heroes::enabled = false;
    int16_t list[8];
    DOCTEST_CHECK(heroes::Famous_Leaders(0, list, 8) == 1);
    DOCTEST_CHECK(list[0] == SHIP_LEADER);
    DOCTEST_CHECK(heroes::Famous_Leaders(-1, nullptr, 0) == 1);
    DOCTEST_CHECK(heroes::Is_Famous(HERO));
    DOCTEST_CHECK_FALSE(heroes::Is_Famous(TELEPATH));
}

DOCTEST_TEST_CASE("payload round trip and bad values refused")
{
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    heroes::State()->count = 3;
    {
        heroes::s_record& a = heroes::State()->record[0];
        a.kind = heroes::KIND_MANAGER; a.player = 0; a.leader = -1; a.other = -1; a.started = 35001; a.ends = 35013;
        heroes::s_record& b = heroes::State()->record[1];
        b.kind = heroes::KIND_MIND; b.player = 1; b.leader = VICTIM_LEADER; b.other = 0; b.started = 35001; b.ends = 35031;
        heroes::s_record& c = heroes::State()->record[2];
        c.kind = heroes::KIND_PARANOIA; c.player = 0; c.leader = SHIP_LEADER; c.other = -1; c.started = 35001; c.ends = 35021; c.value = 2;
    }
    heroes::State()->grown[HERO] = 120;
    const uint32_t size = heroes::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    heroes::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    heroes::Clear();
    heroes::enabled = true;
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(heroes::Parse_Save(&reader, &error));
    DOCTEST_CHECK(heroes::Empty());
    heroes::Commit_Save();
    DOCTEST_REQUIRE(heroes::State()->count == 3);
    DOCTEST_CHECK(heroes::State()->record[1].other == 0);
    DOCTEST_CHECK(heroes::State()->record[1].leader == VICTIM_LEADER);
    DOCTEST_CHECK(heroes::State()->record[2].value == 2);
    DOCTEST_CHECK(heroes::State()->grown[HERO] == 120);

    // Layout: version (2), count (1), then 15 bytes a record: kind 0, player 1, leader 2-3, other 4, started 5-8,
    // ends 9-12, value 13-14 (15 bytes). Record 0 at 3, record 1 at 18, record 2 at 33; grown after 48.
    const auto refuse = [&](size_t offset, uint8_t value) {
        auto bad = std::make_unique<uint8_t[]>(size);
        std::copy(buffer.get(), buffer.get() + size, bad.get());
        bad[offset] = value;
        SERIAL::Reader_ r(bad.get(), size, "test underflow", true);
        return !heroes::Parse_Save(&r, &error);
    };
    DOCTEST_CHECK(refuse(3, 9));            // a bad kind
    DOCTEST_CHECK(refuse(3, 0));
    DOCTEST_CHECK(refuse(4, 0x7F));         // a bad empire
    DOCTEST_CHECK(refuse(3 + 2, 4));        // a manager with a leader
    DOCTEST_CHECK(refuse(18 + 2, 0x7F));    // a leader out of range
    DOCTEST_CHECK(refuse(18 + 4, 1));       // a master that is the victim
    DOCTEST_CHECK(refuse(33 + 4, 1));       // a stray master
    DOCTEST_CHECK(refuse(3 + 8, 0x80));     // a negative stardate
    DOCTEST_CHECK(refuse(33 + 14, 0x7F));   // a bad value
    DOCTEST_CHECK(refuse(2, 30));           // too many records
    DOCTEST_CHECK(refuse(0, 7));            // the version
    DOCTEST_CHECK(refuse(48 + 1, 0x7F));    // too much grown experience
    SERIAL::Reader_ r4(buffer.get(), size - 1, "test underflow", true);
    DOCTEST_CHECK_FALSE(heroes::Parse_Save(&r4, &error));
    DOCTEST_CHECK(heroes::State()->count == 3);  // the failed parses left the state alone
}

}
