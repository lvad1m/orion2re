#include "pch.h"

#include "game/crises.h"

#include "game/serialization.h"

#include <algorithm>

// The open crises and their save section; the crises themselves are in crises.cpp. Kept apart so the
// save code needs no crisis code. Nothing here draws from game_random.
namespace crises {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t SLOT_SIZE = 1 + 1 + 2 + 1 + 4 + 1 + 1;
        constexpr uint32_t SAVE_SIZE = sizeof(uint16_t) + MAX_ACTIVE * SLOT_SIZE;

        s_active slots[MAX_ACTIVE];
        s_active pending[MAX_ACTIVE];
        bool has_pending = false;
    } // namespace

    s_active* Slots() {
        return slots;
    }

    void Clear() {
        for (s_active& s : slots) s = s_active{};
        Discard_Save();
    }

    bool Empty() {
        return std::all_of(std::begin(slots), std::end(slots), [](const s_active& s) { return s.crisis == CRISIS_NONE; });
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (const s_active& s : slots) {
            writer->Write_Uint8(s.crisis);
            writer->Write_Int8(s.player);
            writer->Write_Int16(s.colony);
            writer->Write_Int8(s.option);
            writer->Write_Int32(s.due);
            writer->Write_Uint8(s.result);
            writer->Write_Int8(s.roll);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("crises save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported crises payload version");
        if (reader->Remaining() != SAVE_SIZE - sizeof(uint16_t)) return fail("crises save has the wrong size");
        for (s_active& s : pending) {
            s.crisis = reader->Read_Uint8();
            s.player = reader->Read_Int8();
            s.colony = reader->Read_Int16();
            s.option = reader->Read_Int8();
            s.due = reader->Read_Int32();
            s.result = reader->Read_Uint8();
            s.roll = reader->Read_Int8();
            if (s.crisis >= CRISIS_COUNT) return fail("crises save has an unknown crisis");
            if (s.player < -1 || s.player >= MAX_PLAYERS) return fail("crises save has an out-of-range player");
            if (s.option < -1 || s.option >= OPTION_COUNT) return fail("crises save has an out-of-range option");
            if (s.colony < -1 || s.colony >= MAX_COLONIES) return fail("crises save has an out-of-range colony");
            if (s.result > RESULT_FAILURE || s.due < 0) return fail("crises save has a bad value");
        }
        if (reader->Failed()) return fail("crises save is truncated");
        has_pending = true;
        return true;
    }

    void Commit_Save() {
        if (!has_pending) return;
        std::copy(std::begin(pending), std::end(pending), std::begin(slots));
        has_pending = false;
    }

    void Discard_Save() {
        has_pending = false;
    }

} // namespace crises
