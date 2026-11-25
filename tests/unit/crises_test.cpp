#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/beliefs.h"
#include "game/crises.h"
#include "game/dice.h"
#include "game/random.h"
#include "game/serialization.h"

#include <memory>

namespace {
    struct s_crises_scope {
        test::s_saved_value<bool> enabled{crises::enabled};
        test::s_saved_value<int32_t> chance{crises::chance};
        test::s_saved_value<bool> model{beliefs::neighbour_model};
        test::s_saved_value<uint8_t> roll_kind{dice::roll_kind};
        test::s_saved_value<bool> situations{crises::situations};
        test::s_saved_value<bool> front{crises::crisis_front};
        test::s_saved_value<int32_t> strength{crises::front_strength};
        test::s_saved_value<decltype(MOX::settings.xenons_exist)> xenons{MOX::settings.xenons_exist};
        test::s_saved_value<int16_t> resource{MOX::antarans.resource_offensive};

        s_crises_scope() {
            crises::Clear();
            beliefs::Clear();
            crises::enabled = true;
            crises::chance = 0;  // nothing starts by chance in these tests
            beliefs::neighbour_model = true;
            dice::roll_kind = dice::ROLL_TWO_D10;
            crises::situations = false;
            crises::crisis_front = false;
            crises::front_strength = 0;
        }
        ~s_crises_scope() {
            crises::Clear();
            beliefs::Clear();
        }
    };

    const int32_t TRIUMPH[2] = {10, 10};
    const int32_t NEAR_MISS[2] = {5, 6};   // 11 against 12: a plain failure, a partial success
    const int32_t CATASTROPHE[2] = {1, 1};

    // Two computer empires; player 0 holds colonies 0 (the capital, a hydroponic farm) and 1.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 2;
        MOX::NUM_COLONIES = 2;
        MOX::stardate = 35100;
        for (int i = 0; i < 2; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[i].personality = PLAYER_PERSONALITY_PACIFIST;
            for (int j = 0; j < 2; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
        state->colonies[0].owner = 0;
        state->colonies[0].n_pops = 6;
        state->colonies[0].planet = 0;
        state->colonies[0].buildings[BUILDING_CAPITOL] = 1;
        state->colonies[0].buildings[BUILDING_HYDROPONIC_FARM] = 1;
        state->colonies[1].owner = 0;
        state->colonies[1].n_pops = 3;
        state->colonies[1].planet = 1;
        state->planets[0].colony_index = 0;
        state->planets[1].colony_index = 1;
    }
} // namespace

DOCTEST_TEST_SUITE("crises") {

DOCTEST_TEST_CASE("check bonuses come from the empire's standing and a leader's skill") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].research_produced = 150;
    DOCTEST_CHECK(crises::Check_Bonus(0, crises::CHECK_SCIENCE) == 3);
    s_leader_data& leader = state.leaders[2];
    leader.player_index = 0;
    leader.status = 1;
    leader.general_skills = LEADER_GENERAL_SKILL_RESEARCHER;
    DOCTEST_CHECK(crises::Check_Bonus(0, crises::CHECK_SCIENCE) == 3 + crises::LEADER_BONUS);
    state.players[0].spies[0] = 6;
    DOCTEST_CHECK(crises::Check_Bonus(0, crises::CHECK_ESPIONAGE) == 3);
}

DOCTEST_TEST_CASE("computers choose by character among the options they can pay for") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    // A pacifist leans to diplomacy: evacuate.
    DOCTEST_CHECK(crises::Choose_Option(0, crises::CRISIS_ASTEROID) == 2);
    state.players[0].personality = PLAYER_PERSONALITY_AGGRESSIVE;
    DOCTEST_CHECK(crises::Choose_Option(0, crises::CRISIS_ASTEROID) == 2);  // cannot pay 50 BC to blow it up
    state.players[0].bc = 60;
    DOCTEST_CHECK(crises::Choose_Option(0, crises::CRISIS_ASTEROID) == 1);
}

DOCTEST_TEST_CASE("an asteroid: the choice costs now, the check decides three turns later") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].personality = PLAYER_PERSONALITY_AGGRESSIVE;
    state.players[0].bc = 60;
    DOCTEST_REQUIRE(crises::Open(0, crises::CRISIS_ASTEROID, -1));
    DOCTEST_CHECK(state.players[0].bc == 10);
    DOCTEST_CHECK(crises::Slots()[0].option == 1);
    DOCTEST_CHECK(crises::Slots()[0].due == 35100 + crises::CHECK_DELAY);
    DOCTEST_CHECK_FALSE(crises::Open(0, crises::CRISIS_REVOLUTIONARIES, 1));  // one crisis at a time
    DOCTEST_CHECK(crises::Resolve_With_Dice(0, NEAR_MISS) == crises::RESULT_PARTIAL);
    DOCTEST_CHECK(state.colonies[0].buildings[BUILDING_HYDROPONIC_FARM] == 0);
    DOCTEST_CHECK(crises::Empty());
    DOCTEST_REQUIRE(crises::Open(0, crises::CRISIS_ASTEROID, -1));
    DOCTEST_CHECK(crises::Resolve_With_Dice(0, TRIUMPH) == crises::RESULT_SUCCESS);
    DOCTEST_CHECK(state.colonies[0].n_pops == 6);
}

DOCTEST_TEST_CASE("a chain: failed revolutionaries rise again eight turns later") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(crises::Open(0, crises::CRISIS_REVOLUTIONARIES, 1));
    DOCTEST_CHECK(crises::Resolve_With_Dice(0, CATASTROPHE) == crises::RESULT_FAILURE);
    const crises::s_active& slot = crises::Slots()[0];
    DOCTEST_CHECK(slot.crisis == crises::CRISIS_REVOLUTION);
    DOCTEST_CHECK(slot.option == -1);
    DOCTEST_CHECK(slot.due == 35108);
    MOX::stardate = 35108;
    crises::Turn();  // the follow-up opens and its option is chosen
    DOCTEST_CHECK(slot.option >= 0);
    DOCTEST_CHECK(slot.due == 35108 + crises::CHECK_DELAY);
    DOCTEST_CHECK(crises::Resolve_With_Dice(0, TRIUMPH) == crises::RESULT_SUCCESS);
    DOCTEST_CHECK(crises::Empty());
}

DOCTEST_TEST_CASE("a cruel choice marks the reputation; humans get no crisis yet") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].personality = PLAYER_PERSONALITY_XENOPHOBIC;  // leans to espionage: a weapon
    DOCTEST_REQUIRE(crises::Open(0, crises::CRISIS_BRAIN_PARASITES, 1));
    DOCTEST_CHECK(crises::Slots()[0].option == 2);
    DOCTEST_CHECK(beliefs::Evidence(1, 0, beliefs::BELIEF_HOSTILE) == 1);
    crises::Clear();
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_CHECK_FALSE(crises::Open(1, crises::CRISIS_ASTEROID, -1));
}

DOCTEST_TEST_CASE("off: nothing opens") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    crises::enabled = false;
    DOCTEST_CHECK_FALSE(crises::Open(0, crises::CRISIS_ASTEROID, -1));
    crises::Turn();
    DOCTEST_CHECK(crises::Empty());
}

DOCTEST_TEST_CASE("a situation scale moves within 0..100 and reports the stage reached from below") {
    crises::s_scale scale;
    DOCTEST_CHECK(crises::Scale_Move(scale, 24) == 0);
    DOCTEST_CHECK(crises::Scale_Move(scale, 1) == 1);
    DOCTEST_CHECK(crises::Scale_Move(scale, 30) == 2);
    DOCTEST_CHECK(crises::Scale_Move(scale, -10) == 0);
    DOCTEST_CHECK(crises::Scale_Move(scale, 500) == 4);
    DOCTEST_CHECK(scale.value == 100);
    DOCTEST_CHECK(crises::Scale_Move(scale, -500) == 0);
    DOCTEST_CHECK(scale.value == 0);
}

DOCTEST_TEST_CASE("war weariness: grows at war, stages bite, approaches cost, peace ends it") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::stardate = 35020;  // before the academies' first turn: no dice here
    crises::situations = true;
    state.players[0].treaty[1] = 4;
    state.players[0].bc = 0;
    state.players[0].research_accumulated = 10;
    crises::Turn();
    const crises::s_scale* scale = crises::Find_Scale(crises::SCALE_WAR_WEARINESS, 0);
    DOCTEST_REQUIRE(scale != nullptr);
    DOCTEST_CHECK(scale->approach == 1);              // a pacifist without money: martial law
    DOCTEST_CHECK(scale->value == crises::WEARY_WAR - 2);
    DOCTEST_CHECK(state.players[0].research_accumulated == 7);
    DOCTEST_CHECK(crises::Find_Scale(crises::SCALE_WAR_WEARINESS, 1) == nullptr);  // player 1 is not at war by its own books
    crises::Extra()->scale[0].approach = 2;           // carry on: no price, no relief
    crises::Extra()->scale[0].value = 45;
    state.colonies[0].production_spent = 7;
    crises::Turn();
    DOCTEST_CHECK(state.colonies[0].production_spent == 0);  // stage 2: the strike
    crises::Extra()->scale[0].approach = 2;
    crises::Extra()->scale[0].value = 70;
    state.players[0].bc = 250;
    crises::Turn();
    DOCTEST_CHECK(state.players[0].bc == 150);        // stage 3: the fine
    crises::Extra()->scale[0].approach = 2;
    crises::Extra()->scale[0].value = 97;
    crises::Turn();
    DOCTEST_CHECK(state.colonies[0].buildings[BUILDING_HYDROPONIC_FARM] == 1);  // stage 4: no building is lost any more
    DOCTEST_CHECK(crises::On_Strike(0));                                          // a general strike instead
    DOCTEST_CHECK(crises::Extra()->scale[0].value == crises::WEARY_RESET);
    state.colonies[0].production_spent = 9;
    MOX::stardate += 1;
    crises::Turn();
    DOCTEST_CHECK(state.colonies[0].production_spent == 0);                       // the strike goes on: no output
    crises::Extra()->scale[0].approach = 2;
    MOX::stardate += crises::STRIKE_TURNS;
    state.colonies[0].production_spent = 9;
    crises::Turn();
    DOCTEST_CHECK_FALSE(crises::On_Strike(0));                                    // and ends
    DOCTEST_CHECK(state.colonies[0].production_spent == 9);
    // Propaganda costs 5 BC a turn and takes 3 off the growth.
    crises::Extra()->scale[0].approach = 0;
    crises::Extra()->scale[0].value = 10;
    state.players[0].bc = 150;
    crises::Turn();
    DOCTEST_CHECK(state.players[0].bc == 145);
    DOCTEST_CHECK(crises::Extra()->scale[0].value == 10 + crises::WEARY_WAR - 3);
    state.players[0].treaty[1] = 0;                   // peace: it falls and ends
    for (int i = 0; i < 5; ++i) crises::Turn();
    DOCTEST_CHECK(crises::Find_Scale(crises::SCALE_WAR_WEARINESS, 0) == nullptr);
    DOCTEST_CHECK(crises::Empty());
}

DOCTEST_TEST_CASE("computers pick an approach by character; a human takes propaganda") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].personality = PLAYER_PERSONALITY_AGGRESSIVE;
    state.players[0].bc = 500;
    DOCTEST_CHECK(crises::Choose_Approach(0) == 1);
    state.players[0].personality = PLAYER_PERSONALITY_PACIFIST;
    DOCTEST_CHECK(crises::Choose_Approach(0) == 0);
    MOX::stardate = 35020;
    crises::situations = true;
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.players[1].treaty[0] = 4;
    crises::Turn();
    const crises::s_scale* scale = crises::Find_Scale(crises::SCALE_WAR_WEARINESS, 1);
    DOCTEST_REQUIRE(scale != nullptr);
    DOCTEST_CHECK(scale->approach == 0);
}

// Sets the per-turn outputs the academy contest measures: science, industry, BC of each empire.
void Outputs_(test::s_game_state* state, int p, int science, int industry, int bc) {
    state->players[p].research_produced = static_cast<int16_t>(science);
    state->players[p].industry_produced = static_cast<int16_t>(industry);
    state->players[p].bc_produced = static_cast<int16_t>(bc);
}

DOCTEST_TEST_CASE("the academy: all empires compete in three stages and the winner of each stage gets a prize") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    crises::situations = true;
    state.players[0].bc = 200;
    state.players[1].bc = 200;
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;  // a human takes part too
    Outputs_(&state, 0, 10, 10, 10);
    Outputs_(&state, 1, 10, 10, 10);
    DOCTEST_REQUIRE(crises::Start_Academy());
    DOCTEST_CHECK_FALSE(crises::Start_Academy());  // one at a time
    DOCTEST_CHECK(crises::Extra()->contest_stage == 1);
    DOCTEST_CHECK(state.players[0].bc == 200);     // no fee

    // Stage 1, science: the human empire grows more.
    Outputs_(&state, 0, 14, 10, 10);
    Outputs_(&state, 1, 20, 10, 10);
    MOX::stardate += crises::ACADEMY_STAGE_TURNS - 1;
    crises::Turn();
    DOCTEST_CHECK(crises::Extra()->contest_stage == 1);
    MOX::stardate += 1;
    state.players[1].research_accumulated = 0;
    crises::Turn();
    DOCTEST_CHECK(state.players[1].research_accumulated == crises::ACADEMY_PRIZE);
    DOCTEST_CHECK(state.players[0].research_accumulated == 0);
    DOCTEST_CHECK(crises::Extra()->contest_stage == 2);

    // Stage 2, industry: both grow alike - no winner. The baseline is the output at the start of the stage.
    Outputs_(&state, 0, 14, 15, 10);
    Outputs_(&state, 1, 20, 15, 10);
    state.colonies[0].production_spent = 0;
    MOX::stardate += crises::ACADEMY_STAGE_TURNS;
    crises::Turn();
    DOCTEST_CHECK(state.colonies[0].production_spent == 0);
    DOCTEST_CHECK(crises::Extra()->contest_stage == 3);

    // Stage 3, BC: the computer wins and the contest is over.
    Outputs_(&state, 0, 14, 15, 30);
    MOX::stardate += crises::ACADEMY_STAGE_TURNS;
    crises::Turn();
    DOCTEST_CHECK(state.players[0].bc == 200 + crises::ACADEMY_PRIZE);
    DOCTEST_CHECK(crises::Extra()->contest_stage == 0);
    DOCTEST_CHECK(crises::Extra_Empty());
}

DOCTEST_TEST_CASE("the academy: the industry prize goes to the capital's production; it needs two empires") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    crises::situations = true;
    Outputs_(&state, 0, 0, 5, 0);
    Outputs_(&state, 1, 0, 5, 0);
    DOCTEST_REQUIRE(crises::Start_Academy());
    crises::Extra()->contest_stage = 2;
    crises::Extra()->contest_ends = MOX::stardate;
    Outputs_(&state, 0, 0, 9, 0);
    crises::Turn();
    DOCTEST_CHECK(state.colonies[0].production_spent == crises::ACADEMY_PRIZE);
    crises::Clear();
    state.players[1].eliminated = 1;
    DOCTEST_CHECK_FALSE(crises::Start_Academy());
}

DOCTEST_TEST_CASE("the academy starts by chance from the minimum turn only, once the dice allow it") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    crises::situations = true;
    MOX::stardate = 35000 + crises::MIN_TURN - 1;
    const uint32_t seed = game_random::Get_Random_Seed();
    crises::Turn();
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);  // too early: no draw
    DOCTEST_CHECK(crises::Extra()->contest_stage == 0);
    MOX::stardate = 35000 + crises::MIN_TURN;
    for (int i = 0; i < 4000 && crises::Extra()->contest_stage == 0; ++i) crises::Turn();
    DOCTEST_CHECK(crises::Extra()->contest_stage == 1);
}

DOCTEST_TEST_CASE("become a crisis: atrocities build a threat, the galaxy declares war, the threat decays") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    crises::Add_Threat(0);  // flag off
    DOCTEST_CHECK(crises::Threat(0) == 0);
    crises::crisis_front = true;
    crises::Add_Threat(0);
    DOCTEST_CHECK(crises::Threat(0) == crises::THREAT_ATROCITY);
    state.players[1].treaty[0] = 0;
    state.players[0].treaty[1] = 0;
    crises::Turn();  // below the threshold: no war
    DOCTEST_CHECK(state.players[1].treaty[0] < 4);
    DOCTEST_CHECK(crises::Threat(0) == crises::THREAT_ATROCITY - crises::THREAT_DECAY);
    crises::Add_Threat(0);
    crises::Add_Threat(0);
    DOCTEST_CHECK(crises::Threat(0) >= crises::THREAT_WAR);
    crises::Turn();  // the threshold: a computer that knows the culprit declares war
    DOCTEST_CHECK(state.players[1].treaty[0] >= 4);
    DOCTEST_CHECK(state.players[0].treaty[1] >= 4);
    DOCTEST_CHECK((crises::Extra()->war_mask[0] & 2) != 0);
    // A human culprit works the same; the threat falls away and the mask with it.
    for (int i = 0; i < 4 * crises::THREAT_MAX; ++i) crises::Turn();
    DOCTEST_CHECK(crises::Threat(0) == 0);
    DOCTEST_CHECK(crises::Extra()->war_mask[0] == 0);
}

DOCTEST_TEST_CASE("become a crisis: a human culprit is declared war on, a human never declares; no contact, no war") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    crises::crisis_front = true;
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.players[1].contact[0] = 0;
    state.players[0].contact[1] = 0;
    crises::Add_Threat(0);
    crises::Add_Threat(0);
    crises::Add_Threat(0);
    crises::Turn();
    DOCTEST_CHECK(state.players[1].treaty[0] < 4);  // it does not know the culprit
    state.players[1].contact[0] = 1;
    crises::Turn();
    DOCTEST_CHECK(state.players[1].treaty[0] >= 4);
    state.players[1].treaty[0] = 0;
    state.players[0].treaty[1] = 0;
    crises::Turn();  // declared once: not again while the threat stays up
    DOCTEST_CHECK(state.players[1].treaty[0] < 4);
}

DOCTEST_TEST_CASE("the Antaran front starts at its strength, advances by itself and feeds the Antarans") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    crises::crisis_front = true;
    MOX::settings.xenons_exist = 1;
    MOX::antarans.field6_0x3c = 0;
    MOX::antarans.resource_offensive = 0;
    crises::Turn();  // strength 0: no front
    DOCTEST_CHECK(crises::Find_Scale(crises::SCALE_ANTARAN_FRONT, -1) == nullptr);
    crises::front_strength = 30;
    crises::Turn();
    const crises::s_scale* front = crises::Find_Scale(crises::SCALE_ANTARAN_FRONT, -1);
    DOCTEST_REQUIRE(front != nullptr);
    DOCTEST_CHECK(front->value == 30);
    DOCTEST_CHECK(MOX::antarans.resource_offensive == 30 * crises::FRONT_START_RESOURCE);
    MOX::stardate = 35105;
    crises::Turn();
    DOCTEST_CHECK(front->value == 31);
    crises::Extra()->scale[0].value = 49;
    MOX::stardate = 35110;
    crises::Turn();
    DOCTEST_CHECK(front->value == 50);
    DOCTEST_CHECK(MOX::antarans.resource_offensive == 30 * crises::FRONT_START_RESOURCE + 2 * crises::FRONT_STAGE_RESOURCE);
    MOX::settings.xenons_exist = 0;  // no Antarans in the game: the front stands still
    MOX::stardate = 35115;
    crises::Turn();
    DOCTEST_CHECK(front->value == 50);
}

DOCTEST_TEST_CASE("the council's project: contributors by character, a push back, bonuses") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    crises::crisis_front = true;
    crises::front_strength = 30;
    MOX::settings.xenons_exist = 1;
    MOX::antarans.field6_0x3c = 0;
    MOX::antarans.resource_offensive = 0;
    crises::Turn();  // the front opens
    MOX::antarans.resource_offensive = 1000;
    state.players[0].bc = 300;                      // a pacifist gives COUNCIL_GENEROUS
    state.players[1].bc = 300;
    state.players[1].personality = PLAYER_PERSONALITY_AGGRESSIVE;  // keeps its trophies
    state.players[0].research_accumulated = 0;
    MOX::stardate = 35101;
    crises::Turn();
    DOCTEST_CHECK(state.players[0].bc == 300 - crises::COUNCIL_GENEROUS);
    DOCTEST_CHECK(state.players[1].bc == 300);
    DOCTEST_CHECK(crises::Extra()->council_progress == crises::COUNCIL_GENEROUS);
    crises::Extra()->council_progress = crises::COUNCIL_GOAL - 10;
    MOX::stardate = 35106;
    crises::Turn();
    DOCTEST_CHECK(crises::Find_Scale(crises::SCALE_ANTARAN_FRONT, -1)->value == 30 - crises::COUNCIL_PUSHBACK);
    DOCTEST_CHECK(MOX::antarans.resource_offensive == 500);
    DOCTEST_CHECK(state.players[0].research_accumulated == 2 * 2 * crises::COUNCIL_GENEROUS);
    DOCTEST_CHECK(crises::Extra()->council_progress == 0);
    // A front pushed to nothing is beaten for good.
    crises::Extra()->scale[0].value = 20;
    crises::Extra()->council_progress = crises::COUNCIL_GOAL;
    MOX::stardate = 35111;
    crises::Turn();
    DOCTEST_CHECK(crises::Find_Scale(crises::SCALE_ANTARAN_FRONT, -1) == nullptr);
    DOCTEST_CHECK(crises::Extra()->front_broken == 1);
    MOX::stardate = 35120;
    crises::Turn();
    DOCTEST_CHECK(crises::Find_Scale(crises::SCALE_ANTARAN_FRONT, -1) == nullptr);
}

DOCTEST_TEST_CASE("the new flags off: nothing starts, nothing is kept") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::stardate = 35020;
    state.players[0].treaty[1] = 4;
    state.players[0].bc = 500;
    MOX::settings.xenons_exist = 1;
    crises::front_strength = 50;
    crises::Turn();
    DOCTEST_CHECK_FALSE(crises::Start_Academy());
    crises::Add_Threat(0);
    DOCTEST_CHECK(crises::Threat(0) == 0);
    DOCTEST_CHECK(crises::Extra_Empty());
    DOCTEST_CHECK(crises::Empty());
    DOCTEST_CHECK(crises::Save_Size() == 2 + crises::MAX_ACTIVE * 11);  // the old, version 1 layout
}

DOCTEST_TEST_CASE("the extended save payload round-trips, version 1 still reads, bad values are refused") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    crises::situations = true;
    crises::crisis_front = true;
    state.players[0].bc = 500;
    DOCTEST_REQUIRE(crises::Start_Academy());
    crises::s_extra* extra = crises::Extra();
    extra->scale[3].kind = crises::SCALE_WAR_WEARINESS;
    extra->scale[3].player = 1;
    extra->scale[3].value = 42;
    extra->scale[3].started = 35090;
    extra->scale[3].approach = 1;
    extra->threat[1] = 75;
    extra->war_mask[1] = 5;
    extra->strike_until[0] = 35105;
    extra->baseline[1] = -7;
    extra->council_progress = 123;
    extra->given[0] = 77;
    extra->front_broken = 1;
    const uint32_t size = crises::Save_Size();
    DOCTEST_CHECK(size > 2 + crises::MAX_ACTIVE * 11);
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    crises::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == size);
    DOCTEST_CHECK(buffer[0] == 3);
    crises::Clear();
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(crises::Parse_Save(&reader, &error));
    DOCTEST_CHECK(crises::Extra_Empty());
    crises::Commit_Save();
    extra = crises::Extra();
    DOCTEST_CHECK(extra->contest_stage == 1);
    DOCTEST_CHECK(extra->contest_ends == 35100 + crises::ACADEMY_STAGE_TURNS);
    DOCTEST_CHECK(extra->threat[1] == 75);
    DOCTEST_CHECK(extra->war_mask[1] == 5);
    DOCTEST_CHECK(extra->strike_until[0] == 35105);
    DOCTEST_CHECK(extra->baseline[1] == -7);
    DOCTEST_CHECK(extra->scale[3].value == 42);
    DOCTEST_CHECK(extra->scale[3].approach == 1);
    DOCTEST_CHECK(extra->council_progress == 123);
    DOCTEST_CHECK(extra->given[0] == 77);
    DOCTEST_CHECK(extra->front_broken == 1);
    buffer[2 + crises::MAX_ACTIVE * 11] = 99;  // an unknown scale kind
    SERIAL::Reader_ bad(buffer.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(crises::Parse_Save(&bad, &error));
    buffer[2 + crises::MAX_ACTIVE * 11] = crises::SCALE_WAR_WEARINESS;
    buffer[2 + crises::MAX_ACTIVE * 11 + 2] = 200;  // a value over 100
    SERIAL::Reader_ bad_value(buffer.get(), size, "test underflow", true);
    DOCTEST_CHECK_FALSE(crises::Parse_Save(&bad_value, &error));
    // A version 1 payload (the slots only) still reads and leaves the extras empty.
    crises::Clear();
    const uint32_t old_size = 2 + crises::MAX_ACTIVE * 11;
    auto old_buffer = std::make_unique<uint8_t[]>(old_size);
    SERIAL::Writer_ old_writer(old_buffer.get(), old_size, "test overflow");
    crises::Write_Save(&old_writer);
    DOCTEST_CHECK(old_buffer[0] == 1);
    crises::Extra()->threat[0] = 5;
    SERIAL::Reader_ old_reader(old_buffer.get(), old_size, "test underflow", true);
    DOCTEST_REQUIRE(crises::Parse_Save(&old_reader, &error));
    crises::Commit_Save();
    DOCTEST_CHECK(crises::Extra_Empty());
}

DOCTEST_TEST_CASE("a version 2 payload of wave 4 still reads: its academies and fallen are dropped, the rest stays") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    const uint32_t size = 2 + crises::MAX_ACTIVE * 11 + crises::MAX_SCALES * 9 + MAX_PLAYERS * 1 + MAX_PLAYERS * 4 + 1 + 4 + MAX_PLAYERS * 2;
    auto buffer = std::make_unique<uint8_t[]>(size);
    SERIAL::Writer_ writer(buffer.get(), size, "test overflow");
    writer.Write_Uint16(2);
    for (int i = 0; i < crises::MAX_ACTIVE; ++i) {
        writer.Write_Uint8(0); writer.Write_Int8(-1); writer.Write_Int16(-1); writer.Write_Int8(-1);
        writer.Write_Int32(0); writer.Write_Uint8(0); writer.Write_Int8(0);
    }
    for (int i = 0; i < crises::MAX_SCALES; ++i) {
        const bool first = i == 0;
        writer.Write_Uint8(first ? crises::SCALE_WAR_WEARINESS : 0); writer.Write_Int8(first ? 1 : -1);
        writer.Write_Int16(first ? 40 : 0); writer.Write_Int32(first ? 35050 : 0); writer.Write_Int8(first ? 2 : -1);
    }
    for (int p = 0; p < MAX_PLAYERS; ++p) writer.Write_Uint8(p == 0 ? 5 : 0);        // an academy: dropped
    for (int p = 0; p < MAX_PLAYERS; ++p) writer.Write_Int32(p == 1 ? 35140 : 0);    // a fallen empire: dropped
    writer.Write_Uint8(0);
    writer.Write_Int32(55);
    for (int p = 0; p < MAX_PLAYERS; ++p) writer.Write_Uint16(p == 0 ? 9 : 0);
    DOCTEST_REQUIRE(writer.Length() == size);
    SERIAL::Reader_ reader(buffer.get(), size, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(crises::Parse_Save(&reader, &error));
    crises::Commit_Save();
    DOCTEST_CHECK(crises::Find_Scale(crises::SCALE_WAR_WEARINESS, 1) != nullptr);
    DOCTEST_CHECK(crises::Extra()->council_progress == 55);
    DOCTEST_CHECK(crises::Extra()->given[0] == 9);
    DOCTEST_CHECK(crises::Extra()->contest_stage == 0);
    DOCTEST_CHECK(crises::Extra()->threat[0] == 0);
    // A bad value in the new section is refused (a threat over the maximum).
    crises::Extra()->threat[0] = 5;
    const uint32_t size3 = crises::Save_Size();
    auto buffer3 = std::make_unique<uint8_t[]>(size3);
    SERIAL::Writer_ writer3(buffer3.get(), size3, "test overflow");
    crises::Write_Save(&writer3);
    DOCTEST_CHECK(buffer3[0] == 3);
    const uint32_t threat_offset = 2 + crises::MAX_ACTIVE * 11 + crises::MAX_SCALES * 9 + 1 + 4 + MAX_PLAYERS * 2;
    buffer3[threat_offset + 1] = 0x7f;  // the high byte of threat 0
    SERIAL::Reader_ bad(buffer3.get(), size3, "test underflow", true);
    DOCTEST_CHECK_FALSE(crises::Parse_Save(&bad, &error));
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_crises_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(crises::Open(0, crises::CRISIS_REVOLUTIONARIES, 1, 5));  // a follow-up-style delayed opening
    auto buffer = std::make_unique<uint8_t[]>(crises::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), crises::Save_Size(), "test overflow");
    crises::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == crises::Save_Size());
    crises::Clear();
    SERIAL::Reader_ reader(buffer.get(), crises::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(crises::Parse_Save(&reader, &error));
    DOCTEST_CHECK(crises::Empty());
    crises::Commit_Save();
    DOCTEST_CHECK(crises::Slots()[0].crisis == crises::CRISIS_REVOLUTIONARIES);
    DOCTEST_CHECK(crises::Slots()[0].due == 35105);
    buffer[2] = 99;  // an unknown crisis
    SERIAL::Reader_ bad(buffer.get(), crises::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(crises::Parse_Save(&bad, &error));
    DOCTEST_CHECK(crises::Slots()[0].crisis == crises::CRISIS_REVOLUTIONARIES);
}

}
