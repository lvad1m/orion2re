#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "../fixtures/texts_fixture.h"
#include "game/advisor.h"
#include "game/choices.h"
#include "game/dice.h"
#include "game/espionage.h"
#include "game/hostages.h"
#include "game/intrigue.h"
#include "game/opinion.h"
#include "game/pacts.h"
#include "game/serialization.h"
#include "game/spy_ops.h"

#include <memory>

namespace {
    struct s_intrigue_scope {
        test::s_saved_value<bool> reveal{intrigue::reveal_plans};
        test::s_saved_value<int32_t> reveal_chance{intrigue::reveal_chance};
        test::s_saved_value<bool> forged{intrigue::forged_letters};
        test::s_saved_value<int32_t> forged_cost{intrigue::forged_letter_cost};
        test::s_saved_value<bool> fate{intrigue::spy_fate};
        test::s_saved_value<bool> proxy{intrigue::proxy_wars};
        test::s_saved_value<int32_t> proxy_percent{intrigue::proxy_payment_percent};
        test::s_saved_value<bool> favours{intrigue::favours};
        test::s_saved_value<int32_t> vote_bonus{intrigue::favour_vote_bonus};
        test::s_saved_value<bool> network{espionage::network};
        test::s_saved_value<bool> ci{espionage::counterintelligence};
        test::s_saved_value<int32_t> operations{spy_ops::operations};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<bool> pacts_enabled{pacts::enabled};
        test::s_saved_value<bool> advisor_enabled{advisor::enabled};
        test::s_saved_value<uint8_t> roll_kind{dice::roll_kind};

        s_intrigue_scope() {
            intrigue::Clear();
            espionage::Clear();
            opinion::Clear();
            pacts::Clear();
            intrigue::reveal_plans = true;
            intrigue::reveal_chance = 1000;  // every roll succeeds
            intrigue::forged_letters = true;
            intrigue::forged_letter_cost = 25;
            intrigue::spy_fate = true;
            intrigue::proxy_wars = true;
            intrigue::proxy_payment_percent = 20;
            intrigue::favours = true;
            intrigue::favour_vote_bonus = 150;
            espionage::network = true;
            espionage::counterintelligence = false;
            spy_ops::operations = 0;
            opinion::enabled = true;
            pacts::enabled = false;
            advisor::enabled = false;
            dice::roll_kind = dice::ROLL_TWO_D10;
        }
        ~s_intrigue_scope() {
            intrigue::Clear();
            espionage::Clear();
            opinion::Clear();
            pacts::Clear();
        }
    };

    const int32_t TRIUMPH[2] = {10, 10};
    const int32_t CATASTROPHE[2] = {1, 1};
    constexpr int8_t TREATY_ALLIANCE = 2;
    constexpr int8_t TREATY_WAR = 4;

    // Four computer empires that all know each other, at peace and with classic relations 0.
    // Stardate 35001 (no periodic step).
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 4;
        MOX::NUM_COLONIES = 0;
        MOX::stardate = 35001;
        for (int i = 0; i < 4; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[i].sneak_attack_planet = -1;
            state->players[i].sneak_attack_player = -1;
            for (int j = 0; j < 4; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
    }
} // namespace

DOCTEST_TEST_SUITE("intrigues") {

DOCTEST_TEST_CASE("a sneak attack plan is a planned victim; a war already fought is not") {
    s_intrigue_scope scope;
    test::s_game_state state;
    Setup_(&state);
    int16_t victims[8];
    DOCTEST_CHECK(intrigue::Planned_Victims(0, victims) == 0);
    state.players[0].sneak_attack_planet = 5;
    state.players[0].sneak_attack_player = 2;
    DOCTEST_REQUIRE(intrigue::Planned_Victims(0, victims) == 1);
    DOCTEST_CHECK(victims[0] == 2);
    state.players[0].treaty[2] = state.players[2].treaty[0] = TREATY_WAR;
    DOCTEST_CHECK(intrigue::Planned_Victims(0, victims) == 0);
}

DOCTEST_TEST_CASE("the victim's spies uncover the plan: opinion, the surprise is lost, once in 20 turns") {
    s_intrigue_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].sneak_attack_planet = 5;
    state.players[0].sneak_attack_player = 2;
    state.players[0].war_buildup_duration = 7;
    state.players[2].spies[0] = 2;
    intrigue::Turn();
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_PLOTTED_AGAINST_US) == intrigue::PLOTTED_OPINION);
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == -1);
    DOCTEST_CHECK(state.players[0].war_buildup_duration == 0);
    DOCTEST_CHECK(intrigue::State()->revealed[0][2] == 35001);
    DOCTEST_CHECK(intrigue::Favours(2, 2) == 0);
    // The plan made again soon after is not revealed again.
    state.players[0].sneak_attack_planet = 5;
    state.players[0].sneak_attack_player = 2;
    MOX::stardate = 35005;
    intrigue::Turn();
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == 5);
    MOX::stardate = 35021;
    intrigue::Turn();
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == -1);
}

DOCTEST_TEST_CASE("a third empire warns a victim it likes, never against its ally; the victim owes it") {
    s_intrigue_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].sneak_attack_planet = 5;
    state.players[0].sneak_attack_player = 2;
    state.players[1].spies[0] = 3;
    state.players[1].treaty[0] = state.players[0].treaty[1] = TREATY_ALLIANCE;
    intrigue::Turn();
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == 5);  // keeps its ally's secret
    state.players[1].treaty[0] = state.players[0].treaty[1] = 0;
    state.players[1].relations[2] = -20;  // dislikes the victim
    intrigue::Turn();
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == 5);
    state.players[1].relations[2] = 10;
    intrigue::Turn();
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == -1);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_DID_US_A_SERVICE) == intrigue::SERVICE_OPINION);
    DOCTEST_CHECK(intrigue::Favours(2, 1) == 1);
}

DOCTEST_TEST_CASE("favours: at most 3, a council vote bonus, paid or denied by the vote") {
    s_intrigue_scope scope;
    test::s_game_state state;
    Setup_(&state);
    for (int i = 0; i < 5; ++i) intrigue::Owe(2, 1);
    DOCTEST_CHECK(intrigue::Favours(2, 1) == intrigue::FAVOUR_LIMIT);
    DOCTEST_CHECK(intrigue::Council_Vote_Bias(2, 1) == 150);
    DOCTEST_CHECK(intrigue::Council_Vote_Bias(1, 2) == 0);
    intrigue::On_Council_Vote(2, 1, 1, 3);
    DOCTEST_CHECK(intrigue::Favours(2, 1) == 2);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_DID_US_A_SERVICE) == intrigue::SERVICE_OPINION);
    intrigue::On_Council_Vote(2, -1, 3, 1);  // abstains
    DOCTEST_CHECK(intrigue::Favours(2, 1) == 1);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_ABANDONED_US) == intrigue::REFUSED_OPINION);
    intrigue::favours = false;
    DOCTEST_CHECK(intrigue::Council_Vote_Bias(2, 1) == 0);
}

DOCTEST_TEST_CASE("a favour is called in for a war: the debtor joins or refuses") {
    s_intrigue_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].treaty[3] = state.players[3].treaty[1] = TREATY_WAR;
    intrigue::Owe(2, 1);
    intrigue::Owe(0, 1);
    state.players[0].relations[3] = 60;  // likes the enemy: refuses
    MOX::stardate = 35008;
    intrigue::Turn();
    DOCTEST_CHECK(state.players[2].treaty[3] >= TREATY_WAR);
    DOCTEST_CHECK(state.players[0].treaty[3] < TREATY_WAR);
    DOCTEST_CHECK(intrigue::Favours(2, 1) == 0);
    DOCTEST_CHECK(intrigue::Favours(0, 1) == 0);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_ABANDONED_US) == intrigue::REFUSED_OPINION);
}

DOCTEST_TEST_CASE("the fate of a caught spy follows relations; a human keeps the classic fate") {
    s_intrigue_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[2].relations[1] = -40;
    DOCTEST_CHECK(intrigue::Decide_Fate(2, 1, 100) == intrigue::FATE_EXECUTED);
    state.players[2].relations[1] = 30;
    DOCTEST_CHECK(intrigue::Decide_Fate(2, 1, 100) == intrigue::FATE_SENT_HOME);
    state.players[2].relations[1] = 0;
    DOCTEST_CHECK(intrigue::Decide_Fate(2, 1, 1) == intrigue::FATE_EXPELLED);  // no spy network technology
    state.players[2].tech_applications[TECH_APP_SPY_NETWORK] = TECH_RESEARCH_STATUS_RESEARCHED;
    DOCTEST_CHECK(intrigue::Decide_Fate(2, 1, 1) == intrigue::FATE_TURNED);
    DOCTEST_CHECK(intrigue::Decide_Fate(2, 1, 100) == intrigue::FATE_EXPELLED);
    state.players[2].objectives = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_CHECK(intrigue::Decide_Fate(2, 1, 1) == intrigue::FATE_CLASSIC);
}

DOCTEST_TEST_CASE("the fates: execution, expulsion, home, a double agent") {
    s_intrigue_scope scope;
    test::s_game_state state;
    Setup_(&state);
    intrigue::Apply_Fate(2, 1, intrigue::FATE_EXECUTED);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_TREATED_OUR_SPY) == intrigue::EXECUTION_OPINION);

    state.players[1].spies[2] = 3;
    state.players[1].spies[1] = 1;
    intrigue::Apply_Fate(2, 1, intrigue::FATE_EXPELLED);
    DOCTEST_CHECK(state.players[1].spies[2] == 0);
    DOCTEST_CHECK(state.players[1].spies[1] == 4);
    DOCTEST_CHECK(state.players[1].stop_spying_duration[2] == intrigue::EXPEL_TURNS);

    intrigue::Apply_Fate(3, 0, intrigue::FATE_SENT_HOME);
    DOCTEST_CHECK(state.players[0].spies[0] == 1);
    DOCTEST_CHECK(intrigue::Favours(0, 3) == 1);
    DOCTEST_CHECK(opinion::Modifier(0, 3, opinion::REASON_TREATED_OUR_SPY) == intrigue::RETURN_OPINION);

    intrigue::Apply_Fate(2, 0, intrigue::FATE_TURNED);
    DOCTEST_CHECK(intrigue::Has_Double_Agent(2, 0));
    DOCTEST_CHECK(intrigue::Double_Agent_Difficulty(0, 2) == intrigue::DOUBLE_AGENT_DIFFICULTY);
    DOCTEST_CHECK(intrigue::Double_Agent_Difficulty(2, 0) == 0);
    MOX::stardate = 35001 + intrigue::DOUBLE_AGENT_TURNS;
    DOCTEST_CHECK_FALSE(intrigue::Has_Double_Agent(2, 0));
}

DOCTEST_TEST_CASE("caught spies wait for the end of the spy step") {
    s_intrigue_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[2].relations[1] = -40;
    intrigue::On_Spy_Caught(2, 1);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_TREATED_OUR_SPY) == 0);
    intrigue::Turn();
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_TREATED_OUR_SPY) == intrigue::EXECUTION_OPINION);
    int32_t count = -1;
    intrigue::Caught_Queue(&count);
    DOCTEST_CHECK(count == 0);
}

DOCTEST_TEST_CASE("a proxy war: the payer pays, the hired empire declares war, the spied enemy learns") {
    s_intrigue_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].bc = 1000;
    state.players[0].treaty[3] = state.players[3].treaty[0] = TREATY_WAR;
    state.players[1].relations[3] = -5;   // not disliked enough
    state.players[2].relations[3] = -40;
    state.players[3].spies[0] = 1;
    MOX::stardate = 35003;
    intrigue::Turn();
    DOCTEST_CHECK(state.players[0].bc == 800);
    DOCTEST_CHECK(state.players[2].bc == 200);
    DOCTEST_CHECK(state.players[2].treaty[3] >= TREATY_WAR);
    DOCTEST_CHECK(state.players[1].treaty[3] < TREATY_WAR);
    DOCTEST_CHECK(opinion::Modifier(3, 0, opinion::REASON_PLOTTED_AGAINST_US) == intrigue::PLOTTED_OPINION);
    DOCTEST_CHECK_FALSE(intrigue::Hire_Proxy(0, 1, 3, 5000));  // too poor
}

DOCTEST_TEST_CASE("a forged letter between allies: believed it breaks the alliance, exposed it costs a spy") {
    s_intrigue_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[1].treaty[2] = state.players[2].treaty[1] = TREATY_ALLIANCE;
    state.players[0].spies[1] = 4;
    espionage::State()->points[0][1] = 60;
    DOCTEST_REQUIRE(intrigue::Forge_Letter_With_Dice(0, 1, 2, TRIUMPH));
    DOCTEST_CHECK(espionage::Points(0, 1) == 35);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_PLOTTED_AGAINST_US) == intrigue::FORGED_OPINION);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_PLOTTED_AGAINST_US) == intrigue::FORGED_OPINION);
    DOCTEST_CHECK(state.players[1].treaty[2] == 0);
    DOCTEST_CHECK_FALSE(intrigue::Forge_Letter_With_Dice(0, 1, 2, CATASTROPHE));
    DOCTEST_CHECK(state.players[0].spies[1] == 3);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_PLOTTED_AGAINST_US) == intrigue::PLOTTED_OPINION);
    DOCTEST_CHECK(opinion::Modifier(2, 0, opinion::REASON_PLOTTED_AGAINST_US) == intrigue::PLOTTED_OPINION);
    DOCTEST_CHECK_FALSE(intrigue::Forge_Letter_With_Dice(0, 1, 2, TRIUMPH));  // 10 points left: too few
}

// R2, piece A (human-ui.md section 5): a human catcher chooses the fate of a spy; a human whose ally is the
// subject of a forged letter chooses whether to believe it.
DOCTEST_TEST_CASE("a human catcher chooses the fate of a spy; off: as before; a computer is unaffected") {
    struct s_language_scope {
        test::s_saved_value<uint8_t> language{text_language::language};
    } language_scope;
    s_intrigue_scope scope;
    test::s_saved_value<bool> choices_on{choices::enabled};
    test::s_saved_value<bool> prison{hostages::spy_prison};
    choices::Clear();
    hostages::Clear();
    test::s_game_state state;
    Setup_(&state);
    test::Load_Base_Texts();
    state.players[2].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.players[1].spies[2] = 3;
    state.players[1].spies[1] = 1;
    // Setting off: nothing is queued, the human's spy fate is the classic one.
    choices::enabled = false;
    intrigue::On_Spy_Caught(2, 1);
    intrigue::Turn();
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(state.players[1].spies[2] == 3);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_TREATED_OUR_SPY) == 0);
    // On: a pending choice and no effect (and no jail even with the prison on).
    choices::enabled = true;
    hostages::spy_prison = true;
    intrigue::On_Spy_Caught(2, 1);
    DOCTEST_CHECK(hostages::Spies_Held(2, 1) == 0);
    intrigue::Turn();
    DOCTEST_REQUIRE(choices::Count(2) == 1);
    DOCTEST_CHECK(hostages::Spies_Held(2, 1) == 0);
    DOCTEST_CHECK(state.players[1].spies[2] == 3);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_TREATED_OUR_SPY) == 0);
    const choices::s_choice fate = choices::State()->choice[0];
    text_language::language = text_language::LANGUAGE_EN;
    choices::s_text en;
    DOCTEST_REQUIRE(intrigue::Choice_Text(fate, &en));
    DOCTEST_CHECK(en.count == 3);
    DOCTEST_CHECK(std::string(en.option[0]) == "Execute the spy (their opinion of you -15)");
    DOCTEST_CHECK(std::string(en.option[2]).find("Send the spy home") == 0);  // no spy network technology
    text_language::language = text_language::LANGUAGE_RU;
    choices::s_text ru;
    DOCTEST_REQUIRE(intrigue::Choice_Text(fate, &ru));
    DOCTEST_CHECK(ru.count == 3);
    for (const std::string all : {std::string(ru.title) + ru.body + ru.option[0] + ru.option[1] + ru.option[2]}) {
        DOCTEST_CHECK(all.find('{') == std::string::npos);
        DOCTEST_CHECK(all.find('?') == std::string::npos);
    }
    // Each option does what a computer's fate does.
    DOCTEST_REQUIRE(choices::Apply_Pending(2, 0));
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_TREATED_OUR_SPY) == intrigue::EXECUTION_OPINION);
    DOCTEST_CHECK(choices::Empty());
    intrigue::On_Spy_Caught(2, 1);
    intrigue::Turn();
    DOCTEST_REQUIRE(choices::Apply_Pending(2, 1));  // expel
    DOCTEST_CHECK(state.players[1].spies[2] == 0);
    DOCTEST_CHECK(state.players[1].spies[1] == 4);
    DOCTEST_CHECK(state.players[1].stop_spying_duration[2] == intrigue::EXPEL_TURNS);
    intrigue::On_Spy_Caught(2, 1);
    intrigue::Turn();
    DOCTEST_REQUIRE(choices::Apply_Pending(2, 2));  // no technology: send home
    DOCTEST_CHECK(state.players[1].spies[1] == 5);
    DOCTEST_CHECK(intrigue::Favours(1, 2) == 1);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_TREATED_OUR_SPY) == intrigue::EXECUTION_OPINION + intrigue::RETURN_OPINION);
    // The spy network: the third option turns the spy, once; with a double agent already working it sends home.
    state.players[2].tech_applications[TECH_APP_SPY_NETWORK] = TECH_RESEARCH_STATUS_RESEARCHED;
    intrigue::On_Spy_Caught(2, 1);
    intrigue::Turn();
    text_language::language = text_language::LANGUAGE_EN;
    DOCTEST_REQUIRE(intrigue::Choice_Text(choices::State()->choice[0], &en));
    DOCTEST_CHECK(std::string(en.option[2]).find("Turn the spy") == 0);
    DOCTEST_REQUIRE(choices::Apply_Pending(2, 2));
    DOCTEST_CHECK(intrigue::Has_Double_Agent(2, 1));
    intrigue::On_Spy_Caught(2, 1);
    intrigue::Turn();
    DOCTEST_REQUIRE(intrigue::Choice_Text(choices::State()->choice[0], &en));
    DOCTEST_CHECK(std::string(en.option[2]).find("Send the spy home") == 0);
    // A gone attacker drops the choice.
    const choices::s_choice gone = choices::State()->choice[0];
    state.players[1].eliminated = 1;
    DOCTEST_CHECK_FALSE(intrigue::Choice_Text(gone, &en));
    state.players[1].eliminated = 0;
    choices::Clear();
    // A full queue: the spy goes to the prison, as for a human today.
    for (int i = 0; i < choices::MAX_PENDING; ++i) {
        choices::s_choice filler;
        filler.module = choices::MODULE_INCIDENTS;
        filler.player = 3;
        DOCTEST_REQUIRE(choices::Add(filler));
    }
    intrigue::On_Spy_Caught(2, 1);
    intrigue::Turn();
    DOCTEST_CHECK(choices::Count(2) == 0);
    DOCTEST_CHECK(hostages::Spies_Held(2, 1) == 1);
    choices::Clear();
    // A computer catcher decides at once, with no choice queued.
    state.players[2].objectives = PLAYER_OBJECTIVE_MILITARIST;
    state.players[2].relations[1] = -40;
    intrigue::On_Spy_Caught(2, 1);
    intrigue::Turn();
    DOCTEST_CHECK(choices::Empty());
    hostages::Clear();
}

DOCTEST_TEST_CASE("a forged letter about a human's ally: believe it (break the alliance) or dismiss it") {
    struct s_language_scope {
        test::s_saved_value<uint8_t> language{text_language::language};
    } language_scope;
    s_intrigue_scope scope;
    test::s_saved_value<bool> choices_on{choices::enabled};
    choices::Clear();
    test::s_game_state state;
    Setup_(&state);
    test::Load_Base_Texts();
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.players[1].treaty[2] = state.players[2].treaty[1] = TREATY_ALLIANCE;
    state.players[0].spies[1] = 4;
    // Setting off: the letter changes nothing for the human but the opinions, as before.
    choices::enabled = false;
    espionage::State()->points[0][1] = 60;
    DOCTEST_REQUIRE(intrigue::Forge_Letter_With_Dice(0, 1, 2, TRIUMPH));
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(state.players[1].treaty[2] == TREATY_ALLIANCE);
    // On: a pending choice, the alliance stands until it is answered.
    choices::enabled = true;
    espionage::State()->points[0][1] = 60;
    DOCTEST_REQUIRE(intrigue::Forge_Letter_With_Dice(0, 1, 2, TRIUMPH));
    DOCTEST_REQUIRE(choices::Count(1) == 1);
    DOCTEST_CHECK(state.players[1].treaty[2] == TREATY_ALLIANCE);
    const choices::s_choice letter = choices::State()->choice[0];
    text_language::language = text_language::LANGUAGE_EN;
    choices::s_text en;
    DOCTEST_REQUIRE(intrigue::Choice_Text(letter, &en));
    DOCTEST_CHECK(en.count == 2);
    DOCTEST_CHECK(std::string(en.option[0]).find("break the alliance") != std::string::npos);
    text_language::language = text_language::LANGUAGE_RU;
    choices::s_text ru;
    DOCTEST_REQUIRE(intrigue::Choice_Text(letter, &ru));
    const std::string all = std::string(ru.title) + ru.body + ru.option[0] + ru.option[1];
    DOCTEST_CHECK(all.find('{') == std::string::npos);
    DOCTEST_CHECK(all.find('?') == std::string::npos);
    DOCTEST_REQUIRE(choices::Apply_Pending(1, 1));  // dismiss
    DOCTEST_CHECK(state.players[1].treaty[2] == TREATY_ALLIANCE);
    espionage::State()->points[0][1] = 60;
    DOCTEST_REQUIRE(intrigue::Forge_Letter_With_Dice(0, 1, 2, TRIUMPH));
    DOCTEST_REQUIRE(choices::Apply_Pending(1, 0));  // believe: what a computer does
    DOCTEST_CHECK(state.players[1].treaty[2] == 0);
    DOCTEST_CHECK(state.players[2].treaty[1] == 0);
    // No alliance or pact left (or a gone partner): nothing to ask, and a stale choice drops.
    espionage::State()->points[0][1] = 60;
    DOCTEST_REQUIRE(intrigue::Forge_Letter_With_Dice(0, 1, 2, TRIUMPH));
    DOCTEST_CHECK(choices::Empty());
    state.players[1].treaty[2] = state.players[2].treaty[1] = TREATY_ALLIANCE;
    espionage::State()->points[0][1] = 60;
    DOCTEST_REQUIRE(intrigue::Forge_Letter_With_Dice(0, 1, 2, TRIUMPH));
    const choices::s_choice stale = choices::State()->choice[0];
    state.players[2].eliminated = 1;
    DOCTEST_CHECK_FALSE(intrigue::Choice_Text(stale, &en));
    state.players[2].eliminated = 0;
    state.players[1].treaty[2] = state.players[2].treaty[1] = 0;
    DOCTEST_CHECK_FALSE(intrigue::Choice_Text(stale, &en));
    choices::Clear();
    // A computer ally is unaffected: it breaks the alliance on its own (relations -30), nothing queued.
    state.players[1].objectives = PLAYER_OBJECTIVE_MILITARIST;
    state.players[1].treaty[2] = state.players[2].treaty[1] = TREATY_ALLIANCE;
    espionage::State()->points[0][1] = 60;
    DOCTEST_REQUIRE(intrigue::Forge_Letter_With_Dice(0, 1, 2, TRIUMPH));
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK(state.players[1].treaty[2] == 0);
}

DOCTEST_TEST_CASE("off: nothing happens and nothing is kept") {
    s_intrigue_scope scope;
    test::s_game_state state;
    Setup_(&state);
    intrigue::reveal_plans = false;
    intrigue::forged_letters = false;
    intrigue::spy_fate = false;
    intrigue::proxy_wars = false;
    intrigue::favours = false;
    DOCTEST_CHECK_FALSE(intrigue::Any_Enabled());
    state.players[0].sneak_attack_planet = 5;
    state.players[0].sneak_attack_player = 2;
    state.players[2].spies[0] = 2;
    intrigue::On_Spy_Caught(2, 1);
    intrigue::Owe(2, 1);
    intrigue::Turn();
    DOCTEST_CHECK(state.players[0].sneak_attack_planet == 5);
    DOCTEST_CHECK(intrigue::Empty());
    DOCTEST_CHECK(intrigue::Decide_Fate(2, 1, 1) == intrigue::FATE_CLASSIC);
    DOCTEST_CHECK_FALSE(intrigue::Hire_Proxy(0, 1, 3, 100));
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_intrigue_scope scope;
    test::s_game_state state;
    Setup_(&state);
    intrigue::Owe(2, 1);
    intrigue::State()->revealed[0][2] = 35001;
    intrigue::State()->double_agent_until[2][0] = 35031;
    auto buffer = std::make_unique<uint8_t[]>(intrigue::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), intrigue::Save_Size(), "test overflow");
    intrigue::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == intrigue::Save_Size());
    intrigue::Clear();
    SERIAL::Reader_ reader(buffer.get(), intrigue::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(intrigue::Parse_Save(&reader, &error));
    DOCTEST_CHECK(intrigue::Empty());
    intrigue::Commit_Save();
    DOCTEST_CHECK(intrigue::Favours(2, 1) == 1);
    DOCTEST_CHECK(intrigue::State()->revealed[0][2] == 35001);
    DOCTEST_CHECK(intrigue::State()->double_agent_until[2][0] == 35031);
    buffer[2 + 2 * 8 + 1] = 9;  // 9 favours from 2 to 1
    SERIAL::Reader_ bad(buffer.get(), intrigue::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(intrigue::Parse_Save(&bad, &error));
    DOCTEST_CHECK(intrigue::Favours(2, 1) == 1);  // a failed load keeps the state
}

}
