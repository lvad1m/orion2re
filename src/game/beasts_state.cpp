#include "pch.h"

#include "game/beasts.h"

#include "game/serialization.h"

// The records of the beasts and the save section; the behaviour is in beasts.cpp. Nothing here draws from
// game_random.
namespace beasts {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t RECORD_BYTES = 1 + 1 + 2 + 2 + 4 + 4 + 1 + 2 + 2;

        s_state state;
        s_state pending;
        bool has_pending = false;
    } // namespace

    s_state* State() {
        return &state;
    }

    const s_record* At(e_kind kind, int16_t star) {
        if (!enabled) return nullptr;
        for (int32_t i = 0; i < state.count; ++i) {
            if (state.record[i].kind == kind && (star < 0 || state.record[i].target == star)) return &state.record[i];
        }
        return nullptr;
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
            writer->Write_Int16(r.extra);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t) + 1) return fail("beasts save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported beasts payload version");
        const uint8_t count = reader->Read_Uint8();
        if (count > MAX_RECORDS) return fail("beasts save has too many records");
        if (reader->Remaining() != count * RECORD_BYTES) return fail("beasts save has the wrong size");
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
            r.extra = reader->Read_Int16();
            if (r.kind < KIND_HARVESTERS || r.kind > KIND_LAST) return fail("beasts save has a bad kind");
            if (r.target < 0 || r.target >= MAX_STARS) return fail("beasts save has a bad star");
            // The empire: harvesters have none; a swallowing ambush names the owner of the ship.
            const bool has_player = r.kind == KIND_HIJACK || r.kind == KIND_WORMHOLE || (r.kind == KIND_AMBUSH && r.option == 1);
            if (has_player ? (r.player < 0 || r.player >= MAX_PLAYERS) : r.player != -1) return fail("beasts save has a bad empire");
            if (r.kind == KIND_WORMHOLE) {
                if (r.other < 0 || r.other >= MAX_STARS || r.other == r.target) return fail("beasts save has a bad exit");
            } else if (r.kind == KIND_AMBUSH && r.option == 1) {
                if (r.other < 0 || r.other >= MAX_SHIPS) return fail("beasts save has a bad ship");
            } else if (r.other != -1) {
                return fail("beasts save has a stray party");
            }
            if (r.kind == KIND_AMBUSH ? (r.option < 0 || r.option > 1) : r.option != 0) return fail("beasts save has a bad option");
            if (r.value < 0) return fail("beasts save has a bad value");
            if (r.extra != 0) return fail("beasts save has a bad extra");
            if (r.started < 0 || r.ends < 0) return fail("beasts save has a bad stardate");
        }
        if (reader->Failed()) return fail("beasts save is truncated");
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

} // namespace beasts
