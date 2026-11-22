#include "pch.h"

#include "game/resolutions.h"

#include "game/embargo.h"
#include "game/serialization.h"

// The state of council resolutions, its queries and its save section; the behaviour is in
// resolutions.cpp. Kept apart so the save code needs no diplomacy.
namespace resolutions {

    static_assert(MAX_PLAYERS == 8, "last_use holds 8 players");

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t SAVE_SIZE = sizeof(uint16_t) + sizeof(uint8_t) + BAN_COUNT * sizeof(int32_t) + sizeof(uint8_t) +
            2 * sizeof(int32_t) + BAN_COUNT * MAX_PLAYERS * sizeof(int32_t) + 2 * sizeof(int32_t);

        s_state state;
        s_state pending;
        bool has_pending = false;
    } // namespace

    s_state* State() {
        return &state;
    }

    bool Active() {
        return embargo::enabled && embargo::council_resolutions;
    }

    bool Ban_In_Force(e_ban ban) {
        return Active() && ban < BAN_COUNT && ((state.bans >> ban) & 1) != 0;
    }

    void Clear() {
        state = s_state{};
        Discard_Save();
    }

    bool Empty() {
        if (state.bans != 0 || state.fund != 0 || state.fund_pool != 0 || state.last_antaran_attack != 0 || state.last_meeting != 0) {
            return false;
        }
        for (const auto& row : state.last_use) {
            for (int32_t turn : row) {
                if (turn != 0) return false;
            }
        }
        return true;
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        writer->Write_Uint8(state.bans);
        for (int32_t turn : state.ban_start) writer->Write_Int32(turn);
        writer->Write_Uint8(state.fund);
        writer->Write_Int32(state.fund_start);
        writer->Write_Int32(state.fund_pool);
        for (const auto& row : state.last_use) {
            for (int32_t turn : row) writer->Write_Int32(turn);
        }
        writer->Write_Int32(state.last_antaran_attack);
        writer->Write_Int32(state.last_meeting);
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("resolutions save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported resolutions payload version");
        if (reader->Remaining() != SAVE_SIZE - sizeof(uint16_t)) return fail("resolutions save has the wrong size");
        pending = s_state{};
        pending.bans = reader->Read_Uint8();
        for (int32_t& turn : pending.ban_start) turn = reader->Read_Int32();
        pending.fund = reader->Read_Uint8();
        pending.fund_start = reader->Read_Int32();
        pending.fund_pool = reader->Read_Int32();
        for (auto& row : pending.last_use) {
            for (int32_t& turn : row) turn = reader->Read_Int32();
        }
        pending.last_antaran_attack = reader->Read_Int32();
        pending.last_meeting = reader->Read_Int32();
        if (reader->Failed()) return fail("resolutions save is truncated");
        if (pending.bans >= (1u << BAN_COUNT) || pending.fund > 1 || pending.fund_pool < 0) {
            return fail("resolutions save has an out-of-range value");
        }
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

} // namespace resolutions
