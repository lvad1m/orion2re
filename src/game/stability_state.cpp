#include "pch.h"

#include "game/stability.h"

#include "game/serialization.h"

#include <algorithm>

// The state of colony stability and its save section; the behaviour is in stability.cpp. Kept
// apart so the save code needs no colony calculations.
namespace stability {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t SAVE_SIZE = sizeof(uint16_t) + MAX_PLAYERS * sizeof(int32_t) + MAX_COLONIES * 2;

        struct s_tables {
            int32_t last_victory[MAX_PLAYERS] = {};
            int8_t stability[MAX_COLONIES] = {};
            uint8_t unrest[MAX_COLONIES] = {};
        };
        s_tables tables;
        s_tables pending;
        bool has_pending = false;
    } // namespace

    int8_t* Stability_Slot(int16_t colony) {
        return colony >= 0 && colony < MAX_COLONIES ? &tables.stability[colony] : nullptr;
    }

    uint8_t* Unrest_Slot(int16_t colony) {
        return colony >= 0 && colony < MAX_COLONIES ? &tables.unrest[colony] : nullptr;
    }

    int32_t* Last_Victory_Slot(int16_t player) {
        return player >= 0 && player < MAX_PLAYERS ? &tables.last_victory[player] : nullptr;
    }

    int32_t Of(int16_t colony) {
        const int8_t* slot = Stability_Slot(colony);
        return enabled && slot != nullptr ? *slot : 0;
    }

    void Clear() {
        tables = s_tables{};
        Discard_Save();
    }

    bool Empty() {
        return std::all_of(std::begin(tables.last_victory), std::end(tables.last_victory), [](int32_t v) { return v == 0; }) &&
            std::all_of(std::begin(tables.stability), std::end(tables.stability), [](int8_t v) { return v == 0; }) &&
            std::all_of(std::begin(tables.unrest), std::end(tables.unrest), [](uint8_t v) { return v == 0; });
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (int32_t turn : tables.last_victory) writer->Write_Int32(turn);
        for (int i = 0; i < MAX_COLONIES; ++i) {
            writer->Write_Int8(tables.stability[i]);
            writer->Write_Uint8(tables.unrest[i]);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("stability save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported stability payload version");
        if (reader->Remaining() != SAVE_SIZE - sizeof(uint16_t)) return fail("stability save has the wrong size");
        pending = s_tables{};
        for (int32_t& turn : pending.last_victory) turn = reader->Read_Int32();
        for (int i = 0; i < MAX_COLONIES; ++i) {
            pending.stability[i] = reader->Read_Int8();
            pending.unrest[i] = reader->Read_Uint8();
            if (pending.stability[i] < MIN_STABILITY || pending.stability[i] > MAX_STABILITY) {
                return fail("stability save has an out-of-range value");
            }
        }
        if (reader->Failed()) return fail("stability save is truncated");
        has_pending = true;
        return true;
    }

    void Commit_Save() {
        if (!has_pending) return;
        tables = pending;
        has_pending = false;
    }

    void Discard_Save() {
        has_pending = false;
    }

} // namespace stability
