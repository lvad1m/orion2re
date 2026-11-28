#include "pch.h"

#include "doctest/doctest.h"
#include "../fixtures/game_state.h"
#include "../fixtures/texts_fixture.h"
#include "game/choices.h"
#include "game/council.h"
#include "game/crises.h"
#include "game/diplodef.h"
#include "game/hostages.h"
#include "game/mentat.h"
#include "game/texts.h"
#include "game/msg.h"
#include "game/news.h"
#include "game/opinion.h"
#include "game/random.h"
#include "game/stability.h"
#include "game/ultimatums.h"

#include <string>

namespace {
    // The mentat's settings, the game's globals it reads and its memory, put back at the end of each case.
    struct s_scope {
        test::s_saved_value<uint8_t> mode{mentat::mode};
        test::s_saved_value<uint8_t> detail{mentat::detail};
        test::s_saved_value<uint8_t> open_when{mentat::open_when};
        test::s_saved_value<int32_t> period{mentat::period};
        test::s_saved_value<bool> alerts{mentat::alerts};
        test::s_saved_value<uint8_t> language{text_language::language};
        test::s_saved_value<int16_t> council_flag{diplodef::COUNCIL_FLAG};
        test::s_saved_value<int16_t> council_next{diplodef::next_council_meeting_turn};
        test::s_saved_value<int8_t> game_type{MOX::game_type};
        test::s_saved_value<bool> situations{crises::situations};
        test::s_saved_value<bool> stability_enabled{stability::enabled};
        test::s_saved_value<int32_t> stability_threshold{stability::rebellion_threshold};
        test::s_saved_value<bool> choices_enabled{choices::enabled};
        test::s_saved_value<bool> opinion_enabled{opinion::enabled};
        test::s_saved_value<bool> news_enabled{news::enabled};

        s_scope() {
            mentat::mode = mentat::MODE_SUMMARY;
            mentat::detail = mentat::DETAIL_BRIEF;
            mentat::open_when = mentat::OPEN_CRITICAL;
            mentat::period = 5;
            mentat::alerts = true;
            text_language::language = text_language::LANGUAGE_EN;
            MOX::game_type = 0;
            test::Load_Base_Texts();
            Clear_();
        }
        ~s_scope() {
            Clear_();
            texts::Reset();
        }
        static void Clear_() {
            mentat::Clear();
            ultimatums::Clear();
            hostages::Clear();
            crises::Clear();
            choices::Clear();
            opinion::Clear();
            news::Clear();
            stability::Clear();
            memset(MSG_::first_msg, 0, sizeof(MSG_::first_msg));
            memset(MSG_::n_msgs, 0, sizeof(MSG_::n_msgs));
            memset(MSG_::msgs, 0, sizeof(MSG_::msgs));
        }
    };

    // Four empires; player 0 is the human and knows 1 and 3 (2 is unknown to it); race names Aaaa, Bbbb, Cccc, Dddd.
    // One colony of the human at star 0 (100, 100). Nobody is at war.
    void Setup_(test::s_game_state* state) {
        MOX::NUM_PLAYERS = 4;
        MOX::stardate = 35100;
        MOX::PLAYER_NUM = 0;
        MOX::NUM_SHIPS = 0;
        const char* names[4] = {"Aaaa", "Bbbb", "Cccc", "Dddd"};
        for (int i = 0; i < 4; ++i) {
            s_player& player = state->players[i];
            player.objectives = i == 0 ? PLAYER_OBJECTIVE_HUMAN : PLAYER_OBJECTIVE_MILITARIST;
            player.eliminated = 0;
            player.sneak_attack_player = -1;
            player.council_voted_for = -1;
            strcpy(player.race_name, names[i]);
            for (int j = 0; j < 4; ++j) {
                player.contact[j] = 0;
                player.treaty[j] = 3;
            }
        }
        state->players[0].contact[1] = 1;
        state->players[0].contact[3] = 1;
        state->players[1].contact[0] = 1;
        state->players[3].contact[0] = 1;
        state->players[0].bc = 500;
        state->players[0].surplus_bc = 20;
        strcpy(state->stars[0].name, "Sol");
        state->stars[0].x = 100;
        state->stars[0].y = 100;
        MOX::NUM_STARS = 1;
        state->planets[0].star_index = 0;
        state->planets[0].colony_index = 0;
        MOX::planet_count = 1;
        state->colonies[0].owner = 0;
        state->colonies[0].planet = 0;
        state->colonies[0].n_pops = 1;
        MOX::NUM_COLONIES = 1;
    }

    void War_(test::s_game_state* state, int a, int b) {
        state->players[a].treaty[b] = 4;
        state->players[b].treaty[a] = 4;
    }

    void Add_Msg_(int player, const s_MSG& msg) {
        MSG_::first_msg[player] = 0;
        MSG_::msgs[MSG_::n_msgs[player]] = msg;
        ++MSG_::n_msgs[player];
    }

    s_MSG Diplomacy_Msg_(int code, int a, int b) {
        s_MSG msg = {};
        msg.msg_type = MSG_TYPE_DIPLOMACY;
        msg.u.diplomacy.diplomacy_idx = static_cast<int8_t>(code);
        msg.u.diplomacy.plr_a = static_cast<int8_t>(a);
        msg.u.diplomacy.plr_b = static_cast<int8_t>(b);
        return msg;
    }

    // A warship of `owner` at the map point (x, y); `status` 0 in orbit at `star`.
    void Ship_(test::s_game_state* state, int index, int owner, int star, int x, int y) {
        s_ship_data& ship = state->ships[index];
        ship = s_ship_data{};
        ship.owner = static_cast<int8_t>(owner);
        ship.status = 0;
        ship.location = static_cast<int16_t>(star);
        ship.x = static_cast<int16_t>(x);
        ship.y = static_cast<int16_t>(y);
        ship.d.ship_type = SHIP_TYPE_COMBAT;
        if (index + 1 > MOX::NUM_SHIPS) MOX::NUM_SHIPS = static_cast<int16_t>(index + 1);
    }

    bool Stands_(mentat::e_alert kind, mentat::s_alert* out = nullptr) {
        mentat::s_alert alert;
        const bool stands = mentat::Check(kind, 0, &alert);
        if (out != nullptr) *out = alert;
        return stands;
    }

    // Players 0..3 with 10, 6, 3 and 2 votes (21 in all, 14 needed); a session in three turns.
    void Council_(test::s_game_state* state) {
        const int16_t population[4] = {100, 60, 30, 20};
        for (int i = 0; i < 4; ++i) state->players[i].total_pop = population[i];
        MOX::stardate = 35200;
        diplodef::COUNCIL_FLAG = 1;
        diplodef::next_council_meeting_turn = 203;
    }

    bool Has_Text_(const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    }

    std::string All_Lines_(const mentat::s_summary& summary) {
        std::string all;
        for (const mentat::s_section& section : summary.sections) {
            for (const mentat::s_text& line : section.lines) all += mentat::Utf8(line) + "\n";
        }
        for (const mentat::s_alert& alert : summary.alerts) all += mentat::Utf8(alert.text) + "\n";
        for (const mentat::s_text& line : summary.why) all += mentat::Utf8(line) + "\n";
        return all;
    }
} // namespace

DOCTEST_TEST_SUITE("mentat") {

DOCTEST_TEST_CASE("war: a war that began with you this turn") {
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_WAR));
    War_(&state, 0, 1);
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_WAR));  // the game said nothing this turn
    Add_Msg_(0, Diplomacy_Msg_(0, 0, 1));
    mentat::s_alert alert;
    DOCTEST_REQUIRE(Stands_(mentat::ALERT_WAR, &alert));
    DOCTEST_CHECK(mentat::Utf8(alert.text) == "War: Bbbb is at war with you now.");
    // A war between others, or a peace, is no alert; a war that ended again is none either.
    state.players[0].treaty[1] = 3;
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_WAR));
}

DOCTEST_TEST_CASE("fleet: a detected enemy fleet near a colony, stronger than your ships there") {
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Ship_(&state, 0, 0, 0, 100, 100);  // yours at the colony
    Ship_(&state, 1, 1, -1, 100, 100);  // theirs, at the colony's star too
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_FLEET));  // not at war
    War_(&state, 0, 1);
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_FLEET));  // one hull each: not stronger
    Ship_(&state, 2, 1, -1, 110, 100);  // a second one, near
    mentat::s_alert alert;
    DOCTEST_REQUIRE(Stands_(mentat::ALERT_FLEET, &alert));
    DOCTEST_CHECK(Has_Text_(mentat::Utf8(alert.text), "Bbbb"));
    DOCTEST_CHECK(Has_Text_(mentat::Utf8(alert.text), "Sol"));
    DOCTEST_CHECK(mentat::Utf8(alert.why) == "Near Sol: their strength 2 against your 1 there.");
    // Far away, or a friend's fleet: no alert.
    state.ships[2].x = 400;
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_FLEET));
    state.ships[2].x = 110;
    state.players[0].treaty[1] = 3;
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_FLEET));
}

DOCTEST_TEST_CASE("council: the expected vote goes against you, or you fall a few votes short") {
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Council_(&state);
    // No session close: nothing.
    diplodef::next_council_meeting_turn = 230;
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_COUNCIL_LOSE));
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_COUNCIL_SHORT));
    diplodef::next_council_meeting_turn = 203;

    // Player 0 (10 votes) is a nominee with 1 (6 votes). Member 2 is at peace with 0 and at war with 1, member 3
    // at war with 0: expected 10 + 3 = 13 votes of 14 needed, so a vote short; the rival gets 6 + 2.
    state.players[2].treaty[0] = 2;
    state.players[2].treaty[1] = 4;
    state.players[3].treaty[0] = 4;
    state.players[3].treaty[1] = 2;
    mentat::s_alert alert;
    DOCTEST_REQUIRE(Stands_(mentat::ALERT_COUNCIL_SHORT, &alert));
    DOCTEST_CHECK(mentat::Utf8(alert.text) == "Council session in 3 turns: you fall 1 votes short of the two thirds.");
    DOCTEST_CHECK(mentat::Utf8(alert.why) == "You are expected to get 13 of 21 votes, 14 are needed.");
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_COUNCIL_LOSE));

    // The rival is the bigger empire (10 votes against 6), and both members stand for it: it is elected, against 0.
    state.players[0].total_pop = 60;
    state.players[1].total_pop = 100;
    state.players[2].treaty[0] = 4;
    state.players[2].treaty[1] = 2;
    DOCTEST_REQUIRE(Stands_(mentat::ALERT_COUNCIL_LOSE, &alert));
    DOCTEST_CHECK(mentat::Utf8(alert.text) == "Council session in 3 turns: Bbbb is likely to be elected.");
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_COUNCIL_SHORT));  // 10 votes of 14: far short

    // Nobody close to two thirds: neither.
    state.players[2].treaty[1] = 4;
    state.players[3].treaty[1] = 4;
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_COUNCIL_LOSE));
}

DOCTEST_TEST_CASE("other empires' reckoning (the vote prediction, the council alerts) only at the detailed level") {
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Council_(&state);
    state.players[2].treaty[0] = 2;
    state.players[2].treaty[1] = 4;
    state.players[3].treaty[0] = 4;
    state.players[3].treaty[1] = 2;
    mentat::s_alert standing;
    DOCTEST_CHECK(mentat::Check(mentat::ALERT_COUNCIL_SHORT, 0, &standing, mentat::DETAIL_DETAILED));
    for (const uint8_t level : {mentat::DETAIL_BRIEF, mentat::DETAIL_MEDIUM}) {
        mentat::s_alert alert;
        DOCTEST_CHECK_FALSE(mentat::Check(mentat::ALERT_COUNCIL_SHORT, 0, &alert, level));
        DOCTEST_CHECK_FALSE(mentat::Check(mentat::ALERT_COUNCIL_LOSE, 0, &alert, level));
        for (const mentat::s_alert& found : mentat::Find_Alerts(0, level)) {
            DOCTEST_CHECK(found.kind != mentat::ALERT_COUNCIL_SHORT);
            DOCTEST_CHECK(found.kind != mentat::ALERT_COUNCIL_LOSE);
        }
        mentat::s_summary summary;
        mentat::Build_Summary(0, level, &summary);
        const std::string all = All_Lines_(summary);
        DOCTEST_CHECK(Has_Text_(all, "Council session in 3 turns; the nominees are Aaaa (10 votes) and Bbbb (6 votes)."));
        DOCTEST_CHECK_FALSE(Has_Text_(all, "expected"));  // no prediction
        DOCTEST_CHECK_FALSE(Has_Text_(all, "needed"));
        DOCTEST_CHECK(summary.why.empty());
    }
    mentat::s_summary detailed;
    mentat::Build_Summary(0, mentat::DETAIL_DETAILED, &detailed);
    DOCTEST_CHECK(Has_Text_(All_Lines_(detailed), "you are a nominee"));
    bool short_alert = false;
    for (const mentat::s_alert& found : detailed.alerts) short_alert = short_alert || found.kind == mentat::ALERT_COUNCIL_SHORT;
    DOCTEST_CHECK(short_alert);
    // The opinion reasons likewise.
    opinion::enabled = true;
    opinion::Add(1, 0, opinion::REASON_EMBARGO, -20);
    for (const uint8_t level : {mentat::DETAIL_BRIEF, mentat::DETAIL_MEDIUM}) {
        mentat::s_summary summary;
        mentat::Build_Summary(0, level, &summary);
        DOCTEST_CHECK_FALSE(Has_Text_(All_Lines_(summary), "remembers"));
    }
    mentat::s_summary reasons;
    mentat::Build_Summary(0, mentat::DETAIL_DETAILED, &reasons);
    DOCTEST_CHECK(Has_Text_(All_Lines_(reasons), "remembers: an embargo -20."));
}

DOCTEST_TEST_CASE("unrest: rebels in a colony, or a colony in unrest") {
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_UNREST));
    state.colonies[0].pop[0] = 0x400 | 1;  // a rebel of empire 1
    mentat::s_alert alert;
    DOCTEST_REQUIRE(Stands_(mentat::ALERT_UNREST, &alert));
    DOCTEST_CHECK(mentat::Utf8(alert.text) == "Rebels at Sol: 1.");
    state.colonies[0].pop[0] = 1;
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_UNREST));
    stability::enabled = true;
    stability::rebellion_threshold = -3;
    *stability::Stability_Slot(0) = -5;
    DOCTEST_REQUIRE(Stands_(mentat::ALERT_UNREST, &alert));
    DOCTEST_CHECK(mentat::Utf8(alert.text) == "Unrest is growing at Sol.");
    *stability::Unrest_Slot(0) = static_cast<uint8_t>(stability::UNREST_TURNS);
    DOCTEST_REQUIRE(Stands_(mentat::ALERT_UNREST, &alert));
    DOCTEST_CHECK(mentat::Utf8(alert.text) == "Unrest at Sol: the colony may secede.");
    *stability::Stability_Slot(0) = 5;
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_UNREST));
}

DOCTEST_TEST_CASE("spy: your spy was caught this turn") {
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_SPY));
    s_MSG msg = {};
    msg.msg_type = MSG_TYPE_LOST_ATTACKING_SPY;
    msg.u.lost_attacking_spy.player_idx = 3;
    Add_Msg_(0, msg);
    mentat::s_alert alert;
    DOCTEST_REQUIRE(Stands_(mentat::ALERT_SPY, &alert));
    DOCTEST_CHECK(mentat::Utf8(alert.text) == "Your spy was caught in the empire of Dddd: 1.");
}

DOCTEST_TEST_CASE("ultimatum and prisoner: a deadline within three turns") {
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_ULTIMATUM));
    ultimatums::s_state* ultimatum = ultimatums::State();
    ultimatum->count = 1;
    ultimatum->record[0].demander = 1;
    ultimatum->record[0].victim = 0;
    ultimatum->record[0].state = ultimatums::STATE_OPEN;
    ultimatum->record[0].amount = 80;
    ultimatum->record[0].deadline = MOX::stardate + 8;
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_ULTIMATUM));  // eight turns left
    ultimatum->record[0].deadline = MOX::stardate + 2;
    mentat::s_alert alert;
    DOCTEST_REQUIRE(Stands_(mentat::ALERT_ULTIMATUM, &alert));
    DOCTEST_CHECK(mentat::Utf8(alert.text) == "Ultimatum of Bbbb: 2 turns left.");
    ultimatum->record[0].victim = 3;  // somebody else's
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_ULTIMATUM));

    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_PRISONER));
    strcpy(state.leaders[5].name, "Zed");
    hostages::s_state* held = hostages::State();
    held->count = 1;
    held->held[0].leader = 5;
    held->held[0].owner = 0;
    held->held[0].holder = 1;
    held->held[0].kind = hostages::KIND_PRISONER;
    held->held[0].since = MOX::stardate - 10;
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_PRISONER));
    held->held[0].since = MOX::stardate - (hostages::PRISONER_TURNS - 2);
    DOCTEST_REQUIRE(Stands_(mentat::ALERT_PRISONER, &alert));
    DOCTEST_CHECK(Has_Text_(mentat::Utf8(alert.text), "Zed"));
    held->held[0].kind = hostages::KIND_HOSTAGE;  // a hostage comes home by itself
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_PRISONER));
}

DOCTEST_TEST_CASE("bankruptcy: the treasury runs out within three turns") {
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_BANKRUPTCY));
    state.players[0].bc = 500;
    state.players[0].surplus_bc = -20;
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_BANKRUPTCY));  // 25 turns
    state.players[0].bc = 50;
    mentat::s_alert alert;
    DOCTEST_REQUIRE(Stands_(mentat::ALERT_BANKRUPTCY, &alert));
    DOCTEST_CHECK(mentat::Utf8(alert.text) == "The treasury runs out in 3 turns at the present income.");
    state.players[0].bc = 90;
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_BANKRUPTCY));  // runs out in 5
    state.players[0].bc = -5;
    state.players[0].surplus_bc = 10;
    DOCTEST_REQUIRE(Stands_(mentat::ALERT_BANKRUPTCY, &alert));  // already in debt
    DOCTEST_CHECK(mentat::Utf8(alert.text) == "The treasury is in debt: -5 BC.");
}

DOCTEST_TEST_CASE("war weariness: the next stage within three turns, only in a war") {
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    crises::situations = true;
    crises::Extra()->scale[0].kind = crises::SCALE_WAR_WEARINESS;
    crises::Extra()->scale[0].player = 0;
    crises::Extra()->scale[0].value = 17;
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_WEARINESS));  // at peace the scale only falls
    War_(&state, 0, 1);
    mentat::s_alert alert;
    DOCTEST_REQUIRE(Stands_(mentat::ALERT_WEARINESS, &alert));
    DOCTEST_CHECK(Has_Text_(mentat::Utf8(alert.text), "stage 1"));
    crises::Extra()->scale[0].value = 5;
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_WEARINESS));
    crises::Extra()->scale[0].kind = crises::SCALE_NONE;
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_WEARINESS));
}

DOCTEST_TEST_CASE("crisis: a check of yours due next turn") {
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_CRISIS));
    crises::s_active& slot = crises::Slots()[0];
    slot.crisis = crises::CRISIS_ASTEROID;
    slot.player = 0;
    slot.option = 1;
    slot.result = crises::RESULT_PENDING;
    slot.due = MOX::stardate + 1;
    mentat::s_alert alert;
    DOCTEST_REQUIRE(Stands_(mentat::ALERT_CRISIS, &alert));
    DOCTEST_CHECK(mentat::Utf8(alert.text) == "A check of your crisis (asteroid) is due next turn.");
    slot.due = MOX::stardate + 3;
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_CRISIS));
    slot.due = MOX::stardate + 1;
    slot.option = -1;  // not chosen yet
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_CRISIS));
    slot.option = 1;
    slot.player = 3;  // another empire's
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_CRISIS));
}

DOCTEST_TEST_CASE("Antarans near a colony") {
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_ANTARANS));
    Ship_(&state, 0, 8, -1, 100, 100);
    mentat::s_alert alert;
    DOCTEST_REQUIRE(Stands_(mentat::ALERT_ANTARANS, &alert));
    DOCTEST_CHECK(mentat::Utf8(alert.text) == "Antaran ships near Sol.");
    state.ships[0].x = 500;
    DOCTEST_CHECK_FALSE(Stands_(mentat::ALERT_ANTARANS));
}

DOCTEST_TEST_CASE("the summary at the three levels: keys present, placeholders filled, only what the human knows") {
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    War_(&state, 0, 1);
    Add_Msg_(0, Diplomacy_Msg_(0, 0, 1));
    Add_Msg_(0, Diplomacy_Msg_(1, 1, 3));
    s_MSG lost = {};
    lost.msg_type = MSG_TYPE_LOST_COLONY;
    Add_Msg_(0, lost);
    // A war between two empires the human has not met (player 2 is unknown to it; 1 and 3 are known): nothing of it.
    state.players[2].treaty[3] = 4;
    state.players[3].treaty[2] = 4;
    news::enabled = true;
    news::Add(news::KIND_WAR, 2, 3, 0);
    news::Add(news::KIND_COLONY_FOUNDED, 2, -1, 4);
    // The memory of 1 about us: an embargo.
    opinion::enabled = true;
    opinion::Add(1, 0, opinion::REASON_EMBARGO, -20);
    choices::enabled = true;
    choices::s_choice pending;
    pending.module = choices::MODULE_INCIDENTS;
    pending.player = 0;
    DOCTEST_REQUIRE(choices::Add(pending));

    for (const uint8_t detail : {mentat::DETAIL_BRIEF, mentat::DETAIL_MEDIUM, mentat::DETAIL_DETAILED}) {
        mentat::s_summary summary;
        mentat::Build_Summary(0, detail, &summary);
        DOCTEST_CAPTURE(static_cast<int>(detail));
        DOCTEST_REQUIRE(!summary.sections.empty());
        const std::string all = All_Lines_(summary);
        DOCTEST_CHECK_FALSE(Has_Text_(all, "{"));
        DOCTEST_CHECK_FALSE(Has_Text_(all, "mentat."));  // no key shows through
        DOCTEST_CHECK_FALSE(Has_Text_(all, "Cccc"));     // the unknown empire is not named anywhere
        DOCTEST_CHECK(Has_Text_(all, "At war with: Bbbb."));
        DOCTEST_CHECK(Has_Text_(all, "Waiting for your decision: 1."));
        DOCTEST_CHECK(Has_Text_(all, "Colonies this turn: 1 lost."));
        DOCTEST_CHECK(Has_Text_(all, "Bbbb and Dddd made peace."));
        DOCTEST_CHECK(summary.alerts.size() == 1);  // the war with 1
        if (detail == mentat::DETAIL_BRIEF) {
            DOCTEST_CHECK(summary.sections.size() == 1);
            DOCTEST_CHECK(summary.sections[0].lines.size() >= 3);
            DOCTEST_CHECK(summary.sections[0].lines.size() <= static_cast<size_t>(mentat::MAX_LINES));
            DOCTEST_CHECK(summary.why.empty());
        } else {
            DOCTEST_CHECK(summary.sections.size() == 6);
            DOCTEST_CHECK(Has_Text_(all, "Treasury 500 BC, income 20 per turn."));
            DOCTEST_CHECK(Has_Text_(all, "No spy network yet."));
        }
        if (detail == mentat::DETAIL_DETAILED) {
            DOCTEST_CHECK(Has_Text_(all, "Bbbb: relations"));              // the reason behind the war
            DOCTEST_CHECK(Has_Text_(all, "Bbbb remembers: an embargo -20."));  // what the neighbour remembers
        } else {
            DOCTEST_CHECK(summary.why.empty());
        }
    }
    // Every line of every level has its text in both languages.
    text_language::language = text_language::LANGUAGE_RU;
    mentat::s_summary russian;
    mentat::Build_Summary(0, mentat::DETAIL_DETAILED, &russian);
    const std::string ru = All_Lines_(russian);
    DOCTEST_CHECK_FALSE(Has_Text_(ru, "mentat."));
    DOCTEST_CHECK_FALSE(Has_Text_(ru, "{"));
}

DOCTEST_TEST_CASE("the ranking and the colonies the news names are read through the viewer's eyes") {
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].total_pop = 10;
    state.players[1].total_pop = 30;
    state.players[2].total_pop = 20;
    state.players[3].total_pop = 5;
    news::enabled = true;
    news::Make_Ratings();
    mentat::s_summary summary;
    mentat::Build_Summary(0, mentat::DETAIL_BRIEF, &summary);
    const std::string all = All_Lines_(summary);
    // Among the empires it knows (0, 1 and 3) the human is second by population (1 has more, 3 less).
    DOCTEST_CHECK(Has_Text_(all, "Ranking of turn 100: population #2"));
}

DOCTEST_TEST_CASE("reading changes nothing: no draw from the game's random, no state altered") {
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    Council_(&state);
    War_(&state, 0, 1);
    Add_Msg_(0, Diplomacy_Msg_(0, 0, 1));
    Ship_(&state, 0, 0, 0, 100, 100);
    Ship_(&state, 1, 1, -1, 105, 100);
    Ship_(&state, 2, 1, -1, 105, 100);
    const std::vector<s_player> players = state.players;
    const std::vector<s_ship_data> ships = state.ships;
    const std::vector<s_colony> colonies = state.colonies;
    game_random::Set_Random_Seed(4242);
    for (const uint8_t detail : {mentat::DETAIL_BRIEF, mentat::DETAIL_MEDIUM, mentat::DETAIL_DETAILED}) {
        mentat::s_summary summary;
        mentat::Build_Summary(0, detail, &summary);
    }
    DOCTEST_CHECK_FALSE(mentat::Find_Alerts(0).empty());
    DOCTEST_CHECK(game_random::Get_Random_Seed() == 4242);
    DOCTEST_CHECK(memcmp(players.data(), state.players.data(), players.size() * sizeof(s_player)) == 0);
    DOCTEST_CHECK(memcmp(ships.data(), state.ships.data(), ships.size() * sizeof(s_ship_data)) == 0);
    DOCTEST_CHECK(memcmp(colonies.data(), state.colonies.data(), colonies.size() * sizeof(s_colony)) == 0);
}

DOCTEST_TEST_CASE("off, and off in a network game") {
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].bc = 10;
    state.players[0].surplus_bc = -9;  // a critical situation stands
    DOCTEST_CHECK(mentat::Available());
    mentat::mode = mentat::MODE_OFF;
    DOCTEST_CHECK_FALSE(mentat::Available());
    mentat::Turn_Start(0);
    DOCTEST_CHECK(mentat::Current() == nullptr);
    DOCTEST_CHECK(mentat::Alert_Step(0, false) == 0);
    mentat::mode = mentat::MODE_SUMMARY;
    MOX::game_type = 2;  // a network game: off whatever the settings say
    DOCTEST_CHECK_FALSE(mentat::Available());
    mentat::Turn_Start(0);
    DOCTEST_CHECK(mentat::Current() == nullptr);
    DOCTEST_CHECK_FALSE(mentat::Take_Open_Request());
    DOCTEST_CHECK(mentat::Alert_Step(0, false) == 0);
    MOX::game_type = 0;
    mentat::Turn_Start(0);
    DOCTEST_CHECK(mentat::Current() != nullptr);
}

DOCTEST_TEST_CASE("the start of the turn: the panel opens as the settings say, the alert shows when the panel stays closed") {
    s_scope scope;
    test::s_game_state state;
    Setup_(&state);
    state.players[0].bc = 10;
    state.players[0].surplus_bc = -9;  // bankruptcy in 2 turns

    // critical: the panel opens by itself, so the game-style alert stays quiet.
    mentat::open_when = mentat::OPEN_CRITICAL;
    mentat::Turn_Start(0);
    DOCTEST_REQUIRE(mentat::Current() != nullptr);
    DOCTEST_CHECK(mentat::Current()->alerts.size() == 1);
    DOCTEST_CHECK(mentat::Take_Open_Request());
    DOCTEST_CHECK_FALSE(mentat::Take_Open_Request());
    DOCTEST_CHECK(mentat::Alert_Step(0, false) == 0);
    DOCTEST_CHECK(mentat::New_Count() > 0);
    mentat::Mark_Seen();
    DOCTEST_CHECK(mentat::New_Count() == 0);

    // key: the panel stays shut and the alert is offered (once built this turn, once shown).
    mentat::Clear();
    mentat::open_when = mentat::OPEN_KEY;
    mentat::Turn_Start(0);
    DOCTEST_CHECK_FALSE(mentat::Take_Open_Request());
    DOCTEST_CHECK(mentat::Alert_Step(0, false) == 1);
    DOCTEST_CHECK(mentat::Alert_Text(*mentat::Current()) == "Mentat: The treasury runs out in 2 turns at the present income. (F11 for details)");
    mentat::alerts = false;  // the setting of the alert line
    DOCTEST_CHECK(mentat::Alert_Step(0, false) == 0);
    mentat::alerts = true;

    // every turn, and every N turns.
    mentat::Clear();
    mentat::open_when = mentat::OPEN_TURN;
    mentat::Turn_Start(0);
    DOCTEST_CHECK(mentat::Take_Open_Request());
    mentat::Clear();
    mentat::open_when = mentat::OPEN_PERIOD;
    mentat::period = 5;
    MOX::stardate = 35103;
    mentat::Turn_Start(0);
    DOCTEST_CHECK_FALSE(mentat::Take_Open_Request());
    MOX::stardate = 35105;
    mentat::Turn_Start(0);
    DOCTEST_CHECK(mentat::Take_Open_Request());

    // No alert on a quiet turn, whatever the settings.
    mentat::Clear();
    state.players[0].surplus_bc = 5;
    mentat::open_when = mentat::OPEN_KEY;
    mentat::Turn_Start(0);
    DOCTEST_CHECK(mentat::Alert_Step(0, false) == 0);
    DOCTEST_CHECK(mentat::Current()->alerts.empty());
    // A computer empire is never given a summary.
    mentat::Clear();
    mentat::Turn_Start(1);
    DOCTEST_CHECK(mentat::Current() == nullptr);
}

DOCTEST_TEST_CASE("texts: the alert line in the game's bytes (CP866 in Russian), the panel in UTF-8") {
    s_scope scope;
    const mentat::s_text debt{"mentat.alert.bankrupt_now", {{"bc", "-5"}}};
    text_language::language = text_language::LANGUAGE_EN;
    DOCTEST_CHECK(mentat::Game(debt) == "The treasury is in debt: -5 BC.");
    DOCTEST_CHECK(mentat::Utf8(debt) == "The treasury is in debt: -5 BC.");
    text_language::language = text_language::LANGUAGE_RU;
    const std::string utf8 = "Казна в долгу: -5 МС.";
    std::string cp866;
    DOCTEST_REQUIRE(text_language::From_Utf8(utf8, &cp866));
    DOCTEST_CHECK(mentat::Utf8(debt) == utf8);
    DOCTEST_CHECK(mentat::Game(debt) == cp866);
    DOCTEST_CHECK(cp866 != utf8);
    // The game's own names (CP866 bytes in a Russian game) become UTF-8 in the panel and stay bytes in the alert.
    std::string name;
    DOCTEST_REQUIRE(text_language::From_Utf8("Люди", &name));
    const mentat::s_text war{"mentat.alert.war", {{"name", name}}};
    DOCTEST_CHECK(mentat::Utf8(war) == "Война: Люди теперь воюет с вами.");
    DOCTEST_CHECK(mentat::Game(war).find(name) != std::string::npos);
    DOCTEST_CHECK(text_language::To_Utf8(name) == "Люди");
    text_language::language = text_language::LANGUAGE_EN;
    DOCTEST_CHECK(text_language::To_Utf8("Abc") == "Abc");
    // Get_Utf8 falls back to English like Get.
    DOCTEST_REQUIRE(texts::Load_Source(text_language::LANGUAGE_EN, R"({only_en: "English only"})"));
    text_language::language = text_language::LANGUAGE_RU;
    DOCTEST_CHECK(std::string(texts::Get_Utf8("only_en")) == "English only");
}

}
