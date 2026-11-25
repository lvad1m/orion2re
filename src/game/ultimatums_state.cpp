#include "pch.h"

#include "game/ultimatums.h"

#include "game/serialization.h"

// The records of the ultimatums, the query of a lawful cause and the save section; the behaviour is in
// ultimatums.cpp. Nothing here draws from game_random.
namespace ultimatums {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t RECORD_BYTES = 1 + 1 + 1 + 4 + 4 + 4;

        s_state state;
        s_state pending;
        bool has_pending = false;
    } // namespace

    s_state* State() {
        return &state;
    }

    int32_t Find(int16_t demander, int16_t victim) {
        for (int32_t i = 0; i < state.count; ++i) {
            if (state.record[i].demander == demander && state.record[i].victim == victim) return i;
        }
        return -1;
    }

    bool Has_Cause(int16_t demander, int16_t victim) {
        if (!enabled || state.count == 0) return false;
        const int32_t i = Find(demander, victim);
        return i >= 0 && state.record[i].state == STATE_CAUSE && MOX::stardate < state.record[i].deadline + CAUSE_TURNS;
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
            writer->Write_Int8(r.demander);
            writer->Write_Int8(r.victim);
            writer->Write_Uint8(r.state);
            writer->Write_Int32(r.issued);
            writer->Write_Int32(r.deadline);
            writer->Write_Int32(r.amount);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t) + 1) return fail("ultimatums save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported ultimatums payload version");
        const uint8_t count = reader->Read_Uint8();
        if (count > MAX_RECORDS) return fail("ultimatums save has too many records");
        if (reader->Remaining() != count * RECORD_BYTES) return fail("ultimatums save has the wrong size");
        pending = s_state{};
        pending.count = count;
        for (int32_t i = 0; i < count; ++i) {
            s_record& r = pending.record[i];
            r.demander = reader->Read_Int8();
            r.victim = reader->Read_Int8();
            r.state = reader->Read_Uint8();
            r.issued = reader->Read_Int32();
            r.deadline = reader->Read_Int32();
            r.amount = reader->Read_Int32();
            if (r.demander < 0 || r.demander >= MAX_PLAYERS || r.victim < 0 || r.victim >= MAX_PLAYERS) return fail("ultimatums save has a bad empire");
            if (r.demander == r.victim) return fail("ultimatums save has the same empire twice");
            if (r.state != STATE_OPEN && r.state != STATE_CAUSE) return fail("ultimatums save has a bad state");
            if (r.issued < 0 || r.deadline < r.issued) return fail("ultimatums save has a bad stardate");
            if (r.amount < 0) return fail("ultimatums save has a bad sum");
            for (int32_t j = 0; j < i; ++j) {
                if (pending.record[j].demander == r.demander && pending.record[j].victim == r.victim) return fail("ultimatums save has a pair twice");
            }
        }
        if (reader->Failed()) return fail("ultimatums save is truncated");
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

} // namespace ultimatums
