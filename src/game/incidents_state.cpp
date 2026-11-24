#include "pch.h"

#include "game/incidents.h"

#include "game/serialization.h"

#include <algorithm>

// The incident records, the hooks that read them and the save section; the behaviour is in incidents.cpp.
// Nothing here draws from game_random.
namespace incidents {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t RECORD_BYTES = 1 + 1 + 2 + 2 + 4 + 4 + 1 + 1 + 1 + 1 + 2;

        s_state state;
        s_state pending;
        bool has_pending = false;

        bool Live_(const s_incident& i, int16_t colony) {
            return i.kind != KIND_NONE && i.kind != KIND_TREASURY && i.kind != KIND_REBEL_GENERAL && i.target == colony &&
                i.ends > MOX::stardate;
        }
    } // namespace

    s_state* State() {
        return &state;
    }

    int32_t Morale_Bonus(const s_colony* colony) {
        if (!enabled || colony == nullptr) return 0;
        const int16_t index = static_cast<int16_t>(colony - MOX::colony);
        int32_t bonus = 0;
        for (int32_t i = 0; i < state.count; ++i) {
            if (Live_(state.incident[i], index)) bonus += state.incident[i].morale;
        }
        return bonus;
    }

    int32_t Unrest(int16_t colony) {
        if (!enabled || colony < 0) return 0;
        int32_t unrest = 0;
        for (int32_t i = 0; i < state.count; ++i) {
            if (Live_(state.incident[i], colony)) unrest += state.incident[i].unrest;
        }
        return unrest;
    }

    bool Plague(int16_t colony) {
        if (!enabled || colony < 0) return false;
        for (int32_t i = 0; i < state.count; ++i) {
            const s_incident& r = state.incident[i];
            if (r.kind == KIND_PAINLESS && r.option == PAINLESS_RECRUIT && r.target == colony && r.started + r.value > MOX::stardate) return true;
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
            const s_incident& r = state.incident[i];
            writer->Write_Uint8(r.kind);
            writer->Write_Int8(r.player);
            writer->Write_Int16(r.target);
            writer->Write_Int16(r.other);
            writer->Write_Int32(r.started);
            writer->Write_Int32(r.ends);
            writer->Write_Int8(r.option);
            writer->Write_Uint8(r.stage);
            writer->Write_Int8(r.morale);
            writer->Write_Int8(r.unrest);
            writer->Write_Int16(r.value);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t) + 1) return fail("incidents save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported incidents payload version");
        const uint8_t count = reader->Read_Uint8();
        if (count > MAX_INCIDENTS) return fail("incidents save has too many records");
        if (reader->Remaining() != count * RECORD_BYTES) return fail("incidents save has the wrong size");
        pending = s_state{};
        pending.count = count;
        for (int32_t i = 0; i < count; ++i) {
            s_incident& r = pending.incident[i];
            r.kind = reader->Read_Uint8();
            r.player = reader->Read_Int8();
            r.target = reader->Read_Int16();
            r.other = reader->Read_Int16();
            r.started = reader->Read_Int32();
            r.ends = reader->Read_Int32();
            r.option = reader->Read_Int8();
            r.stage = reader->Read_Uint8();
            r.morale = reader->Read_Int8();
            r.unrest = reader->Read_Int8();
            r.value = reader->Read_Int16();
            if (r.kind < KIND_PAINLESS || r.kind > KIND_LAST) return fail("incidents save has a bad kind");
            if (r.player < 0 || r.player >= MAX_PLAYERS) return fail("incidents save has a bad empire");
            if (r.kind == KIND_TREASURY) {
                if (r.other < 0 || r.other >= MAX_PLAYERS || r.other == r.player) return fail("incidents save has a bad owner of the treasury");
            } else if (r.kind == KIND_REBEL_GENERAL) {
                if (r.other < 0 || r.other >= MAX_LEADERS || r.target != -1) return fail("incidents save has a bad general");
            } else {
                if (r.target < 0 || r.target >= MAX_COLONIES) return fail("incidents save has a bad colony");
                if (r.other < -1 || r.other >= MAX_LEADERS) return fail("incidents save has a bad leader");
            }
            if (r.option < 0 || r.option > 2 || r.stage > 1) return fail("incidents save has a bad option");
            if (r.morale < -10 || r.morale > 10 || r.unrest < 0 || r.unrest > 30) return fail("incidents save has a bad modifier");
            if (r.started < 0 || r.ends < 0) return fail("incidents save has a bad stardate");
        }
        if (reader->Failed()) return fail("incidents save is truncated");
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

} // namespace incidents
