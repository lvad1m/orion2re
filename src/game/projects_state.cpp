#include "pch.h"

#include "game/projects.h"

#include "game/serialization.h"

// The running projects, their queries and the save section; the behaviour is in projects.cpp. Nothing here
// draws from game_random.
namespace projects {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t RECORD_BYTES = 1 + 1 + 2 + 4 + 4 + 1 + 1;

        s_state state;
        s_state pending;
        bool has_pending = false;
    } // namespace

    s_state* State() {
        return &state;
    }

    int32_t Active(int16_t player) {
        int32_t n = 0;
        for (int32_t i = 0; i < state.count; ++i) {
            if (state.project[i].player == player) ++n;
        }
        return n;
    }

    const s_project* Find(e_kind kind, int16_t player, int16_t star) {
        for (int32_t i = 0; i < state.count; ++i) {
            const s_project& p = state.project[i];
            if (p.kind == kind && p.player == player && (star < 0 || p.star == star)) return &p;
        }
        return nullptr;
    }

    bool Studied(int16_t star) {
        if (star < 0 || star >= MAX_STARS) return false;
        return ((state.studied[star >> 3] >> (star & 7)) & 1) != 0;
    }

    void Clear() {
        state = s_state{};
        Discard_Save();
    }

    bool Empty() {
        if (state.count != 0) return false;
        for (uint8_t b : state.studied) {
            if (b != 0) return false;
        }
        return true;
    }

    uint32_t Save_Size() {
        return sizeof(uint16_t) + 1 + static_cast<uint32_t>(state.count) * RECORD_BYTES + STUDIED_BYTES;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        writer->Write_Uint8(static_cast<uint8_t>(state.count));
        for (int32_t i = 0; i < state.count; ++i) {
            const s_project& p = state.project[i];
            writer->Write_Uint8(p.kind);
            writer->Write_Int8(p.player);
            writer->Write_Int16(p.star);
            writer->Write_Int32(p.started);
            writer->Write_Uint8(p.turns);
            writer->Write_Uint8(p.elapsed);
            writer->Write_Uint8(p.chapters);
            writer->Write_Uint8(p.stage);
            writer->Write_Int8(p.difficulty);
            writer->Write_Uint8(p.failures);
        }
        writer->Write_Bytes(state.studied, STUDIED_BYTES);
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t) + 1) return fail("projects save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported projects payload version");
        const uint8_t count = reader->Read_Uint8();
        if (count > MAX_PROJECTS) return fail("projects save has too many records");
        if (reader->Remaining() != count * RECORD_BYTES + STUDIED_BYTES) return fail("projects save has the wrong size");
        pending = s_state{};
        pending.count = count;
        for (int32_t i = 0; i < count; ++i) {
            s_project& p = pending.project[i];
            p.kind = reader->Read_Uint8();
            p.player = reader->Read_Int8();
            p.star = reader->Read_Int16();
            p.started = reader->Read_Int32();
            p.turns = reader->Read_Uint8();
            p.elapsed = reader->Read_Uint8();
            p.chapters = reader->Read_Uint8();
            p.stage = reader->Read_Uint8();
            p.difficulty = reader->Read_Int8();
            p.failures = reader->Read_Uint8();
            if (p.kind != KIND_ANOMALY && p.kind != KIND_EXCAVATION) return fail("projects save has a bad kind");
            if (p.player < 0 || p.player >= MAX_PLAYERS) return fail("projects save has a bad empire");
            if (p.star < 0 || p.star >= MAX_STARS) return fail("projects save has a bad star");
            if (p.started < 0) return fail("projects save has a bad stardate");
            if (p.turns == 0 || p.elapsed > p.turns) return fail("projects save has a bad timer");
            if (p.chapters == 0 || p.chapters > MAX_CHAPTERS || p.stage >= p.chapters) return fail("projects save has bad chapters");
            if (p.failures > DIG_MAX_FAILURES) return fail("projects save has too many failures");
        }
        reader->Read_Bytes(pending.studied, STUDIED_BYTES);
        if (reader->Failed()) return fail("projects save is truncated");
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

} // namespace projects
