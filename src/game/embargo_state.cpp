#include "pch.h"

#include "game/embargo.h"

#include "game/serialization.h"

#include <algorithm>

// The state of embargoes, its queries and its save section; the behaviour is in embargo.cpp.
// Kept apart so the save code needs no diplomacy.
namespace embargo {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t RECORD_SIZE = 4 * sizeof(uint8_t) + sizeof(int32_t);
        constexpr uint32_t SAVE_SIZE = sizeof(uint16_t) + MAX_PLAYERS * RECORD_SIZE;

        s_embargo table[MAX_PLAYERS];
        s_embargo pending[MAX_PLAYERS];
        bool has_pending = false;

        bool Valid_(int16_t player) {
            return player >= 0 && player < MAX_PLAYERS && player < MOX::NUM_PLAYERS;
        }

        int Count_(uint8_t mask) {
            int count = 0;
            for (; mask != 0; mask = static_cast<uint8_t>(mask & (mask - 1))) ++count;
            return count;
        }
    } // namespace

    s_embargo* Record(int16_t target) {
        return target >= 0 && target < MAX_PLAYERS ? &table[target] : nullptr;
    }

    bool Active(int16_t target) {
        return enabled && Valid_(target) && table[target].participants != 0;
    }

    bool Participates(int16_t member, int16_t target) {
        return Active(target) && Valid_(member) && ((table[target].participants >> member) & 1) != 0;
    }

    bool Coalition_Holds(int16_t target) {
        if (!Active(target)) return false;
        const uint8_t former = table[target].former_partners;
        return former != 0 && 2 * Count_(static_cast<uint8_t>(table[target].participants & former)) > Count_(former);
    }

    bool Blocks_Treaty(int16_t a, int16_t b) {
        return Participates(a, b) || Participates(b, a);
    }

    void Clear() {
        std::fill(std::begin(table), std::end(table), s_embargo{});
        Discard_Save();
    }

    bool Empty() {
        return std::all_of(std::begin(table), std::end(table), [](const s_embargo& e) { return e.participants == 0; });
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (const s_embargo& e : table) {
            writer->Write_Int8(e.initiator);
            writer->Write_Uint8(e.participants);
            writer->Write_Uint8(e.former_partners);
            writer->Write_Uint8(e.by_council);
            writer->Write_Int32(e.start);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("embargo save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported embargo payload version");
        if (reader->Remaining() != SAVE_SIZE - sizeof(uint16_t)) return fail("embargo save has the wrong size");
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            s_embargo& e = pending[i];
            e.initiator = reader->Read_Int8();
            e.participants = reader->Read_Uint8();
            e.former_partners = reader->Read_Uint8();
            e.by_council = reader->Read_Uint8();
            e.start = reader->Read_Int32();
            const uint8_t self = static_cast<uint8_t>(1u << i);
            if (e.initiator < -1 || e.initiator >= MAX_PLAYERS || e.initiator == i || (e.participants & self) != 0 ||
                (e.former_partners & self) != 0 || e.by_council > 1 || e.start < 0) {
                return fail("embargo save has an out-of-range value");
            }
        }
        if (reader->Failed()) return fail("embargo save is truncated");
        has_pending = true;
        return true;
    }

    void Commit_Save() {
        if (!has_pending) return;
        std::copy(std::begin(pending), std::end(pending), std::begin(table));
        has_pending = false;
    }

    void Discard_Save() {
        has_pending = false;
    }

} // namespace embargo
