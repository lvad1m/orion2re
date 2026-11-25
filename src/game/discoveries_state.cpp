#include "pch.h"

#include "game/discoveries.h"

#include "game/serialization.h"

#include <algorithm>

// The records of the discoveries and the save section; the behaviour is in discoveries.cpp. Nothing here draws
// from game_random.
namespace discoveries {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 2;  // 1 had no finder race
        constexpr uint32_t RECORD_BYTES = 1 + 1 + 2 + 2 + 4 + 4 + 1 + 1 + 2;

        s_state state;
        s_state pending;
        bool has_pending = false;
    } // namespace

    s_state* State() {
        return &state;
    }

    uint8_t Finder_Race() {
        return state.finder_race;
    }

    bool Used(int16_t star) {
        if (star < 0 || star >= MAX_STARS) return false;
        return (state.used[star >> 3] & (1u << (star & 7))) != 0;
    }

    const s_record* Find(e_kind kind, int16_t player, int16_t star) {
        if (!enabled) return nullptr;
        for (int32_t i = 0; i < state.count; ++i) {
            const s_record& r = state.record[i];
            if (r.kind == kind && (player < 0 || r.player == player) && (star < 0 || r.star == star)) return &r;
        }
        return nullptr;
    }

    int32_t Active(int16_t player) {
        int32_t n = 0;
        for (int32_t i = 0; i < state.count; ++i) n += state.record[i].player == player ? 1 : 0;
        return n;
    }

    void Clear() {
        state = s_state{};
        Discard_Save();
    }

    bool Empty() {
        if (state.count != 0 || state.vaults_found != 0 || state.finder_race != NO_RACE) return false;
        return std::all_of(state.used, state.used + USED_BYTES, [](uint8_t b) { return b == 0; });
    }

    uint32_t Save_Size() {
        return sizeof(uint16_t) + 1 + 1 + static_cast<uint32_t>(state.count) * RECORD_BYTES + USED_BYTES + 1;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        writer->Write_Uint8(static_cast<uint8_t>(state.count));
        writer->Write_Uint8(state.vaults_found);
        for (int32_t i = 0; i < state.count; ++i) {
            const s_record& r = state.record[i];
            writer->Write_Uint8(r.kind);
            writer->Write_Int8(r.player);
            writer->Write_Int16(r.star);
            writer->Write_Int16(r.other);
            writer->Write_Int32(r.started);
            writer->Write_Int32(r.ends);
            writer->Write_Int8(r.option);
            writer->Write_Int8(r.stage);
            writer->Write_Int16(r.value);
        }
        for (int32_t i = 0; i < USED_BYTES; ++i) writer->Write_Uint8(state.used[i]);
        writer->Write_Uint8(state.finder_race);
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t) + 2) return fail("discoveries save is truncated");
        const uint16_t version = reader->Read_Uint16();
        if (version != 1 && version != SAVE_PAYLOAD_VERSION) return fail("unsupported discoveries payload version");
        const uint8_t count = reader->Read_Uint8();
        const uint8_t vaults = reader->Read_Uint8();
        if (count > MAX_RECORDS) return fail("discoveries save has too many records");
        if (vaults > VAULTS_TOTAL) return fail("discoveries save has too many vaults");
        if (reader->Remaining() != count * RECORD_BYTES + USED_BYTES + (version == 1 ? 0 : 1)) return fail("discoveries save has the wrong size");
        pending = s_state{};
        pending.count = count;
        pending.vaults_found = vaults;
        for (int32_t i = 0; i < count; ++i) {
            s_record& r = pending.record[i];
            r.kind = reader->Read_Uint8();
            r.player = reader->Read_Int8();
            r.star = reader->Read_Int16();
            r.other = reader->Read_Int16();
            r.started = reader->Read_Int32();
            r.ends = reader->Read_Int32();
            r.option = reader->Read_Int8();
            r.stage = reader->Read_Int8();
            r.value = reader->Read_Int16();
            if (r.kind < KIND_POWER_PLANT || r.kind > KIND_LAST) return fail("discoveries save has a bad kind");
            if (r.player < 0 || r.player >= MAX_PLAYERS) return fail("discoveries save has a bad empire");
            if (r.star < 0 || r.star >= MAX_STARS) return fail("discoveries save has a bad star");
            if (r.kind == KIND_VAULT ? r.other != -1 : (r.other < 0 || r.other >= MAX_COLONIES)) return fail("discoveries save has a bad colony");
            const bool has_choice = r.kind == KIND_POWER_PLANT || r.kind == KIND_SYMBIONTS || r.kind == KIND_NATIVES;
            if (has_choice ? (r.option < 0 || r.option > 2) : r.option != 0) return fail("discoveries save has a bad option");
            if (r.kind == KIND_TOMB ? (r.stage < 0 || r.stage > 1) : r.stage != 0) return fail("discoveries save has a bad stage");
            if (r.value != 0) return fail("discoveries save has a bad value");
            if (r.started < 0 || r.ends < 0) return fail("discoveries save has a bad stardate");
        }
        for (int32_t i = 0; i < USED_BYTES; ++i) pending.used[i] = reader->Read_Uint8();
        if (version != 1) {
            pending.finder_race = reader->Read_Uint8();
            if (pending.finder_race != NO_RACE && pending.finder_race >= MAX_RACE) return fail("discoveries save has a bad finder race");
        }
        if (reader->Failed()) return fail("discoveries save is truncated");
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

} // namespace discoveries
