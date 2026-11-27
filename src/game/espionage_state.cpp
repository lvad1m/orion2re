#include "pch.h"

#include "game/espionage.h"

#include "game/serialization.h"

#include <algorithm>

// The state of the spy network core and its save section; the behaviour is in espionage.cpp. Kept
// apart so the save code needs no spy code.
namespace espionage {

    static_assert(MAX_PLAYERS == 8, "the state holds 8 players");

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 3;
        constexpr uint32_t SAVE_SIZE_V1 = sizeof(uint16_t) + MAX_PLAYERS * MAX_PLAYERS * sizeof(int16_t) +
            MAX_PLAYERS * (2 * sizeof(uint8_t) + sizeof(int32_t));
        constexpr uint32_t SAVE_SIZE_V2 = SAVE_SIZE_V1 + MAX_PLAYERS * sizeof(int32_t);  // version 2: the last arrests
        // Version 3: the catch ring and the last harm.
        constexpr uint32_t SAVE_SIZE = SAVE_SIZE_V2 + MAX_PLAYERS * (WITCH_CATCHES + 1) * sizeof(int32_t);

        s_state state;
        s_state pending;
        bool has_pending = false;

        bool Valid_(int16_t player) {
            return player >= 0 && player < MAX_PLAYERS;
        }
    } // namespace

    s_state* State() {
        return &state;
    }

    int32_t Points(int16_t owner, int16_t target) {
        return network && Valid_(owner) && Valid_(target) ? state.points[owner][target] : 0;
    }

    bool Spend_Points(int16_t owner, int16_t target, int32_t amount) {
        if (!network || !Valid_(owner) || !Valid_(target) || amount < 0 || state.points[owner][target] < amount) return false;
        state.points[owner][target] = static_cast<int16_t>(state.points[owner][target] - amount);
        return true;
    }

    e_mode Mode(int16_t player) {
        if (!counterintelligence || !Valid_(player) || state.mode[player] >= MODE_COUNT) return MODE_NORMAL;
        return static_cast<e_mode>(state.mode[player]);
    }

    void Set_Mode(int16_t player, e_mode mode) {
        if (!counterintelligence || !Valid_(player) || mode >= MODE_COUNT || state.mode[player] == mode) return;
        state.mode[player] = mode;
        static const char* const names[MODE_COUNT] = {"normal", "heightened", "a witch hunt"};
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "espionage: stardate %d, player %d sets its counterintelligence to %s",
            (int)MOX::stardate, (int)player, names[mode]);
    }

    int32_t Defense_Bonus(int16_t player) {
        return MODE_DEFENSE[Mode(player)];
    }

    int32_t Morale_Effect(int16_t player) {
        return MODE_MORALE[Mode(player)];
    }

    void Clear() {
        state = s_state{};
        Discard_Save();
    }

    bool Empty() {
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            if (state.mode[i] != 0 || state.caught[i] != 0 || state.last_caught[i] != 0 || state.last_arrest[i] != 0 ||
                state.last_harm[i] != 0) return false;
            for (int j = 0; j < MAX_PLAYERS; ++j) {
                if (state.points[i][j] != 0) return false;
            }
            for (int32_t t : state.catch_ring[i]) {
                if (t != 0) return false;
            }
        }
        return true;
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (const auto& row : state.points) {
            for (int16_t p : row) writer->Write_Int16(p);
        }
        for (uint8_t m : state.mode) writer->Write_Uint8(m);
        for (uint8_t c : state.caught) writer->Write_Uint8(c);
        for (int32_t t : state.last_caught) writer->Write_Int32(t);
        for (int32_t t : state.last_arrest) writer->Write_Int32(t);
        for (const auto& row : state.catch_ring) {
            for (int32_t t : row) writer->Write_Int32(t);
        }
        for (int32_t t : state.last_harm) writer->Write_Int32(t);
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("spy network save is truncated");
        const uint16_t version = reader->Read_Uint16();
        if (version < 1 || version > SAVE_PAYLOAD_VERSION) return fail("unsupported spy network payload version");
        if (reader->Remaining() != (version == 1 ? SAVE_SIZE_V1 : version == 2 ? SAVE_SIZE_V2 : SAVE_SIZE) - sizeof(uint16_t)) return fail("spy network save has the wrong size");
        pending = s_state{};
        for (auto& row : pending.points) {
            for (int16_t& p : row) {
                p = reader->Read_Int16();
                if (p < 0) return fail("spy network save has an out-of-range value");
            }
        }
        for (uint8_t& m : pending.mode) {
            m = reader->Read_Uint8();
            if (m >= MODE_COUNT) return fail("spy network save has an unknown mode");
        }
        for (uint8_t& c : pending.caught) c = reader->Read_Uint8();
        for (int32_t& t : pending.last_caught) t = reader->Read_Int32();
        if (version >= 2) {
            for (int32_t& t : pending.last_arrest) {
                t = reader->Read_Int32();
                if (t < 0) return fail("spy network save has a bad arrest stardate");
            }
        }
        if (version >= 3) {
            for (auto& row : pending.catch_ring) {
                for (int32_t& t : row) {
                    t = reader->Read_Int32();
                    if (t < 0) return fail("spy network save has a bad catch stardate");
                }
            }
            for (int32_t& t : pending.last_harm) {
                t = reader->Read_Int32();
                if (t < 0) return fail("spy network save has a bad harm stardate");
            }
        } else {
            // Versions 1 and 2 kept no ring: rebuild it from the catch count and the turn of the last catch.
            for (int i = 0; i < MAX_PLAYERS; ++i) {
                const int32_t n = std::min<int32_t>(pending.caught[i], WITCH_CATCHES);
                for (int32_t k = 0; k < n; ++k) pending.catch_ring[i][k] = pending.last_caught[i];
            }
        }
        if (reader->Failed()) return fail("spy network save is truncated");
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

} // namespace espionage
