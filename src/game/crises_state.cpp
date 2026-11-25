#include "pch.h"

#include "game/crises.h"

#include "game/serialization.h"

#include <algorithm>

// The open crises and their save section; the crises themselves are in crises.cpp. Kept apart so the
// save code needs no crisis code. Nothing here draws from game_random.
namespace crises {

    namespace {
        constexpr uint16_t SAVE_PAYLOAD_VERSION = 3;  // 1: the slots only; 2: the slots and s_extra; 3: s_extra of the threat and the contest
        constexpr uint32_t SLOT_SIZE = 1 + 1 + 2 + 1 + 4 + 1 + 1;
        constexpr uint32_t SAVE_SIZE_V1 = sizeof(uint16_t) + MAX_ACTIVE * SLOT_SIZE;
        constexpr uint32_t SCALE_SIZE = 1 + 1 + 2 + 4 + 1;
        constexpr uint32_t EXTRA_SIZE_V2 = MAX_SCALES * SCALE_SIZE + MAX_PLAYERS * 1 + MAX_PLAYERS * 4 + 1 + 4 + MAX_PLAYERS * 2;
        constexpr uint32_t EXTRA_SIZE = MAX_SCALES * SCALE_SIZE + 1 + 4 + MAX_PLAYERS * 2 + MAX_PLAYERS * 2 + MAX_PLAYERS * 1
            + MAX_PLAYERS * 4 + 1 + 4 + MAX_PLAYERS * 4;

        s_active slots[MAX_ACTIVE];
        s_active pending[MAX_ACTIVE];
        s_extra extra;
        s_extra pending_extra;
        bool has_pending = false;

        bool Extra_Is_Empty_(const s_extra& e) {
            for (const s_scale& s : e.scale) {
                if (s.kind != SCALE_NONE) return false;
            }
            for (int32_t p = 0; p < MAX_PLAYERS; ++p) {
                if (e.threat[p] != 0 || e.war_mask[p] != 0 || e.strike_until[p] != 0 || e.given[p] != 0) return false;
            }
            return e.front_broken == 0 && e.council_progress == 0 && e.contest_stage == 0;
        }
    } // namespace

    s_active* Slots() {
        return slots;
    }

    s_extra* Extra() {
        return &extra;
    }

    const s_scale* Find_Scale(e_scale kind, int16_t player) {
        for (const s_scale& s : extra.scale) {
            if (s.kind == kind && s.player == player) return &s;
        }
        return nullptr;
    }

    bool Extra_Empty() {
        return Extra_Is_Empty_(extra);
    }

    void Clear() {
        for (s_active& s : slots) s = s_active{};
        extra = s_extra{};
        Discard_Save();
    }

    bool Empty() {
        return std::all_of(std::begin(slots), std::end(slots), [](const s_active& s) { return s.crisis == CRISIS_NONE; })
            && Extra_Is_Empty_(extra);
    }

    // Version 1 (the old layout) while nothing of the wave-4 state is kept, so a save made with the new flags
    // off is the same bytes as before.
    uint32_t Save_Size() {
        return Extra_Is_Empty_(extra) ? SAVE_SIZE_V1 : SAVE_SIZE_V1 + EXTRA_SIZE;
    }

    void Write_Save(SERIAL::Writer_* writer) {
        const bool with_extra = !Extra_Is_Empty_(extra);
        writer->Write_Uint16(with_extra ? SAVE_PAYLOAD_VERSION : 1);
        for (const s_active& s : slots) {
            writer->Write_Uint8(s.crisis);
            writer->Write_Int8(s.player);
            writer->Write_Int16(s.colony);
            writer->Write_Int8(s.option);
            writer->Write_Int32(s.due);
            writer->Write_Uint8(s.result);
            writer->Write_Int8(s.roll);
        }
        if (!with_extra) return;
        for (const s_scale& s : extra.scale) {
            writer->Write_Uint8(s.kind);
            writer->Write_Int8(s.player);
            writer->Write_Int16(s.value);
            writer->Write_Int32(s.started);
            writer->Write_Int8(s.approach);
        }
        writer->Write_Uint8(extra.front_broken);
        writer->Write_Int32(extra.council_progress);
        for (int32_t p = 0; p < MAX_PLAYERS; ++p) writer->Write_Uint16(extra.given[p]);
        for (int32_t p = 0; p < MAX_PLAYERS; ++p) writer->Write_Uint16(extra.threat[p]);
        for (int32_t p = 0; p < MAX_PLAYERS; ++p) writer->Write_Uint8(extra.war_mask[p]);
        for (int32_t p = 0; p < MAX_PLAYERS; ++p) writer->Write_Int32(extra.strike_until[p]);
        writer->Write_Uint8(extra.contest_stage);
        writer->Write_Int32(extra.contest_ends);
        for (int32_t p = 0; p < MAX_PLAYERS; ++p) writer->Write_Int32(extra.baseline[p]);
    }

    bool Parse_Save(SERIAL::Reader_* reader, std::string* error) {
        const auto fail = [&](const char* text) {
            if (error != nullptr) *error = text;
            has_pending = false;
            return false;
        };
        if (reader == nullptr || reader->Remaining() < sizeof(uint16_t)) return fail("crises save is truncated");
        const uint16_t version = reader->Read_Uint16();
        if (version < 1 || version > SAVE_PAYLOAD_VERSION) return fail("unsupported crises payload version");
        const uint32_t expected = version == 1 ? SAVE_SIZE_V1 : SAVE_SIZE_V1 + (version == 2 ? EXTRA_SIZE_V2 : EXTRA_SIZE);
        if (reader->Remaining() != expected - sizeof(uint16_t)) return fail("crises save has the wrong size");
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
        pending_extra = s_extra{};
        if (version >= 2) {
            for (s_scale& s : pending_extra.scale) {
                s.kind = reader->Read_Uint8();
                s.player = reader->Read_Int8();
                s.value = reader->Read_Int16();
                s.started = reader->Read_Int32();
                s.approach = reader->Read_Int8();
                if (s.kind >= SCALE_COUNT) return fail("crises save has an unknown scale");
                if (s.player < -1 || s.player >= MAX_PLAYERS) return fail("crises save has an out-of-range scale player");
                if (s.value < 0 || s.value > SCALE_MAX || s.started < 0) return fail("crises save has a bad scale");
                if (s.approach < -1 || s.approach >= APPROACH_COUNT) return fail("crises save has an out-of-range approach");
            }
            if (version == 2) {
                // The wave-4 academies and fallen empires are dropped: those mechanics are replaced.
                for (int32_t p = 0; p < MAX_PLAYERS; ++p) {
                    if (reader->Read_Uint8() > 12) return fail("crises save has a bad academy");
                }
                for (int32_t p = 0; p < MAX_PLAYERS; ++p) {
                    if (reader->Read_Int32() < 0) return fail("crises save has a bad crisis end");
                }
            }
            pending_extra.front_broken = reader->Read_Uint8();
            pending_extra.council_progress = reader->Read_Int32();
            for (int32_t p = 0; p < MAX_PLAYERS; ++p) pending_extra.given[p] = reader->Read_Uint16();
            if (pending_extra.front_broken > 1 || pending_extra.council_progress < 0 || pending_extra.council_progress > 1000000) {
                return fail("crises save has a bad council project");
            }
            if (version >= 3) {
                for (int32_t p = 0; p < MAX_PLAYERS; ++p) {
                    pending_extra.threat[p] = reader->Read_Uint16();
                    if (pending_extra.threat[p] > THREAT_MAX) return fail("crises save has a bad threat");
                }
                for (int32_t p = 0; p < MAX_PLAYERS; ++p) pending_extra.war_mask[p] = reader->Read_Uint8();
                for (int32_t p = 0; p < MAX_PLAYERS; ++p) {
                    pending_extra.strike_until[p] = reader->Read_Int32();
                    if (pending_extra.strike_until[p] < 0) return fail("crises save has a bad strike");
                }
                pending_extra.contest_stage = reader->Read_Uint8();
                pending_extra.contest_ends = reader->Read_Int32();
                if (pending_extra.contest_stage > ACADEMY_STAGES || pending_extra.contest_ends < 0) return fail("crises save has a bad contest");
                for (int32_t p = 0; p < MAX_PLAYERS; ++p) {
                    pending_extra.baseline[p] = reader->Read_Int32();
                    if (pending_extra.baseline[p] < -40000 || pending_extra.baseline[p] > 40000) return fail("crises save has a bad baseline");
                }
            }
        }
        if (reader->Failed()) return fail("crises save is truncated");
        has_pending = true;
        return true;
    }

    void Commit_Save() {
        if (!has_pending) return;
        std::copy(std::begin(pending), std::end(pending), std::begin(slots));
        extra = pending_extra;
        has_pending = false;
    }

    void Discard_Save() {
        has_pending = false;
    }

} // namespace crises
