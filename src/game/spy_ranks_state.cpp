#include "pch.h"

#include "game/spy_ranks.h"

#include "game/serialization.h"

#include <algorithm>

// The state of counter-agents and spy ranks and its save section; the behaviour is in
// spy_ranks.cpp. Kept apart so the save code needs no spy code.
namespace spy_ranks {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 1;
        constexpr uint32_t SAVE_SIZE = sizeof(uint16_t) + MAX_COLONIES + MAX_PLAYERS + MAX_PLAYERS * MAX_PLAYERS +
            MAX_PLAYERS * sizeof(int16_t);

        s_state state;
        s_state pending;
        bool has_pending = false;
    } // namespace

    s_state* State() {
        return &state;
    }

    bool Is_Product(int16_t product) {
        return product == PRODUCT_COUNTER_AGENT || product == PRODUCT_SABOTEUR || product == PRODUCT_RESIDENT;
    }

    int32_t Counter_Agents(int16_t colony) {
        return counter_agents && colony >= 0 && colony < MAX_COLONIES ? state.counter_agents[colony] : 0;
    }

    int32_t Residents(int16_t owner, int16_t target) {
        return ranks && owner >= 0 && owner < MAX_PLAYERS && target >= 0 && target < MAX_PLAYERS ? state.residents[owner][target] : 0;
    }

    int32_t Resident_Bonus(int16_t owner, int16_t target) {
        return Residents(owner, target) > 0 ? RESIDENT_BONUS : 0;
    }

    int32_t Uncover_Bonus(int16_t colony) {
        return Counter_Agents(colony) * UNCOVER_BRIBE_PER_MILLE;
    }

    void Clear() {
        state = s_state{};
        Discard_Save();
    }

    bool Empty() {
        return std::all_of(std::begin(state.counter_agents), std::end(state.counter_agents), [](uint8_t v) { return v == 0; }) &&
            std::all_of(std::begin(state.saboteurs), std::end(state.saboteurs), [](uint8_t v) { return v == 0; }) &&
            std::all_of(&state.residents[0][0], &state.residents[0][0] + MAX_PLAYERS * MAX_PLAYERS, [](uint8_t v) { return v == 0; }) &&
            std::all_of(std::begin(state.last_sabotaged), std::end(state.last_sabotaged), [](int16_t v) { return v == -1; });
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (uint8_t v : state.counter_agents) writer->Write_Uint8(v);
        for (uint8_t v : state.saboteurs) writer->Write_Uint8(v);
        for (const auto& row : state.residents) {
            for (uint8_t v : row) writer->Write_Uint8(v);
        }
        for (int16_t v : state.last_sabotaged) writer->Write_Int16(v);
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("spy ranks save is truncated");
        if (reader->Read_Uint16() != SAVE_PAYLOAD_VERSION) return fail("unsupported spy ranks payload version");
        if (reader->Remaining() != SAVE_SIZE - sizeof(uint16_t)) return fail("spy ranks save has the wrong size");
        pending = s_state{};
        for (uint8_t& v : pending.counter_agents) {
            v = reader->Read_Uint8();
            if (v > MAX_PER_COLONY) return fail("spy ranks save has too many counter-agents");
        }
        for (uint8_t& v : pending.saboteurs) v = reader->Read_Uint8();
        for (auto& row : pending.residents) {
            for (uint8_t& v : row) v = reader->Read_Uint8();
        }
        for (int16_t& v : pending.last_sabotaged) {
            v = reader->Read_Int16();
            if (v < -1 || v >= MAX_COLONIES) return fail("spy ranks save has an out-of-range colony");
        }
        if (reader->Failed()) return fail("spy ranks save is truncated");
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

} // namespace spy_ranks
