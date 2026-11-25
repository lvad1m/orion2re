#include "pch.h"

#include "game/spy_ops.h"

#include "game/serialization.h"

#include <algorithm>

// The state of spy operations, its queries and its save section; the behaviour is in spy_ops.cpp.
// Kept apart so the save code needs no spy code.
namespace spy_ops {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 3;
        constexpr uint32_t SAVE_SIZE_V1 = sizeof(uint16_t) + MAX_COLONIES + MAX_LEADERS * 2;
        constexpr uint32_t SAVE_SIZE_V2 = SAVE_SIZE_V1 + MAX_COLONIES * 2;
        constexpr uint32_t SAVE_SIZE = SAVE_SIZE_V2 + MAX_COLONIES;

        struct s_tables {
            uint8_t unrest[MAX_COLONIES] = {};
            int8_t bribed_by[MAX_LEADERS];
            uint8_t immune[MAX_LEADERS] = {};
            uint8_t stall[MAX_COLONIES] = {};
            uint8_t famine[MAX_COLONIES] = {};
            uint8_t mourn[MAX_COLONIES] = {};
            s_tables() {
                std::fill(std::begin(bribed_by), std::end(bribed_by), static_cast<int8_t>(-1));
            }
        };
        s_tables tables;
        s_tables pending;
        bool has_pending = false;
    } // namespace

    bool Enabled(e_op op) {
        return op < OP_COUNT && ((operations >> op) & 1) != 0;
    }

    int32_t Cost(e_op op) {
        switch (op) {
            case OP_UNREST: return cost_unrest;
            case OP_KILL_SCIENTIST: return cost_kill_scientist;
            case OP_BRIBERY: return cost_bribery;
            case OP_FRAME_LEADER: return cost_frame_leader;
            case OP_DATA: return cost_data;
            case OP_FALSE_FLAG: return cost_false_flag;
            case OP_ASSASSINATION:
            case OP_POISON_FEAST: return cost_assassination;
            case OP_SABOTAGE_SHIPYARD:
            case OP_SABOTAGE_RESEARCH:
            case OP_SABOTAGE_FOOD:
            case OP_SABOTAGE_TRADE:
            case OP_STRIKE:
            case OP_POISON_WATER: return cost_sabotage;
            default: return 0;
        }
    }

    uint8_t* Unrest_Slot(int16_t colony) {
        return colony >= 0 && colony < MAX_COLONIES ? &tables.unrest[colony] : nullptr;
    }

    int8_t* Bribed_Slot(int16_t leader) {
        return leader >= 0 && leader < MAX_LEADERS ? &tables.bribed_by[leader] : nullptr;
    }

    uint8_t* Immune_Slot(int16_t leader) {
        return leader >= 0 && leader < MAX_LEADERS ? &tables.immune[leader] : nullptr;
    }

    uint8_t* Stall_Slot(int16_t colony) {
        return colony >= 0 && colony < MAX_COLONIES ? &tables.stall[colony] : nullptr;
    }

    uint8_t* Famine_Slot(int16_t colony) {
        return colony >= 0 && colony < MAX_COLONIES ? &tables.famine[colony] : nullptr;
    }

    uint8_t* Mourn_Slot(int16_t colony) {
        return colony >= 0 && colony < MAX_COLONIES ? &tables.mourn[colony] : nullptr;
    }

    bool Stalled(int16_t colony) {
        const uint8_t* slot = Stall_Slot(colony);
        return slot != nullptr && *slot > 0;
    }

    int32_t Food2_Penalty(const s_colony* colony) {
        if (!Enabled(OP_SABOTAGE_FOOD) || colony == nullptr) return 0;
        const int32_t index = colony - MOX::colony;
        if (index < 0 || index >= MAX_COLONIES) return 0;
        return tables.famine[index] > 0 ? FAMINE_FOOD2 : 0;
    }

    int32_t Unrest(int16_t colony) {
        const uint8_t* slot = Unrest_Slot(colony);
        // Stirred up by spies, or left by a killed governor (the assassination bits).
        const bool on = Enabled(OP_UNREST) || Enabled(OP_ASSASSINATION) || Enabled(OP_POISON_FEAST);
        return on && slot != nullptr ? *slot : 0;
    }

    int16_t Bribed_By(int16_t leader) {
        const int8_t* slot = Bribed_Slot(leader);
        return Enabled(OP_BRIBERY) && slot != nullptr ? *slot : -1;
    }

    void Clear() {
        tables = s_tables{};
        Discard_Save();
    }

    bool Empty() {
        return std::all_of(std::begin(tables.unrest), std::end(tables.unrest), [](uint8_t v) { return v == 0; }) &&
            std::all_of(std::begin(tables.stall), std::end(tables.stall), [](uint8_t v) { return v == 0; }) &&
            std::all_of(std::begin(tables.famine), std::end(tables.famine), [](uint8_t v) { return v == 0; }) &&
            std::all_of(std::begin(tables.mourn), std::end(tables.mourn), [](uint8_t v) { return v == 0; }) &&
            std::all_of(std::begin(tables.bribed_by), std::end(tables.bribed_by), [](int8_t v) { return v == -1; }) &&
            std::all_of(std::begin(tables.immune), std::end(tables.immune), [](uint8_t v) { return v == 0; });
    }

    uint32_t Save_Size() {
        return SAVE_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        writer->Write_Uint16(SAVE_PAYLOAD_VERSION);
        for (uint8_t v : tables.unrest) writer->Write_Uint8(v);
        for (int8_t v : tables.bribed_by) writer->Write_Int8(v);
        for (uint8_t v : tables.immune) writer->Write_Uint8(v);
        for (uint8_t v : tables.stall) writer->Write_Uint8(v);
        for (uint8_t v : tables.famine) writer->Write_Uint8(v);
        for (uint8_t v : tables.mourn) writer->Write_Uint8(v);
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("spy operations save is truncated");
        const uint16_t version = reader->Read_Uint16();
        if (version < 1 || version > SAVE_PAYLOAD_VERSION) return fail("unsupported spy operations payload version");
        const uint32_t size = version == 1 ? SAVE_SIZE_V1 : version == 2 ? SAVE_SIZE_V2 : SAVE_SIZE;
        if (reader->Remaining() != size - sizeof(uint16_t)) return fail("spy operations save has the wrong size");
        pending = s_tables{};
        for (uint8_t& v : pending.unrest) {
            v = reader->Read_Uint8();
            if (v > UNREST_MAX) return fail("spy operations save has an out-of-range unrest");
        }
        for (int8_t& v : pending.bribed_by) {
            v = reader->Read_Int8();
            if (v < -1 || v >= MAX_PLAYERS) return fail("spy operations save has an out-of-range briber");
        }
        for (uint8_t& v : pending.immune) {
            v = reader->Read_Uint8();
            if (v > 1) return fail("spy operations save has an out-of-range immunity");
        }
        if (version >= 2) {
            for (uint8_t& v : pending.stall) {
                v = reader->Read_Uint8();
                if (v > SABOTAGE_TURNS) return fail("spy operations save has an out-of-range stall");
            }
            for (uint8_t& v : pending.famine) {
                v = reader->Read_Uint8();
                if (v > SABOTAGE_TURNS) return fail("spy operations save has an out-of-range famine");
            }
        }
        if (version >= 3) {
            for (uint8_t& v : pending.mourn) {
                v = reader->Read_Uint8();
                if (v > MOURN_TURNS) return fail("spy operations save has an out-of-range mourning");
            }
        }
        if (reader->Failed()) return fail("spy operations save is truncated");
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

} // namespace spy_ops
