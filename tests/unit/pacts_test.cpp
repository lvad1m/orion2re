#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "../fixtures/texts_fixture.h"
#include "game/advisor.h"
#include "game/advisor_directive.h"
#include "game/choices.h"
#include "game/diplomacy_memory.h"
#include "game/opinion.h"
#include "game/pacts.h"
#include "game/reputation.h"
#include "game/serialization.h"

#include <memory>

namespace {
    struct s_pacts_scope {
        test::s_saved_value<bool> enabled{pacts::enabled};
        test::s_saved_value<int32_t> period{pacts::period};
        test::s_saved_value<int32_t> opinion_min{pacts::opinion_min};
        test::s_saved_value<int32_t> defended{pacts::defended_opinion};
        test::s_saved_value<int32_t> abandoned{pacts::abandoned_opinion};
        test::s_saved_value<int32_t> guarantees_max{pacts::guarantees_max};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<bool> memory_enabled{diplomemory::enabled};
        test::s_saved_value<int32_t> weary_limit{diplomemory::weary_limit};
        test::s_saved_value<bool> advisor_enabled{advisor::enabled};
        test::s_saved_value<int32_t> active_players{advisor::active_players};

        s_pacts_scope() {
            pacts::Clear();
            opinion::Clear();
            advisor::Clear_All_Directives();
            pacts::enabled = true;
            pacts::period = 10;
            pacts::opinion_min = 20;
            pacts::defended_opinion = 25;
            pacts::abandoned_opinion = -40;
            pacts::guarantees_max = 2;
            opinion::enabled = true;
            diplomemory::enabled = false;
            diplomemory::weary_limit = 0;
            advisor::enabled = false;
            advisor::active_players = 255;
        }
        ~s_pacts_scope() {
            pacts::Clear();
            opinion::Clear();
            advisor::Clear_All_Directives();
        }
    };

    constexpr int8_t TREATY_WAR = 4;

    // Five computer empires in contact and in range of each other, at a review turn.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 5;
        MOX::stardate = 35010;
        for (int i = 0; i < 5; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            for (int j = 0; j < 5; ++j) {
                state->players[i].contact[j] = i == j ? 0 : 1;
                state->players[i].within_range[j] = i == j ? 0 : 1;
            }
        }
    }

    void Like_(test::s_game_state* state, int a, int b, int value) {
        state->players[a].relations[b] = static_cast<int8_t>(value);
        state->players[b].relations[a] = static_cast<int8_t>(value);
    }

    // 1 and 2 like each other and both dislike 3, whose fleet matches each of theirs.
    void Common_Threat_(test::s_game_state* state) {
        Like_(state, 1, 2, 30);
        state->players[1].relations[3] = -10;
        state->players[2].relations[3] = -10;
        state->players[3].total_ship_strength[1] = 100;
        state->players[1].total_ship_strength[3] = 50;
        state->players[3].total_ship_strength[2] = 100;
        state->players[2].total_ship_strength[3] = 50;
    }
} // namespace

DOCTEST_TEST_SUITE("defence pacts") {

DOCTEST_TEST_CASE("empires facing a common threat sign a pact; a strong empire guarantees a weak neighbour it values") {
    s_pacts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Common_Threat_(&state);
    // 0 values 2 and is twice as strong against it; 0 dislikes 3, which outmatches 2.
    state.players[0].relations[2] = 40;
    state.players[0].relations[3] = -5;
    state.players[0].total_ship_strength[2] = 300;
    state.players[2].total_ship_strength[0] = 100;
    pacts::End_Of_Turn();
    DOCTEST_CHECK(pacts::Has_Pact(1, 2));
    DOCTEST_CHECK(pacts::Has_Pact(2, 1));
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 3));
    DOCTEST_CHECK(pacts::Guarantees(0, 2));
    DOCTEST_CHECK_FALSE(pacts::Guarantees(2, 0));
    MOX::stardate = 35011;  // not a review turn
    pacts::Clear();
    pacts::End_Of_Turn();
    DOCTEST_CHECK(pacts::Empty());
}

DOCTEST_TEST_CASE("no pact without the opinion, the threat, or the advisor's leave") {
    s_pacts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Common_Threat_(&state);
    state.players[2].relations[1] = 10;  // below pacts_opinion
    pacts::End_Of_Turn();
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
    state.players[2].relations[1] = 30;
    state.players[3].total_ship_strength[2] = 10;  // 3 no match for 2: no common threat
    MOX::stardate = 35020;
    pacts::End_Of_Turn();
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
    state.players[3].total_ship_strength[2] = 100;
    advisor::enabled = true;
    advisor::s_directive directive;
    directive.source = advisor::SOURCE_RULES;
    directive.issued_stardate = 35000;
    directive.expires_stardate = 35100;
    directive.attitude[2] = advisor::ATTITUDE_WARY;
    advisor::Set_Directive(1, &directive);
    MOX::stardate = 35030;
    pacts::End_Of_Turn();
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
    directive.attitude[2] = advisor::ATTITUDE_FRIENDLY;
    advisor::Set_Directive(1, &directive);
    MOX::stardate = 35040;
    pacts::End_Of_Turn();
    DOCTEST_CHECK(pacts::Has_Pact(1, 2));
}

DOCTEST_TEST_CASE("an attack on a partner brings its partners and guarantors into the war, without a chain reaction") {
    s_pacts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    pacts::Sign_Pact(1, 2, 3);
    pacts::Give_Guarantee(0, 1, 3);
    pacts::Sign_Pact(3, 4, -1);  // the aggressor's own partner
    state.players[3].treaty[1] = state.players[1].treaty[3] = TREATY_WAR;  // 3 attacks 1
    pacts::On_Aggression(3, 1);
    DOCTEST_CHECK(state.players[2].treaty[3] >= TREATY_WAR);
    DOCTEST_CHECK(state.players[0].treaty[3] >= TREATY_WAR);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_DEFENDED_US) == 25);
    DOCTEST_CHECK(opinion::Modifier(1, 0, opinion::REASON_DEFENDED_US) == 25);
    DOCTEST_CHECK(state.players[4].treaty[2] < TREATY_WAR);  // defensive wars call on no pacts
    DOCTEST_CHECK(state.players[4].treaty[0] < TREATY_WAR);
    pacts::On_Aggression(3, 1);  // partners already at war: nothing more
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_DEFENDED_US) == 25);
}

DOCTEST_TEST_CASE("a partner too weary for a new war stays out: remembered, and the pact ends") {
    s_pacts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    diplomemory::enabled = true;
    diplomemory::weary_limit = 10;
    // 2 is weary of its war with 4: it lost half of its fleet units (8 of 16 left).
    state.players[2].treaty[4] = state.players[4].treaty[2] = TREATY_WAR;
    MOX::NUM_SHIPS = 1;
    state.ships[0].owner = 2;
    state.ships[0].status = 0;
    state.ships[0].d.size = 3;
    state.players[2].ship_war_losses[4] = 16;
    DOCTEST_REQUIRE(diplomemory::Too_Weary_For_New_War(2));
    pacts::Sign_Pact(1, 2, 3);
    pacts::On_Aggression(3, 1);
    DOCTEST_CHECK(state.players[2].treaty[3] < TREATY_WAR);
    DOCTEST_CHECK(opinion::Modifier(1, 2, opinion::REASON_ABANDONED_US) == -40);
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
}

DOCTEST_TEST_CASE("pacts and guarantees end when their terms are gone") {
    s_pacts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Like_(&state, 1, 2, 30);
    Like_(&state, 0, 4, 30);
    pacts::Sign_Pact(1, 2, -1);
    pacts::Sign_Pact(0, 4, -1);
    pacts::Give_Guarantee(0, 3, -1);
    state.players[0].relations[3] = 30;
    state.players[0].total_ship_strength[3] = 100;
    state.players[3].total_ship_strength[0] = 10;
    state.players[2].relations[1] = -1;  // 2 fell out with 1
    state.players[4].eliminated = 1;
    pacts::End_Of_Turn();
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(0, 4));
    DOCTEST_CHECK(pacts::Guarantees(0, 3));
    state.players[3].total_ship_strength[0] = 100;  // no longer the weaker
    MOX::stardate = 35020;
    pacts::End_Of_Turn();
    DOCTEST_CHECK_FALSE(pacts::Guarantees(0, 3));
}

DOCTEST_TEST_CASE("off: nothing is signed and no one answers an attack") {
    s_pacts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    pacts::Sign_Pact(1, 2, 3);
    pacts::enabled = false;
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
    pacts::On_Aggression(3, 1);
    DOCTEST_CHECK(state.players[2].treaty[3] < TREATY_WAR);
    pacts::Clear();
    pacts::Sign_Pact(1, 2, 3);
    pacts::Give_Guarantee(0, 1, 3);
    DOCTEST_CHECK(pacts::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; a one-sided pact is refused") {
    s_pacts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    pacts::Sign_Pact(1, 2, -1);
    pacts::Give_Guarantee(0, 3, -1);
    auto buffer = std::make_unique<uint8_t[]>(pacts::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), pacts::Save_Size(), "test overflow");
    pacts::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == pacts::Save_Size());

    pacts::Clear();
    SERIAL::Reader_ reader(buffer.get(), pacts::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(pacts::Parse_Save(&reader, &error));
    DOCTEST_CHECK(pacts::Empty());  // nothing changes before the commit
    pacts::Commit_Save();
    DOCTEST_CHECK(pacts::Has_Pact(2, 1));
    DOCTEST_CHECK(pacts::Guarantees(0, 3));

    buffer[2 + 1] = 0;  // player 1's pact mask loses 2; player 2's still names 1
    SERIAL::Reader_ bad(buffer.get(), pacts::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(pacts::Parse_Save(&bad, &error));
    buffer[2 + 1] = 0x01;  // player 1 bound to... player 0, who does not name it back
    SERIAL::Reader_ bad2(buffer.get(), pacts::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(pacts::Parse_Save(&bad2, &error));
}


DOCTEST_TEST_CASE("an aggression during talks is recognised as a red wedding (diplomacy red_wedding)") {
    s_pacts_scope scope;
    test::s_game_state state;
    Setup_(&state);
    test::s_saved_value<bool> red_wedding{reputation::red_wedding};
    reputation::Clear();
    // Flag off: the hook returns at once and keeps nothing.
    reputation::red_wedding = false;
    reputation::On_Treaty_Signed(1, 2);
    pacts::On_Aggression(1, 2);
    DOCTEST_CHECK(reputation::Empty());
    // Flag on: talks between 1 and 2 this turn; 1 attacks 2.
    reputation::red_wedding = true;
    reputation::On_Treaty_Signed(1, 2);
    pacts::On_Aggression(3, 2);  // no talks between them
    DOCTEST_CHECK_FALSE(reputation::Has(3, reputation::TITLE_HOSPITALITY_BREAKER));
    pacts::On_Aggression(1, 2);
    DOCTEST_CHECK(reputation::Has(1, reputation::TITLE_HOSPITALITY_BREAKER));
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_VIOLATED_HOSPITALITY) == reputation::RED_WEDDING_VICTIM_OPINION);
    reputation::Clear();
}

}

// R2, piece C (human-ui.md section 5): a computer offers a human a defence pact or a guarantee by its own side of
// the rule; the human's answer replaces the human's opinion and beliefs.
namespace {
    struct s_choice_scope {
        test::s_saved_value<bool> choices_on{choices::enabled};
        test::s_saved_value<uint8_t> language{text_language::language};
        test::s_saved_value<int8_t> game_type{MOX::game_type};
        test::s_saved_value<decltype(NETMOX::net.is_network_active)> network{NETMOX::net.is_network_active};

        s_choice_scope() {
            choices::Clear();
            choices::enabled = true;
            MOX::game_type = 0;
            NETMOX::net.is_network_active = 0;
        }
        ~s_choice_scope() { choices::Clear(); }
    };

    // The pending offer of a kind (a guarantee is offered along with a pact when both rules hold).
    choices::s_choice Offer_Of_(uint8_t kind) {
        const choices::s_state* queue = choices::State();
        for (int32_t i = 0; i < queue->count; ++i) {
            if (queue->choice[i].module == choices::MODULE_PACTS && queue->choice[i].kind == kind) return queue->choice[i];
        }
        return choices::s_choice{};
    }

    void Check_Texts_(const choices::s_choice& c) {
        for (const uint8_t language : {text_language::LANGUAGE_EN, text_language::LANGUAGE_RU}) {
            text_language::language = language;
            choices::s_text t;
            DOCTEST_REQUIRE(pacts::Choice_Text(c, &t));
            DOCTEST_CHECK(t.count == 2);
            std::string all = std::string(t.title) + t.body + t.option[0] + t.option[1];
            DOCTEST_CHECK(all.find('{') == std::string::npos);
            DOCTEST_CHECK(all.find('?') == std::string::npos);
        }
        text_language::language = text_language::LANGUAGE_EN;
    }
}

DOCTEST_TEST_SUITE("defence pacts offered to a human") {

DOCTEST_TEST_CASE("a computer offers a human a pact against a common threat; accept signs it, refuse is not repeated for 50 turns") {
    s_pacts_scope scope;
    s_choice_scope cscope;
    test::s_game_state state;
    Setup_(&state);
    test::Load_Base_Texts();
    Common_Threat_(&state);
    state.players[2].objectives = PLAYER_OBJECTIVE_HUMAN;
    // The setting off: nothing is asked; the computers' pairs are as before.
    choices::enabled = false;
    pacts::End_Of_Turn();
    DOCTEST_CHECK(choices::Empty());
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
    // On: one pending offer from computer 1 aimed at 3 and no effect yet.
    choices::enabled = true;
    MOX::stardate = 35020;
    pacts::End_Of_Turn();
    DOCTEST_REQUIRE(choices::Count(2) == 1);
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
    const choices::s_choice ask = choices::State()->choice[0];
    DOCTEST_CHECK(ask.module == choices::MODULE_PACTS);
    DOCTEST_CHECK(ask.kind == pacts::CHOICE_PACT);
    DOCTEST_CHECK(ask.player == 2);
    DOCTEST_CHECK(ask.target == 1);
    DOCTEST_CHECK(ask.other == 3);
    Check_Texts_(ask);
    MOX::stardate = 35030;
    pacts::End_Of_Turn();  // asked once, not again while it waits
    DOCTEST_CHECK(choices::Count(2) == 1);
    // Accept: the same pact a computer pair signs.
    DOCTEST_REQUIRE(choices::Apply_Pending(2, 0));
    DOCTEST_CHECK(pacts::Has_Pact(1, 2));
    DOCTEST_CHECK(pacts::Has_Pact(2, 1));
    pacts::Clear();
    // Refuse: nothing signed, and the offer is not repeated for REOFFER_TURNS turns.
    MOX::stardate = 35040;
    pacts::End_Of_Turn();
    DOCTEST_REQUIRE(choices::Count(2) == 1);
    DOCTEST_REQUIRE(choices::Apply_Pending(2, 1));
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
    MOX::stardate = 35050;
    pacts::End_Of_Turn();
    DOCTEST_CHECK(choices::Empty());
    MOX::stardate = 35090;
    pacts::End_Of_Turn();
    DOCTEST_CHECK(choices::Count(2) == 1);
}

DOCTEST_TEST_CASE("the computer's side of the rule still holds for a human partner; a gone subject makes Choice_Text false") {
    s_pacts_scope scope;
    s_choice_scope cscope;
    test::s_game_state state;
    Setup_(&state);
    test::Load_Base_Texts();
    Common_Threat_(&state);
    state.players[2].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.players[1].relations[2] = 10;  // the computer thinks too little of the human
    pacts::End_Of_Turn();
    DOCTEST_CHECK(choices::Empty());
    state.players[1].relations[2] = 30;
    // The human's own opinion of the threat is its answer to give: it may even like the threat.
    state.players[2].relations[3] = 50;
    MOX::stardate = 35020;
    pacts::End_Of_Turn();
    DOCTEST_REQUIRE(choices::Count(2) == 1);
    const choices::s_choice ask = choices::State()->choice[0];
    choices::s_text t;
    DOCTEST_CHECK(pacts::Choice_Text(ask, &t));
    state.players[1].treaty[2] = state.players[2].treaty[1] = TREATY_WAR;  // at war now
    DOCTEST_CHECK_FALSE(pacts::Choice_Text(ask, &t));
    pacts::Choice_Apply(ask, 0);
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 2));
    state.players[1].treaty[2] = state.players[2].treaty[1] = 0;
    state.players[3].eliminated = 1;  // the aim is gone
    DOCTEST_CHECK_FALSE(pacts::Choice_Text(ask, &t));
    state.players[3].eliminated = 0;
    DOCTEST_CHECK(pacts::Choice_Text(ask, &t));
    // A human is not a network game's player: no offer.
    choices::Clear();
    MOX::game_type = 2;
    MOX::stardate = 35090;
    pacts::End_Of_Turn();
    DOCTEST_CHECK(choices::Empty());
}

DOCTEST_TEST_CASE("a computer guarantees a weak human it values; a full guarantor's offer lapses; a full queue gives it as today") {
    s_pacts_scope scope;
    s_choice_scope cscope;
    test::s_game_state state;
    Setup_(&state);
    test::Load_Base_Texts();
    state.players[2].objectives = PLAYER_OBJECTIVE_HUMAN;
    state.players[0].relations[2] = 40;
    state.players[0].relations[3] = -5;
    state.players[0].total_ship_strength[2] = 300;
    state.players[2].total_ship_strength[0] = 100;
    state.players[3].total_ship_strength[2] = 100;
    state.players[2].total_ship_strength[3] = 50;
    pacts::End_Of_Turn();
    DOCTEST_CHECK(choices::Count(2) == 2);  // the pact against the same threat is offered too
    const choices::s_choice ask = Offer_Of_(pacts::CHOICE_GUARANTEE);
    DOCTEST_CHECK(ask.kind == pacts::CHOICE_GUARANTEE);
    DOCTEST_CHECK(ask.target == 0);
    DOCTEST_CHECK(ask.other == 3);
    Check_Texts_(ask);
    DOCTEST_CHECK_FALSE(pacts::Guarantees(0, 2));
    // The guarantor's slots fill up with computer wards before the human answers: the offer lapses.
    pacts::guarantees_max = 1;
    pacts::Give_Guarantee(0, 1, 3);
    choices::s_text t;
    DOCTEST_CHECK_FALSE(pacts::Choice_Text(ask, &t));
    pacts::End_Guarantee(0, 1, "test");
    DOCTEST_CHECK(pacts::Choice_Text(ask, &t));
    DOCTEST_REQUIRE(choices::Apply_Pending(2, 0));
    DOCTEST_CHECK(pacts::Guarantees(0, 2));
    DOCTEST_CHECK_FALSE(pacts::Guarantees(2, 0));
    // A full queue: no offer, nothing happens (as before the setting).
    pacts::Clear();
    choices::Clear();
    MOX::stardate = 35100;
    for (int i = 0; i < choices::MAX_PENDING; ++i) {
        choices::s_choice filler;
        filler.module = choices::MODULE_INCIDENTS;
        filler.player = 2;
        DOCTEST_REQUIRE(choices::Add(filler));
    }
    pacts::End_Of_Turn();
    DOCTEST_CHECK(choices::Count(2) == choices::MAX_PENDING);
    DOCTEST_CHECK(pacts::Guarantees(0, 2));  // the human gets the guarantee as today
}

DOCTEST_TEST_CASE("a human in the game changes no pair of computers; a game without a human makes no offer") {
    s_pacts_scope scope;
    s_choice_scope cscope;
    test::s_game_state state;
    Setup_(&state);
    Common_Threat_(&state);
    // Computers only, the setting on: the same pacts as with the setting off, and nothing queued.
    pacts::End_Of_Turn();
    DOCTEST_CHECK(pacts::Has_Pact(1, 2));
    DOCTEST_CHECK(choices::Empty());
    // Player 4 human, indifferent to everyone: computers 1 and 2 sign as before.
    pacts::Clear();
    state.players[4].objectives = PLAYER_OBJECTIVE_HUMAN;
    MOX::stardate = 35020;
    pacts::End_Of_Turn();
    DOCTEST_CHECK(pacts::Has_Pact(1, 2));
    DOCTEST_CHECK_FALSE(pacts::Has_Pact(1, 4));
    DOCTEST_CHECK(choices::Empty());
}

}

DOCTEST_TEST_SUITE("guarantees to a human keep the computer's picks") {

DOCTEST_TEST_CASE("a human ward asked instead of guaranteed keeps its slot: the computer's other pick is the one it made before") {
    s_pacts_scope scope;
    s_choice_scope cscope;
    test::s_game_state state;
    Setup_(&state);
    test::Load_Base_Texts();
    pacts::guarantees_max = 1;
    // Computer 0 values 1 and 2 alike and is twice as strong as each; 3 threatens both.
    for (int w = 1; w <= 2; ++w) {
        state.players[0].relations[w] = 40;
        state.players[0].total_ship_strength[w] = 300;
        state.players[w].total_ship_strength[0] = 100;
        state.players[3].total_ship_strength[w] = 100;
        state.players[w].total_ship_strength[3] = 50;
    }
    state.players[0].relations[3] = -5;
    // Without a human: the first ward in the order (1) is guaranteed, 2 is not.
    pacts::End_Of_Turn();
    DOCTEST_CHECK(pacts::Guarantees(0, 1));
    DOCTEST_CHECK_FALSE(pacts::Guarantees(0, 2));
    DOCTEST_CHECK(choices::Empty());
    // Ward 1 human, the setting on: 1 is asked, 2 is still not guaranteed (the same pick as before).
    pacts::Clear();
    state.players[1].objectives = PLAYER_OBJECTIVE_HUMAN;
    MOX::stardate = 35020;
    pacts::End_Of_Turn();
    DOCTEST_CHECK_FALSE(pacts::Guarantees(0, 1));
    DOCTEST_CHECK_FALSE(pacts::Guarantees(0, 2));
    DOCTEST_CHECK(Offer_Of_(pacts::CHOICE_GUARANTEE).player == 1);
    // The setting off: the human gets the guarantee silently, as today.
    choices::Clear();
    choices::enabled = false;
    MOX::stardate = 35030;
    pacts::End_Of_Turn();
    DOCTEST_CHECK(pacts::Guarantees(0, 1));
    DOCTEST_CHECK_FALSE(pacts::Guarantees(0, 2));
    DOCTEST_CHECK(choices::Empty());
    // A full queue: the human gets the guarantee as today.
    pacts::Clear();
    choices::enabled = true;
    for (int i = 0; i < choices::MAX_PENDING; ++i) {
        choices::s_choice filler;
        filler.module = choices::MODULE_INCIDENTS;
        filler.player = 1;
        DOCTEST_REQUIRE(choices::Add(filler));
    }
    MOX::stardate = 35040;
    pacts::End_Of_Turn();
    DOCTEST_CHECK(pacts::Guarantees(0, 1));
}

}
