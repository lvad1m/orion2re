#include "pch.h"

#include "game/heroes.h"

#include "game/serialization.h"

#include <algorithm>

// The records of the leaders' events, the queries and the save section; the behaviour is in heroes.cpp. Nothing
// here draws from game_random.
namespace heroes {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t RECORD_BYTES = 1 + 1 + 2 + 1 + 4 + 4 + 2;

        s_state state;
        s_state pending;
        bool has_pending = false;
    } // namespace

    s_state* State() {
        return &state;
    }

    const s_record* Find(e_kind kind, int16_t player, int16_t leader) {
        if (!enabled) return nullptr;
        for (int32_t i = 0; i < state.count; ++i) {
            const s_record& r = state.record[i];
            if (r.kind == kind && (player < 0 || r.player == player) && (leader < 0 || r.leader == leader)) return &r;
        }
        return nullptr;
    }

    int32_t Active(int16_t player) {
        int32_t n = 0;
        for (int32_t i = 0; i < state.count; ++i) n += state.record[i].player == player ? 1 : 0;
        return n;
    }

    bool Controlled(int16_t leader) {
        return Find(KIND_MIND, -1, leader) != nullptr;
    }

    void Clear() {
        state = s_state{};
        Discard_Save();
    }

    bool Empty() {
        if (state.count != 0) return false;
        return std::all_of(std::begin(state.grown), std::end(state.grown), [](int16_t g) { return g == 0; });
    }

    uint32_t Save_Size() {
        return sizeof(uint16_t) + 1 + static_cast<uint32_t>(state.count) * RECORD_BYTES + MAX_LEADERS * sizeof(int16_t);
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        writer->Write_Uint8(static_cast<uint8_t>(state.count));
        for (int32_t i = 0; i < state.count; ++i) {
            const s_record& r = state.record[i];
            writer->Write_Uint8(r.kind);
            writer->Write_Int8(r.player);
            writer->Write_Int16(r.leader);
            writer->Write_Int8(r.other);
            writer->Write_Int32(r.started);
            writer->Write_Int32(r.ends);
            writer->Write_Int16(r.value);
        }
        for (int32_t i = 0; i < MAX_LEADERS; ++i) writer->Write_Int16(state.grown[i]);
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t) + 1) return fail("heroes save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported heroes payload version");
        const uint8_t count = reader->Read_Uint8();
        if (count > MAX_RECORDS) return fail("heroes save has too many records");
        if (reader->Remaining() != count * RECORD_BYTES + MAX_LEADERS * sizeof(int16_t)) return fail("heroes save has the wrong size");
        pending = s_state{};
        pending.count = count;
        for (int32_t i = 0; i < count; ++i) {
            s_record& r = pending.record[i];
            r.kind = reader->Read_Uint8();
            r.player = reader->Read_Int8();
            r.leader = reader->Read_Int16();
            r.other = reader->Read_Int8();
            r.started = reader->Read_Int32();
            r.ends = reader->Read_Int32();
            r.value = reader->Read_Int16();
            if (r.kind < KIND_MANAGER || r.kind > KIND_LAST) return fail("heroes save has a bad kind");
            if (r.player < 0 || r.player >= MAX_PLAYERS) return fail("heroes save has a bad empire");
            if (r.kind == KIND_MANAGER ? r.leader != -1 : (r.leader < 0 || r.leader >= MAX_LEADERS)) return fail("heroes save has a bad leader");
            if (r.kind == KIND_MIND ? (r.other < 0 || r.other >= MAX_PLAYERS || r.other == r.player) : r.other != -1) return fail("heroes save has a bad captor");
            if (r.started < 0 || r.ends < 0) return fail("heroes save has a bad stardate");
            if (r.value < 0 || r.value > 1000) return fail("heroes save has a bad value");
        }
        for (int32_t i = 0; i < MAX_LEADERS; ++i) {
            pending.grown[i] = reader->Read_Int16();
            if (pending.grown[i] < 0 || pending.grown[i] > RIVAL_XP_CAP) return fail("heroes save has a bad experience");
        }
        if (reader->Failed()) return fail("heroes save is truncated");
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

} // namespace heroes
