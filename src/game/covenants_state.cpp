#include "pch.h"

#include "game/covenants.h"

#include "game/serialization.h"

#include <algorithm>

// The state of secret clauses and marriages, its queries and its save section; the behaviour is in
// covenants.cpp. Nothing here draws from game_random.
namespace covenants {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t CLAUSE_BYTES = 3 + 1 + 4;
        constexpr uint32_t MARRIAGE_BYTES = MAX_PLAYERS * (1 + 4);

        static_assert(MAX_PLAYERS == 8, "the covenants state is sized for 8 players");

        s_state state;
        s_state pending;
        bool has_pending = false;
    } // namespace

    s_state* State() {
        return &state;
    }

    int16_t Spouse(int16_t player) {
        if (!marriages || player < 0 || player >= MAX_PLAYERS) return -1;
        return state.spouse[player];
    }

    int32_t Find_Clause(int16_t guarantor, int16_t protege, int16_t target) {
        for (int32_t i = 0; i < state.count; ++i) {
            const s_clause& c = state.clauses[i];
            if (c.guarantor == guarantor && c.protege == protege && c.target == target) return i;
        }
        return -1;
    }

    bool Clause_Against(int16_t party, int16_t target) {
        if (!secret_clauses) return false;
        for (int32_t i = 0; i < state.count; ++i) {
            const s_clause& c = state.clauses[i];
            if (c.target == target && (c.guarantor == party || c.protege == party)) return true;
        }
        return false;
    }

    void Clear() {
        state = s_state{};
        Discard_Save();
    }

    bool Empty() {
        return state.count == 0 && std::all_of(std::begin(state.spouse), std::end(state.spouse), [](int8_t v) { return v == -1; });
    }

    uint32_t Save_Size() {
        return sizeof(uint16_t) + 1 + static_cast<uint32_t>(state.count) * CLAUSE_BYTES + MARRIAGE_BYTES;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        writer->Write_Uint8(static_cast<uint8_t>(state.count));
        for (int32_t i = 0; i < state.count; ++i) {
            const s_clause& c = state.clauses[i];
            writer->Write_Int8(c.guarantor);
            writer->Write_Int8(c.protege);
            writer->Write_Int8(c.target);
            writer->Write_Uint8(c.revealed);
            writer->Write_Int32(c.since);
        }
        for (int p = 0; p < MAX_PLAYERS; ++p) {
            writer->Write_Int8(state.spouse[p]);
            writer->Write_Int32(state.married_since[p]);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t) + 1) return fail("covenants save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported covenants payload version");
        const uint8_t count = reader->Read_Uint8();
        if (count > MAX_CLAUSES) return fail("covenants save has too many clauses");
        if (reader->Remaining() != count * CLAUSE_BYTES + MARRIAGE_BYTES) return fail("covenants save has the wrong size");
        pending = s_state{};
        pending.count = count;
        const auto valid_player = [](int8_t p) { return p >= 0 && p < MAX_PLAYERS; };
        for (int32_t i = 0; i < count; ++i) {
            s_clause& c = pending.clauses[i];
            c.guarantor = reader->Read_Int8();
            c.protege = reader->Read_Int8();
            c.target = reader->Read_Int8();
            c.revealed = reader->Read_Uint8();
            c.since = reader->Read_Int32();
            if (!valid_player(c.guarantor) || !valid_player(c.protege) || !valid_player(c.target)) return fail("covenants save has a bad clause party");
            if (c.guarantor == c.protege || c.guarantor == c.target || c.protege == c.target) return fail("covenants save has a clause with a repeated party");
            if (c.revealed > 1) return fail("covenants save has a bad revealed flag");
            if (c.since < 0) return fail("covenants save has a bad stardate");
        }
        for (int p = 0; p < MAX_PLAYERS; ++p) {
            pending.spouse[p] = reader->Read_Int8();
            pending.married_since[p] = reader->Read_Int32();
            const int8_t s = pending.spouse[p];
            if (s < -1 || s >= MAX_PLAYERS || s == p) return fail("covenants save has a bad spouse");
            if (pending.married_since[p] < 0) return fail("covenants save has a bad stardate");
        }
        for (int p = 0; p < MAX_PLAYERS; ++p) {
            const int8_t s = pending.spouse[p];
            if (s >= 0 && pending.spouse[s] != p) return fail("covenants save has a one-sided marriage");
        }
        if (reader->Failed()) return fail("covenants save is truncated");
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

} // namespace covenants
