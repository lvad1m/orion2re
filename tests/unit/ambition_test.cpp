#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "../fixtures/texts_fixture.h"
#include "game/ambition.h"
#include "game/choices.h"
#include "game/loyalty.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/secrets.h"
#include "game/spy_ops.h"

namespace {
    struct s_ambition_scope {
        test::s_saved_value<bool> governors{ambition::ambitious_governors};
        test::s_saved_value<bool> bribery{ambition::council_bribery};
        test::s_saved_value<bool> loyal{loyalty::enabled};
        test::s_saved_value<bool> secrets_on{secrets::enabled};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};

        s_ambition_scope() {
            loyalty::Clear();
            secrets::Clear();
            opinion::Clear();
            ambition::ambitious_governors = true;
            ambition::council_bribery = true;
            loyalty::enabled = true;
            secrets::enabled = true;
            opinion::enabled = true;
        }
        ~s_ambition_scope() {
            loyalty::Clear();
            secrets::Clear();
            opinion::Clear();
        }
    };

    // Four computer empires in contact. Player 0's capital is planet 0 at star 0; its colony on planet 1 at star 1
    // is governed by leader 20 (level 4, loyalty 10). Player 1 has a colony at star 2 (near), player 2 at star 3 (far).
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 4;
        MOX::stardate = 35107;
        MOX::NUM_COLONIES = 3;
        for (int i = 0; i < 4; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[i].personality = PLAYER_PERSONALITY_ERRATIC;
            state->players[i].bc = 1000;
            state->players[i].sneak_attack_planet = -1;
            state->players[i].sneak_attack_player = -1;
            for (int j = 0; j < 4; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
        for (int s = 0; s < 4; ++s) {
            for (int slot = 0; slot < 5; ++slot) state->stars[s].planet_index[slot] = -1;
            for (int p = 0; p < 8; ++p) state->stars[s].officer_index[p] = -1;
            state->stars[s].x = static_cast<int16_t>(s == 3 ? 500 : s * 10);
            state->stars[s].y = 0;
            state->planets[s].star_index = static_cast<int16_t>(s);
            state->planets[s].colony_index = -1;
        }
        state->players[0].home_planet_id = 0;
        for (int c = 0; c < 3; ++c) {
            state->colonies[c].planet = static_cast<int16_t>(c == 0 ? 0 : c == 1 ? 1 : 2);
            state->colonies[c].outpost_flag = 0;
        }
        state->colonies[0].owner = 0;  // the capital's colony
        state->colonies[1].owner = 0;  // the governed colony, planet 1 at star 1
        state->colonies[2].owner = 1;  // planet 2 at star 2
        state->planets[0].colony_index = 0;
        state->planets[1].colony_index = 1;
        state->planets[2].colony_index = 2;
        // Player 2's colony is far away: planet 3 at star 3 is not a colony slot; give it colony 3.
        MOX::NUM_COLONIES = 4;
        state->colonies[3].planet = 3;
        state->colonies[3].owner = 2;
        state->colonies[3].outpost_flag = 0;
        state->planets[3].colony_index = 3;

        state->leaders[20].player_index = 0;
        state->leaders[20].status = 1;
        state->leaders[20].type = LEADER_TYPE_COLONY;
        state->leaders[20].xp = 30000;  // a high level
        state->leaders[20].location = 1;
        state->leaders[20].eta = 0;
        state->stars[1].officer_index[0] = 20;
        loyalty::Record(20)->loyalty = 10;
        loyalty::Record(20)->owner = 0;
        loyalty::Record(20)->last_level = 4;
    }

    // Council: nominees 1 and 2; voters 0 and 3.
    void Council_Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 4;
        MOX::stardate = 35100;
        for (int i = 0; i < 4; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[i].personality = PLAYER_PERSONALITY_ERRATIC;
            state->players[i].bc = 150;
            state->players[i].sneak_attack_planet = -1;
            state->players[i].sneak_attack_player = -1;
        }
        state->players[1].bc = 1000;
        state->players[2].bc = 1000;
    }
} // namespace

DOCTEST_TEST_SUITE("ambition") {

DOCTEST_TEST_CASE("a restless governor defects with its colony to the nearest neighbour") {
    s_ambition_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK(ambition::Is_Restless(20));
    DOCTEST_CHECK(ambition::Pick_Neighbour(20) == 1);
    DOCTEST_CHECK_FALSE(ambition::Try_Defect(20, ambition::DEFECT_PERCENT));  // the roll missed
    DOCTEST_CHECK(state.colonies[1].owner == 0);
    DOCTEST_REQUIRE(ambition::Try_Defect(20, 0));
    DOCTEST_CHECK(state.colonies[1].owner == 1);
    DOCTEST_CHECK(state.colonies[0].owner == 0);  // the capital stays
    DOCTEST_CHECK(state.leaders[20].player_index == 1);
    DOCTEST_CHECK(state.leaders[20].status == 0);
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_PLOTTED_AGAINST_US) == ambition::DEFECTION_OPINION);
}

DOCTEST_TEST_CASE("a war enemy is preferred to a nearer neighbour") {
    s_ambition_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].treaty[2] = 4;  // at war with the far one
    DOCTEST_CHECK(ambition::Pick_Neighbour(20) == 2);
    DOCTEST_REQUIRE(ambition::Try_Defect(20, 0));
    DOCTEST_CHECK(state.colonies[1].owner == 2);
}

DOCTEST_TEST_CASE("with no neighbour the governor deserts to the hire list") {
    s_ambition_scope scope;
    test::s_game_state state;
    Setup_(&state);
    for (int j = 1; j < 4; ++j) state.players[0].contact[j] = 0;
    DOCTEST_CHECK(ambition::Pick_Neighbour(20) == -1);
    DOCTEST_REQUIRE(ambition::Try_Defect(20, 0));
    DOCTEST_CHECK(state.leaders[20].status == -1);
    DOCTEST_CHECK(state.leaders[20].player_index == -1);
    DOCTEST_CHECK(state.stars[1].officer_index[0] == -1);
    DOCTEST_CHECK(state.colonies[1].owner == 0);
}

DOCTEST_TEST_CASE("loyal, low-level, capital and human governors stay") {
    s_ambition_scope scope;
    test::s_game_state state;
    Setup_(&state);
    loyalty::Record(20)->loyalty = 21;
    DOCTEST_CHECK_FALSE(ambition::Is_Restless(20));
    loyalty::Record(20)->loyalty = 20;
    DOCTEST_CHECK(ambition::Is_Restless(20));
    state.leaders[20].xp = 0;
    DOCTEST_CHECK_FALSE(ambition::Is_Restless(20));
    state.leaders[20].xp = 30000;
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_CHECK_FALSE(ambition::Is_Restless(20));
    state.players[0].objectives = PLAYER_OBJECTIVE_MILITARIST;
    state.leaders[20].location = 0;  // the capital's star
    DOCTEST_CHECK_FALSE(ambition::Is_Restless(20));
    state.leaders[20].location = 1;
    loyalty::Record(20)->loyalty = loyalty::UNTRACKED;
    DOCTEST_CHECK_FALSE(ambition::Is_Restless(20));
    DOCTEST_CHECK_FALSE(ambition::Try_Defect(20, 0));
    DOCTEST_CHECK(state.colonies[1].owner == 0);
}

DOCTEST_TEST_CASE("the turn rolls only on the phase and moves nothing when the roll cannot succeed") {
    s_ambition_scope scope;
    test::s_game_state state;
    Setup_(&state);
    MOX::stardate = 35108;  // not the phase
    const uint32_t seed = game_random::Get_Random_Seed();
    ambition::Turn();
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
    DOCTEST_CHECK(state.colonies[1].owner == 0);
}

DOCTEST_TEST_CASE("a computer nominee bribes the voters that do not hate it; a bought vote scores +100") {
    s_ambition_scope scope;
    test::s_game_state state;
    Council_Setup_(&state);
    opinion::Add(3, 1, opinion::REASON_PLOTTED_AGAINST_US, -90);  // voter 3 hates nominee 1
    ambition::Before_Council_Vote(1, 2);
    DOCTEST_CHECK(ambition::Council_Vote_Bias(0, 1) == ambition::BRIBE_BIAS);
    DOCTEST_CHECK(ambition::Council_Vote_Bias(0, 2) == 0);  // already bought by nominee 1
    DOCTEST_CHECK(ambition::Council_Vote_Bias(3, 1) == 0);
    DOCTEST_CHECK(ambition::Council_Vote_Bias(3, 2) == ambition::BRIBE_BIAS);
    DOCTEST_CHECK(state.players[1].bc == 1000 - ambition::BRIBE_PRICE);
    DOCTEST_CHECK(state.players[0].bc == 150 + ambition::BRIBE_PRICE);
    ambition::After_Council_Vote();
    DOCTEST_CHECK(ambition::Council_Vote_Bias(0, 1) == 0);
}

DOCTEST_TEST_CASE("a poor nominee does not bribe and keeps its reserve") {
    s_ambition_scope scope;
    test::s_game_state state;
    Council_Setup_(&state);
    state.players[1].bc = ambition::MIN_BC_TO_BRIBE - 1;
    state.players[2].bc = ambition::MIN_BC_TO_BRIBE;
    ambition::Before_Council_Vote(1, 2);
    DOCTEST_CHECK(ambition::Council_Vote_Bias(0, 1) == 0);
    DOCTEST_CHECK(ambition::Council_Vote_Bias(0, 2) == ambition::BRIBE_BIAS);
    DOCTEST_CHECK(ambition::Council_Vote_Bias(3, 2) == ambition::BRIBE_BIAS);
    DOCTEST_CHECK(state.players[2].bc == ambition::MIN_BC_TO_BRIBE - 2 * ambition::BRIBE_PRICE);
    state.players[2].bc = ambition::MIN_BC_TO_BRIBE - 1;
    ambition::Before_Council_Vote(1, 2);
    DOCTEST_CHECK(state.players[2].bc == ambition::MIN_BC_TO_BRIBE - 1);  // below the threshold
}

DOCTEST_TEST_CASE("iteration 1: a nominee with 100 BC buys a voter that thinks of it as -45 (it did not before)") {
    s_ambition_scope scope;
    test::s_game_state state;
    Council_Setup_(&state);
    state.players[1].bc = 100;
    opinion::Add(0, 1, opinion::REASON_PLOTTED_AGAINST_US, -45);
    ambition::Before_Council_Vote(1, 2);
    DOCTEST_CHECK(ambition::Council_Vote_Bias(0, 1) == ambition::BRIBE_BIAS);
    DOCTEST_CHECK(state.players[1].bc >= ambition::BRIBE_RESERVE);
    ambition::After_Council_Vote();
    ambition::council_bribery = false;
    ambition::Before_Council_Vote(1, 2);
    DOCTEST_CHECK(ambition::Council_Vote_Bias(0, 1) == 0);  // flag off: nothing
}

DOCTEST_TEST_CASE("a secret about a voter is spent on blackmail: +150, the secret is gone") {
    s_ambition_scope scope;
    test::s_game_state state;
    Council_Setup_(&state);
    state.players[1].bc = 50;  // no money to bribe with
    state.players[0].sneak_attack_planet = 5;
    state.players[0].sneak_attack_player = 3;
    DOCTEST_REQUIRE(secrets::Learn(1, secrets::KIND_PLAN, 0, 3, -1));
    ambition::Before_Council_Vote(1, 2);
    DOCTEST_CHECK(ambition::Council_Vote_Bias(0, 1) == ambition::BLACKMAIL_BIAS);
    DOCTEST_CHECK(secrets::Empty());
    DOCTEST_CHECK(state.players[1].bc == 50);
    DOCTEST_CHECK(state.players[0].bc == 150);
}

DOCTEST_TEST_CASE("the other nominee with spies in the briber may learn of the bribes") {
    s_ambition_scope scope;
    test::s_game_state state;
    Council_Setup_(&state);
    state.players[2].bc = 0;  // only nominee 1 bribes
    state.players[2].spies[1] = 2;
    bool learned = false;
    for (uint32_t seed = 1; seed < 200 && !learned; ++seed) {
        opinion::Clear();
        state.players[1].bc = 1000;
        game_random::Set_Random_Seed(seed);
        ambition::Before_Council_Vote(1, 2);
        learned = opinion::Modifier(2, 1, opinion::REASON_PLOTTED_AGAINST_US) == ambition::EXPOSURE_OPINION;
        ambition::After_Council_Vote();
    }
    DOCTEST_CHECK(learned);

    // Without spies in the briber there is no roll at all.
    state.players[2].spies[1] = 0;
    state.players[1].bc = 1000;
    opinion::Clear();
    const uint32_t seed = game_random::Get_Random_Seed();
    ambition::Before_Council_Vote(1, 2);
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_PLOTTED_AGAINST_US) == 0);
}

DOCTEST_TEST_CASE("off: no defection, no bribes, no draws") {
    s_ambition_scope scope;
    test::s_game_state state;
    Setup_(&state);
    ambition::ambitious_governors = false;
    ambition::council_bribery = false;
    const uint32_t seed = game_random::Get_Random_Seed();
    DOCTEST_CHECK_FALSE(ambition::Is_Restless(20));
    DOCTEST_CHECK_FALSE(ambition::Try_Defect(20, 0));
    ambition::Turn();
    DOCTEST_CHECK(state.colonies[1].owner == 0);
    DOCTEST_CHECK(state.leaders[20].status == 1);
    Council_Setup_(&state);
    ambition::Before_Council_Vote(1, 2);
    DOCTEST_CHECK(ambition::Council_Vote_Bias(0, 1) == 0);
    DOCTEST_CHECK(state.players[1].bc == 1000);
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
}

} // DOCTEST_TEST_SUITE

DOCTEST_TEST_CASE("council_bribery_exposes_voters: the same roll also lowers the rival's opinion of the bought voters") {
    s_ambition_scope scope;
    test::s_saved_value<bool> flag{ambition::council_bribery_exposes_voters};
    test::s_game_state state;
    Council_Setup_(&state);
    state.players[2].bc = 0;  // only nominee 1 bribes; voters 0 and 3 are bought
    state.players[2].spies[1] = 2;
    for (const bool on : {false, true}) {
        ambition::council_bribery_exposes_voters = on;
        int hits = 0;
        for (uint32_t seed = 1; seed < 60; ++seed) {
            opinion::Clear();
            state.players[1].bc = 1000;
            game_random::Set_Random_Seed(seed);
            ambition::Before_Council_Vote(1, 2);
            const uint32_t after = game_random::Get_Random_Seed();
            game_random::Set_Random_Seed(seed);
            (void)game_random::Random(100);
            DOCTEST_CHECK(game_random::Get_Random_Seed() == after);  // one draw whatever the setting
            const bool hit = opinion::Modifier(2, 1, opinion::REASON_PLOTTED_AGAINST_US) == ambition::EXPOSURE_OPINION;
            hits += hit ? 1 : 0;
            for (const int voter : {0, 3}) {
                const int expected = (hit && on) ? ambition::EXPOSURE_OPINION : 0;
                DOCTEST_CHECK(opinion::Modifier(2, voter, opinion::REASON_PLOTTED_AGAINST_US) == expected);
            }
            ambition::After_Council_Vote();
        }
        DOCTEST_CHECK(hits > 0);
    }
}

// R2, piece C (human-ui.md section 5): a computer nominee offers a human voter the bribe it would give a computer
// voter; accepting binds the human's council vote.
namespace {
    struct s_bribe_choice_scope {
        test::s_saved_value<bool> choices_on{choices::enabled};
        test::s_saved_value<uint8_t> language{text_language::language};
        test::s_saved_value<int8_t> game_type{MOX::game_type};
        test::s_saved_value<decltype(NETMOX::net.is_network_active)> network{NETMOX::net.is_network_active};

        s_bribe_choice_scope() {
            choices::Clear();
            choices::enabled = true;
            MOX::game_type = 0;
            NETMOX::net.is_network_active = 0;
        }
        ~s_bribe_choice_scope() { choices::Clear(); }
    };
}

DOCTEST_TEST_SUITE("council bribes offered to a human") {

DOCTEST_TEST_CASE("a nominee that would bribe a computer voter offers the human the same; accept pays and binds the vote") {
    s_ambition_scope scope;
    s_bribe_choice_scope cscope;
    test::s_game_state state;
    Council_Setup_(&state);
    test::Load_Base_Texts();
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    opinion::Add(3, 1, opinion::REASON_PLOTTED_AGAINST_US, -90);  // computer voter 3 hates nominee 1
    ambition::Before_Council_Vote(1, 2);
    // The computers' purchases are those of a game without the human (voter 3 is bought by nominee 2).
    DOCTEST_CHECK(ambition::Council_Vote_Bias(3, 2) == ambition::BRIBE_BIAS);
    DOCTEST_CHECK(state.players[2].bc == 1000 - ambition::BRIBE_PRICE);
    DOCTEST_CHECK(state.players[1].bc == 1000);
    choices::s_choice offer;
    DOCTEST_REQUIRE(ambition::Human_Offer(0, 1, 2, &offer));
    DOCTEST_CHECK(offer.module == choices::MODULE_AMBITION);
    DOCTEST_CHECK(offer.player == 0);
    DOCTEST_CHECK(offer.target == 1);
    DOCTEST_CHECK(offer.other == 2);
    DOCTEST_CHECK(offer.value == ambition::BRIBE_PRICE);
    DOCTEST_CHECK(state.players[0].bc == 150);  // nothing paid until it is accepted
    for (const uint8_t language : {text_language::LANGUAGE_EN, text_language::LANGUAGE_RU}) {
        text_language::language = language;
        choices::s_text t;
        DOCTEST_REQUIRE(ambition::Choice_Text(offer, &t));
        DOCTEST_CHECK(t.count == 2);
        std::string all = std::string(t.title) + t.body + t.option[0] + t.option[1];
        DOCTEST_CHECK(all.find('{') == std::string::npos);
        DOCTEST_CHECK(all.find('?') == std::string::npos);
    }
    text_language::language = text_language::LANGUAGE_EN;
    // Refuse: nothing changes.
    ambition::Choice_Apply(offer, 1);
    DOCTEST_CHECK(state.players[0].bc == 150);
    DOCTEST_CHECK(ambition::Take_Human_Vote_Promise() == -1);
    // Accept: the money moves, the vote is promised once.
    ambition::Choice_Apply(offer, 0);
    DOCTEST_CHECK(state.players[0].bc == 150 + ambition::BRIBE_PRICE);
    DOCTEST_CHECK(state.players[1].bc == 1000 - ambition::BRIBE_PRICE);
    DOCTEST_CHECK(ambition::Take_Human_Vote_Promise() == 1);
    DOCTEST_CHECK(ambition::Take_Human_Vote_Promise() == -1);
    // The next session forgets an unspent promise.
    ambition::Choice_Apply(offer, 0);
    ambition::Before_Council_Vote(1, 2);
    DOCTEST_CHECK(ambition::Take_Human_Vote_Promise() == -1);
}

DOCTEST_TEST_CASE("the human who sells its vote may be found out: one roll, both opinions drop on a hit") {
    s_ambition_scope scope;
    s_bribe_choice_scope cscope;
    test::s_game_state state;
    Council_Setup_(&state);
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    choices::s_choice offer;
    offer.module = choices::MODULE_AMBITION;
    offer.kind = ambition::CHOICE_BRIBE;
    offer.player = 0;
    offer.target = 1;
    offer.other = 2;
    offer.value = ambition::BRIBE_PRICE;
    // The rival has no spies on either: no roll at all.
    game_random::Set_Random_Seed(77);
    ambition::Choice_Apply(offer, 0);
    DOCTEST_CHECK(game_random::Get_Random_Seed() == 77);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_PLOTTED_AGAINST_US) == 0);
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_PLOTTED_AGAINST_US) == 0);
    (void)ambition::Take_Human_Vote_Promise();
    // Spies on the human only: one roll; a hit lowers the rival's opinion of the briber and of the human, a miss nothing.
    state.players[2].spies[0] = 1;
    int hits = 0;
    int misses = 0;
    for (uint32_t seed = 1; seed < 100 && (hits == 0 || misses == 0); ++seed) {
        opinion::Clear();
        game_random::Set_Random_Seed(seed);
        const bool will_hit = static_cast<int32_t>(game_random::Random(100)) < ambition::EXPOSURE_PERCENT;
        const uint32_t after_one = game_random::Get_Random_Seed();
        game_random::Set_Random_Seed(seed);
        ambition::Choice_Apply(offer, 0);
        (void)ambition::Take_Human_Vote_Promise();
        DOCTEST_CHECK(game_random::Get_Random_Seed() == after_one);  // exactly one draw
        const int expected = will_hit ? ambition::EXPOSURE_OPINION : 0;
        DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_PLOTTED_AGAINST_US) == expected);
        DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_PLOTTED_AGAINST_US) == expected);
        (will_hit ? hits : misses)++;
    }
    DOCTEST_CHECK(hits > 0);
    DOCTEST_CHECK(misses > 0);
    // Refusing never rolls.
    state.players[2].spies[0] = 0;
    state.players[2].spies[1] = 1;
    game_random::Set_Random_Seed(5);
    ambition::Choice_Apply(offer, 1);
    DOCTEST_CHECK(game_random::Get_Random_Seed() == 5);
}

DOCTEST_TEST_CASE("no offer with the setting off, a poor briber, a hated briber, a human nominee, a network game; none to computers") {
    s_ambition_scope scope;
    s_bribe_choice_scope cscope;
    test::s_game_state state;
    Council_Setup_(&state);
    test::Load_Base_Texts();
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    choices::s_choice offer;
    ambition::Before_Council_Vote(1, 2);
    DOCTEST_CHECK(ambition::Human_Offer(0, 1, 2, &offer));
    choices::enabled = false;
    DOCTEST_CHECK_FALSE(ambition::Human_Offer(0, 1, 2, &offer));
    choices::enabled = true;
    DOCTEST_CHECK_FALSE(ambition::Human_Offer(3, 1, 2, &offer));  // a computer is never offered through this path
    DOCTEST_CHECK_FALSE(ambition::Human_Offer(0, 0, 2, &offer));  // a human nominee does not bribe itself
    ambition::council_bribery = false;
    DOCTEST_CHECK_FALSE(ambition::Human_Offer(0, 1, 2, &offer));
    ambition::council_bribery = true;
    state.players[0].relations[1] = -90;  // the human hates both nominees (a human's opinion is the classic one)
    state.players[0].relations[2] = -90;
    DOCTEST_CHECK_FALSE(ambition::Human_Offer(0, 1, 2, &offer));
    state.players[0].relations[1] = 0;
    state.players[0].relations[2] = 0;
    // Poor nominees: no money, no offer (the check is made once, before the first purchase).
    state.players[1].bc = ambition::MIN_BC_TO_BRIBE - 1;
    state.players[2].bc = ambition::MIN_BC_TO_BRIBE - 1;
    ambition::Before_Council_Vote(1, 2);
    DOCTEST_CHECK_FALSE(ambition::Human_Offer(0, 1, 2, &offer));
    state.players[1].bc = 1000;
    state.players[2].bc = 1000;
    ambition::Before_Council_Vote(1, 2);
    MOX::game_type = 2;  // a network game
    DOCTEST_CHECK_FALSE(ambition::Human_Offer(0, 1, 2, &offer));
    MOX::game_type = 0;
    DOCTEST_CHECK(ambition::Human_Offer(0, 1, 2, &offer));
    // The briber's treasury is gone by the time of the answer: nothing is paid and no vote is promised.
    state.players[offer.target].bc = 5;
    choices::s_text t;
    DOCTEST_CHECK_FALSE(ambition::Choice_Text(offer, &t));
    ambition::Choice_Apply(offer, 0);
    DOCTEST_CHECK(state.players[0].bc == 150);
    DOCTEST_CHECK(ambition::Take_Human_Vote_Promise() == -1);
}

DOCTEST_TEST_CASE("without a human the computers' bribes are as before and nothing is offered or kept") {
    s_ambition_scope scope;
    s_bribe_choice_scope cscope;
    test::s_game_state state;
    Council_Setup_(&state);
    opinion::Add(3, 1, opinion::REASON_PLOTTED_AGAINST_US, -90);
    const uint32_t seed = game_random::Get_Random_Seed();
    ambition::Before_Council_Vote(1, 2);
    DOCTEST_CHECK(ambition::Council_Vote_Bias(0, 1) == ambition::BRIBE_BIAS);
    DOCTEST_CHECK(ambition::Council_Vote_Bias(3, 2) == ambition::BRIBE_BIAS);
    DOCTEST_CHECK(state.players[1].bc == 1000 - ambition::BRIBE_PRICE);
    choices::s_choice offer;
    for (int16_t p = 0; p < 4; ++p) DOCTEST_CHECK_FALSE(ambition::Human_Offer(p, 1, 2, &offer));
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(game_random::Get_Random_Seed() == seed);
}

}
