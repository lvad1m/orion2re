#include "pch.h"

#include "game/pacts.h"

#include "game/serialization.h"

#include <cstring>

// The state of defence pacts and guarantees, its queries and its save section; the behaviour (who
// answers an attack, the review) is in pacts.cpp. Kept apart so the save code needs no diplomacy.
namespace pacts {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t SAVE_SIZE = sizeof(uint16_t) + 2 * MAX_PLAYERS;

        uint8_t pact[MAX_PLAYERS] = {};       // bit k: a pact with player k (kept symmetric)
        uint8_t guarantee[MAX_PLAYERS] = {};  // bit k: this empire guarantees player k
        uint8_t pending_pact[MAX_PLAYERS];
        uint8_t pending_guarantee[MAX_PLAYERS];
        bool has_pending = false;
        int32_t refused[MAX_PLAYERS][MAX_PLAYERS][2];  // [human][computer][kind - 1]: the stardate of the last refusal; not saved
        bool refused_ready = false;

        void Init_Refused_() {
            if (refused_ready) return;
            std::memset(refused, 0xff, sizeof(refused));
            refused_ready = true;
        }

        uint8_t Bit_(int16_t player) {
            return static_cast<uint8_t>(1u << (player & 7));
        }

        bool Valid_(int16_t player) {
            return player >= 0 && player < MAX_PLAYERS && player < MOX::NUM_PLAYERS;
        }

        bool Alive_(int16_t player) {
            return Valid_(player) && MOX::player[player].eliminated == 0;
        }
    } // namespace

    bool Has_Pact(int16_t a, int16_t b) {
        if (!enabled || !Valid_(a) || !Valid_(b) || a == b) return false;
        return (pact[a] & Bit_(b)) != 0;
    }

    bool Guarantees(int16_t guarantor, int16_t protege) {
        if (!enabled || !Valid_(guarantor) || !Valid_(protege) || guarantor == protege) return false;
        return (guarantee[guarantor] & Bit_(protege)) != 0;
    }

    int32_t Guarantee_Count(int16_t guarantor) {
        if (!Valid_(guarantor)) return 0;
        int32_t count = 0;
        for (uint8_t mask = guarantee[guarantor]; mask != 0; mask = static_cast<uint8_t>(mask & (mask - 1))) ++count;
        return count;
    }

    void Sign_Pact(int16_t a, int16_t b, int16_t threat) {
        if (!enabled || !Alive_(a) || !Alive_(b) || a == b || Has_Pact(a, b)) return;
        pact[a] |= Bit_(b);
        pact[b] |= Bit_(a);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pacts: stardate %d, players %d and %d sign a defence pact against player %d",
            (int)MOX::stardate, (int)a, (int)b, (int)threat);
    }

    void End_Pact(int16_t a, int16_t b, const char* why) {
        if (!Valid_(a) || !Valid_(b) || a == b || (pact[a] & Bit_(b)) == 0) return;
        pact[a] = static_cast<uint8_t>(pact[a] & ~Bit_(b));
        pact[b] = static_cast<uint8_t>(pact[b] & ~Bit_(a));
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pacts: stardate %d, the pact of players %d and %d ends: %s",
            (int)MOX::stardate, (int)a, (int)b, why != nullptr ? why : "");
    }

    void Give_Guarantee(int16_t guarantor, int16_t protege, int16_t threat) {
        if (!enabled || !Alive_(guarantor) || !Alive_(protege) || guarantor == protege || Guarantees(guarantor, protege)) return;
        guarantee[guarantor] |= Bit_(protege);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pacts: stardate %d, player %d guarantees player %d against player %d",
            (int)MOX::stardate, (int)guarantor, (int)protege, (int)threat);
    }

    void End_Guarantee(int16_t guarantor, int16_t protege, const char* why) {
        if (!Valid_(guarantor) || !Valid_(protege) || (guarantee[guarantor] & Bit_(protege)) == 0) return;
        guarantee[guarantor] = static_cast<uint8_t>(guarantee[guarantor] & ~Bit_(protege));
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "pacts: stardate %d, player %d's guarantee of player %d ends: %s",
            (int)MOX::stardate, (int)guarantor, (int)protege, why != nullptr ? why : "");
    }

    int32_t Last_Refusal(int16_t human, int16_t computer, uint8_t kind) {
        Init_Refused_();
        if (!Valid_(human) || !Valid_(computer) || kind < CHOICE_PACT || kind > CHOICE_GUARANTEE) return -1;
        return refused[human][computer][kind - 1];
    }

    void Note_Refusal(int16_t human, int16_t computer, uint8_t kind) {
        Init_Refused_();
        if (!Valid_(human) || !Valid_(computer) || kind < CHOICE_PACT || kind > CHOICE_GUARANTEE) return;
        refused[human][computer][kind - 1] = MOX::stardate;
    }

    void Clear() {
        refused_ready = false;
        std::memset(pact, 0, sizeof(pact));
        std::memset(guarantee, 0, sizeof(guarantee));
        Discard_Save();
    }

    bool Empty() {
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            if (pact[i] != 0 || guarantee[i] != 0) return false;
        }
        return true;
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (uint8_t mask : pact) writer->Write_Uint8(mask);
        for (uint8_t mask : guarantee) writer->Write_Uint8(mask);
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("pacts save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported pacts payload version");
        if (reader->Remaining() != SAVE_SIZE - sizeof(uint16_t)) return fail("pacts save has the wrong size");
        for (uint8_t& mask : pending_pact) mask = reader->Read_Uint8();
        for (uint8_t& mask : pending_guarantee) mask = reader->Read_Uint8();
        if (reader->Failed()) return fail("pacts save is truncated");
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            const uint8_t self = Bit_(static_cast<int16_t>(i));
            if ((pending_pact[i] & self) != 0 || (pending_guarantee[i] & self) != 0) {
                return fail("pacts save binds an empire to itself");
            }
            for (int j = 0; j < MAX_PLAYERS; ++j) {
                const bool ij = (pending_pact[i] & Bit_(static_cast<int16_t>(j))) != 0;
                const bool ji = (pending_pact[j] & self) != 0;
                if (ij != ji) return fail("pacts save has a one-sided pact");
            }
        }
        has_pending = true;
        return true;
    }

    void Commit_Save() {
        if (!has_pending) return;
        std::memcpy(pact, pending_pact, sizeof(pact));
        std::memcpy(guarantee, pending_guarantee, sizeof(guarantee));
        has_pending = false;
    }

    void Discard_Save() {
        has_pending = false;
    }

} // namespace pacts
