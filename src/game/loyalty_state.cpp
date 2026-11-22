#include "pch.h"

#include "game/loyalty.h"

#include "game/serialization.h"

#include <algorithm>

// The state of leader loyalty and its save section; the behaviour is in loyalty.cpp. Kept apart so
// the save code needs no officer code.
namespace loyalty {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t SAVE_SIZE = sizeof(uint16_t) + MAX_LEADERS * (3 * sizeof(int8_t) + sizeof(int16_t));

        s_record records[MAX_LEADERS];
        s_record pending[MAX_LEADERS];
        bool has_pending = false;
    } // namespace

    s_record* Record(int16_t leader) {
        return leader >= 0 && leader < MAX_LEADERS ? &records[leader] : nullptr;
    }

    int32_t Of(int16_t leader) {
        const s_record* r = Record(leader);
        return enabled && r != nullptr ? r->loyalty : UNTRACKED;
    }

    void Clear() {
        std::fill(std::begin(records), std::end(records), s_record{});
        Discard_Save();
    }

    bool Empty() {
        return std::all_of(std::begin(records), std::end(records), [](const s_record& r) { return r.loyalty == UNTRACKED; });
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (const s_record& r : records) {
            writer->Write_Int8(r.loyalty);
            writer->Write_Int8(r.owner);
            writer->Write_Int8(r.last_level);
            writer->Write_Int16(r.idle);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("loyalty save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported loyalty payload version");
        if (reader->Remaining() != SAVE_SIZE - sizeof(uint16_t)) return fail("loyalty save has the wrong size");
        for (s_record& r : pending) {
            r.loyalty = reader->Read_Int8();
            r.owner = reader->Read_Int8();
            r.last_level = reader->Read_Int8();
            r.idle = reader->Read_Int16();
            if (r.loyalty < UNTRACKED || r.loyalty > 100 || r.owner < -1 || r.owner >= MAX_PLAYERS || r.idle < 0) {
                return fail("loyalty save has an out-of-range value");
            }
        }
        if (reader->Failed()) return fail("loyalty save is truncated");
        has_pending = true;
        return true;
    }

    void Commit_Save() {
        if (!has_pending) return;
        std::copy(std::begin(pending), std::end(pending), std::begin(records));
        has_pending = false;
    }

    void Discard_Save() {
        has_pending = false;
    }

} // namespace loyalty
