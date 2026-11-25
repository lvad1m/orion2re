#include "pch.h"

#include "game/moo_world.h"

#include "game/serialization.h"

// The records of the world events, the queries and the save section; the behaviour is in moo_world.cpp. Nothing
// here draws from game_random.
namespace moo_world {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 2;
        constexpr uint32_t HEADER_BYTES_V1 = sizeof(uint16_t) + 1 + sizeof(int16_t);
        // Version 2 adds the three masks (uint16) and the last scout's stardate (int32).
        constexpr uint32_t HEADER_BYTES_V2 = HEADER_BYTES_V1 + 3 * sizeof(uint16_t) + sizeof(int32_t);
        constexpr uint16_t PLAYER_BITS = static_cast<uint16_t>((1u << MAX_PLAYERS) - 1);
        constexpr uint32_t RECORD_BYTES = 1 + 1 + 2 + 2 + 4 + 4 + 1 + 2 + 2;
        constexpr int32_t MAX_VALUE = 10000;

        s_state state;
        s_state pending;
        bool has_pending = false;
    } // namespace

    s_state* State() {
        return &state;
    }

    bool Star_Grounded(int16_t star) {
        if (!enabled) return false;
        for (int32_t i = 0; i < state.count; ++i) {
            const s_record& r = state.record[i];
            if (r.kind == KIND_FLEET_EPIDEMIC && r.target == star && r.ends > MOX::stardate) return true;
        }
        return false;
    }

    bool King_Near(int16_t player) {
        if (!enabled) return false;
        for (int32_t i = 0; i < state.count; ++i) {
            const s_record& r = state.record[i];
            if (r.kind != KIND_PIRATE_KING || r.target < 0 || r.target >= MAX_STARS) continue;
            for (int16_t c = 0; c < MOX::NUM_COLONIES && c < MAX_COLONIES; ++c) {
                const s_colony& colony = MOX::colony[c];
                if (colony.owner != player || colony.planet < 0) continue;
                const int16_t star = MOX::planet[colony.planet].star_index;
                if (star < 0 || star >= MAX_STARS) continue;
                const int64_t dx = MOX::star[star].x - MOX::star[r.target].x;
                const int64_t dy = MOX::star[star].y - MOX::star[r.target].y;
                if (dx * dx + dy * dy <= KING_REACH2) return true;
            }
        }
        return false;
    }

    bool Scout_Near(int16_t player) {
        if (!enabled) return false;
        for (int32_t i = 0; i < state.count; ++i) {
            const s_record& r = state.record[i];
            if (r.kind == KIND_ANTARAN_SCOUT && r.player == player && r.ends > MOX::stardate) return true;
        }
        return false;
    }

    void Clear() {
        state = s_state{};
        Discard_Save();
    }

    bool Empty() {
        return state.count == 0 && state.unite_turns == 0 && state.exile_mask == 0 && state.guilty_mask == 0 && state.agreed_mask == 0 &&
               state.last_scout == 0;
    }

    uint32_t Save_Size() {
        return HEADER_BYTES_V2 + static_cast<uint32_t>(state.count) * RECORD_BYTES;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        writer->Write_Uint8(static_cast<uint8_t>(state.count));
        writer->Write_Int16(state.unite_turns);
        writer->Write_Uint16(state.exile_mask);
        writer->Write_Uint16(state.guilty_mask);
        writer->Write_Uint16(state.agreed_mask);
        writer->Write_Int32(state.last_scout);
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
        if (reader == nullptr || reader->Remaining() < HEADER_BYTES_V1) return fail("moo_world save is truncated");
        const uint16_t version = reader->Read_Uint16();
        if (version != 1 && version != SAVE_PAYLOAD_VERSION) return fail("unsupported moo_world payload version");
        if (version == SAVE_PAYLOAD_VERSION && reader->Remaining() < HEADER_BYTES_V2 - sizeof(uint16_t)) return fail("moo_world save is truncated");
        const uint8_t count = reader->Read_Uint8();
        if (count > MAX_RECORDS) return fail("moo_world save has too many records");
        pending = s_state{};
        pending.unite_turns = reader->Read_Int16();
        if (pending.unite_turns < 0 || pending.unite_turns > MAX_VALUE) return fail("moo_world save has a bad unite counter");
        if (version == SAVE_PAYLOAD_VERSION) {
            pending.exile_mask = reader->Read_Uint16();
            pending.guilty_mask = reader->Read_Uint16();
            pending.agreed_mask = reader->Read_Uint16();
            pending.last_scout = reader->Read_Int32();
            if ((pending.exile_mask & ~PLAYER_BITS) != 0 || (pending.guilty_mask & ~PLAYER_BITS) != 0 || (pending.agreed_mask & ~PLAYER_BITS) != 0) {
                return fail("moo_world save has a bad empire mask");
            }
            if (pending.last_scout < 0) return fail("moo_world save has a bad scout stardate");
        }
        if (reader->Remaining() != count * RECORD_BYTES) return fail("moo_world save has the wrong size");
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
            if (r.kind < KIND_PIRATE_KING || r.kind > KIND_LAST) return fail("moo_world save has a bad kind");
            if (r.player < -1 || r.player >= MAX_PLAYERS) return fail("moo_world save has a bad empire");
            if ((r.kind == KIND_FLEET_EPIDEMIC || r.kind == KIND_ANTARAN_SCOUT || r.kind == KIND_REFUGEES) && r.player < 0) {
                return fail("moo_world save has a record without an empire");
            }
            if (r.kind == KIND_REFUGEES && (r.option < 0 || r.option >= MAX_PLAYERS || r.value > 9)) return fail("moo_world save has bad refugees");
            if (r.target < 0 || r.target >= MAX_STARS) return fail("moo_world save has a bad star");
            if (r.other < -1 || r.other >= MAX_STARS) return fail("moo_world save has a bad second star");
            if (r.started < 0 || r.ends < 0) return fail("moo_world save has a bad stardate");
            if (r.value < 0 || r.value > MAX_VALUE || r.extra < 0 || r.extra > MAX_VALUE) return fail("moo_world save has a bad value");
        }
        if (reader->Failed()) return fail("moo_world save is truncated");
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

} // namespace moo_world
