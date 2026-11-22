#pragma once

#include <cstdint>
#include <string>

namespace SERIAL {
    class Reader_;
    class Writer_;
} // namespace SERIAL

// Council resolutions besides the embargo (docs/LocalLLM/mechanics.md 2.7.2; README.md 10.9, tier 1):
// bans on biological weapons and on destroying planets, and a defence fund against the Antarans.
// At a council meeting without humans one resolution is put to the vote - the proposal with the
// most support among an embargo (embargo.h), the bans and the fund. Effective only while
// diplomacy embargo and diplomacy council_resolutions are on (off by default).
namespace resolutions {

    extern int32_t turns;               // diplomacy resolution_turns: how long a ban or the fund lasts
    extern int32_t violation_opinion;   // diplomacy resolution_violation_opinion: everyone's opinion of a violator
    extern int32_t fund_percent;        // diplomacy antaran_fund_percent: income paid into the fund each turn

    enum e_ban : uint8_t {
        BAN_BIO_WEAPONS = 0,
        BAN_PLANET_DESTRUCTION = 1,
        BAN_COUNT = 2,
    };

    // State (resolutions_state.cpp).
    struct s_state {
        uint8_t bans = 0;                              // bit e_ban: the ban is in force
        int32_t ban_start[BAN_COUNT] = {};
        uint8_t fund = 0;                              // 1: the Antaran defence fund exists
        int32_t fund_start = 0;
        int32_t fund_pool = 0;                         // BC in the fund
        int32_t last_use[BAN_COUNT][8] = {};           // turn each empire last did the banned act (0: never)
        int32_t last_antaran_attack = 0;               // turn of the last Antaran attack (0: none)
        int32_t last_meeting = 0;                      // turn of the last council meeting
    };
    s_state* State();
    bool Active();  // diplomacy embargo and council_resolutions are on
    bool Ban_In_Force(e_ban ban);

    // Events. A banned act (biological weapons killed population; a planet was destroyed): it is
    // remembered, and if the ban is in force the violator gets "broke a ban of the council" from
    // every empire and the council puts it under an embargo. An Antaran attack on `victim`: it is
    // remembered, and with the fund in force the victim receives half of it.
    void On_Banned_Act(int16_t player, e_ban ban);
    void On_Antaran_Attack(int16_t victim);

    // Once a turn: payments into the fund, and bans and the fund whose term ran out end.
    void End_Of_Turn();

    // At a council meeting without humans where no leader was elected: the proposals - an embargo
    // (embargo::Council_Proposal), a ban on an act done since the last meeting, the fund after an
    // Antaran attack since then - are weighed by the council votes for them; the one with the most
    // is voted on and passes with more than half of all votes.
    void Council_Meeting();

    void Clear();
    bool Empty();

    // Save section 'RESO' (game/rules_sections.h): payload version 1, then the fields of s_state in
    // order (bans uint8, ban_start int32 x2, fund uint8, fund_start, fund_pool int32, last_use
    // int32 x16, last_antaran_attack, last_meeting int32).
    uint32_t Save_Size();
    void Write_Save(SERIAL::Writer_* writer);
    bool Parse_Save(SERIAL::Reader_* reader, std::string* error);
    void Commit_Save();
    void Discard_Save();

} // namespace resolutions
