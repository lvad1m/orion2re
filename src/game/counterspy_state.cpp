#include "pch.h"

#include "game/counterspy.h"

#include "game/serialization.h"

// The state of counter-intelligence and its save section; the behaviour is in counterspy.cpp. Kept
// apart so the save code needs no spy code. Nothing here draws from game_random.
namespace counterspy {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 3;
        constexpr uint32_t SAVE_SIZE_V1 = sizeof(uint16_t) + MAX_PLAYERS * (3 * 4 + 1);
        constexpr uint32_t SAVE_SIZE_V2 = SAVE_SIZE_V1 + MAX_PLAYERS * CANARY_SUSPECTS * 2;
        constexpr uint32_t SAVE_SIZE = SAVE_SIZE_V2 + MAX_PLAYERS * (4 + 1);
        static_assert(CANARY_SUSPECTS == 3, "the state array is sized for 3 suspects");

        static_assert(MAX_PLAYERS == 8, "the counterspy state is sized for 8 players");

        s_state state;
        s_state pending;
        bool has_pending = false;
    } // namespace

    s_state* State() {
        return &state;
    }

    void Clear() {
        state = s_state{};
        Discard_Save();
    }

    bool Empty() {
        for (int p = 0; p < MAX_PLAYERS; ++p) {
            if (state.suspicion[p] != 0 || state.canary[p] != 0 || state.attempt[p] != 0 || state.guarded[p] != 0 ||
                state.last_hunt[p] != 0 || state.innocents[p] != 0) {
                return false;
            }
            for (int k = 0; k < CANARY_SUSPECTS; ++k) {
                if (state.canary_leader[p][k] != -1) return false;
            }
        }
        return true;
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (int p = 0; p < MAX_PLAYERS; ++p) {
            writer->Write_Int32(state.suspicion[p]);
            writer->Write_Int32(state.canary[p]);
            writer->Write_Int32(state.attempt[p]);
            writer->Write_Uint8(state.guarded[p]);
            for (int k = 0; k < CANARY_SUSPECTS; ++k) writer->Write_Int16(state.canary_leader[p][k]);
        }
        for (int p = 0; p < MAX_PLAYERS; ++p) {
            writer->Write_Int32(state.last_hunt[p]);
            writer->Write_Uint8(state.innocents[p]);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("counterspy save is truncated");
        const uint16_t version = reader->Read_Uint16();
        if (version < 1 || version > SAVE_PAYLOAD_VERSION) return fail("unsupported counterspy payload version");
        const uint32_t expected = version == 1 ? SAVE_SIZE_V1 : version == 2 ? SAVE_SIZE_V2 : SAVE_SIZE;
        if (reader->Remaining() != expected - sizeof(uint16_t)) return fail("counterspy save has the wrong size");
        pending = s_state{};
        for (int p = 0; p < MAX_PLAYERS; ++p) {
            pending.suspicion[p] = reader->Read_Int32();
            pending.canary[p] = reader->Read_Int32();
            pending.attempt[p] = reader->Read_Int32();
            pending.guarded[p] = reader->Read_Uint8();
            if (version >= 2) {
                for (int k = 0; k < CANARY_SUSPECTS; ++k) {
                    pending.canary_leader[p][k] = reader->Read_Int16();
                    if (pending.canary_leader[p][k] < -1 || pending.canary_leader[p][k] >= MAX_LEADERS) return fail("counterspy save has a bad suspect");
                }
            }
            if (pending.suspicion[p] < 0 || pending.canary[p] < 0 || pending.attempt[p] < 0) return fail("counterspy save has a bad stardate");
            if (pending.guarded[p] > 1) return fail("counterspy save has a bad guard flag");
        }
        if (version >= 3) {
            for (int p = 0; p < MAX_PLAYERS; ++p) {
                pending.last_hunt[p] = reader->Read_Int32();
                pending.innocents[p] = reader->Read_Uint8();
                if (pending.last_hunt[p] < 0) return fail("counterspy save has a bad hunt stardate");
            }
        }
        if (reader->Failed()) return fail("counterspy save is truncated");
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

} // namespace counterspy
