#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/ambition.h"
#include "game/covenants.h"
#include "game/dice.h"
#include "game/espionage.h"
#include "game/intrigue.h"
#include "game/opinion.h"
#include "game/projects.h"
#include "game/random.h"
#include "game/secrets.h"
#include "game/serialization.h"
#include "game/spy_ops.h"

#include <memory>
#include <string>
#include <vector>

namespace {
    struct s_secrets_scope {
        test::s_saved_value<bool> enabled{secrets::enabled};
        test::s_saved_value<bool> smear{secrets::smear};
        test::s_saved_value<int32_t> cost{secrets::smear_cost};
        test::s_saved_value<bool> network{espionage::network};
        test::s_saved_value<bool> ci{espionage::counterintelligence};
        test::s_saved_value<int32_t> operations{spy_ops::operations};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<uint8_t> roll_kind{dice::roll_kind};
        test::s_saved_value<bool> council{ambition::council_bribery};
        test::s_saved_value<bool> favours{intrigue::favours};

        s_secrets_scope() {
            covenants::Clear();
            secrets::Clear();
            espionage::Clear();
            spy_ops::Clear();
            opinion::Clear();
            secrets::enabled = true;
            secrets::smear = true;
            secrets::smear_cost = 20;
            espionage::network = true;
            espionage::counterintelligence = false;
            spy_ops::operations = 63;
            opinion::enabled = true;
            dice::roll_kind = dice::ROLL_TWO_D10;
            ambition::council_bribery = true;
            intrigue::favours = true;
        }
        ~s_secrets_scope() {
            covenants::Clear();
            secrets::Clear();
            espionage::Clear();
            spy_ops::Clear();
            opinion::Clear();
        }
    };

    const int32_t TRIUMPH[2] = {10, 10};
    const int32_t CATASTROPHE[2] = {1, 1};

    // Four computer empires that know each other. Player 0 plans a sneak attack on player 2.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 4;
        MOX::stardate = 35101;
        for (int i = 0; i < 4; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[i].personality = PLAYER_PERSONALITY_ERRATIC;
            state->players[i].sneak_attack_planet = -1;
            state->players[i].sneak_attack_player = -1;
            for (int j = 0; j < 4; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
        state->players[0].sneak_attack_planet = 5;
        state->players[0].sneak_attack_player = 2;
    }
} // namespace

DOCTEST_TEST_SUITE("secrets and smear") {

DOCTEST_TEST_CASE("selling a secret: the victim pays and learns the plan; the surprise is lost") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_CHECK_FALSE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));  // known already
    DOCTEST_CHECK(secrets::Still_True(secrets::Slots()[0]));
    DOCTEST_CHECK_FALSE(secrets::Sell(0));  // the victim cannot pay
    state.players[2].bc = 80;
    DOCTEST_REQUIRE(secrets::Sell(0));
    DOCTEST_CHECK(state.players[2].bc == 80 - secrets::SALE_PRICE);
    DOCTEST_CHECK(state.players[1].bc == secrets::SALE_PRICE);
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == -1);
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_PLOTTED_AGAINST_US) < 0);
    DOCTEST_CHECK(secrets::Empty());
}

DOCTEST_TEST_CASE("blackmail: the owner pays for silence, or refuses and the secret is made public") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].bc = 1000;
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_REQUIRE(secrets::Blackmail(0));
    DOCTEST_CHECK(state.players[0].bc == 1000 - 1000 * secrets::BLACKMAIL_PERCENT / 100);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_BLACKMAILED_US) == secrets::BLACKMAIL_OPINION);
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == 5);  // silence kept
    state.players[0].bc = 50;  // too poor: refuses
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_CHECK_FALSE(secrets::Blackmail(0));
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == -1);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_EXPOSED_US) == secrets::EXPOSED_OPINION);
    DOCTEST_CHECK(opinion::Modifier(3, 0, opinion::REASON_BAD_RUMOURS) == secrets::RUMOUR_OPINION);
}

DOCTEST_TEST_CASE("a bribed governor is a secret too; it goes stale when the bribe ends") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.leaders[4].player_index = 2;
    *spy_ops::Bribed_Slot(4) = 0;
    DOCTEST_REQUIRE(secrets::Learn(3, secrets::KIND_BRIBE, 0, 2, 4));
    DOCTEST_CHECK(secrets::Still_True(secrets::Slots()[0]));
    secrets::Make_Public(0);
    DOCTEST_CHECK(spy_ops::Bribed_By(4) == -1);
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_BRIBED_OUR_LEADER) < 0);
    DOCTEST_REQUIRE(secrets::Learn(3, secrets::KIND_PLAN, 0, 2, -1));
    MOX::stardate = 35101 + secrets::SECRET_TURNS;
    DOCTEST_CHECK_FALSE(secrets::Still_True(secrets::Slots()[0]));
}

DOCTEST_TEST_CASE("smear: believed, the others think worse of the target; exposed, the target remembers") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].spies[2] = 4;
    espionage::State()->points[1][2] = 50;
    DOCTEST_REQUIRE(secrets::Smear_With_Dice(1, 2, TRIUMPH));
    DOCTEST_CHECK(espionage::Points(1, 2) == 30);
    DOCTEST_CHECK(opinion::Modifier(0, 2, opinion::REASON_BAD_RUMOURS) == secrets::RUMOUR_OPINION);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_BAD_RUMOURS) == 0);
    DOCTEST_CHECK_FALSE(secrets::Smear_With_Dice(1, 2, CATASTROPHE));
    DOCTEST_CHECK(state.players[1].spies[2] == 3);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_SMEARED_US) == secrets::SMEARED_OPINION);
    DOCTEST_CHECK_FALSE(secrets::Smear_With_Dice(1, 2, TRIUMPH));  // 10 points left
}

// 3.5.2 "Тайны как валюта": "тайный договор" - the secret clause of covenants.h as a secret.
DOCTEST_TEST_CASE("a secret treaty is a secret: made public it reveals the clause; it goes stale when revealed otherwise") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    // Player 0 guarantees player 1 against player 2 (and the twin).
    covenants::State()->count = 2;
    covenants::State()->clauses[0] = {0, 1, 2, 0, 35100};
    covenants::State()->clauses[1] = {1, 0, 2, 0, 35100};
    DOCTEST_REQUIRE(secrets::Learn(3, secrets::KIND_TREATY, 0, 2, 1));
    DOCTEST_CHECK(secrets::Still_True(secrets::Slots()[0]));
    secrets::Make_Public(0);
    DOCTEST_CHECK(covenants::State()->clauses[0].revealed == 1);
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_PLOTTED_AGAINST_US) == covenants::REVEALED_OPINION);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_BAD_RUMOURS) == secrets::RUMOUR_OPINION);

    covenants::State()->clauses[0].revealed = covenants::State()->clauses[1].revealed = 0;
    DOCTEST_REQUIRE(secrets::Learn(3, secrets::KIND_TREATY, 0, 2, 1));
    covenants::Reveal(0, "test");  // revealed by other means: the secret is stale
    DOCTEST_CHECK_FALSE(secrets::Still_True(secrets::Slots()[0]));
    covenants::End_Clause(0, "test");
    DOCTEST_CHECK_FALSE(secrets::Still_True(secrets::Slots()[0]));
}

// 3.5.2: "применение биологического оружия" as a secret; the victim learns the culprit, made public
// it is an atrocity in everyone's eyes.
DOCTEST_TEST_CASE("the use of biological weapons is noted and is a secret") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::NUM_COLONIES = 1;
    state.colonies[0].owner = 2;
    state.colonies[0].planet = 0;
    state.planets[0].star_index = 7;
    secrets::On_Bio_Weapon(0, 8);  // no colony of another empire there
    DOCTEST_CHECK(secrets::Empty());
    secrets::On_Bio_Weapon(0, 7);
    DOCTEST_CHECK(secrets::Bio()[0].turn == MOX::stardate);
    DOCTEST_CHECK(secrets::Bio()[0].victim == 2);
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_BIOWEAPON, 0, 2, -1));
    DOCTEST_CHECK(secrets::Still_True(secrets::Slots()[0]));
    state.players[2].bc = 100;
    DOCTEST_REQUIRE(secrets::Sell(0));  // the victim buys the proof
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_ATROCITY) == -40);
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_BIOWEAPON, 0, 2, -1));
    secrets::Make_Public(0);
    DOCTEST_CHECK(opinion::Modifier(3, 0, opinion::REASON_ATROCITY) == -40);
    DOCTEST_CHECK(opinion::Modifier(3, 0, opinion::REASON_BAD_RUMOURS) == 0);
    secrets::enabled = false;
    secrets::Bio()[0] = secrets::s_bio{};
    secrets::On_Bio_Weapon(0, 7);  // off: nothing noted
    DOCTEST_CHECK(secrets::Bio()[0].turn == 0);
}

// 3.5.2: "Шантажировать владельца - дань, технология или голос в совете за молчание".
DOCTEST_TEST_CASE("blackmail for a technology: the owner gives the cheapest one it has and the holder lacks") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    const int16_t app = projects::Cheapest_Unknown_App(1);
    DOCTEST_REQUIRE(app >= 0);
    state.players[0].bc = 1000;
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    // The owner has nothing to give: tribute instead.
    DOCTEST_REQUIRE(secrets::Blackmail_For(0, secrets::DEMAND_TECH));
    DOCTEST_CHECK(state.players[0].bc == 1000 - 1000 * secrets::BLACKMAIL_PERCENT / 100);
    state.players[0].bc = 1000;
    state.players[0].tech_applications[app] = TECH_RESEARCH_STATUS_RESEARCHED;
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;  // a granted technology then only refreshes the designs (no AI design code in the test)
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_REQUIRE(secrets::Blackmail_For(0, secrets::DEMAND_TECH));
    DOCTEST_CHECK(state.players[1].tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED);
    DOCTEST_CHECK(state.players[0].bc == 1000);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_BLACKMAILED_US) <= 2 * secrets::BLACKMAIL_OPINION);  // tribute, then the technology
    DOCTEST_CHECK(secrets::Empty());
}

DOCTEST_TEST_CASE("blackmail for a council vote: the secret is kept and ambition spends it as a vote") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].bc = 1000;
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_REQUIRE(secrets::Blackmail_For(0, secrets::DEMAND_VOTE));
    DOCTEST_CHECK(secrets::Slots()[0].kind == secrets::KIND_PLAN);  // waits for the vote
    DOCTEST_CHECK(secrets::Slots()[0].demanded == 1);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_BLACKMAILED_US) == secrets::BLACKMAIL_OPINION);
    state.players[0].bc = 1000;
    state.players[1].bc = 0;
    ambition::Before_Council_Vote(1, 3);  // the holder stands; the owner votes
    DOCTEST_CHECK(ambition::Council_Vote_Bias(0, 1) == ambition::BLACKMAIL_BIAS);
    DOCTEST_CHECK(secrets::Empty());
    DOCTEST_CHECK(state.players[0].bc == 1000);  // no money changed hands
    ambition::After_Council_Vote();

    // Without council bribery the demand falls back to tribute.
    ambition::council_bribery = false;
    state.players[0].bc = 1000;
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_REQUIRE(secrets::Blackmail_For(0, secrets::DEMAND_VOTE));
    DOCTEST_CHECK(state.players[0].bc == 1000 - 1000 * secrets::BLACKMAIL_PERCENT / 100);
    DOCTEST_CHECK(secrets::Empty());
}

// 3.5.2: "Продать третьей стороне - BC или услуга".
DOCTEST_TEST_CASE("sale to a third party: BC, or a favour when the buyer is poor; the buyer now holds the secret") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    MOX::stardate += 5;
    DOCTEST_CHECK_FALSE(secrets::Sell_To_Third(0));  // nobody thinks ill of the owner
    opinion::Add(3, 0, opinion::REASON_PLOTTED_AGAINST_US, -40);
    state.players[3].bc = 100;
    int32_t copy = -1;
    DOCTEST_REQUIRE(secrets::Sell_To_Third(0, &copy));
    DOCTEST_CHECK(state.players[3].bc == 100 - secrets::SALE_PRICE);
    DOCTEST_CHECK(state.players[1].bc == secrets::SALE_PRICE);
    DOCTEST_REQUIRE(copy >= 0);
    DOCTEST_CHECK(secrets::Slots()[copy].holder == 3);
    DOCTEST_CHECK(secrets::Slots()[copy].learned == 35101);  // as old as the original
    DOCTEST_CHECK(secrets::Slots()[0].holder != 1);          // the seller's copy is gone (the slot may be reused)
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == 5);  // the victim was not told

    // A poor buyer pays with a favour.
    secrets::Clear();
    state.players[3].bc = 0;
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_REQUIRE(secrets::Sell_To_Third(0));
    DOCTEST_CHECK(intrigue::Favours(3, 1) == 1);
    // ... and cannot with favours off.
    secrets::Clear();
    intrigue::favours = false;
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_CHECK_FALSE(secrets::Sell_To_Third(0));
}

DOCTEST_TEST_CASE("iteration 1: one spy learns a secret on a roll of 30 per mille (needed 20 before)") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::stardate = 35102;  // neither the use phase nor the smear phase
    state.players[1].spies[0] = 1;
    uint32_t seed = 1;
    for (; seed < 100000; ++seed) {
        game_random::Set_Random_Seed(seed);
        if (game_random::Random(1000) == 30) break;
    }
    DOCTEST_REQUIRE(seed < 100000);
    game_random::Set_Random_Seed(seed);
    secrets::Turn();
    DOCTEST_CHECK_FALSE(secrets::Empty());
    DOCTEST_CHECK(secrets::Slots()[0].holder == 1);
    DOCTEST_CHECK(secrets::Slots()[0].owner == 0);
}

DOCTEST_TEST_CASE("off: nothing is learned, nothing smeared") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    secrets::enabled = false;
    secrets::smear = false;
    state.players[1].spies[0] = 5;
    DOCTEST_CHECK_FALSE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_CHECK_FALSE(secrets::Blackmail_For(0, secrets::DEMAND_TECH));
    DOCTEST_CHECK_FALSE(secrets::Sell_To_Third(0));
    state.players[1].spies[2] = 4;
    espionage::State()->points[1][2] = 50;
    DOCTEST_CHECK_FALSE(secrets::Smear_With_Dice(1, 2, TRIUMPH));
    secrets::Turn();
    DOCTEST_CHECK(secrets::Empty());
}

DOCTEST_TEST_CASE("the version 1 payload is still read; version 2 keeps the demand and the deeds") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    constexpr uint32_t V1_SIZE = 2 + secrets::MAX_SECRETS * 10;
    std::vector<uint8_t> old_buffer(V1_SIZE);
    {
        SERIAL::Writer_ writer(old_buffer.data(), V1_SIZE, "test overflow");
        writer.Write_Uint16(1);
        for (int i = 0; i < secrets::MAX_SECRETS; ++i) {
            writer.Write_Uint8(i == 0 ? secrets::KIND_PLAN : secrets::KIND_NONE);
            writer.Write_Int8(0);
            writer.Write_Int8(2);
            writer.Write_Int8(1);
            writer.Write_Int16(-1);
            writer.Write_Int32(35100);
        }
    }
    SERIAL::Reader_ old_reader(old_buffer.data(), V1_SIZE, "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(secrets::Parse_Save(&old_reader, &error));
    secrets::Commit_Save();
    DOCTEST_CHECK(secrets::Slots()[0].kind == secrets::KIND_PLAN);
    DOCTEST_CHECK(secrets::Slots()[0].demanded == 0);
    DOCTEST_CHECK(secrets::Bio()[0].turn == 0);

    secrets::Clear();
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_TREATY, 0, 2, 1));
    secrets::Slots()[0].demanded = 1;
    secrets::Bio()[3].turn = 35090;
    secrets::Bio()[3].victim = 1;
    auto buffer = std::make_unique<uint8_t[]>(secrets::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), secrets::Save_Size(), "test overflow");
    secrets::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == secrets::Save_Size());
    secrets::Clear();
    SERIAL::Reader_ reader(buffer.get(), secrets::Save_Size(), "test underflow", true);
    DOCTEST_REQUIRE(secrets::Parse_Save(&reader, &error));
    secrets::Commit_Save();
    DOCTEST_CHECK(secrets::Slots()[0].kind == secrets::KIND_TREATY);
    DOCTEST_CHECK(secrets::Slots()[0].value == 1);
    DOCTEST_CHECK(secrets::Slots()[0].demanded == 1);
    DOCTEST_CHECK(secrets::Bio()[3].turn == 35090);
    DOCTEST_CHECK(secrets::Bio()[3].victim == 1);
    buffer[2 + 10] = 7;  // the first entry's demand: neither 0 nor 1
    SERIAL::Reader_ bad(buffer.get(), secrets::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(secrets::Parse_Save(&bad, &error));
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_secrets_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    auto buffer = std::make_unique<uint8_t[]>(secrets::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), secrets::Save_Size(), "test overflow");
    secrets::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == secrets::Save_Size());
    secrets::Clear();
    SERIAL::Reader_ reader(buffer.get(), secrets::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(secrets::Parse_Save(&reader, &error));
    DOCTEST_CHECK(secrets::Empty());
    secrets::Commit_Save();
    DOCTEST_CHECK(secrets::Slots()[0].kind == secrets::KIND_PLAN);
    buffer[2] = 9;  // an unknown kind
    SERIAL::Reader_ bad(buffer.get(), secrets::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(secrets::Parse_Save(&bad, &error));
    DOCTEST_CHECK(secrets::Slots()[0].kind == secrets::KIND_PLAN);
}

}
