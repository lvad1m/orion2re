#include "pch.h"

#include "game/news.h"

#include "game/serialization.h"

#include <algorithm>

// The ring of facts, the stored ranking, the queries and the save section; the behaviour is in news.cpp.
// Nothing here draws from game_random.
namespace news {

    static_assert(RANK_SLOTS == MAX_PLAYERS, "news ranks one slot per empire");

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t ITEM_BYTES = 4 + 1 + 1 + 1 + 1 + 2;
        constexpr uint32_t RATINGS_BYTES = 4 + RATING_COUNT * RANK_SLOTS * (1 + 4);

        struct s_ring {
            s_item items[NEWS_ITEMS];
            int32_t total = 0;  // facts ever pushed since the ring was last loaded or cleared; slot = total % NEWS_ITEMS
            s_ratings ratings;
        };

        s_ring state;
        s_ring pending;
        bool has_pending = false;
        s_watch watch;

        bool Same_(const s_item& a, const s_item& b) {
            return a.kind == b.kind && a.subject == b.subject && a.object == b.object && a.reader == b.reader && a.value == b.value;
        }
    } // namespace

    void Push_Item(const s_item& item) {
        state.items[state.total % NEWS_ITEMS] = item;
        ++state.total;
    }

    int32_t Count() {
        return std::min(state.total, NEWS_ITEMS);
    }

    const s_item* Nth_Newest(int32_t age) {
        if (age < 0 || age >= Count()) return nullptr;
        return &state.items[(state.total - 1 - age) % NEWS_ITEMS];
    }

    bool Contains(const s_item& item) {
        for (int32_t i = 0; i < Count(); ++i) {
            if (Same_(*Nth_Newest(i), item)) return true;
        }
        return false;
    }

    void Set_Ratings(const s_ratings& ratings) {
        state.ratings = ratings;
    }

    const s_ratings& Ratings() {
        return state.ratings;
    }

    s_watch* Watch() {
        return &watch;
    }

    void Clear() {
        state = s_ring{};
        watch = s_watch{};
        Discard_Save();
    }

    bool Empty() {
        return state.total == 0 && state.ratings.stardate == 0;
    }

    uint32_t Save_Size() {
        return sizeof(uint16_t) + RATINGS_BYTES + 1 + static_cast<uint32_t>(Count()) * ITEM_BYTES;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        writer->Write_Int32(state.ratings.stardate);
        for (int32_t r = 0; r < RATING_COUNT; ++r) {
            for (int32_t i = 0; i < RANK_SLOTS; ++i) {
                writer->Write_Int8(state.ratings.order[r][i]);
                writer->Write_Int32(state.ratings.value[r][i]);
            }
        }
        const int32_t count = Count();
        writer->Write_Uint8(static_cast<uint8_t>(count));
        for (int32_t age = count - 1; age >= 0; --age) {  // oldest first
            const s_item& item = *Nth_Newest(age);
            writer->Write_Int32(item.stardate);
            writer->Write_Uint8(item.kind);
            writer->Write_Int8(item.subject);
            writer->Write_Int8(item.object);
            writer->Write_Int8(item.reader);
            writer->Write_Int16(item.value);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        const auto empire_ok = [](int32_t player) { return player >= -1 && player < MAX_PLAYERS; };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t) + RATINGS_BYTES + 1) return fail("news save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported news payload version");
        pending = s_ring{};
        pending.ratings.stardate = reader->Read_Int32();
        if (pending.ratings.stardate < 0) return fail("news save has a bad rating stardate");
        for (int32_t r = 0; r < RATING_COUNT; ++r) {
            for (int32_t i = 0; i < RANK_SLOTS; ++i) {
                pending.ratings.order[r][i] = reader->Read_Int8();
                pending.ratings.value[r][i] = reader->Read_Int32();
                if (!empire_ok(pending.ratings.order[r][i])) return fail("news save has a bad ranked empire");
            }
        }
        const uint8_t count = reader->Read_Uint8();
        if (count > NEWS_ITEMS) return fail("news save has too many facts");
        if (reader->Remaining() != count * ITEM_BYTES) return fail("news save has the wrong size");
        for (int32_t i = 0; i < count; ++i) {
            s_item item;
            item.stardate = reader->Read_Int32();
            item.kind = reader->Read_Uint8();
            item.subject = reader->Read_Int8();
            item.object = reader->Read_Int8();
            item.reader = reader->Read_Int8();
            item.value = reader->Read_Int16();
            if (item.stardate < 0 || item.kind == KIND_NONE || item.kind >= KIND_COUNT) return fail("news save has a bad fact");
            if (!empire_ok(item.subject) || !empire_ok(item.object) || !empire_ok(item.reader)) return fail("news save has a bad empire");
            pending.items[i] = item;
        }
        pending.total = count;
        if (reader->Failed()) return fail("news save is truncated");
        has_pending = true;
        return true;
    }

    void Commit_Save() {
        if (!has_pending) return;
        state = pending;
        watch = s_watch{};
        has_pending = false;
    }

    void Discard_Save() {
        has_pending = false;
    }

} // namespace news
