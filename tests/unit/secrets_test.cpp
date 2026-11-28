#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "../fixtures/texts_fixture.h"
#include "game/ambition.h"
#include "game/choices.h"
#include "game/council.h"
#include "game/covenants.h"
#include "game/diplodef.h"
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
        test::s_saved_value<bool> kompromat{secrets::council_kompromat};
        test::s_saved_value<bool> scandal{secrets::kompromat_scandal};
        test::s_saved_value<bool> pacts{secrets::scandal_breaks_pacts};
        test::s_saved_value<int16_t> council_flag{diplodef::COUNCIL_FLAG};
        test::s_saved_value<int16_t> next_session{diplodef::next_council_meeting_turn};

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
            intrigue::Clear();
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

// R2, piece A: a human owner answers a blackmailer in the choice window.
DOCTEST_TEST_CASE("a human owner chooses: pay for silence or refuse; off: the secret goes public as before") {
    struct s_language_scope {
        test::s_saved_value<uint8_t> language{text_language::language};
    } language_scope;
    s_secrets_scope scope;
    test::s_saved_value<bool> choices_on{choices::enabled};
    choices::Clear();
    test::s_game_state state;
    Setup_(&state);
    test::Load_Base_Texts();
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.players[0].bc = 1000;
    // Off: the human refuses at once, as before.
    choices::enabled = false;
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_CHECK_FALSE(secrets::Blackmail(0));
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == -1);
    // On: a pending choice, the secret kept, nothing paid, asked once.
    state.players[0].sneak_attack_planet = 5;
    state.players[0].sneak_attack_player = 2;
    choices::enabled = true;
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_CHECK_FALSE(secrets::Blackmail(0));
    DOCTEST_CHECK_FALSE(secrets::Blackmail(0));
    DOCTEST_REQUIRE(choices::Count(0) == 1);
    DOCTEST_CHECK(state.players[0].bc == 1000);
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == 5);
    DOCTEST_CHECK(secrets::Slots()[0].kind == secrets::KIND_PLAN);
    const choices::s_choice demand = choices::State()->choice[0];
    text_language::language = text_language::LANGUAGE_EN;
    choices::s_text en;
    DOCTEST_REQUIRE(secrets::Choice_Text(demand, &en));
    DOCTEST_CHECK(en.count == 2);
    DOCTEST_CHECK(std::string(en.option[0]) == "Pay 150 BC (the  keep the secret)");  // race names are empty in the fixture
    text_language::language = text_language::LANGUAGE_RU;
    choices::s_text ru;
    DOCTEST_REQUIRE(secrets::Choice_Text(demand, &ru));
    const std::string all = std::string(ru.title) + ru.body + ru.option[0] + ru.option[1];
    DOCTEST_CHECK(all.find('{') == std::string::npos);
    DOCTEST_CHECK(all.find('?') == std::string::npos);
    // Pay: what a computer owner pays.
    DOCTEST_REQUIRE(choices::Apply_Pending(0, 0));
    DOCTEST_CHECK(state.players[0].bc == 1000 - 1000 * secrets::BLACKMAIL_PERCENT / 100);
    DOCTEST_CHECK(state.players[1].bc == 1000 * secrets::BLACKMAIL_PERCENT / 100);
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == 5);  // silence kept
    DOCTEST_CHECK(secrets::Empty());
    // Refuse: the secret is made public.
    opinion::Clear();
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_CHECK_FALSE(secrets::Blackmail(0));
    DOCTEST_REQUIRE(choices::Apply_Pending(0, 1));
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == -1);
    DOCTEST_CHECK(opinion::Modifier(3, 0, opinion::REASON_BAD_RUMOURS) == secrets::RUMOUR_OPINION);
    DOCTEST_CHECK(secrets::Empty());
    // Too poor to pay: asked nothing, the secret goes public (as before).
    state.players[0].sneak_attack_planet = 5;
    state.players[0].sneak_attack_player = 2;
    state.players[0].bc = 50;
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_CHECK_FALSE(secrets::Blackmail(0));
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(secrets::Empty());
    // A demand for a technology the owner cannot give is paid as tribute, as for any human owner; the demand
    // for a vote too. A secret gone stale drops the choice; a paying choice with an empty purse refuses.
    state.players[0].bc = 1000;
    state.players[0].sneak_attack_planet = 5;
    state.players[0].sneak_attack_player = 2;
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_CHECK_FALSE(secrets::Blackmail_For(0, secrets::DEMAND_VOTE));
    DOCTEST_REQUIRE(choices::Count(0) == 1);
    const choices::s_choice vote = choices::State()->choice[0];
    DOCTEST_CHECK(secrets::Slots()[0].demanded == 0);
    state.players[0].bc = 20;
    text_language::language = text_language::LANGUAGE_EN;
    DOCTEST_REQUIRE(secrets::Choice_Text(vote, &en));
    DOCTEST_CHECK(std::string(en.option[0]).find("not enough BC") != std::string::npos);
    state.players[0].sneak_attack_planet = -1;  // the plan is over: stale
    DOCTEST_CHECK_FALSE(secrets::Choice_Text(vote, &en));
    choices::Clear();
    // A computer owner is unaffected: it pays at once, nothing queued.
    state.players[0].objectives = PLAYER_OBJECTIVE_MILITARIST;
    state.players[0].bc = 1000;
    state.players[0].sneak_attack_planet = 5;
    state.players[0].sneak_attack_player = 2;
    secrets::Clear();
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_CHECK(secrets::Blackmail(0));
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(state.players[0].bc == 1000 - 1000 * secrets::BLACKMAIL_PERCENT / 100);
}

DOCTEST_TEST_CASE("blackmail for a technology asked of a human: the owner gives the cheapest one") {
    s_secrets_scope scope;
    test::s_saved_value<bool> choices_on{choices::enabled};
    choices::Clear();
    test::s_game_state state;
    Setup_(&state);
    test::Load_Base_Texts();
    const int16_t app = projects::Cheapest_Unknown_App(1);
    DOCTEST_REQUIRE(app >= 0);
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;  // a granted technology then only refreshes the designs
    state.players[0].bc = 1000;
    state.players[0].tech_applications[app] = TECH_RESEARCH_STATUS_RESEARCHED;
    choices::enabled = true;
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 2, -1));
    DOCTEST_CHECK_FALSE(secrets::Blackmail_For(0, secrets::DEMAND_TECH));
    DOCTEST_REQUIRE(choices::Count(0) == 1);
    choices::s_text en;
    DOCTEST_REQUIRE(secrets::Choice_Text(choices::State()->choice[0], &en));
    DOCTEST_CHECK(std::string(en.option[0]).find("Give them a technology") == 0);
    DOCTEST_REQUIRE(choices::Apply_Pending(0, 0));
    DOCTEST_CHECK(state.players[1].tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED);
    DOCTEST_CHECK(state.players[0].bc == 1000);
    DOCTEST_CHECK(secrets::Empty());
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


// Iteration 7, piece A: "компромат накануне совета". Nominees are 0 (10 votes) and 1 (6); 2 and 3 are members.
namespace {
    void Kompromat_Setup_(test::s_game_state* state, int32_t stardate, int16_t next_session) {
        Setup_(state);
        intrigue::Clear();  // no favours owed from an earlier case: they add to a vote
        MOX::stardate = stardate;
        const int16_t population[4] = {100, 60, 30, 20};
        for (int i = 0; i < 4; ++i) {
            state->players[i].total_pop = population[i];
            state->players[i].council_voted_for = -1;
            for (int j = 0; j < 4; ++j) {  // a neutral opinion of everyone: no trust, no history
                state->players[i].trust_worthiness[j] = 0;
                state->players[i].broken_treaties_modifier[j] = 0;
                state->players[i].diplomacy_proposal_rejection[j] = 0;
                state->players[i].trade_treaty[j] = 0;
                state->players[i].research_treaty[j] = 0;
            }
            for (int j = 0; j < 4; ++j) state->players[i].treaty[j] = 0;
        }
        diplodef::COUNCIL_FLAG = 1;
        diplodef::next_council_meeting_turn = next_session;
        secrets::council_kompromat = true;
        secrets::enabled = false;  // the smear only: nothing else draws
        for (int from = 0; from < 4; ++from) {
            for (int to = 0; to < 4; ++to) {
                if (from == to) continue;
                state->players[from].spies[to] = 4;
                espionage::State()->points[from][to] = 50;
            }
        }
    }

    // The empire `a` ran a campaign at `t` this turn (the points were spent).
    bool Smeared_(int a, int t) {
        return espionage::Points(a, t) < 50;
    }
}

DOCTEST_TEST_CASE("kompromat: a member smears the nominee it does not vote for, not the foe") {
    s_secrets_scope scope;
    test::s_game_state state;
    Kompromat_Setup_(&state, 35101, 105);  // a session in 4 turns
    state.players[3].treaty[2] = 4;  // a foe of 3 - the usual target
    state.players[3].treaty[0] = 2;  // supports nominee 0: an ally always votes for its ally
    // treaty with 1 is none: the score is far below an even chance
    DOCTEST_CHECK(secrets::Kompromat_Target(3) == 1);
    secrets::Turn();
    DOCTEST_CHECK(Smeared_(3, 1));
    DOCTEST_CHECK_FALSE(Smeared_(3, 2));
    DOCTEST_CHECK_FALSE(Smeared_(3, 0));
}

DOCTEST_TEST_CASE("kompromat: against both nominees the one with more votes; for both nobody") {
    s_secrets_scope scope;
    test::s_game_state state;
    Kompromat_Setup_(&state, 35101, 105);
    state.players[3].treaty[2] = 4;
    DOCTEST_CHECK(secrets::Kompromat_Target(3) == 0);  // opposes both: 0 has 10 votes against 6
    secrets::Turn();
    DOCTEST_CHECK(Smeared_(3, 0));
    DOCTEST_CHECK_FALSE(Smeared_(3, 2));

    espionage::Clear();
    for (int from = 0; from < 4; ++from) {
        for (int to = 0; to < 4; ++to) {
            if (from != to) espionage::State()->points[from][to] = 50;
        }
    }
    state.players[3].treaty[0] = 2;  // supports both: an ally always votes for its ally
    state.players[3].treaty[1] = 2;
    DOCTEST_CHECK(secrets::Kompromat_Target(3) == -1);
    secrets::Turn();
    DOCTEST_CHECK_FALSE(Smeared_(3, 0));
    DOCTEST_CHECK_FALSE(Smeared_(3, 1));
    DOCTEST_CHECK(Smeared_(3, 2));  // the usual foe rule applies
}

DOCTEST_TEST_CASE("kompromat: a nominee smears the other nominee") {
    s_secrets_scope scope;
    test::s_game_state state;
    Kompromat_Setup_(&state, 35101, 105);
    state.players[1].treaty[2] = 4;  // a foe of nominee 1
    DOCTEST_CHECK(secrets::Kompromat_Target(1) == 0);
    DOCTEST_CHECK(secrets::Kompromat_Target(0) == 1);
    secrets::Turn();
    DOCTEST_CHECK(Smeared_(1, 0));
    DOCTEST_CHECK_FALSE(Smeared_(1, 2));
}

DOCTEST_TEST_CASE("kompromat: outside the window or with the setting off the usual foe") {
    s_secrets_scope scope;
    test::s_game_state state;
    Kompromat_Setup_(&state, 35101, 200);  // the session is 99 turns away
    state.players[3].treaty[2] = 4;
    state.players[3].treaty[0] = 2;
    DOCTEST_CHECK(secrets::Kompromat_Target(3) == -1);
    secrets::Turn();
    DOCTEST_CHECK(Smeared_(3, 2));
    DOCTEST_CHECK_FALSE(Smeared_(3, 1));

    espionage::Clear();
    for (int from = 0; from < 4; ++from) {
        for (int to = 0; to < 4; ++to) {
            if (from != to) espionage::State()->points[from][to] = 50;
        }
    }
    diplodef::next_council_meeting_turn = 105;  // in the window, but the setting is off
    secrets::council_kompromat = false;
    DOCTEST_CHECK(secrets::Kompromat_Target(3) == -1);
    secrets::Turn();
    DOCTEST_CHECK(Smeared_(3, 2));
    DOCTEST_CHECK_FALSE(Smeared_(3, 1));
}

DOCTEST_TEST_CASE("kompromat: a held secret of the opposed nominee is made public, not blackmailed") {
    s_secrets_scope scope;
    test::s_game_state state;
    Kompromat_Setup_(&state, 35109, 115);  // the use phase; a session in 6 turns
    secrets::enabled = true;
    secrets::smear = false;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) state.players[i].spies[j] = 0;  // nobody learns anything new
    }
    for (int i = 0; i < 4; ++i) state.players[i].personality = PLAYER_PERSONALITY_RUTHLESS;
    state.players[3].treaty[0] = 4;  // 3 will not vote for nominee 0, supports 1
    state.players[3].treaty[1] = 2;
    state.players[0].bc = 1000;
    DOCTEST_REQUIRE(secrets::Learn(3, secrets::KIND_PLAN, 0, 2, -1));
    secrets::Turn();
    DOCTEST_CHECK(secrets::Empty());
    DOCTEST_CHECK(opinion::Modifier(0, 3, opinion::REASON_EXPOSED_US) == secrets::EXPOSED_OPINION);
    DOCTEST_CHECK(state.players[0].bc == 1000);  // no tribute

    // Setting off: the ruthless holder blackmails as before.
    opinion::Clear();
    state.players[0].sneak_attack_planet = 5;  // telling the victim spoiled the plan
    state.players[0].sneak_attack_player = 2;
    secrets::council_kompromat = false;
    DOCTEST_REQUIRE(secrets::Learn(3, secrets::KIND_PLAN, 0, 2, -1));
    secrets::Turn();
    DOCTEST_CHECK(state.players[0].bc == 1000 - 1000 * secrets::BLACKMAIL_PERCENT / 100);
}

// Iteration 8, «А: беречь очки»: before the session the spy operations keep the points of the network in the
// nominee the computer runs its kompromat against; their window reaches a turn further than the smear's.
DOCTEST_TEST_CASE("kompromat: before the session the operations keep the opposed nominee's points") {
    s_secrets_scope scope;
    test::s_game_state state;
    Kompromat_Setup_(&state, 35100, 111);  // the operations' turn; the session in 11 turns, the smear's next turn in 10
    state.players[3].treaty[2] = 4;          // opposes both nominees: its target is 0, the one with more votes
    spy_ops::operations = 1 << spy_ops::OP_DATA;
    DOCTEST_CHECK(secrets::Kompromat_Target(3) == -1);    // the smear's own window: 11 turns is outside
    DOCTEST_CHECK(secrets::Kompromat_Target(3, 1) == 0);  // the operations look a turn further
    DOCTEST_CHECK(spy_ops::Choose_Operation(3, 0) == spy_ops::OP_COUNT);  // the points wait for the smear
    DOCTEST_CHECK(spy_ops::Choose_Operation(3, 1) == spy_ops::OP_DATA);   // the other nominee: as usual

    diplodef::next_council_meeting_turn = 200;  // no session near: as usual
    DOCTEST_CHECK(spy_ops::Choose_Operation(3, 0) == spy_ops::OP_DATA);

    diplodef::next_council_meeting_turn = 111;
    secrets::council_kompromat = false;  // the setting off: as usual
    DOCTEST_CHECK(spy_ops::Choose_Operation(3, 0) == spy_ops::OP_DATA);
}

// Iteration 9, «Уотергейт»: an exposed kompromat against a nominee is a public scandal for the smearer.
DOCTEST_TEST_CASE("kompromat: exposed against a nominee it is a public scandal; otherwise only the victim remembers") {
    s_secrets_scope scope;
    test::s_game_state state;
    Kompromat_Setup_(&state, 35101, 105);  // a session in 4 turns
    state.players[3].treaty[2] = 4;          // opposes both nominees: its kompromat target is 0
    secrets::kompromat_scandal = true;
    DOCTEST_REQUIRE(secrets::Kompromat_Target(3) == 0);
    DOCTEST_CHECK_FALSE(secrets::Smear_With_Dice(3, 0, CATASTROPHE));
    DOCTEST_CHECK(opinion::Modifier(0, 3, opinion::REASON_SMEARED_US) == secrets::SMEARED_OPINION);
    DOCTEST_CHECK(opinion::Modifier(1, 3, opinion::REASON_BAD_RUMOURS) == secrets::SCANDAL_OPINION);
    DOCTEST_CHECK(opinion::Modifier(2, 3, opinion::REASON_BAD_RUMOURS) == secrets::SCANDAL_OPINION);
    DOCTEST_CHECK(opinion::Modifier(0, 3, opinion::REASON_BAD_RUMOURS) == 0);  // the victim: its own memory only

    opinion::Clear();  // a smear of the foe, not of the nominee: no scandal
    DOCTEST_CHECK_FALSE(secrets::Smear_With_Dice(3, 2, CATASTROPHE));
    DOCTEST_CHECK(opinion::Modifier(1, 3, opinion::REASON_BAD_RUMOURS) == 0);

    opinion::Clear();  // the setting off: no scandal
    secrets::kompromat_scandal = false;
    DOCTEST_CHECK_FALSE(secrets::Smear_With_Dice(3, 0, CATASTROPHE));
    DOCTEST_CHECK(opinion::Modifier(1, 3, opinion::REASON_BAD_RUMOURS) == 0);

    opinion::Clear();  // outside the window: no scandal
    secrets::kompromat_scandal = true;
    diplodef::next_council_meeting_turn = 200;
    DOCTEST_CHECK_FALSE(secrets::Smear_With_Dice(3, 0, CATASTROPHE));
    DOCTEST_CHECK(opinion::Modifier(1, 3, opinion::REASON_BAD_RUMOURS) == 0);
}

// Iteration 10, «скандал рвёт пакты»: the smearer's non-aggression pacts with computers end, the victim's too.
DOCTEST_TEST_CASE("kompromat: a scandal ends the smearer's non-aggression pacts, without a war or a stain") {
    s_secrets_scope scope;
    test::s_game_state state;
    Kompromat_Setup_(&state, 35101, 105);
    state.players[3].treaty[2] = 4;  // at war with 2: opposes both nominees, its kompromat target is 0
    state.players[2].treaty[3] = 4;
    for (int o : {0, 1}) {           // pacts with the victim (0) and with a bystander (1)
        state.players[3].treaty[o] = 1;
        state.players[o].treaty[3] = 1;
    }
    secrets::kompromat_scandal = true;
    secrets::scandal_breaks_pacts = true;
    DOCTEST_REQUIRE(secrets::Kompromat_Target(3) == 0);
    DOCTEST_CHECK_FALSE(secrets::Smear_With_Dice(3, 0, CATASTROPHE));
    for (int o : {0, 1}) {
        DOCTEST_CHECK(state.players[3].treaty[o] == 0);
        DOCTEST_CHECK(state.players[o].treaty[3] == 0);
        DOCTEST_CHECK(state.players[o].dishonored_flag[3] == 0);  // no 1.40 break: nobody is dishonoured
    }
    DOCTEST_CHECK(state.players[3].treaty[2] == 4);  // a war stays a war

    // Without the setting the scandal leaves the pacts alone.
    for (int o : {0, 1}) {
        state.players[3].treaty[o] = 1;
        state.players[o].treaty[3] = 1;
    }
    secrets::scandal_breaks_pacts = false;
    state.players[3].spies[0] = 4;
    espionage::State()->points[3][0] = 50;
    DOCTEST_CHECK_FALSE(secrets::Smear_With_Dice(3, 0, CATASTROPHE));
    DOCTEST_CHECK(state.players[3].treaty[1] == 1);
    DOCTEST_CHECK(state.players[1].treaty[3] == 1);
}

}
