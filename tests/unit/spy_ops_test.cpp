#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/advisor.h"
#include "game/advisor_intel.h"
#include "game/dice.h"
#include "game/colbldg.h"
#include "game/espionage.h"
#include "game/hostages.h"
#include "game/opinion.h"
#include "game/reputation.h"
#include "game/serialization.h"
#include "game/spy_ops.h"
#include "game/stability.h"

#include <algorithm>
#include <memory>

namespace {
    struct s_spy_ops_scope {
        test::s_saved_value<int32_t> operations{spy_ops::operations};
        test::s_saved_value<bool> network{espionage::network};
        test::s_saved_value<bool> ci{espionage::counterintelligence};
        test::s_saved_value<int32_t> caught{espionage::caught_opinion};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<bool> stability_enabled{stability::enabled};
        test::s_saved_value<bool> advisor_enabled{advisor::enabled};
        test::s_saved_value<uint8_t> roll_kind{dice::roll_kind};

        s_spy_ops_scope() {
            spy_ops::Clear();
            espionage::Clear();
            opinion::Clear();
            advisor::Clear_Seen_Ships();
            spy_ops::operations = 63;
            espionage::network = true;
            espionage::counterintelligence = true;
            espionage::caught_opinion = -15;
            opinion::enabled = true;
            stability::enabled = false;
            advisor::enabled = false;
            dice::roll_kind = dice::ROLL_TWO_D10;
        }
        ~s_spy_ops_scope() {
            spy_ops::Clear();
            espionage::Clear();
            opinion::Clear();
            advisor::Clear_Seen_Ships();
        }
    };

    const int32_t TRIUMPH[2] = {10, 10};
    const int32_t CATASTROPHE[2] = {1, 1};
    const int32_t ALL_OPS = 16383;

    // Dice for a plain success (not brilliant, not a triumph) of `op` against player 2 by player 1.
    void Plain_Success_(spy_ops::e_op op, int32_t* dice) {
        const int32_t natural = std::clamp(spy_ops::Difficulty(1, op, 2) - spy_ops::Bonus(1, 2), 3, 19);
        dice[0] = natural / 2;
        dice[1] = natural - natural / 2;
    }

    // Three computer empires; player 1 has 4 spies and 30 network points at player 2, whose colony 0
    // is not its capitol. Stardate 35001 (no computer turn).
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 3;
        MOX::NUM_COLONIES = 1;
        MOX::stardate = 35001;
        for (int i = 0; i < 3; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            for (int j = 0; j < 3; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
        state->players[1].spies[2] = 4;
        espionage::State()->points[1][2] = 30;
        s_colony& c = state->colonies[0];
        c.owner = 2;
        c.n_pops = 5;
        for (int i = 0; i < 5; ++i) c.pop[i] = 2;
    }

    s_leader_data& Leader_(test::s_game_state* state, int index, int owner, int8_t status) {
        s_leader_data& l = state->leaders[index];
        l.player_index = static_cast<int8_t>(owner);
        l.status = status;
        l.location = -1;
        return l;
    }
} // namespace

DOCTEST_TEST_SUITE("spy operations") {

DOCTEST_TEST_CASE("an operation needs the flag, spies and points, and spends the points") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(2, spy_ops::OP_UNREST, 1, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);  // no spies
    spy_ops::operations = 63 & ~1;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);
    spy_ops::operations = 63;
    espionage::State()->points[1][2] = 9;  // unrest costs 10
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);
    DOCTEST_CHECK(espionage::Points(1, 2) == 9);
    espionage::State()->points[1][2] = 30;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(espionage::Points(1, 2) == 20);
    espionage::network = false;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);
}

DOCTEST_TEST_CASE("unrest lowers the colony's stability and fades; an exposed operation is an incident") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    stability::enabled = true;
    const int32_t calm = stability::Compute(0);
    DOCTEST_REQUIRE(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(spy_ops::Unrest(0) == spy_ops::UNREST_PER_OPERATION);
    DOCTEST_CHECK(stability::Compute(0) == calm - spy_ops::UNREST_PER_OPERATION);
    spy_ops::Turn();
    DOCTEST_CHECK(spy_ops::Unrest(0) == spy_ops::UNREST_PER_OPERATION - 1);

    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, CATASTROPHE) == spy_ops::RESULT_EXPOSED);
    DOCTEST_CHECK((state.players[1].spies[2] & 0x3F) == 3);  // the spy is lost
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_STIRRED_UNREST) == -25);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_CAUGHT_SPYING) == -15);
    DOCTEST_CHECK(espionage::State()->caught[2] == 1);
}

DOCTEST_TEST_CASE("a scientist is killed, a leader bribed or framed; a framed leader once exposed is immune") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    s_leader_data& scientist = Leader_(&state, 5, 2, 1);
    scientist.special_skills = LEADER_COLONY_SKILL_SCIENCE_LEADER;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_KILL_SCIENTIST, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(scientist.status == -2);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_KILL_SCIENTIST, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);

    espionage::State()->points[1][2] = 30;
    Leader_(&state, 6, 2, 1).type = 1;  // a governor at a post
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_BRIBERY, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(spy_ops::Bribed_By(6) == 1);

    espionage::State()->points[1][2] = 30;
    s_leader_data& honest = Leader_(&state, 7, 2, 0);
    honest.xp = 1000;  // the highest level: the one framed
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_FRAME_LEADER, 2, -1, CATASTROPHE) == spy_ops::RESULT_EXPOSED);
    DOCTEST_CHECK(*spy_ops::Immune_Slot(7) == 1);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_FRAMED_OUR_LEADER) == -25);
    // Only the bribed governor is left to frame now.
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_FRAME_LEADER, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(state.leaders[6].status == -2);  // jailed, not dismissed to the hire list (ab-review 3.1)
    DOCTEST_REQUIRE(hostages::Find(6) >= 0);
    DOCTEST_CHECK(hostages::State()->held[hostages::Find(6)].kind == hostages::KIND_JAIL);
    hostages::Clear();
    DOCTEST_CHECK(honest.status == 0);
}

DOCTEST_TEST_CASE("data collection fills the intel memory; a false flag needs someone to blame") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::NUM_SHIPS = 1;
    s_ship_data& ship = state.ships[0];
    ship.owner = 2;
    ship.status = 0;
    ship.d.ship_weapon[0].type = WEAPON_NUCLEAR_MISSILE;
    ship.d.ship_weapon[0].count = 4;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_DATA, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(advisor::Seen_Ships(1, 2).ships == 1);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_FALSE_FLAG, 2, 1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_FALSE_FLAG, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_FALSE_FLAG, 2, 0, TRIUMPH) == spy_ops::RESULT_DONE);
}

DOCTEST_TEST_CASE("the difficulty grows with the target's watch; paranoia makes framing easier") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    const int32_t calm = spy_ops::Difficulty(1, spy_ops::OP_UNREST, 2);
    const int32_t frame_calm = spy_ops::Difficulty(1, spy_ops::OP_FRAME_LEADER, 2);
    espionage::Set_Mode(2, espionage::MODE_WITCH_HUNT);
    DOCTEST_CHECK(spy_ops::Difficulty(1, spy_ops::OP_UNREST, 2) == calm + 4);
    DOCTEST_CHECK(spy_ops::Difficulty(1, spy_ops::OP_FRAME_LEADER, 2) == frame_calm);
    DOCTEST_CHECK(spy_ops::Difficulty(1, spy_ops::OP_KILL_SCIENTIST, 2) == calm + 4 + 2);
    DOCTEST_CHECK(spy_ops::Bonus(1, 2) == 2);  // 4 spies, no spy skill
}

DOCTEST_TEST_CASE("computer empires at war go for the enemy's scientists") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].treaty[2] = state.players[2].treaty[1] = 4;
    Leader_(&state, 5, 2, 1).special_skills = LEADER_COLONY_SKILL_SCIENCE_LEADER;
    MOX::stardate = 35010;
    spy_ops::Turn();
    DOCTEST_CHECK(espionage::Points(1, 2) == 10);  // 20 spent on the operation, whatever the dice said
}

DOCTEST_TEST_CASE("off: no state, no effect") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    spy_ops::operations = 0;
    spy_ops::Turn();
    DOCTEST_CHECK(spy_ops::Unrest(0) == 0);
    DOCTEST_CHECK(spy_ops::Bribed_By(6) == -1);
    DOCTEST_CHECK(spy_ops::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Leader_(&state, 6, 2, 1).type = 1;
    DOCTEST_REQUIRE(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    espionage::State()->points[1][2] = 30;  // a bribe costs 25
    DOCTEST_REQUIRE(spy_ops::Execute_With_Dice(1, spy_ops::OP_BRIBERY, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    auto buffer = std::make_unique<uint8_t[]>(spy_ops::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), spy_ops::Save_Size(), "test overflow");
    spy_ops::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == spy_ops::Save_Size());
    spy_ops::Clear();
    SERIAL::Reader_ reader(buffer.get(), spy_ops::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(spy_ops::Parse_Save(&reader, &error));
    DOCTEST_CHECK(spy_ops::Empty());
    spy_ops::Commit_Save();
    DOCTEST_CHECK(spy_ops::Unrest(0) == spy_ops::UNREST_PER_OPERATION);
    DOCTEST_CHECK(spy_ops::Bribed_By(6) == 1);
    buffer[2] = 200;  // colony 0's unrest over the maximum
    SERIAL::Reader_ bad(buffer.get(), spy_ops::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(spy_ops::Parse_Save(&bad, &error));
}

// ---- wave 2: assassination, poison at a feast, sabotage, inside man ----

DOCTEST_TEST_CASE("assassination kills the best non-scientist leader; plain success leaves traces; exposure is an incident") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    test::s_saved_value<bool> titles{reputation::enabled};
    reputation::Clear();
    reputation::enabled = true;
    spy_ops::operations = ALL_OPS;
    espionage::State()->points[1][2] = 200;
    Leader_(&state, 5, 2, 1).special_skills = LEADER_COLONY_SKILL_SCIENCE_LEADER;
    state.leaders[5].xp = 3000;
    Leader_(&state, 6, 2, 1).xp = 0;
    Leader_(&state, 7, 2, 1).xp = 1000;
    DOCTEST_CHECK(spy_ops::Difficulty(1, spy_ops::OP_ASSASSINATION, 2) == spy_ops::Difficulty(1, spy_ops::OP_UNREST, 2) + 3);

    int32_t plain[2];
    Plain_Success_(spy_ops::OP_ASSASSINATION, plain);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_ASSASSINATION, 2, -1, plain) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(state.leaders[7].status == -2);
    DOCTEST_CHECK(state.leaders[5].status == 1);  // the scientist is not the target
    DOCTEST_CHECK(reputation::Has(1, reputation::TITLE_KINGSLAYER));
    DOCTEST_CHECK(espionage::Points(1, 2) == 160);

    reputation::Clear();
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_ASSASSINATION, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(state.leaders[6].status == -2);
    DOCTEST_CHECK_FALSE(reputation::Has(1, reputation::TITLE_KINGSLAYER));  // brilliant: no traces
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_ASSASSINATION, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);  // only the scientist is left

    state.leaders[7].status = 1;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_ASSASSINATION, 2, -1, CATASTROPHE) == spy_ops::RESULT_EXPOSED);
    DOCTEST_CHECK((state.players[1].spies[2] & 0x3F) == 3);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_TRIED_TO_KILL_OUR_LEADER) == -50);  // peace, a friend: the betrayal's doubled blow
    DOCTEST_CHECK_FALSE(reputation::Has(1, reputation::TITLE_ATROCITIES));
}

DOCTEST_TEST_CASE("poison at a feast needs a treaty; an exposed poisoning is an atrocity") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    test::s_saved_value<bool> titles{reputation::enabled};
    reputation::Clear();
    reputation::enabled = true;
    spy_ops::operations = ALL_OPS;
    espionage::State()->points[1][2] = 200;
    Leader_(&state, 7, 2, 1).xp = 1000;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_POISON_FEAST, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);  // no talks
    state.players[1].treaty[2] = 4;  // war
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_POISON_FEAST, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);
    state.players[1].treaty[2] = 0;
    state.players[1].trade_treaty[2] = 1;  // a trade treaty gives access too
    DOCTEST_CHECK(spy_ops::Difficulty(1, spy_ops::OP_POISON_FEAST, 2) == spy_ops::Difficulty(1, spy_ops::OP_UNREST, 2));

    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_POISON_FEAST, 2, -1, CATASTROPHE) == spy_ops::RESULT_EXPOSED);
    DOCTEST_CHECK(reputation::Has(1, reputation::TITLE_ATROCITIES));
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_TRIED_TO_KILL_OUR_LEADER) == -100);  // -25 twice, doubled: a friend betrayed
    DOCTEST_CHECK(state.leaders[7].status == 1);

    state.players[1].trade_treaty[2] = 0;
    state.players[1].treaty[2] = 2;
    int32_t plain[2];
    Plain_Success_(spy_ops::OP_POISON_FEAST, plain);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_POISON_FEAST, 2, -1, plain) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(state.leaders[7].status == -2);
    DOCTEST_CHECK(reputation::Has(1, reputation::TITLE_KINGSLAYER));
}

DOCTEST_TEST_CASE("sabotaging a shipyard wipes the progress and stalls the colony for twelve turns") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    spy_ops::operations = ALL_OPS;
    espionage::State()->points[1][2] = 200;
    s_colony& c = state.colonies[0];
    c.production[ECON_INDUSTRY] = 30;
    c.production_spent = 100;
    c.producing[0] = BUILDING_NO_BUILDING + 1;  // a building, not a ship
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_SABOTAGE_SHIPYARD, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);
    c.producing[0] = COLBLDG::Colony_Production_From_Ship_Design_Index(0);
    DOCTEST_CHECK(spy_ops::Difficulty(1, spy_ops::OP_SABOTAGE_SHIPYARD, 2) == spy_ops::Difficulty(1, spy_ops::OP_UNREST, 2) + 1);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_SABOTAGE_SHIPYARD, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(c.production_spent == 0);
    DOCTEST_CHECK(spy_ops::Stalled(0));
    DOCTEST_CHECK(espionage::Points(1, 2) == 175);

    c.production_spent = 100;
    spy_ops::Turn();
    DOCTEST_CHECK(c.production_spent == 70);  // the turn's industry is lost
    for (int i = 0; i < spy_ops::SABOTAGE_TURNS - 1; ++i) spy_ops::Turn();
    DOCTEST_CHECK(spy_ops::SABOTAGE_TURNS == 12);
    DOCTEST_CHECK(c.production_spent == 0);   // clamped
    DOCTEST_CHECK_FALSE(spy_ops::Stalled(0));
    c.production_spent = 100;
    spy_ops::Turn();
    DOCTEST_CHECK(c.production_spent == 100);  // the stall is over

    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_SABOTAGE_SHIPYARD, 2, -1, CATASTROPHE) == spy_ops::RESULT_EXPOSED);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_SABOTAGE) == -50);  // a friend betrayed: doubled
}

DOCTEST_TEST_CASE("sabotaged research loses three quarters; a strike stalls without wiping; sabotaged trade loses freighters") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    spy_ops::operations = ALL_OPS;
    espionage::State()->points[1][2] = 200;
    state.players[2].research_accumulated = 100;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_SABOTAGE_RESEARCH, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(state.players[2].research_accumulated == 25);

    s_colony& c = state.colonies[0];
    c.production_spent = 40;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_STRIKE, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);  // no industry
    c.production[ECON_INDUSTRY] = 10;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_STRIKE, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(c.production_spent == 40);
    DOCTEST_CHECK(spy_ops::Stalled(0));

    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_SABOTAGE_TRADE, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);  // none to lose
    state.players[2].n_freighters = 20;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_SABOTAGE_TRADE, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(state.players[2].n_freighters == 10);  // half
    state.players[2].n_freighters = 4;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_SABOTAGE_TRADE, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(state.players[2].n_freighters == 1);  // at least three
    state.players[2].n_freighters = 2;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_SABOTAGE_TRADE, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(state.players[2].n_freighters == 0);  // never more than there are
}

DOCTEST_TEST_CASE("a famine lasts twelve turns and lowers the food per farmer") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    spy_ops::operations = ALL_OPS;
    espionage::State()->points[1][2] = 200;
    DOCTEST_CHECK(spy_ops::Food2_Penalty(&state.colonies[0]) == 0);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_SABOTAGE_FOOD, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(spy_ops::Food2_Penalty(&state.colonies[0]) == spy_ops::FAMINE_FOOD2);
    for (int i = 0; i < spy_ops::SABOTAGE_TURNS - 1; ++i) spy_ops::Turn();
    DOCTEST_CHECK(spy_ops::Food2_Penalty(&state.colonies[0]) == spy_ops::FAMINE_FOOD2);
    spy_ops::Turn();
    DOCTEST_CHECK(spy_ops::Food2_Penalty(&state.colonies[0]) == 0);
    DOCTEST_CHECK(spy_ops::Food2_Penalty(nullptr) == 0);
}

DOCTEST_TEST_CASE("poisoned water kills population; exposure is an atrocity") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    test::s_saved_value<bool> titles{reputation::enabled};
    reputation::Clear();
    reputation::enabled = true;
    spy_ops::operations = ALL_OPS;
    espionage::State()->points[1][2] = 200;
    state.colonies[0].buildings[BUILDING_CAPITOL] = 1;
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_POISON_WATER, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);  // only the capital
    state.colonies[0].buildings[BUILDING_CAPITOL] = 0;
    DOCTEST_CHECK(spy_ops::Difficulty(1, spy_ops::OP_POISON_WATER, 2) == spy_ops::Difficulty(1, spy_ops::OP_UNREST, 2) + 2);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_POISON_WATER, 2, -1, CATASTROPHE) == spy_ops::RESULT_EXPOSED);
    DOCTEST_CHECK(reputation::Has(1, reputation::TITLE_ATROCITIES));
    DOCTEST_CHECK(state.colonies[0].n_pops == 5);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_POISON_WATER, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(state.colonies[0].n_pops == 5 - spy_ops::POISON_WATER_BIO_HITS);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_POISON_WATER, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);  // three still count
    DOCTEST_CHECK(state.colonies[0].n_pops == 1);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_POISON_WATER, 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);  // never wiped out
}

DOCTEST_TEST_CASE("inside man: the briber's invasion meets half the troops and spends the bribe") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    test::s_saved_value<bool> flag{spy_ops::inside_man};
    spy_ops::inside_man = true;
    Leader_(&state, 6, 2, 1).type = 1;
    state.colonies[0].planet = 0;
    state.planets[0].star_index = 0;
    state.stars[0].officer_index[2] = 6;
    *spy_ops::Bribed_Slot(6) = 1;
    s_colony* colony = &state.colonies[0];
    DOCTEST_CHECK(spy_ops::Inside_Man_Troops(colony, 1, 10) == 5);
    DOCTEST_CHECK(spy_ops::Inside_Man_Troops(colony, 0, 10) == 10);  // not the briber
    DOCTEST_CHECK_FALSE(spy_ops::Inside_Man_Spend(colony, 0));
    DOCTEST_CHECK(spy_ops::Bribed_By(6) == 1);
    spy_ops::inside_man = false;
    DOCTEST_CHECK(spy_ops::Inside_Man_Troops(colony, 1, 10) == 10);
    spy_ops::inside_man = true;
    DOCTEST_CHECK(spy_ops::Inside_Man_Spend(colony, 1));
    DOCTEST_CHECK(spy_ops::Bribed_By(6) == -1);
    DOCTEST_CHECK(state.leaders[6].status != 1);  // no longer at the post
    DOCTEST_CHECK(spy_ops::Inside_Man_Troops(colony, 1, 10) == 10);  // the bribe is spent
    spy_ops::operations = 63 & ~4;  // without bribery nothing is bribed
    DOCTEST_CHECK(spy_ops::Inside_Man_Troops(colony, 1, 10) == 10);
}

DOCTEST_TEST_CASE("the v3 payload round-trips the stall, famine and mourning timers; v1 and v2 payloads still load") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    spy_ops::operations = ALL_OPS;
    espionage::State()->points[1][2] = 200;
    state.colonies[0].production[ECON_INDUSTRY] = 10;
    DOCTEST_REQUIRE(spy_ops::Execute_With_Dice(1, spy_ops::OP_STRIKE, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_REQUIRE(spy_ops::Execute_With_Dice(1, spy_ops::OP_SABOTAGE_FOOD, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_REQUIRE(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    *spy_ops::Mourn_Slot(0) = 4;
    const uint32_t size = spy_ops::Save_Size();
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    spy_ops::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    DOCTEST_CHECK(buffer[0] == 3);
    spy_ops::Clear();
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(spy_ops::Parse_Save(&reader, &error));
    spy_ops::Commit_Save();
    DOCTEST_CHECK(spy_ops::Stalled(0));
    DOCTEST_CHECK(spy_ops::Food2_Penalty(&state.colonies[0]) == spy_ops::FAMINE_FOOD2);
    DOCTEST_CHECK(spy_ops::Unrest(0) == spy_ops::UNREST_PER_OPERATION);
    DOCTEST_CHECK(*spy_ops::Mourn_Slot(0) == 4);

    // A version 2 payload has no mourning table; version 1 lacks the two stall and famine tables too.
    const uint32_t v2_size = size - MAX_COLONIES;
    auto v2 = std::make_unique<uint8_t[]>(v2_size);
    std::copy(buffer.get(), buffer.get() + v2_size, v2.get());
    v2[0] = 2;
    spy_ops::Clear();
    SERIAL::Reader_ v2_reader(v2.get(), v2_size, "test underflow", true);
    DOCTEST_REQUIRE(spy_ops::Parse_Save(&v2_reader, &error));
    spy_ops::Commit_Save();
    DOCTEST_CHECK(spy_ops::Stalled(0));
    DOCTEST_CHECK(*spy_ops::Mourn_Slot(0) == 0);
    const uint32_t v1_size = size - 3 * MAX_COLONIES;
    auto old = std::make_unique<uint8_t[]>(v1_size);
    std::copy(buffer.get(), buffer.get() + v1_size, old.get());
    old[0] = 1;
    old[1] = 0;
    spy_ops::Clear();
    SERIAL::Reader_ old_reader(old.get(), v1_size, "test underflow", true);
    DOCTEST_REQUIRE(spy_ops::Parse_Save(&old_reader, &error));
    spy_ops::Commit_Save();
    DOCTEST_CHECK(spy_ops::Unrest(0) == spy_ops::UNREST_PER_OPERATION);
    DOCTEST_CHECK_FALSE(spy_ops::Stalled(0));
    DOCTEST_CHECK(spy_ops::Food2_Penalty(&state.colonies[0]) == 0);

    buffer[2 + MAX_COLONIES + MAX_LEADERS * 2] = spy_ops::SABOTAGE_TURNS + 1;  // colony 0's stall over the maximum
    spy_ops::Clear();
    SERIAL::Reader_ bad(buffer.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(spy_ops::Parse_Save(&bad, &error));
    buffer[2 + MAX_COLONIES + MAX_LEADERS * 2] = 0;
    buffer[2 + MAX_COLONIES + MAX_LEADERS * 2 + 2 * MAX_COLONIES] = spy_ops::MOURN_TURNS + 1;  // mourning over the maximum
    spy_ops::Clear();
    SERIAL::Reader_ bad2(buffer.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(spy_ops::Parse_Save(&bad2, &error));
}

DOCTEST_TEST_CASE("a killed governor leaves unrest and mourning in his colony; the bits off leave nothing") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    spy_ops::operations = ALL_OPS;
    espionage::State()->points[1][2] = 200;
    Leader_(&state, 7, 2, 1).type = 1;
    state.leaders[7].location = 0;
    state.leaders[7].xp = 1000;
    state.colonies[0].planet = 0;
    state.planets[0].star_index = 0;
    state.stars[0].officer_index[2] = 7;
    DOCTEST_CHECK(spy_ops::Morale_Effect(&state.colonies[0]) == 0);
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_ASSASSINATION, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(state.leaders[7].status == -2);
    DOCTEST_CHECK(spy_ops::Unrest(0) == spy_ops::GOVERNOR_UNREST);
    DOCTEST_CHECK(spy_ops::Morale_Effect(&state.colonies[0]) == spy_ops::MOURN_MORALE);
    for (int i = 0; i < spy_ops::MOURN_TURNS; ++i) spy_ops::Turn();
    DOCTEST_CHECK(spy_ops::Morale_Effect(&state.colonies[0]) == 0);
    DOCTEST_CHECK(spy_ops::Unrest(0) == 0);

    // The assassination bits off: a stored mark is invisible.
    *spy_ops::Mourn_Slot(0) = 5;
    *spy_ops::Unrest_Slot(0) = 5;
    spy_ops::operations = 62;  // the classic bits without unrest
    DOCTEST_CHECK(spy_ops::Morale_Effect(&state.colonies[0]) == 0);
    DOCTEST_CHECK(spy_ops::Unrest(0) == 0);
    spy_ops::operations = 63 | 128;
    DOCTEST_CHECK(spy_ops::Morale_Effect(&state.colonies[0]) == spy_ops::MOURN_MORALE);
}

DOCTEST_TEST_CASE("a killed ship officer's group loses crew experience") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    test::s_saved_value<decltype(MOX::NUM_SHIPS)> ship_count{MOX::NUM_SHIPS};
    MOX::NUM_SHIPS = 4;
    spy_ops::operations = ALL_OPS;
    espionage::State()->points[1][2] = 200;
    Leader_(&state, 7, 2, 1).location = 0;  // type 0: a ship officer
    state.leaders[7].xp = 1000;
    for (int i = 0; i < 4; ++i) {
        state.ships[i].owner = 2;
        state.ships[i].status = 1;
        state.ships[i].location = 5;
        state.ships[i].x = 10;
        state.ships[i].y = 10;
        state.ships[i].crew_experience = 100;
    }
    state.ships[0].officer_index = 7;
    state.ships[2].x = 40;      // elsewhere
    state.ships[3].owner = 1;   // not his
    DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, spy_ops::OP_ASSASSINATION, 2, -1, TRIUMPH) == spy_ops::RESULT_DONE);
    DOCTEST_CHECK(state.leaders[7].status == -2);
    DOCTEST_CHECK(state.ships[0].crew_experience == 50);
    DOCTEST_CHECK(state.ships[1].crew_experience == 50);
    DOCTEST_CHECK(state.ships[2].crew_experience == 100);
    DOCTEST_CHECK(state.ships[3].crew_experience == 100);
}

DOCTEST_TEST_CASE("the computers rotate over all sabotage kinds and skip a stalled or starving colony") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    spy_ops::operations = ALL_OPS;
    espionage::State()->points[1][2] = 1000;
    state.players[1].treaty[2] = state.players[2].treaty[1] = 4;
    state.players[1].personality = PLAYER_PERSONALITY_AGGRESSIVE;
    state.players[2].research_accumulated = 100;
    state.players[2].n_freighters = 20;
    s_colony& c = state.colonies[0];
    c.production[ECON_INDUSTRY] = 10;
    c.production_spent = 50;
    c.producing[0] = COLBLDG::Colony_Production_From_Ship_Design_Index(0);
    bool seen[spy_ops::OP_COUNT] = {};
    for (int turn = 0; turn < 5; ++turn) {
        MOX::stardate = 35010 + turn * 10;
        seen[spy_ops::Choose_Operation(1, 2)] = true;
    }
    for (spy_ops::e_op op : {spy_ops::OP_SABOTAGE_SHIPYARD, spy_ops::OP_SABOTAGE_RESEARCH, spy_ops::OP_SABOTAGE_FOOD,
             spy_ops::OP_STRIKE, spy_ops::OP_SABOTAGE_TRADE}) {
        DOCTEST_CHECK(seen[op]);
    }
    *spy_ops::Stall_Slot(0) = 5;
    *spy_ops::Famine_Slot(0) = 5;
    for (int turn = 0; turn < 5; ++turn) {
        MOX::stardate = 35010 + turn * 10;
        const spy_ops::e_op op = spy_ops::Choose_Operation(1, 2);
        DOCTEST_CHECK((op == spy_ops::OP_SABOTAGE_RESEARCH || op == spy_ops::OP_SABOTAGE_TRADE));
    }
}

DOCTEST_TEST_CASE("with the new bits off nothing new happens") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);  // operations = 63: the six classic bits only
    espionage::State()->points[1][2] = 200;
    Leader_(&state, 7, 2, 1).xp = 1000;
    state.players[1].treaty[2] = 2;
    state.colonies[0].production[ECON_INDUSTRY] = 10;
    state.players[2].research_accumulated = 100;
    state.players[2].n_freighters = 20;
    for (int op = spy_ops::OP_ASSASSINATION; op < spy_ops::OP_COUNT; ++op) {
        DOCTEST_CHECK(spy_ops::Execute_With_Dice(1, static_cast<spy_ops::e_op>(op), 2, -1, TRIUMPH) == spy_ops::RESULT_NOT_POSSIBLE);
    }
    DOCTEST_CHECK(espionage::Points(1, 2) == 200);
    DOCTEST_CHECK(state.leaders[7].status == 1);
    DOCTEST_CHECK(state.players[2].research_accumulated == 100);
    DOCTEST_CHECK(spy_ops::Food2_Penalty(&state.colonies[0]) == 0);
    DOCTEST_CHECK(spy_ops::Empty());
    // The computers use only the enabled operations.
    state.players[1].personality = PLAYER_PERSONALITY_RUTHLESS;
    state.players[1].treaty[2] = state.players[2].treaty[1] = 4;
    MOX::stardate = 35010;
    spy_ops::operations = 0;
    spy_ops::Turn();
    DOCTEST_CHECK(espionage::Points(1, 2) == 200);
}

DOCTEST_TEST_CASE("a ruthless computer at war chooses an assassination") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    spy_ops::operations = 64;
    espionage::State()->points[1][2] = 100;
    Leader_(&state, 7, 2, 1).xp = 1000;
    state.players[1].treaty[2] = state.players[2].treaty[1] = 4;
    state.players[1].personality = PLAYER_PERSONALITY_RUTHLESS;
    MOX::stardate = 35010;
    spy_ops::Turn();
    DOCTEST_CHECK(espionage::Points(1, 2) == 60);  // 40 spent, whatever the dice said
}

// ---- Iteration 4, piece A: "все против всех" (every computer against everyone, by character).
}  // the suite's block ends here: the helpers below live in an anonymous namespace

namespace {
    // Everything sabotage and assassination could bite: player 2 has a shipyard colony, research, freighters,
    // industry and a leader; `scientist` adds a scientist. Player 1 has plenty of points.
    void Arm_Target_(test::s_game_state* state, bool scientist) {
        spy_ops::operations = ALL_OPS;
        espionage::State()->points[1][2] = 1000;
        state->players[2].research_accumulated = 100;
        state->players[2].n_freighters = 20;
        s_colony& c = state->colonies[0];
        c.production[ECON_INDUSTRY] = 10;
        c.production_spent = 50;
        c.producing[0] = COLBLDG::Colony_Production_From_Ship_Design_Index(0);
        Leader_(state, 7, 2, 1).xp = 1000;
        if (scientist) Leader_(state, 5, 2, 1).special_skills = LEADER_COLONY_SKILL_SCIENCE_LEADER;
    }

    // The set of operations player 1 chooses against player 2 over `periods` computer turns (bit per e_op;
    // bit OP_COUNT: the points were saved).
    uint32_t Seen_(int periods) {
        uint32_t seen = 0;
        for (int i = 0; i < periods; ++i) {
            MOX::stardate = 35000 + i * 10;
            seen |= 1u << spy_ops::Choose_Operation(1, 2);
        }
        return seen;
    }

    constexpr uint32_t Bit_(spy_ops::e_op op) { return 1u << op; }
    constexpr uint32_t SABOTAGE_SET = Bit_(spy_ops::OP_SABOTAGE_SHIPYARD) | Bit_(spy_ops::OP_SABOTAGE_RESEARCH) |
        Bit_(spy_ops::OP_SABOTAGE_FOOD) | Bit_(spy_ops::OP_SABOTAGE_TRADE) | Bit_(spy_ops::OP_STRIKE);
}

DOCTEST_TEST_SUITE("spy operations, all against all") {

DOCTEST_TEST_CASE("an aggressive computer sabotages a foe in peace and saves points instead of collecting data") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Arm_Target_(&state, true);
    state.players[1].personality = PLAYER_PERSONALITY_AGGRESSIVE;
    state.players[1].relations[2] = -50;
    const uint32_t seen = Seen_(12);
    DOCTEST_CHECK((seen & ~SABOTAGE_SET) == 0);  // sabotage only in peace: no assassination, no data
    DOCTEST_CHECK(seen != 0);
    espionage::State()->points[1][2] = 10;  // not yet 25
    MOX::stardate = 35010;
    DOCTEST_CHECK(spy_ops::Choose_Operation(1, 2) == spy_ops::OP_COUNT);
    spy_ops::operations = 63;  // the old bits: the old choice, data collection
    DOCTEST_CHECK(spy_ops::Choose_Operation(1, 2) == spy_ops::OP_DATA);
}

DOCTEST_TEST_CASE("an aggressive computer at war assassinates and sabotages; a ruthless one also poisons the water") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Arm_Target_(&state, true);
    state.players[1].treaty[2] = state.players[2].treaty[1] = 4;
    state.players[1].personality = PLAYER_PERSONALITY_AGGRESSIVE;
    uint32_t seen = Seen_(12);
    DOCTEST_CHECK((seen & Bit_(spy_ops::OP_ASSASSINATION)) != 0);
    DOCTEST_CHECK((seen & Bit_(spy_ops::OP_KILL_SCIENTIST)) != 0);  // a scientist is still killed at war
    DOCTEST_CHECK((seen & SABOTAGE_SET) != 0);
    DOCTEST_CHECK((seen & (Bit_(spy_ops::OP_POISON_WATER) | Bit_(spy_ops::OP_POISON_FEAST))) == 0);
    state.players[1].personality = PLAYER_PERSONALITY_XENOPHOBIC;
    seen = Seen_(12);
    DOCTEST_CHECK((seen & Bit_(spy_ops::OP_POISON_WATER)) == 0);
    state.players[1].personality = PLAYER_PERSONALITY_RUTHLESS;
    seen = Seen_(24);
    DOCTEST_CHECK((seen & Bit_(spy_ops::OP_POISON_WATER)) != 0);
    DOCTEST_CHECK((seen & Bit_(spy_ops::OP_ASSASSINATION)) != 0);
}

DOCTEST_TEST_CASE("a ruthless computer takes everything against a foe in peace, poison at a feast needs a treaty") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Arm_Target_(&state, true);
    state.players[1].personality = PLAYER_PERSONALITY_RUTHLESS;
    state.players[1].relations[2] = -80;
    state.players[1].treaty[2] = 1;  // a non-aggression pact: a seat at the table
    const uint32_t seen = Seen_(30);
    DOCTEST_CHECK((seen & Bit_(spy_ops::OP_POISON_FEAST)) != 0);
    DOCTEST_CHECK((seen & Bit_(spy_ops::OP_ASSASSINATION)) != 0);
    DOCTEST_CHECK((seen & SABOTAGE_SET) != 0);
    DOCTEST_CHECK((seen & Bit_(spy_ops::OP_DATA)) == 0);
}

DOCTEST_TEST_CASE("a pacifist at war picks only bloodless sabotage; an honorable one sabotages only at war") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Arm_Target_(&state, true);
    state.players[1].treaty[2] = state.players[2].treaty[1] = 4;
    state.players[1].personality = PLAYER_PERSONALITY_PACIFIST;
    const uint32_t bloodless = Bit_(spy_ops::OP_SABOTAGE_SHIPYARD) | Bit_(spy_ops::OP_SABOTAGE_RESEARCH) | Bit_(spy_ops::OP_SABOTAGE_TRADE);
    uint32_t seen = Seen_(12);
    DOCTEST_CHECK(seen != 0);
    DOCTEST_CHECK((seen & ~bloodless) == 0);  // no famine, no strike, no poisoned water, no killing
    state.players[1].personality = PLAYER_PERSONALITY_HONORABLE;
    seen = Seen_(12);
    DOCTEST_CHECK((seen & ~SABOTAGE_SET) == 0);
    DOCTEST_CHECK((seen & bloodless) != 0);
    // In peace, even against a foe: nothing new, the old choice (data collection).
    state.players[1].treaty[2] = state.players[2].treaty[1] = 0;
    state.players[1].relations[2] = -80;
    MOX::stardate = 35010;
    DOCTEST_CHECK(spy_ops::Choose_Operation(1, 2) == spy_ops::OP_DATA);
    state.players[1].personality = PLAYER_PERSONALITY_PACIFIST;
    DOCTEST_CHECK(spy_ops::Choose_Operation(1, 2) == spy_ops::OP_DATA);
}

DOCTEST_TEST_CASE("a friend weaker than us is left alone, a stronger one is sabotaged once in thirty turns") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Arm_Target_(&state, false);
    state.players[1].personality = PLAYER_PERSONALITY_RUTHLESS;
    state.players[1].relations[2] = 10;
    // Weaker (equal): data collection as before, whatever the turn.
    DOCTEST_CHECK((Seen_(30) & ~Bit_(spy_ops::OP_DATA)) == 0);
    // Stronger than us: sabotage on the friend's turn, the points saved in between; every 30 turns.
    state.players[2].total_ship_strength[1] = 100;
    int last = -1;
    int count = 0;
    for (int i = 0; i < 30; ++i) {
        MOX::stardate = 35000 + i * 10;
        const spy_ops::e_op op = spy_ops::Choose_Operation(1, 2);
        if (op == spy_ops::OP_COUNT) continue;
        DOCTEST_CHECK((Bit_(op) & SABOTAGE_SET) != 0);
        if (last >= 0) DOCTEST_CHECK(i - last == 3);
        last = i;
        ++count;
    }
    DOCTEST_CHECK(count == 10);
    // The strongest empire, though not stronger than us at us: also a target.
    state.players[2].total_ship_strength[1] = 0;
    state.players[2].total_ship_strength[0] = 50;
    DOCTEST_CHECK((Seen_(3) & SABOTAGE_SET) != 0);
    // A friend in a common war is left alone.
    state.players[1].treaty[0] = state.players[0].treaty[1] = 4;
    state.players[2].treaty[0] = state.players[0].treaty[2] = 4;
    DOCTEST_CHECK((Seen_(30) & ~Bit_(spy_ops::OP_DATA)) == 0);
}

DOCTEST_TEST_CASE("the false flag sets two empires that get on against each other, when one is stronger than us") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    spy_ops::operations = ALL_OPS;
    espionage::State()->points[1][2] = 1000;
    state.players[1].personality = PLAYER_PERSONALITY_AGGRESSIVE;
    state.players[2].relations[0] = state.players[0].relations[2] = 30;
    state.players[0].total_ship_strength[1] = 100;  // player 0 stronger than us
    DOCTEST_CHECK((Seen_(30) & Bit_(spy_ops::OP_FALSE_FLAG)) != 0);
    state.players[0].relations[2] = 10;  // they do not get on
    DOCTEST_CHECK((Seen_(30) & Bit_(spy_ops::OP_FALSE_FLAG)) == 0);
    state.players[0].relations[2] = 30;
    state.players[2].treaty[0] = state.players[0].treaty[2] = 4;  // at war with each other
    DOCTEST_CHECK((Seen_(30) & Bit_(spy_ops::OP_FALSE_FLAG)) == 0);
    state.players[2].treaty[0] = state.players[0].treaty[2] = 0;
    state.players[0].total_ship_strength[1] = 0;  // neither stronger than us
    DOCTEST_CHECK((Seen_(30) & Bit_(spy_ops::OP_FALSE_FLAG)) == 0);
    state.players[0].total_ship_strength[1] = 100;
    state.players[1].personality = PLAYER_PERSONALITY_HONORABLE;  // honourable computers use no false flag
    DOCTEST_CHECK((Seen_(30) & Bit_(spy_ops::OP_FALSE_FLAG)) == 0);
}

DOCTEST_TEST_CASE("an exposed operation against a friend is a betrayal: the blow to the relations is doubled") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    spy_ops::operations = ALL_OPS;
    DOCTEST_REQUIRE(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, CATASTROPHE) == spy_ops::RESULT_EXPOSED);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_STIRRED_UNREST) == -50);
    // At war or against a foe: the usual blow.
    opinion::Clear();
    state.players[1].treaty[2] = state.players[2].treaty[1] = 4;
    DOCTEST_REQUIRE(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, CATASTROPHE) == spy_ops::RESULT_EXPOSED);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_STIRRED_UNREST) == -25);
    // The old bits only: the usual blow, whatever the terms.
    opinion::Clear();
    state.players[1].treaty[2] = state.players[2].treaty[1] = 0;
    spy_ops::operations = 63;
    DOCTEST_REQUIRE(spy_ops::Execute_With_Dice(1, spy_ops::OP_UNREST, 2, -1, CATASTROPHE) == spy_ops::RESULT_EXPOSED);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_STIRRED_UNREST) == -25);
}

DOCTEST_TEST_CASE("with only the old bits the chooser is the old one; with the bits off nothing runs") {
    s_spy_ops_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Arm_Target_(&state, true);
    spy_ops::operations = 63;
    state.players[1].personality = PLAYER_PERSONALITY_AGGRESSIVE;
    state.players[1].relations[2] = -80;
    state.players[2].total_ship_strength[1] = 100;
    DOCTEST_CHECK(Seen_(12) == Bit_(spy_ops::OP_DATA));  // not at war, not the main rival: data, as before
    state.players[1].treaty[2] = state.players[2].treaty[1] = 4;
    DOCTEST_CHECK((Seen_(12) & ~(Bit_(spy_ops::OP_KILL_SCIENTIST) | Bit_(spy_ops::OP_UNREST) | Bit_(spy_ops::OP_DATA))) == 0);
    spy_ops::operations = 0;
    MOX::stardate = 35010;
    const int32_t before = espionage::Points(1, 2);
    spy_ops::Turn();
    DOCTEST_CHECK(espionage::Points(1, 2) == before);
}

}
