#include "pch.h"

#include "game/world_events.h"

#include "game/serialization.h"

#include <algorithm>

// The table of the new events, the modifiers the engine reads and the save section; the events
// themselves are in world_events.cpp. Kept apart so the save code and the colony calculations need
// no event code. Nothing here draws from game_random.
namespace world_events {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t ENTRY_SIZE = 1 + 1 + 2 + 4 + 4 + 2;
        constexpr uint32_t SAVE_SIZE = sizeof(uint16_t) + MAX_ENTRIES * ENTRY_SIZE;

        s_entry entries[MAX_ENTRIES];
        s_entry pending[MAX_ENTRIES];
        bool has_pending = false;

        bool Live_(const s_entry& e) {
            return e.kind != KIND_NONE && (e.ends == 0 || e.ends > MOX::stardate);
        }

        bool Race_Modifier_(const s_entry& e, int16_t player, e_race_event event) {
            return Live_(e) && e.kind == KIND_RACE_EVENT && e.player == player && e.value == event;
        }

        bool Known_Kind_(uint8_t kind) {
            return kind == KIND_NONE || kind == KIND_GOLDEN_AGE || kind == KIND_TOURIST_BOOM || kind == KIND_ORE_RUSH ||
                kind == KIND_AMBASSADOR_KILLED || kind == KIND_DISCOVERER || kind == KIND_ANCIENT_WRECK ||
                kind == KIND_RACE_EVENT || kind == KIND_BANKRUPTCY;
        }
    } // namespace

    s_entry* Entry_Slots() {
        return entries;
    }

    const s_entry* Entries(int32_t* count) {
        if (count != nullptr) *count = MAX_ENTRIES;
        return entries;
    }

    const s_entry* Find(e_kind kind, int16_t player, int32_t race_event) {
        if (!enabled) return nullptr;
        for (const s_entry& e : entries) {
            if (!Live_(e) || e.kind != kind || e.player != player) continue;
            if (kind == KIND_RACE_EVENT && race_event >= 0 && e.value != race_event) continue;
            return &e;
        }
        return nullptr;
    }

    bool Add(const s_entry& entry) {
        for (s_entry& e : entries) {
            if (Live_(e)) continue;
            e = entry;
            return true;
        }
        return false;
    }

    int32_t Speed_Bonus(int16_t player) {
        if (!enabled) return 0;
        for (const s_entry& e : entries) {
            if (Race_Modifier_(e, player, RACE_FAIR_WIND)) return 1;
        }
        return 0;
    }

    int32_t Morale_Bonus(const s_colony* colony) {
        if (!enabled || colony == nullptr) return 0;
        for (const s_entry& e : entries) {
            if (Race_Modifier_(e, colony->owner, RACE_DUEL_OF_HONOUR)) return 1;
        }
        return 0;
    }

    int32_t Food2_Bonus(const s_colony* colony) {
        if (!enabled || colony == nullptr || colony->planet < 0) return 0;
        if (MOX::planet[colony->planet].climate != PLANET_CLIMATE_OCEAN) return 0;
        for (const s_entry& e : entries) {
            if (Race_Modifier_(e, colony->owner, RACE_WARM_CURRENT)) return 2;  // food2 counts half food
        }
        return 0;
    }

    bool Population_Boom(int16_t colony) {
        if (!enabled || colony < 0) return false;
        for (const s_entry& e : entries) {
            if (Live_(e) && e.kind == KIND_RACE_EVENT && e.value == RACE_CLUTCH && e.target == colony) return true;
        }
        return false;
    }

    void Clear() {
        for (s_entry& e : entries) e = s_entry{};
        Discard_Save();
    }

    bool Empty() {
        return std::all_of(std::begin(entries), std::end(entries), [](const s_entry& e) { return e.kind == KIND_NONE; });
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (const s_entry& e : entries) {
            writer->Write_Uint8(e.kind);
            writer->Write_Int8(e.player);
            writer->Write_Int16(e.target);
            writer->Write_Int32(e.started);
            writer->Write_Int32(e.ends);
            writer->Write_Int16(e.value);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("events save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported events payload version");
        if (reader->Remaining() != SAVE_SIZE - sizeof(uint16_t)) return fail("events save has the wrong size");
        for (s_entry& e : pending) {
            e.kind = reader->Read_Uint8();
            e.player = reader->Read_Int8();
            e.target = reader->Read_Int16();
            e.started = reader->Read_Int32();
            e.ends = reader->Read_Int32();
            e.value = reader->Read_Int16();
            if (!Known_Kind_(e.kind)) return fail("events save has an unknown event");
            if (e.player < -1 || e.player >= MAX_PLAYERS) return fail("events save has an out-of-range player");
            if (e.started < 0 || e.ends < 0) return fail("events save has a bad stardate");
        }
        if (reader->Failed()) return fail("events save is truncated");
        has_pending = true;
        return true;
    }

    void Commit_Save() {
        if (!has_pending) return;
        std::copy(std::begin(pending), std::end(pending), std::begin(entries));
        has_pending = false;
    }

    void Discard_Save() {
        has_pending = false;
    }

} // namespace world_events
