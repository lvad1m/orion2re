#include "pch.h"

#include "game/opinion.h"

#include "game/beliefs.h"
#include "game/reputation.h"
#include "game/serialization.h"
#include "game/ultimatums.h"

#include <algorithm>
#include <cstring>

// Nothing in this file draws from game_random.
namespace opinion {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 3;  // 32 reasons per pair (version 1: 8, version 2: 16)
        constexpr uint32_t TABLE_SIZE = MAX_PLAYERS * MAX_PLAYERS * REASON_COUNT;
        constexpr uint32_t SAVE_SIZE = sizeof(uint16_t) + TABLE_SIZE;
        // Treaty states 4 and above are war (advisor_view.cpp Treaty_Name).
        constexpr int32_t TREATY_FIRST_WAR = 4;

        int8_t table[MAX_PLAYERS][MAX_PLAYERS][REASON_COUNT];
        int8_t pending[MAX_PLAYERS][MAX_PLAYERS][REASON_COUNT];
        bool has_pending = false;

        bool Valid_(int16_t player) {
            return player >= 0 && player < MAX_PLAYERS && player < MOX::NUM_PLAYERS;
        }

        bool Holds_Opinions_(int16_t player) {
            return Valid_(player) && MOX::player[player].objectives != PLAYER_OBJECTIVE_HUMAN &&
                MOX::player[player].eliminated == 0;
        }

        bool At_War_(int16_t a, int16_t b) {
            return MOX::player[a].treaty[b] >= TREATY_FIRST_WAR;
        }
    } // namespace

    void Add(int16_t holder, int16_t target, e_reason reason, int32_t value) {
        if (!enabled || value == 0 || reason >= REASON_COUNT || holder == target) return;
        if (!Holds_Opinions_(holder) || !Valid_(target) || MOX::player[target].eliminated != 0) return;
        int8_t& slot = table[holder][target][reason];
        const int32_t limit = Reason_Limit(reason);
        const int8_t before = slot;
        slot = static_cast<int8_t>(std::clamp<int32_t>(slot + value, -limit, limit));
        // The neighbour model (off by default) learns from a memory that changed: a grudge at its
        // limit is no new evidence.
        if (slot != before) beliefs::On_Opinion(holder, target, reason, value);
    }

    void Set(int16_t holder, int16_t target, e_reason reason, int32_t value) {
        if (!enabled || reason >= REASON_COUNT || holder == target) return;
        if (!Holds_Opinions_(holder) || !Valid_(target) || MOX::player[target].eliminated != 0) return;
        const int32_t limit = Reason_Limit(reason);
        table[holder][target][reason] = static_cast<int8_t>(std::clamp<int32_t>(value, -limit, limit));
    }

    int32_t Reason_Limit(e_reason reason) {
        return reason == REASON_CAUGHT_SPYING || reason == REASON_TREATED_OUR_SPY ? ROUTINE_LIMIT : MODIFIER_LIMIT;
    }

    int32_t Modifier(int16_t holder, int16_t target, e_reason reason) {
        if (!Valid_(holder) || !Valid_(target) || reason >= REASON_COUNT) return 0;
        return table[holder][target][reason];
    }

    int32_t Sum(int16_t holder, int16_t target) {
        if (!enabled || !Valid_(holder) || !Valid_(target)) return 0;
        int32_t sum = 0;
        for (int8_t value : table[holder][target]) sum += value;
        return sum;
    }

    int32_t Relations(int16_t holder, int16_t target) {
        if (holder < 0 || holder >= MAX_PLAYERS || target < 0 || target >= MAX_PLAYERS) return 0;
        const int32_t classic = MOX::player[holder].relations[target];
        if (!enabled) return classic;
        // Stains and titles (diplomacy titles, off by default) add to what the holder feels.
        return std::clamp<int32_t>(classic + Sum(holder, target) + reputation::Relations_Bias(holder, target), -100, 100);
    }

    const char* Reason_Name(e_reason reason) {
        switch (reason) {
            case REASON_SNEAK_ATTACK: return "attacked us without declaring war";
            case REASON_HELPED_IN_WAR: return "went to war with our enemy";
            case REASON_EMBARGO: return "put us under an embargo";
            case REASON_RIVAL: return "declared us its rival";
            case REASON_DEFENDED_US: return "came to our defence";
            case REASON_ABANDONED_US: return "left us alone against an attacker";
            case REASON_BROKE_BAN: return "broke a ban of the council";
            case REASON_CAUGHT_SPYING: return "its spies were caught working against us";
            case REASON_BRIBED_OUR_LEADER: return "bribed one of our leaders";
            case REASON_STIRRED_UNREST: return "stirred up unrest among our people";
            case REASON_KILLED_OUR_SCIENTIST: return "killed one of our scientists";
            case REASON_FRAMED_OUR_LEADER: return "framed one of our leaders";
            case REASON_SABOTAGE: return "sent saboteurs against us";
            case REASON_DID_US_A_SERVICE: return "did us a service";
            case REASON_PLOTTED_AGAINST_US: return "plotted against us";
            case REASON_TREATED_OUR_SPY: return "decided the fate of a spy of ours";
            case REASON_KILLED_OUR_DIPLOMAT: return "killed one of our diplomats";
            case REASON_ATROCITY: return "committed an atrocity";
            case REASON_BROKE_ITS_WORD: return "broke its word to us";
            case REASON_BLACKMAILED_US: return "blackmailed us";
            case REASON_EXPOSED_US: return "made our secret public";
            case REASON_SMEARED_US: return "smeared us";
            case REASON_RAIDED_US: return "paid raiders to strike us";
            case REASON_TRIED_TO_KILL_OUR_LEADER: return "tried to kill one of our leaders";
            case REASON_BETRAYED_A_SECRET_PACT: return "betrayed a secret pact";
            case REASON_HARMED_A_HOSTAGE: return "harmed a hostage of ours";
            case REASON_FAMILY_TIES: return "family ties";
            case REASON_OVERLORD: return "our overlord";
            case REASON_BAD_RUMOURS: return "bad rumours about it";
            case REASON_VIOLATED_HOSPITALITY: return "attacked its guest: a red wedding";
            default: return "unknown";
        }
    }

    void End_Of_Turn() {
        if (!enabled || decay_period < 1 || MOX::stardate % decay_period != 0) return;
        // Fading steps: every memory a point a step; an atrocity only every ATROCITY_FADE_STEPS steps;
        // with slow_grudges a bad memory only every second step (events-catalog 3.12: bad reputation
        // passes slower than good grows).
        const int32_t step = MOX::stardate / decay_period;
        for (auto& row : table) {
            for (auto& reasons : row) {
                for (int32_t r = 0; r < REASON_COUNT; ++r) {
                    int8_t& value = reasons[r];
                    if (value == 0) continue;
                    if (r == REASON_ATROCITY && step % ATROCITY_FADE_STEPS != 0) continue;
                    if (r == REASON_VIOLATED_HOSPITALITY) continue;  // a red wedding is never forgiven (the design row: without fading)
                    if (value < 0 && slow_grudges && step % 2 != 0) continue;
                    value = static_cast<int8_t>(value > 0 ? value - 1 : value + 1);
                }
            }
        }
    }

    void On_Sneak_Attack(int16_t attacker, int16_t victim) {
        if (ultimatums::Has_Cause(attacker, victim)) return;  // a lawful cause (an ultimatum unmet): not a sneak attack
        Add(victim, attacker, REASON_SNEAK_ATTACK, sneak_attack);
    }

    void On_War_Started(int16_t a, int16_t b) {
        if (!enabled || !Valid_(a) || !Valid_(b) || a == b) return;
        for (int16_t h = 0; h < MOX::NUM_PLAYERS && h < MAX_PLAYERS; ++h) {
            if (h == a || h == b || !Holds_Opinions_(h)) continue;
            if (At_War_(h, b) && MOX::player[h].contact[a] != 0) Add(h, a, REASON_HELPED_IN_WAR, helped_in_war);
            if (At_War_(h, a) && MOX::player[h].contact[b] != 0) Add(h, b, REASON_HELPED_IN_WAR, helped_in_war);
        }
    }

    void Clear() {
        std::memset(table, 0, sizeof(table));
        Discard_Save();
    }

    bool Empty() {
        for (const auto& row : table) {
            for (const auto& reasons : row) {
                for (int8_t value : reasons) {
                    if (value != 0) return false;
                }
            }
        }
        return true;
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (const auto& row : table) {
            for (const auto& reasons : row) {
                for (int8_t value : reasons) writer->Write_Int8(value);
            }
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("opinion save is truncated");
        const uint16_t version = reader->Read_Uint16();
        if (version < 1 || version > SAVE_PAYLOAD_VERSION) return fail("unsupported opinion payload version");
        // Version 1 held 8 reasons per pair, version 2 held 16; the newer reasons start empty.
        const uint32_t reasons_saved = version == 1 ? 8 : version == 2 ? 16 : REASON_COUNT;
        if (reader->Remaining() != MAX_PLAYERS * MAX_PLAYERS * reasons_saved) return fail("opinion save has the wrong size");
        std::memset(pending, 0, sizeof(pending));
        for (auto& row : pending) {
            for (auto& reasons : row) {
                for (uint32_t r = 0; r < reasons_saved; ++r) {
                    int8_t& value = reasons[r];
                    value = reader->Read_Int8();
                    if (value < -MODIFIER_LIMIT || value > MODIFIER_LIMIT) return fail("opinion save has an out-of-range value");
                }
            }
        }
        if (reader->Failed()) return fail("opinion save is truncated");
        has_pending = true;
        return true;
    }

    void Commit_Save() {
        if (!has_pending) return;
        std::memcpy(table, pending, sizeof(table));
        has_pending = false;
    }

    void Discard_Save() {
        has_pending = false;
    }

} // namespace opinion
