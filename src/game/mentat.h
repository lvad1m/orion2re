#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The mentat: the human's advisor (docs/LocalLLM/mentat.md, stage M1; the author's answers of 28.11.2026: F11 (F9 stays the game's star distances), a summary,
// no rule numbers, off in a network game). It only reads: it never draws from game_random, never changes the game and
// writes nothing to a save. Everything it says comes from what the human knows: the empire's own numbers, the message
// queue the game itself shows the player at the start of the turn, the news the empire may read (news.h Recent_For), the
// detected ships (SHIPSTAK::Is_Ship_Detected_By) and the council's pure predictions. The settings are the "interface"
// group: local, not saved, not synchronized.
//
// Engine functions called (each one read-only, checked while writing this module): MSG_::N_Msgs_For_Player,
// MSG_::First_Msg_For_Player and MSG_::msgs (the queue); news::Recent_For, news::Visible_To, news::Ratings;
// council::Session_Due_Within, Predict_Nominees, Get_Num_Votes, Vote_Score; SHIPSTAK::Is_Ship_Detected_By
// (Best_Detection_At and Ship_Detection_Threshold: scans over the tables); stability::Of, stability::Unrest_Slot;
// crises::Find_Scale, Slots, Weariness_Growth, Extra; ultimatums::State; hostages::State; choices::Count;
// espionage::Points, espionage::State; opinion::Modifier; TECHDATA tables. The panel's own memory (the previous
// summary's figures) lives in this module for the session and is not saved.
namespace mentat {

    // Settings (specs/config/main.json5, group interface).
    enum e_mode : uint8_t {
        MODE_OFF = 0,
        MODE_SUMMARY = 1,
        MODE_ADVICE = 2,  // the advice comes with M2; it shows the summary for now
    };
    enum e_detail : uint8_t {
        DETAIL_BRIEF = 0,
        DETAIL_MEDIUM = 1,
        DETAIL_DETAILED = 2,
    };
    enum e_open : uint8_t {
        OPEN_TURN = 0,      // at the start of every turn
        OPEN_PERIOD = 1,    // every `period` turns
        OPEN_CRITICAL = 2,  // when a critical situation stands
        OPEN_KEY = 3,       // only on F11
    };
    extern uint8_t mode;        // interface mentat
    extern uint8_t detail;      // interface mentat_detail
    extern uint8_t open_when;   // interface mentat_open
    extern int32_t period;      // interface mentat_period
    extern bool alerts;         // interface mentat_alerts

    // Programming choices (tuned by the author's play).
    constexpr int32_t COUNCIL_WARNING_TURNS = 5;    // a council session this close is a critical situation
    constexpr int32_t COUNCIL_FEW_VOTES = 3;        // "a few votes" short of the two thirds, at least
    constexpr int32_t COUNCIL_LOOKAHEAD = 20;       // the summary names a session this close
    constexpr int32_t DEADLINE_TURNS = 3;           // an ultimatum or a prisoner's term this close is critical
    constexpr int32_t BANKRUPTCY_TURNS = 3;         // the treasury runs out this soon at the present income
    constexpr int32_t WEARINESS_TURNS = 3;          // the next stage of war weariness this soon
    constexpr int32_t NEAR_UNITS = 45;              // "near a colony": its star and ships up to this far (map units, a parsec)
    constexpr int32_t NEWS_TURNS = 10;              // the first summary names news this old
    constexpr int32_t OPINION_MIN = 5;              // the detailed level names a neighbour's memory this deep or deeper
    constexpr int32_t MAX_LINES = 6;                // the brief summary

    // The mentat is on: the setting is not off and the game is not a network game (the network game is open: off).
    bool Available();

    // A line of text: a key of data/base/texts (mentat.*) and its named values, in the game's bytes (CP866 in a Russian
    // game, as the game's own names are). Utf8 converts the values and fills the UTF-8 text for the panel; Game gives the
    // game's bytes for the alert in the game's own font.
    struct s_text {
        std::string key;
        // texts::t_args, spelled out: this header is a config bind header and must build on its own
        // (the oracle baseline copies it without texts.h).
        std::vector<std::pair<std::string, std::string>> args;
    };
    std::string Utf8(const s_text& text);
    std::string Game(const s_text& text);

    // The critical situations (docs/LocalLLM/mentat.md section 3). One alert of each kind at most: the worst instance.
    enum e_alert : uint8_t {
        ALERT_WAR = 0,            // a war began with you this turn
        ALERT_FLEET = 1,          // a detected enemy fleet near your colony, stronger than your ships there
        ALERT_COUNCIL_LOSE = 2,   // a council session soon and the rival nominee is expected to be elected
        ALERT_COUNCIL_SHORT = 3,  // a council session soon and you, a nominee, fall a few votes short of the two thirds
        ALERT_UNREST = 4,         // a colony at rebellion or secession risk
        ALERT_SPY = 5,            // your spy was caught this turn
        ALERT_ULTIMATUM = 6,      // an ultimatum's deadline within DEADLINE_TURNS
        ALERT_PRISONER = 7,       // your leader is held as a prisoner and the holder's term runs out within DEADLINE_TURNS
        ALERT_BANKRUPTCY = 8,     // the treasury runs out within BANKRUPTCY_TURNS
        ALERT_WEARINESS = 9,      // the next stage of war weariness within WEARINESS_TURNS
        ALERT_CRISIS = 10,        // a crisis check of yours is due next turn
        ALERT_ANTARANS = 11,      // an Antaran ship detected near your colony
        ALERT_COUNT = 12,
    };

    struct s_alert {
        uint8_t kind = ALERT_COUNT;
        s_text text;  // the situation, a line
        s_text why;   // the numbers behind it, for the detailed level
    };

    // Whether `kind` stands for `player` now; fills `out` when it does. Pure over the game state.
    // The council alerts rest on other empires' reckoning (Vote_Score) and stand only at DETAIL_DETAILED.
    bool Check(e_alert kind, int16_t player, s_alert* out, uint8_t level = DETAIL_DETAILED);
    // Every kind that stands, in the order of e_alert.
    std::vector<s_alert> Find_Alerts(int16_t player, uint8_t level = DETAIL_DETAILED);

    // The summary of the turn (built once at the start of the human's turn).
    struct s_section {
        std::string title_key;  // mentat.section.*
        std::vector<s_text> lines;
    };
    struct s_summary {
        int32_t stardate = -1;
        int16_t player = -1;
        uint8_t detail = DETAIL_BRIEF;
        std::vector<s_section> sections;  // the summary of the turn: one section (brief) or one per field (medium, detailed)
        std::vector<s_alert> alerts;      // the threats
        std::vector<s_text> why;          // detailed only: the reasons behind the threats and the opinions
        bool advice_pending = false;      // the mode is advice and M2 is not here yet
    };
    // Builds the summary of `player` at `detail`; reads only, and does not touch the memory.
    void Build_Summary(int16_t player, uint8_t detail, s_summary* out);

    // The memory kept between turns (the figures of the previous summary): treasury, relations, the ranking.
    // Not saved; a load starts it again. Clear forgets it.
    void Clear();

    // The start of the human's turn (report.cpp Display_Report_Aux, once per stardate and player): builds the summary
    // and decides whether the panel opens by itself and whether the game-style alert is shown.
    void Turn_Start(int16_t player);

    // The latest summary, nullptr when none was built (mentat off, a network game) or it is of an earlier turn.
    const s_summary* Current();
    // The panel asks: a request to open itself came with the last Turn_Start (and is taken by this call).
    bool Take_Open_Request();
    // New lines since the panel was last open (the count of the collapsed bar "Ментат: N new").
    int32_t New_Count();
    void Mark_Seen();

    // The game-style alert in the chain of the turn's reports (report.cpp, after the choice window): non-zero while
    // there is one to show (the setting is on, the panel is closed and will not open itself, a critical situation stands,
    // it has not been shown this turn); with `show_ui` it shows a message box with one or two lines and "F11" and
    // marks it shown.
    int8_t Alert_Step(int16_t player, bool show_ui);
    // The text of that alert in the game's bytes.
    std::string Alert_Text(const s_summary& summary);

} // namespace mentat
