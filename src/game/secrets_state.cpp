#include "pch.h"

#include "game/secrets.h"

#include "game/serialization.h"

#include <algorithm>

// The store of known secrets and its save section; the behaviour is in secrets.cpp. Kept apart so
// the save code needs no spy code. Nothing here draws from game_random.
namespace secrets {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 2;
        constexpr uint32_t ENTRY_SIZE_V1 = 1 + 1 + 1 + 1 + 2 + 4;
        constexpr uint32_t ENTRY_SIZE = ENTRY_SIZE_V1 + 1;
        constexpr uint32_t SAVE_SIZE_V1 = sizeof(uint16_t) + MAX_SECRETS * ENTRY_SIZE_V1;
        constexpr uint32_t SAVE_SIZE = sizeof(uint16_t) + MAX_SECRETS * ENTRY_SIZE + MAX_PLAYERS * (4 + 1);

        s_secret slots[MAX_SECRETS];
        s_secret pending[MAX_SECRETS];
        s_bio bio[MAX_PLAYERS];
        s_bio pending_bio[MAX_PLAYERS];
        bool has_pending = false;
    } // namespace

    s_bio* Bio() {
        return bio;
    }

    s_secret* Slots() {
        return slots;
    }

    const s_secret* Secrets(int32_t* count) {
        if (count != nullptr) *count = MAX_SECRETS;
        return slots;
    }

    bool Learn(int16_t holder, e_kind kind, int16_t owner, int16_t victim, int16_t value) {
        if (!enabled || kind == KIND_NONE || kind >= KIND_COUNT) return false;
        if (holder < 0 || holder >= MAX_PLAYERS || owner < 0 || owner >= MAX_PLAYERS || victim < 0 || victim >= MAX_PLAYERS) return false;
        s_secret* free_slot = nullptr;
        for (s_secret& s : slots) {
            if (s.kind == kind && s.owner == owner && s.victim == victim && s.holder == holder && s.value == value) return false;
            if (s.kind == KIND_NONE && free_slot == nullptr) free_slot = &s;
        }
        if (free_slot == nullptr) return false;
        free_slot->kind = kind;
        free_slot->owner = static_cast<int8_t>(owner);
        free_slot->victim = static_cast<int8_t>(victim);
        free_slot->holder = static_cast<int8_t>(holder);
        free_slot->value = value;
        free_slot->learned = MOX::stardate;
        return true;
    }

    void Clear() {
        for (s_secret& s : slots) s = s_secret{};
        for (s_bio& b : bio) b = s_bio{};
        Discard_Save();
    }

    bool Empty() {
        return std::all_of(std::begin(slots), std::end(slots), [](const s_secret& s) { return s.kind == KIND_NONE; }) &&
            std::all_of(std::begin(bio), std::end(bio), [](const s_bio& b) { return b.turn == 0; });
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (const s_secret& s : slots) {
            writer->Write_Uint8(s.kind);
            writer->Write_Int8(s.owner);
            writer->Write_Int8(s.victim);
            writer->Write_Int8(s.holder);
            writer->Write_Int16(s.value);
            writer->Write_Int32(s.learned);
            writer->Write_Uint8(s.demanded);
        }
        for (const s_bio& b : bio) {
            writer->Write_Int32(b.turn);
            writer->Write_Int8(b.victim);
        }
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("secrets save is truncated");
        const uint16_t version = reader->Read_Uint16();
        if (version != 1 && version != SAVE_PAYLOAD_VERSION) return fail("unsupported secrets payload version");
        if (reader->Remaining() != (version == 1 ? SAVE_SIZE_V1 : SAVE_SIZE) - sizeof(uint16_t)) return fail("secrets save has the wrong size");
        for (s_bio& b : pending_bio) b = s_bio{};
        for (s_secret& s : pending) {
            s.kind = reader->Read_Uint8();
            s.owner = reader->Read_Int8();
            s.victim = reader->Read_Int8();
            s.holder = reader->Read_Int8();
            s.value = reader->Read_Int16();
            s.learned = reader->Read_Int32();
            s.demanded = version >= 2 ? reader->Read_Uint8() : 0;
            if (s.demanded > 1) return fail("secrets save has a bad demand");
            if (s.kind >= KIND_COUNT) return fail("secrets save has an unknown secret");
            const bool players_ok = s.kind == KIND_NONE ||
                (s.owner >= 0 && s.owner < MAX_PLAYERS && s.victim >= 0 && s.victim < MAX_PLAYERS && s.holder >= 0 && s.holder < MAX_PLAYERS);
            if (!players_ok || s.learned < 0) return fail("secrets save has a bad value");
        }
        if (version >= 2) {
            for (s_bio& b : pending_bio) {
                b.turn = reader->Read_Int32();
                b.victim = reader->Read_Int8();
                if (b.turn < 0 || b.victim < -1 || b.victim >= MAX_PLAYERS) return fail("secrets save has a bad deed");
            }
        }
        if (reader->Failed()) return fail("secrets save is truncated");
        has_pending = true;
        return true;
    }

    void Commit_Save() {
        if (!has_pending) return;
        std::copy(std::begin(pending), std::end(pending), std::begin(slots));
        std::copy(std::begin(pending_bio), std::end(pending_bio), std::begin(bio));
        has_pending = false;
    }

    void Discard_Save() {
        has_pending = false;
    }

} // namespace secrets
