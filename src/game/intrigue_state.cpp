#include "pch.h"

#include "game/intrigue.h"

#include "game/serialization.h"

#include <algorithm>

// The state of the intrigues and its save section; the behaviour is in intrigue.cpp. Kept apart so
// the save code needs no diplomacy code.
namespace intrigue {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t CELLS = MAX_PLAYERS * MAX_PLAYERS;
        constexpr uint32_t SAVE_SIZE = sizeof(uint16_t) + CELLS + 2 * CELLS * sizeof(int32_t);

        static_assert(MAX_PLAYERS == 8, "the intrigue state is sized for 8 players");

        s_state state;
        s_state pending;
        bool has_pending = false;
        s_caught caught[CAUGHT_QUEUE];
        int32_t caught_count = 0;
    } // namespace

    s_state* State() {
        return &state;
    }

    s_caught* Caught_Queue(int32_t* count) {
        if (count != nullptr) *count = caught_count;
        return caught;
    }

    void Clear_Caught_Queue() {
        caught_count = 0;
    }

    void On_Spy_Caught(int16_t victim, int16_t attacker) {
        if (!spy_fate || victim < 0 || victim >= MAX_PLAYERS || attacker < 0 || attacker >= MAX_PLAYERS || victim == attacker) return;
        if (caught_count >= CAUGHT_QUEUE) return;
        caught[caught_count].victim = static_cast<int8_t>(victim);
        caught[caught_count].attacker = static_cast<int8_t>(attacker);
        ++caught_count;
    }

    bool Any_Enabled() {
        return reveal_plans || forged_letters || spy_fate || proxy_wars || favours;
    }

    int32_t Favours(int16_t debtor, int16_t creditor) {
        if (!favours || debtor < 0 || debtor >= MAX_PLAYERS || creditor < 0 || creditor >= MAX_PLAYERS) return 0;
        return state.favours[debtor][creditor];
    }

    bool Has_Double_Agent(int16_t catcher, int16_t owner) {
        if (!spy_fate || catcher < 0 || catcher >= MAX_PLAYERS || owner < 0 || owner >= MAX_PLAYERS) return false;
        return state.double_agent_until[catcher][owner] > MOX::stardate;
    }

    int32_t Double_Agent_Difficulty(int16_t attacker, int16_t target) {
        return Has_Double_Agent(target, attacker) ? DOUBLE_AGENT_DIFFICULTY : 0;
    }

    void Clear() {
        state = s_state{};
        caught_count = 0;
        Discard_Save();
    }

    bool Empty() {
        const auto zero_u8 = [](uint8_t v) { return v == 0; };
        const auto zero_i32 = [](int32_t v) { return v == 0; };
        return std::all_of(&state.favours[0][0], &state.favours[0][0] + CELLS, zero_u8) &&
            std::all_of(&state.revealed[0][0], &state.revealed[0][0] + CELLS, zero_i32) &&
            std::all_of(&state.double_agent_until[0][0], &state.double_agent_until[0][0] + CELLS, zero_i32);
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (const auto& row : state.favours) {
            for (uint8_t v : row) writer->Write_Uint8(v);
        }
        for (const auto& row : state.revealed) {
            for (int32_t v : row) writer->Write_Int32(v);
        }
        for (const auto& row : state.double_agent_until) {
            for (int32_t v : row) writer->Write_Int32(v);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("intrigue save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported intrigue payload version");
        if (reader->Remaining() != SAVE_SIZE - sizeof(uint16_t)) return fail("intrigue save has the wrong size");
        pending = s_state{};
        for (int a = 0; a < MAX_PLAYERS; ++a) {
            for (int b = 0; b < MAX_PLAYERS; ++b) {
                const uint8_t v = reader->Read_Uint8();
                if (v > FAVOUR_LIMIT || (a == b && v != 0)) return fail("intrigue save has a bad favour count");
                pending.favours[a][b] = v;
            }
        }
        for (auto& row : pending.revealed) {
            for (int32_t& v : row) {
                v = reader->Read_Int32();
                if (v < 0) return fail("intrigue save has a bad stardate");
            }
        }
        for (auto& row : pending.double_agent_until) {
            for (int32_t& v : row) {
                v = reader->Read_Int32();
                if (v < 0) return fail("intrigue save has a bad stardate");
            }
        }
        if (reader->Failed()) return fail("intrigue save is truncated");
        has_pending = true;
        return true;
    }

    void Commit_Save() {
        if (!has_pending) return;
        state = pending;
        caught_count = 0;
        has_pending = false;
    }

    void Discard_Save() {
        has_pending = false;
    }

} // namespace intrigue
