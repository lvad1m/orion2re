#include "pch.h"

#include "game/vassals.h"

#include "game/serialization.h"

#include <algorithm>

// The state of vassalages and its save section; the behaviour is in vassals.cpp. Kept apart so the
// save code needs no diplomacy code. Nothing here draws from game_random.
namespace vassals {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t SAVE_SIZE = sizeof(uint16_t) + MAX_PLAYERS * (1 + 4);

        static_assert(MAX_PLAYERS == 8, "the vassals state is sized for 8 players");

        s_state state;
        s_state pending;
        bool has_pending = false;
    } // namespace

    s_state* State() {
        return &state;
    }

    int16_t Overlord(int16_t vassal) {
        if (!enabled || vassal < 0 || vassal >= MAX_PLAYERS) return -1;
        return state.overlord[vassal];
    }

    bool Is_Vassal_Of(int16_t vassal, int16_t overlord) {
        return overlord >= 0 && Overlord(vassal) == overlord;
    }

    void Clear() {
        state = s_state{};
        Discard_Save();
    }

    bool Empty() {
        return std::all_of(std::begin(state.overlord), std::end(state.overlord), [](int8_t v) { return v == -1; });
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (int p = 0; p < MAX_PLAYERS; ++p) {
            writer->Write_Int8(state.overlord[p]);
            writer->Write_Int32(state.since[p]);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("vassals save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported vassals payload version");
        if (reader->Remaining() != SAVE_SIZE - sizeof(uint16_t)) return fail("vassals save has the wrong size");
        pending = s_state{};
        for (int p = 0; p < MAX_PLAYERS; ++p) {
            pending.overlord[p] = reader->Read_Int8();
            pending.since[p] = reader->Read_Int32();
            if (pending.overlord[p] < -1 || pending.overlord[p] >= MAX_PLAYERS || pending.overlord[p] == p) {
                return fail("vassals save has a bad overlord");
            }
            if (pending.since[p] < 0) return fail("vassals save has a bad stardate");
        }
        if (reader->Failed()) return fail("vassals save is truncated");
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

} // namespace vassals
