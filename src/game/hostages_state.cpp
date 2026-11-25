#include "pch.h"

#include "game/hostages.h"

#include "game/serialization.h"

// The held leaders, their queries and the save section; the behaviour is in hostages.cpp. Nothing here
// draws from game_random.
namespace hostages {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 2;  // 1: leaders only
        constexpr uint32_t RECORD_BYTES = 3 + 1 + 4;
        constexpr uint32_t SPY_RECORD_BYTES = 2 + 1 + 4;

        s_state state;
        s_state pending;
        bool has_pending = false;
    } // namespace

    s_state* State() {
        return &state;
    }

    int32_t Find(int16_t leader) {
        for (int32_t i = 0; i < state.count; ++i) {
            if (state.held[i].leader == leader) return i;
        }
        return -1;
    }

    int32_t Spies_Held(int16_t jailer, int16_t owner) {
        if (!spy_prison) return 0;
        int32_t n = 0;
        for (int32_t i = 0; i < state.spy_count; ++i) {
            if (state.spies[i].jailer == jailer && state.spies[i].owner == owner) n += state.spies[i].count;
        }
        return n;
    }

    int32_t Count_Held(int16_t owner, int16_t holder) {
        if (!hostage_swaps && !prisoners) return 0;
        int32_t n = 0;
        for (int32_t i = 0; i < state.count; ++i) {
            if (state.held[i].owner == owner && state.held[i].holder == holder) ++n;
        }
        return n;
    }

    void Clear() {
        state = s_state{};
        Discard_Save();
    }

    bool Empty() {
        return state.count == 0 && state.spy_count == 0;
    }

    uint32_t Save_Size() {
        return sizeof(uint16_t) + 1 + static_cast<uint32_t>(state.count) * RECORD_BYTES + 1 +
            static_cast<uint32_t>(state.spy_count) * SPY_RECORD_BYTES;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        writer->Write_Uint8(static_cast<uint8_t>(state.count));
        for (int32_t i = 0; i < state.count; ++i) {
            const s_held& h = state.held[i];
            writer->Write_Int8(h.leader);
            writer->Write_Int8(h.owner);
            writer->Write_Int8(h.holder);
            writer->Write_Uint8(h.kind);
            writer->Write_Int32(h.since);
        }
        writer->Write_Uint8(static_cast<uint8_t>(state.spy_count));
        for (int32_t i = 0; i < state.spy_count; ++i) {
            const s_spies& c = state.spies[i];
            writer->Write_Int8(c.jailer);
            writer->Write_Int8(c.owner);
            writer->Write_Uint8(c.count);
            writer->Write_Int32(c.since);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t) + 1) return fail("hostages save is truncated");
        const uint16_t version = reader->Read_Uint16();
        if (version != 1 && version != SAVE_PAYLOAD_VERSION) return fail("unsupported hostages payload version");
        const uint8_t count = reader->Read_Uint8();
        if (count > MAX_HELD) return fail("hostages save has too many records");
        if (version == 1 ? reader->Remaining() != count * RECORD_BYTES : reader->Remaining() < count * RECORD_BYTES + 1) {
            return fail("hostages save has the wrong size");
        }
        pending = s_state{};
        pending.count = count;
        for (int32_t i = 0; i < count; ++i) {
            s_held& h = pending.held[i];
            h.leader = reader->Read_Int8();
            h.owner = reader->Read_Int8();
            h.holder = reader->Read_Int8();
            h.kind = reader->Read_Uint8();
            h.since = reader->Read_Int32();
            if (h.leader < 0 || h.leader >= MAX_LEADERS) return fail("hostages save has a bad leader");
            if (h.owner < 0 || h.owner >= MAX_PLAYERS || h.holder < 0 || h.holder >= MAX_PLAYERS) return fail("hostages save has a bad empire");
            if (h.kind > KIND_TRAITOR) return fail("hostages save has a bad kind");
            if ((h.owner == h.holder) != (h.kind >= KIND_JAIL)) return fail("hostages save has a leader held by the wrong empire");
            if (h.since < 0) return fail("hostages save has a bad stardate");
            for (int32_t j = 0; j < i; ++j) {
                if (pending.held[j].leader == h.leader) return fail("hostages save holds a leader twice");
            }
        }
        if (version >= 2) {
            const uint8_t spy_count = reader->Read_Uint8();
            if (spy_count > MAX_SPY_CELLS) return fail("hostages save has too many spy cells");
            if (reader->Remaining() != spy_count * SPY_RECORD_BYTES) return fail("hostages save has the wrong size");
            pending.spy_count = spy_count;
            for (int32_t i = 0; i < spy_count; ++i) {
                s_spies& c = pending.spies[i];
                c.jailer = reader->Read_Int8();
                c.owner = reader->Read_Int8();
                c.count = reader->Read_Uint8();
                c.since = reader->Read_Int32();
                if (c.jailer < 0 || c.jailer >= MAX_PLAYERS || c.owner < 0 || c.owner >= MAX_PLAYERS || c.jailer == c.owner) {
                    return fail("hostages save has a bad spy empire");
                }
                if (c.count < 1 || c.count > MAX_SPIES_PER_CELL) return fail("hostages save has a bad spy count");
                if (c.since < 0) return fail("hostages save has a bad stardate");
                for (int32_t j = 0; j < i; ++j) {
                    if (pending.spies[j].jailer == c.jailer && pending.spies[j].owner == c.owner) return fail("hostages save has a spy cell twice");
                }
            }
        }
        if (reader->Failed()) return fail("hostages save is truncated");
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

} // namespace hostages
