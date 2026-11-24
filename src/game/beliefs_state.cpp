#include "pch.h"

#include "game/beliefs.h"

#include "game/opinion.h"
#include "game/serialization.h"

#include <algorithm>

// The state of the neighbour model and of disinformation, the evidence from opinion memories and
// the save section; the rest of the behaviour is in beliefs.cpp. Kept apart so the save code and
// opinion.cpp need no diplomacy code. Nothing here draws from game_random.
namespace beliefs {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t CELLS = MAX_PLAYERS * MAX_PLAYERS;
        constexpr uint32_t SAVE_SIZE = sizeof(uint16_t) + CELLS * BELIEF_COUNT + CELLS * sizeof(int16_t) +
            CELLS * sizeof(int32_t);
        // round(100 / (1 + e^(-x / 4))) for x = -25..25.
        constexpr int8_t LOGISTIC[51] = {0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 3, 4, 5, 6, 8, 10, 12, 15, 18, 22, 27, 32, 38,
            44, 50, 56, 62, 68, 73, 78, 82, 85, 88, 90, 92, 94, 95, 96, 97, 98, 98, 99, 99, 99, 99, 99, 100, 100, 100, 100};
        // Priors by personality (xenophobic, ruthless, aggressive, erratic, honorable, pacifist,
        // dishonored), in log-odds steps.
        constexpr int8_t PRIOR[BELIEF_COUNT][7] = {
            {2, 3, 4, 1, -2, -4, 2},     // hostile
            {0, -2, -1, -4, 5, 2, -5},   // faithful
            {0, -3, 0, -2, 4, 2, -4},    // honest
        };

        static_assert(MAX_PLAYERS == 8, "the beliefs state is sized for 8 players");

        s_state state;
        s_state pending;
        bool has_pending = false;

        bool Valid_(int16_t player) {
            return player >= 0 && player < MAX_PLAYERS && player < MOX::NUM_PLAYERS;
        }

        bool Computer_(int16_t player) {
            return Valid_(player) && MOX::player[player].objectives != PLAYER_OBJECTIVE_HUMAN && MOX::player[player].eliminated == 0;
        }
    } // namespace

    s_state* State() {
        return &state;
    }

    int32_t Prior(uint8_t personality, e_belief belief) {
        if (belief >= BELIEF_COUNT || personality >= 7) return 0;
        return PRIOR[belief][personality];
    }

    int32_t Evidence(int16_t observer, int16_t subject, e_belief belief) {
        if (!neighbour_model || !Valid_(observer) || !Valid_(subject) || belief >= BELIEF_COUNT) return 0;
        return state.evidence[observer][subject][belief];
    }

    int32_t Percent(int16_t observer, int16_t subject, e_belief belief) {
        if (!neighbour_model || !Valid_(observer) || !Valid_(subject) || belief >= BELIEF_COUNT) return 50;
        const int32_t x = std::clamp<int32_t>(Prior(MOX::player[subject].personality, belief) + Evidence(observer, subject, belief), -25, 25);
        return LOGISTIC[x + 25];
    }

    bool Trusts(int16_t observer, int16_t subject) {
        return !neighbour_model || Percent(observer, subject, BELIEF_HONEST) >= TRUST_MIN_PERCENT;
    }

    bool Believes_Faithful(int16_t observer, int16_t subject) {
        return !neighbour_model || Percent(observer, subject, BELIEF_FAITHFUL) >= TRUST_MIN_PERCENT;
    }

    void Observe(int16_t observer, int16_t subject, e_belief belief, int32_t steps) {
        if (!neighbour_model || steps == 0 || belief >= BELIEF_COUNT || observer == subject) return;
        if (!Computer_(observer) || !Valid_(subject)) return;
        int8_t& slot = state.evidence[observer][subject][belief];
        slot = static_cast<int8_t>(std::clamp<int32_t>(slot + steps, -EVIDENCE_LIMIT, EVIDENCE_LIMIT));
    }

    void On_Opinion(int16_t holder, int16_t target, uint8_t reason, int32_t value) {
        if (!neighbour_model) return;
        switch (reason) {
            case opinion::REASON_SNEAK_ATTACK:
                Observe(holder, target, BELIEF_HOSTILE, 3);
                Observe(holder, target, BELIEF_FAITHFUL, -2);
                break;
            case opinion::REASON_HELPED_IN_WAR: Observe(holder, target, BELIEF_HOSTILE, -1); break;
            case opinion::REASON_EMBARGO:
            case opinion::REASON_RIVAL: Observe(holder, target, BELIEF_HOSTILE, 1); break;
            case opinion::REASON_DEFENDED_US:
                Observe(holder, target, BELIEF_FAITHFUL, 2);
                Observe(holder, target, BELIEF_HOSTILE, -1);
                break;
            case opinion::REASON_ABANDONED_US: Observe(holder, target, BELIEF_FAITHFUL, -2); break;
            case opinion::REASON_BROKE_BAN: Observe(holder, target, BELIEF_FAITHFUL, -1); break;
            case opinion::REASON_CAUGHT_SPYING:
            case opinion::REASON_BRIBED_OUR_LEADER:
            case opinion::REASON_STIRRED_UNREST:
            case opinion::REASON_KILLED_OUR_SCIENTIST:
            case opinion::REASON_FRAMED_OUR_LEADER:
            case opinion::REASON_SABOTAGE:
                Observe(holder, target, BELIEF_HOSTILE, 1);
                Observe(holder, target, BELIEF_HONEST, -1);
                break;
            case opinion::REASON_DID_US_A_SERVICE:
                Observe(holder, target, BELIEF_HOSTILE, -1);
                Observe(holder, target, BELIEF_FAITHFUL, 1);
                Observe(holder, target, BELIEF_HONEST, 1);
                break;
            case opinion::REASON_PLOTTED_AGAINST_US:
                Observe(holder, target, BELIEF_HOSTILE, 2);
                Observe(holder, target, BELIEF_HONEST, -1);
                break;
            case opinion::REASON_TREATED_OUR_SPY: Observe(holder, target, BELIEF_HOSTILE, value < 0 ? 1 : -1); break;
            case opinion::REASON_KILLED_OUR_DIPLOMAT:
            case opinion::REASON_TRIED_TO_KILL_OUR_LEADER: Observe(holder, target, BELIEF_HOSTILE, 2); break;
            case opinion::REASON_ATROCITY:
                Observe(holder, target, BELIEF_HOSTILE, 2);
                Observe(holder, target, BELIEF_FAITHFUL, -1);
                break;
            case opinion::REASON_BROKE_ITS_WORD:
            case opinion::REASON_HARMED_A_HOSTAGE: Observe(holder, target, BELIEF_FAITHFUL, -2); break;
            case opinion::REASON_BETRAYED_A_SECRET_PACT: Observe(holder, target, BELIEF_FAITHFUL, -3); break;
            case opinion::REASON_BLACKMAILED_US:
            case opinion::REASON_RAIDED_US:
                Observe(holder, target, BELIEF_HOSTILE, 1);
                Observe(holder, target, BELIEF_HONEST, -1);
                break;
            case opinion::REASON_EXPOSED_US: Observe(holder, target, BELIEF_HOSTILE, 1); break;
            case opinion::REASON_SMEARED_US: Observe(holder, target, BELIEF_HONEST, -2); break;
            case opinion::REASON_BAD_RUMOURS: Observe(holder, target, BELIEF_HONEST, -1); break;
            case opinion::REASON_FAMILY_TIES:
                Observe(holder, target, BELIEF_HOSTILE, -2);
                Observe(holder, target, BELIEF_FAITHFUL, 1);
                break;
            case opinion::REASON_OVERLORD: Observe(holder, target, BELIEF_HOSTILE, value < 0 ? 1 : -1); break;
            default: break;
        }
    }

    int32_t Distortion_Percent(int16_t observer, int16_t subject) {
        if (!disinformation || !Valid_(observer) || !Valid_(subject)) return 100;
        const int32_t percent = state.distortion[observer][subject];
        return percent > 0 && state.distortion_until[observer][subject] > MOX::stardate ? percent : 100;
    }

    void Clear() {
        state = s_state{};
        Discard_Save();
    }

    bool Empty() {
        const auto zero8 = [](int8_t v) { return v == 0; };
        const auto zero16 = [](int16_t v) { return v == 0; };
        const auto zero32 = [](int32_t v) { return v == 0; };
        return std::all_of(&state.evidence[0][0][0], &state.evidence[0][0][0] + CELLS * BELIEF_COUNT, zero8) &&
            std::all_of(&state.distortion[0][0], &state.distortion[0][0] + CELLS, zero16) &&
            std::all_of(&state.distortion_until[0][0], &state.distortion_until[0][0] + CELLS, zero32);
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (const auto& row : state.evidence) {
            for (const auto& cell : row) {
                for (int8_t v : cell) writer->Write_Int8(v);
            }
        }
        for (const auto& row : state.distortion) {
            for (int16_t v : row) writer->Write_Int16(v);
        }
        for (const auto& row : state.distortion_until) {
            for (int32_t v : row) writer->Write_Int32(v);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("beliefs save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported beliefs payload version");
        if (reader->Remaining() != SAVE_SIZE - sizeof(uint16_t)) return fail("beliefs save has the wrong size");
        pending = s_state{};
        for (auto& row : pending.evidence) {
            for (auto& cell : row) {
                for (int8_t& v : cell) {
                    v = reader->Read_Int8();
                    if (v < -EVIDENCE_LIMIT || v > EVIDENCE_LIMIT) return fail("beliefs save has out-of-range evidence");
                }
            }
        }
        for (auto& row : pending.distortion) {
            for (int16_t& v : row) {
                v = reader->Read_Int16();
                if (v < 0 || v > 1000) return fail("beliefs save has a bad distortion");
            }
        }
        for (auto& row : pending.distortion_until) {
            for (int32_t& v : row) {
                v = reader->Read_Int32();
                if (v < 0) return fail("beliefs save has a bad stardate");
            }
        }
        if (reader->Failed()) return fail("beliefs save is truncated");
        has_pending = true;
        return true;
    }

    void Commit_Save() {
        if (!has_pending) return;
        state = pending;
        has_pending = false;
    }

    void Discard_Save() {
        has_pending = false;
    }

} // namespace beliefs
