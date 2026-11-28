#include "pch.h"

#include "game/choices.h"

#include "game/consts.h"
#include "game/serialization.h"

// The queue of pending choices and its save section; the behaviour is in choices.cpp. Nothing here draws
// from game_random.
namespace choices {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t RECORD_BYTES = 1 + 1 + 1 + 2 + 2 + 4 + 4;

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
        return state.count == 0;
    }

    int32_t Count(int16_t player) {
        int32_t n = 0;
        for (int32_t i = 0; i < state.count; ++i) {
            if (state.choice[i].player == player) ++n;
        }
        return n;
    }

    uint32_t Save_Size() {
        return sizeof(uint16_t) + 1 + static_cast<uint32_t>(state.count) * RECORD_BYTES;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        writer->Write_Uint8(static_cast<uint8_t>(state.count));
        for (int32_t i = 0; i < state.count; ++i) {
            const s_choice& c = state.choice[i];
            writer->Write_Uint8(c.module);
            writer->Write_Uint8(c.kind);
            writer->Write_Int8(c.player);
            writer->Write_Int16(c.target);
            writer->Write_Int16(c.other);
            writer->Write_Int32(c.value);
            writer->Write_Int32(c.asked);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t) + 1) return fail("choices save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported choices payload version");
        const uint8_t count = reader->Read_Uint8();
        if (count > MAX_PENDING) return fail("choices save has too many records");
        if (reader->Remaining() != count * RECORD_BYTES) return fail("choices save has the wrong size");
        pending = s_state{};
        pending.count = count;
        for (int32_t i = 0; i < count; ++i) {
            s_choice& c = pending.choice[i];
            c.module = reader->Read_Uint8();
            c.kind = reader->Read_Uint8();
            c.player = reader->Read_Int8();
            c.target = reader->Read_Int16();
            c.other = reader->Read_Int16();
            c.value = reader->Read_Int32();
            c.asked = reader->Read_Int32();
            if (c.module < MODULE_INCIDENTS || c.module > MODULE_LAST) return fail("choices save has a bad module");
            if (c.player < 0 || c.player >= MAX_PLAYERS) return fail("choices save has a bad empire");
            if (c.target < -1 || c.other < -1) return fail("choices save has a bad subject");
            if (c.asked < 0) return fail("choices save has a bad stardate");
        }
        if (reader->Failed()) return fail("choices save is truncated");
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

} // namespace choices
