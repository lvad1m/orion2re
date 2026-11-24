#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "game/opinion.h"
#include "game/reputation.h"
#include "game/serialization.h"

#include <memory>
#include <string>

namespace {
    struct s_reputation_scope {
        test::s_saved_value<bool> enabled{reputation::enabled};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};

        s_reputation_scope() {
            reputation::Clear();
            opinion::Clear();
            reputation::enabled = true;
            opinion::enabled = true;
        }
        ~s_reputation_scope() {
            reputation::Clear();
            opinion::Clear();
        }
    };

    constexpr int8_t TREATY_NAP = 1;
    constexpr int8_t TREATY_ALLIANCE = 2;
    constexpr int8_t TREATY_PEACE = 3;
    constexpr int8_t TREATY_WAR = 4;

    // Four computer empires that know each other; player 0 is honorable, 1 aggressive.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 4;
        MOX::stardate = 35101;
        for (int i = 0; i < 4; ++i) {
            state->players[i].objectives = PLAYER_OBJECTIVE_MILITARIST;
            state->players[i].personality = PLAYER_PERSONALITY_XENOPHOBIC;
            for (int j = 0; j < 4; ++j) state->players[i].contact[j] = i == j ? 0 : 1;
        }
        state->players[0].personality = PLAYER_PERSONALITY_HONORABLE;
        state->players[1].personality = PLAYER_PERSONALITY_AGGRESSIVE;
    }
} // namespace

DOCTEST_TEST_SUITE("stains and titles") {

DOCTEST_TEST_CASE("an oathbreaker: a broken alliance at once, other treaties twice in 50 turns") {
    s_reputation_scope scope;
    test::s_game_state state;
    Setup_(&state);
    reputation::On_Treaty_Broken(2, 3, TREATY_PEACE);
    DOCTEST_CHECK_FALSE(reputation::Has(2, reputation::TITLE_OATHBREAKER));
    DOCTEST_CHECK(opinion::Modifier(3, 2, opinion::REASON_BROKE_ITS_WORD) == reputation::BROKE_WORD_OPINION);
    MOX::stardate = 35130;
    reputation::On_Treaty_Broken(2, 1, TREATY_NAP);
    DOCTEST_CHECK(reputation::Has(2, reputation::TITLE_OATHBREAKER));
    DOCTEST_CHECK(reputation::Relations_Bias(0, 2) == -30);  // honorable
    DOCTEST_CHECK(reputation::Relations_Bias(3, 2) == -20);
    reputation::On_Treaty_Broken(3, 1, TREATY_ALLIANCE);
    DOCTEST_CHECK(reputation::Has(3, reputation::TITLE_OATHBREAKER));
    reputation::On_Treaty_Broken(1, 0, 0);  // no treaty: no breach
    DOCTEST_CHECK(reputation::State()->breaches[1][0] == 0);
    MOX::stardate = 35130 + reputation::OATHBREAKER_TURNS;
    DOCTEST_CHECK_FALSE(reputation::Has(2, reputation::TITLE_OATHBREAKER));
}

DOCTEST_TEST_CASE("the North remembers: an atrocity is a title and a slow memory of everyone in contact") {
    s_reputation_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[2].contact[1] = 0;  // player 2 does not know player 1
    reputation::On_Atrocity(1);
    DOCTEST_CHECK(reputation::Has(1, reputation::TITLE_ATROCITIES));
    DOCTEST_CHECK(opinion::Modifier(0, 1, opinion::REASON_ATROCITY) == reputation::ATROCITY_OPINION);
    DOCTEST_CHECK(opinion::Modifier(2, 1, opinion::REASON_ATROCITY) == 0);
    DOCTEST_CHECK(reputation::Relations_Bias(0, 1) == -20);
    DOCTEST_CHECK(reputation::Relations_Bias(3, 1) == -10);
    reputation::On_Leader_Killed(3);
    DOCTEST_CHECK(reputation::Has(3, reputation::TITLE_KINGSLAYER));
}

DOCTEST_TEST_CASE("good conduct: keeps its word and peacemaker, reviewed every 10 turns") {
    s_reputation_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].treaty[1] = TREATY_PEACE;
    state.players[0].treaty[2] = TREATY_NAP;
    state.players[0].peace_duration[1] = 40;
    state.players[0].peace_duration[2] = 35;
    // Player 3 makes peace twice: at war, then not, twice.
    state.players[3].treaty[1] = state.players[1].treaty[3] = TREATY_WAR;
    MOX::stardate = 35101;
    reputation::Turn();
    state.players[3].treaty[1] = state.players[1].treaty[3] = TREATY_PEACE;
    MOX::stardate = 35102;
    reputation::Turn();
    state.players[3].treaty[2] = state.players[2].treaty[3] = TREATY_WAR;
    MOX::stardate = 35103;
    reputation::Turn();
    state.players[3].treaty[2] = state.players[2].treaty[3] = TREATY_PEACE;
    MOX::stardate = 35104;
    reputation::Turn();
    MOX::stardate = 35110;
    reputation::Turn();
    DOCTEST_CHECK(reputation::Has(0, reputation::TITLE_KEEPS_WORD));
    DOCTEST_CHECK(reputation::Has(3, reputation::TITLE_PEACEMAKER));
    DOCTEST_CHECK_FALSE(reputation::Has(2, reputation::TITLE_KEEPS_WORD));
    DOCTEST_CHECK(reputation::Relations_Bias(1, 0) == 10);
    reputation::On_Treaty_Broken(0, 1, TREATY_PEACE);
    DOCTEST_CHECK_FALSE(reputation::Has(0, reputation::TITLE_KEEPS_WORD));
}

DOCTEST_TEST_CASE("style titles: the one leader of the galaxy in trade, fleet and raids") {
    s_reputation_scope scope;
    test::s_game_state state;
    Setup_(&state);
    for (int o : {0, 1, 3}) state.players[2].trade_treaty[o] = 1;
    state.players[1].total_ship_strength[0] = 3000;
    state.players[0].total_ship_strength[1] = 1000;
    for (int i = 0; i < 4; ++i) reputation::Note_Deed(3);
    reputation::Note_Deed(0);
    MOX::stardate = 35125;
    reputation::Turn();
    DOCTEST_CHECK(reputation::Has(2, reputation::TITLE_TRADE_PRINCE));
    DOCTEST_CHECK(reputation::Has(1, reputation::TITLE_WARLORD));
    DOCTEST_CHECK(reputation::Has(3, reputation::TITLE_CORSAIR));
    DOCTEST_CHECK(reputation::Relations_Bias(1, 1) == 0);
    DOCTEST_CHECK(reputation::Relations_Bias(0, 1) == -5);  // a warlord worries the peaceable
    DOCTEST_CHECK(reputation::Relations_Bias(3, 1) == -5);
    state.players[0].total_ship_strength[1] = 2500;  // no longer half again as strong as the second
    MOX::stardate = 35150;
    reputation::Turn();
    DOCTEST_CHECK_FALSE(reputation::Has(1, reputation::TITLE_WARLORD));
}

DOCTEST_TEST_CASE("off or a human holder: no titles, no bias") {
    s_reputation_scope scope;
    test::s_game_state state;
    Setup_(&state);
    reputation::On_Atrocity(1);
    state.players[0].objectives = PLAYER_OBJECTIVE_HUMAN;
    DOCTEST_CHECK(reputation::Relations_Bias(0, 1) == 0);
    reputation::enabled = false;
    DOCTEST_CHECK_FALSE(reputation::Has(1, reputation::TITLE_ATROCITIES));
    DOCTEST_CHECK(reputation::Relations_Bias(2, 1) == 0);
    reputation::Clear();
    reputation::On_Treaty_Broken(2, 3, TREATY_ALLIANCE);
    reputation::Note_Deed(2);
    MOX::stardate = 35125;
    reputation::Turn();
    DOCTEST_CHECK(reputation::Empty());
}

DOCTEST_TEST_CASE("the save payload round-trips; bad values are refused") {
    s_reputation_scope scope;
    test::s_game_state state;
    Setup_(&state);
    reputation::On_Treaty_Broken(2, 3, TREATY_ALLIANCE);
    reputation::Note_Deed(1);
    auto buffer = std::make_unique<uint8_t[]>(reputation::Save_Size());
    SERIAL::Writer_ writer(buffer.get(), reputation::Save_Size(), "test overflow");
    reputation::Write_Save(&writer);
    DOCTEST_REQUIRE(writer.Length() == reputation::Save_Size());
    reputation::Clear();
    SERIAL::Reader_ reader(buffer.get(), reputation::Save_Size(), "test underflow", true);
    std::string error;
    DOCTEST_REQUIRE(reputation::Parse_Save(&reader, &error));
    DOCTEST_CHECK(reputation::Empty());
    reputation::Commit_Save();
    DOCTEST_CHECK(reputation::Has(2, reputation::TITLE_OATHBREAKER));
    DOCTEST_CHECK(reputation::State()->deeds[1] == 1);
    const uint32_t player_size = reputation::TITLE_COUNT * 4 + 8 + 8 + 2 + 1;
    buffer[2 + 3 * player_size + 3] = 0x80;  // player 3's oathbreaker stardate negative
    SERIAL::Reader_ bad(buffer.get(), reputation::Save_Size(), "test underflow", true);
    DOCTEST_CHECK_FALSE(reputation::Parse_Save(&bad, &error));
    DOCTEST_CHECK(reputation::Has(2, reputation::TITLE_OATHBREAKER));
}

}
