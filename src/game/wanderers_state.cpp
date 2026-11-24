#include "pch.h"

#include "game/wanderers.h"

#include "game/serialization.h"

// The wanderers, their queries and the save section; the behaviour is in wanderers.cpp. Nothing here draws
// from game_random.
namespace wanderers {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t RECORD_BYTES = 1 + 2 + 1 + 1 + 2 * MAX_ROUTE + 4 + 4 + 2 + 4 + 4;

        s_state state;
        s_state pending;
        bool has_pending = false;
    } // namespace

    s_state* State() {
        return &state;
    }

    const s_wanderer* At(e_kind kind, int16_t star) {
        for (int32_t i = 0; i < state.count; ++i) {
            if (state.wanderer[i].kind == kind && state.wanderer[i].star == star) return &state.wanderer[i];
        }
        return nullptr;
    }

    bool Star_Grounded(int16_t star) {
        if (!enabled) return false;
        for (int32_t i = 0; i < state.count; ++i) {
            const s_wanderer& w = state.wanderer[i];
            if (w.kind != KIND_STORM) continue;
            if (w.star == star || (w.trail_star == star && w.trail_until > MOX::stardate)) return true;
        }
        return false;
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
            const s_wanderer& w = state.wanderer[i];
            writer->Write_Uint8(w.kind);
            writer->Write_Int16(w.star);
            writer->Write_Int8(w.step);
            writer->Write_Int8(w.length);
            for (int32_t r = 0; r < MAX_ROUTE; ++r) writer->Write_Int16(w.route[r]);
            writer->Write_Int32(w.started);
            writer->Write_Int32(w.next_move);
            writer->Write_Int16(w.trail_star);
            writer->Write_Int32(w.trail_until);
            writer->Write_Int32(w.value);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t) + 1) return fail("wanderers save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported wanderers payload version");
        const uint8_t count = reader->Read_Uint8();
        if (count > MAX_WANDERERS) return fail("wanderers save has too many records");
        if (reader->Remaining() != count * RECORD_BYTES) return fail("wanderers save has the wrong size");
        pending = s_state{};
        pending.count = count;
        for (int32_t i = 0; i < count; ++i) {
            s_wanderer& w = pending.wanderer[i];
            w.kind = reader->Read_Uint8();
            w.star = reader->Read_Int16();
            w.step = reader->Read_Int8();
            w.length = reader->Read_Int8();
            for (int32_t r = 0; r < MAX_ROUTE; ++r) w.route[r] = reader->Read_Int16();
            w.started = reader->Read_Int32();
            w.next_move = reader->Read_Int32();
            w.trail_star = reader->Read_Int16();
            w.trail_until = reader->Read_Int32();
            w.value = reader->Read_Int32();
            if (w.kind != KIND_STORM && w.kind != KIND_TRADER) return fail("wanderers save has a bad kind");
            if (w.length < 2 || w.length > MAX_ROUTE || w.step < 0 || w.step >= w.length) return fail("wanderers save has a bad route");
            for (int32_t r = 0; r < w.length; ++r) {
                if (w.route[r] < 0 || w.route[r] >= MAX_STARS) return fail("wanderers save has a bad star");
            }
            if (w.star != w.route[w.step]) return fail("wanderers save has a wanderer off its route");
            if (w.trail_star < -1 || w.trail_star >= MAX_STARS) return fail("wanderers save has a bad trail");
            if (w.started < 0 || w.next_move < 0 || w.trail_until < 0) return fail("wanderers save has a bad stardate");
            if (w.value < 0) return fail("wanderers save has a bad value");
        }
        if (reader->Failed()) return fail("wanderers save is truncated");
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

} // namespace wanderers
