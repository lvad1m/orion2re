#include "pch.h"

#include "game/overtures.h"

#include "game/serialization.h"

// The records of the peacekeepers and the paid offensives and the save section; the behaviour is in
// overtures.cpp. Nothing here draws from game_random.
namespace overtures {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t RECORD_BYTES = 1 + 1 + 2 + 2 + 4 + 4 + 1 + 2;

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

    uint32_t Save_Size() {
        return sizeof(uint16_t) + 1 + static_cast<uint32_t>(state.count) * RECORD_BYTES;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        writer->Write_Uint8(static_cast<uint8_t>(state.count));
        for (int32_t i = 0; i < state.count; ++i) {
            const s_record& r = state.record[i];
            writer->Write_Uint8(r.kind);
            writer->Write_Int8(r.player);
            writer->Write_Int16(r.target);
            writer->Write_Int16(r.other);
            writer->Write_Int32(r.started);
            writer->Write_Int32(r.ends);
            writer->Write_Int8(r.option);
            writer->Write_Int16(r.value);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t) + 1) return fail("overtures save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported overtures payload version");
        const uint8_t count = reader->Read_Uint8();
        if (count > MAX_RECORDS) return fail("overtures save has too many records");
        if (reader->Remaining() != count * RECORD_BYTES) return fail("overtures save has the wrong size");
        pending = s_state{};
        pending.count = count;
        for (int32_t i = 0; i < count; ++i) {
            s_record& r = pending.record[i];
            r.kind = reader->Read_Uint8();
            r.player = reader->Read_Int8();
            r.target = reader->Read_Int16();
            r.other = reader->Read_Int16();
            r.started = reader->Read_Int32();
            r.ends = reader->Read_Int32();
            r.option = reader->Read_Int8();
            r.value = reader->Read_Int16();
            if (r.kind != KIND_PEACEKEEPERS && r.kind != KIND_OFFENSIVE) return fail("overtures save has a bad kind");
            if (r.player < 0 || r.player >= MAX_PLAYERS) return fail("overtures save has a bad empire");
            if (r.target < 0 || r.target >= MAX_PLAYERS || r.other < 0 || r.other >= MAX_PLAYERS) return fail("overtures save has a bad party");
            if (r.target == r.other || r.target == r.player || r.other == r.player) return fail("overtures save has the same empire twice");
            if (r.option < 0 || r.option > 2) return fail("overtures save has a bad option");
            if (r.value < 0) return fail("overtures save has a bad payment");
            if (r.started < 0 || r.ends < 0) return fail("overtures save has a bad stardate");
        }
        if (reader->Failed()) return fail("overtures save is truncated");
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

} // namespace overtures
